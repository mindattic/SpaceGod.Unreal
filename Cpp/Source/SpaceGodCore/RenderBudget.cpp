// US-T719: the render budget's hard caps per tier, over RenderLod's per-size tiers. Not a C# port.

#include "RenderBudget.h"
#include "Settings/GameSetting.h"

#include <algorithm>
#include <chrono>

namespace SpaceGod::Core
{
	struct RenderBudget::State
	{
		cs::Ref<RangeSetting> FullCap, SimplifiedCap, IconCap, Hysteresis;
		RenderBudget Ships;
	};

	RenderBudget::State& RenderBudget::S()
	{
		static State* s = []
		{
			auto* st = new State();
			auto count = cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); });
			st->FullCap = cs::New<RangeSetting>("lod.fullCap", SettingGroups::RenderBudget,
				"Full models at most", 40, 0, 400, 1,
				"At most this many ships are drawn as full models at once; past that the smallest on screen draw their hull alone. Your target, your wing and whoever is attacking you are served first. Lower is faster in a big fight.",
				count, "lod budget cap full model detail limit performance battle");
			st->SimplifiedCap = cs::New<RangeSetting>("lod.simplifiedCap", SettingGroups::RenderBudget,
				"Simplified models at most", 150, 0, 2000, 10,
				"At most this many ships are drawn as simplified hulls at once; past that the smallest on screen are drawn as icons. Lower is faster in a big fight.",
				count, "lod budget cap simplified hull model limit performance battle");
			st->IconCap = cs::New<RangeSetting>("lod.iconCap", SettingGroups::RenderBudget,
				"Icons and impostors at most", 2000, 0, 20000, 100,
				"At most this many far-off ships are drawn as icons or glows at once; past that the smallest are not drawn (the scanners still show their contacts).",
				count, "lod budget cap icon impostor glow limit performance");
			st->Hysteresis = cs::New<RangeSetting>("lod.hysteresis", SettingGroups::RenderBudget,
				"Detail change margin", 0.15, 0, 0.5, 0.05,
				"How far past a detail swap point a ship must go before it changes form, so ships at the edge do not flicker between model and icon.",
				cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v * 100, "0"), "%"); }),
				"lod hysteresis flicker margin swap detail");
			return st;
		}();
		return *s;
	}

	const cs::Ref<RangeSetting>& RenderBudget::FullCap() { return S().FullCap; }
	const cs::Ref<RangeSetting>& RenderBudget::SimplifiedCap() { return S().SimplifiedCap; }
	const cs::Ref<RangeSetting>& RenderBudget::IconCap() { return S().IconCap; }
	const cs::Ref<RangeSetting>& RenderBudget::Hysteresis() { return S().Hysteresis; }
	RenderBudget& RenderBudget::Ships() { return S().Ships; }

	void RenderBudget::Begin()
	{
		frame++;
		candidates.clear();
		for (int k : marked) marks[static_cast<size_t>(k)] = LodRelevance::None;
		marked.clear();
		assigned = false;
	}

	void RenderBudget::Mark(int key, uint8_t flags)
	{
		if (key < 0 || flags == LodRelevance::None) return;
		size_t k = static_cast<size_t>(key);
		if (k >= marks.size()) marks.resize(k + 1, LodRelevance::None);
		if (marks[k] == LodRelevance::None) marked.push_back(key);
		marks[k] |= flags;
	}

	void RenderBudget::Add(int key, double px, double distance, uint8_t relevance, const LodClass& c)
	{
		if (key < 0) return;
		size_t k = static_cast<size_t>(key);
		if (k >= frameByKey.size())
		{
			frameByKey.resize(k + 1, 0);
			tierByKey.resize(k + 1, static_cast<uint8_t>(LodTier::Culled));
		}
		if (frameByKey[k] == frame) return;
		bool seen = frameByKey[k] == frame - 1;
		frameByKey[k] = frame;
		Candidate cand{};
		cand.Key = key;
		cand.Px = px;
		cand.Distance = distance;
		cand.Relevance = relevance;
		cand.Class = &c;
		cand.Seen = seen;
		cand.Previous = seen ? static_cast<LodTier>(tierByKey[k]) : LodTier::Culled;
		candidates.push_back(cand);
	}

	LodTier RenderBudget::Earned(const Candidate& c, double horizon, double simplifyBelowPx, double cullBelowPx, double band)
	{
		bool relevant = c.Relevance != LodRelevance::None;
		auto at = [&](double scale)
		{
			double px = c.Px * scale;
			if (!relevant && (px < cullBelowPx || c.Distance > horizon * scale)) return LodTier::Culled;
			return RenderLod::Tier(px, c.Class->ModelBelowPx(), c.Class->Far, c.Class->Simplifies, simplifyBelowPx, cullBelowPx);
		};
		LodTier now = at(1);
		if (!c.Seen || band <= 0 || now == c.Previous) return now;
		// Worse than last frame: keep last frame's until it is band past the swap point. Better: take it only once band past.
		if (now > c.Previous)
		{
			LodTier held = at(1 + band);
			return held < c.Previous ? c.Previous : held;
		}
		LodTier gained = at(1 / (1 + band));
		return gained > c.Previous ? c.Previous : gained;
	}

	void RenderBudget::Assign(double horizon)
	{
		Assign(horizon, cs::ToInt(FullCap()->Value), cs::ToInt(SimplifiedCap()->Value), cs::ToInt(IconCap()->Value),
			RenderLod::SimplifyBelowPx()->Value, RenderLod::CullBelowPx()->Value, Hysteresis()->Value);
	}

	void RenderBudget::Assign(double horizon, int fullCap, int simplifiedCap, int iconCap, double simplifyBelowPx, double cullBelowPx, double hysteresis)
	{
		auto started = std::chrono::steady_clock::now();
		double band = std::max(0.0, hysteresis);
		stats = LodBudgetStats();
		stats.FullCap = fullCap;
		stats.SimplifiedCap = simplifiedCap;
		stats.IconCap = iconCap;
		stats.Candidates = static_cast<int>(candidates.size());

		order.clear();
		for (size_t i = 0; i < candidates.size(); i++)
		{
			auto& c = candidates[i];
			size_t k = static_cast<size_t>(c.Key);
			if (k < marks.size()) c.Relevance |= marks[k];
			c.Wanted = Earned(c, horizon, simplifyBelowPx, cullBelowPx, band);
			// Already holding a slot at least this good: it outranks a newcomer up to band bigger.
			bool incumbent = c.Seen && c.Previous <= c.Wanted;
			c.Score = c.Px * (incumbent ? 1 + band : 1);
			order.push_back(static_cast<int>(i));
		}
		std::sort(order.begin(), order.end(), [this](int a, int b)
		{
			const auto& ca = candidates[static_cast<size_t>(a)];
			const auto& cb = candidates[static_cast<size_t>(b)];
			bool ra = ca.Relevance != LodRelevance::None, rb = cb.Relevance != LodRelevance::None;
			if (ra != rb) return ra;
			if (ca.Score != cb.Score) return ca.Score > cb.Score;
			return ca.Key < cb.Key;
		});

		int full = 0, simplified = 0, icons = 0;
		for (int i : order)
		{
			const auto& c = candidates[static_cast<size_t>(i)];
			LodTier far = c.Class->Far == LodFar::None ? LodTier::Culled : LodTier::Icon;
			LodTier t = c.Wanted;
			if (t == LodTier::Full)
			{
				if (full < fullCap) full++;
				else t = c.Class->Simplifies ? LodTier::Simplified : far;
			}
			if (t == LodTier::Simplified)
			{
				if (simplified < simplifiedCap) simplified++;
				else t = far;
			}
			if (t == LodTier::Icon)
			{
				if (icons < iconCap) icons++;
				else t = LodTier::Culled;
			}
			if (t != c.Wanted) stats.Demoted++;
			else if (t == LodTier::Culled) stats.CulledSmallOrFar++;
			stats.Count[static_cast<int>(t)]++;
			tierByKey[static_cast<size_t>(c.Key)] = static_cast<uint8_t>(t);
		}
		assigned = true;
		stats.AssignMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	}

	bool RenderBudget::Has(int key) const
	{
		return assigned && key >= 0 && static_cast<size_t>(key) < frameByKey.size() && frameByKey[static_cast<size_t>(key)] == frame;
	}

	LodTier RenderBudget::TierOf(int key) const
	{
		return Has(key) ? static_cast<LodTier>(tierByKey[static_cast<size_t>(key)]) : LodTier::Culled;
	}

	cs::String RenderBudget::Describe() const
	{
		const auto& s = stats;
		return cs::Cat("Full ", s.Count[static_cast<int>(LodTier::Full)], "/", s.FullCap,
			", Simplified ", s.Count[static_cast<int>(LodTier::Simplified)], "/", s.SimplifiedCap,
			", Icon ", s.Count[static_cast<int>(LodTier::Icon)], "/", s.IconCap,
			", Culled ", s.Count[static_cast<int>(LodTier::Culled)], " (", s.CulledSmallOrFar, " small or past the horizon)",
			"; ", s.Demoted, " demoted by a cap, ", s.Candidates, " candidates, ", cs::Fmt(s.AssignMs, "0.000"), " ms");
	}
}
