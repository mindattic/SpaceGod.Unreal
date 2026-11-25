// US-T724: the sim tier scheduler.

#include "SimTier.h"

#include <chrono>

namespace SpaceGod::Core
{
	const char* SimTierName(SimTier tier)
	{
		switch (tier)
		{
		case SimTier::Near: return "Near";
		case SimTier::Mid: return "Mid";
		default: return "Far";
		}
	}

	void SimTiers::Observe(const cs::List<DVec3>& helioKm, double now)
	{
		observers = helioKm;
		observedS = now;
		if (observers.Count() == 0) Clear();
	}

	void SimTiers::Clear()
	{
		observers.Clear();
		observedS = -1e300;
		active = false;
		entries.clear();
	}

	SimTier SimTiers::TierFor(double distanceKm, SimTier current, double nearKm, double farKm, double hysteresis)
	{
		double out = 1 + cs::Math::Max(0.0, hysteresis);
		// Inward at the edge itself, outward only past it by the hysteresis: the tier held is the stickiest that still fits.
		if (current == SimTier::Near)
			return distanceKm <= nearKm * out ? SimTier::Near : distanceKm <= farKm * out ? SimTier::Mid : SimTier::Far;
		if (current == SimTier::Mid)
			return distanceKm <= nearKm ? SimTier::Near : distanceKm <= farKm * out ? SimTier::Mid : SimTier::Far;
		return distanceKm <= nearKm ? SimTier::Near : distanceKm <= farKm ? SimTier::Mid : SimTier::Far;
	}

	double SimTiers::NearestObserverKm(DVec3 helioKm) const
	{
		double best = cs::Double::PositiveInfinity;
		for (const auto& o : observers) best = cs::Math::Min(best, (helioKm - o).Length());
		return best;
	}

	SimTier SimTiers::TierOf(int key) const
	{
		if (!active) return SimTier::Near;
		auto it = entries.find(key);
		return it == entries.end() ? SimTier::Near : it->second.Tier;
	}

	void SimTiers::BeginFrame(double now)
	{
		nowS = now;
		bool was = active;
		active = observers.Count() > 0 && now - observedS <= ObserverStaleS;
		if (was && !active) entries.clear();
		if (!active) return;
		retierCursor = retierCursor > (1 << 30) ? 0 : retierCursor + cs::Math::Max(1, RetierPerFrame);
		// The Mid batch follows its cost: halved while the tier runs over budget, grown back while it has room to spare.
		double mid = stats.LastMs[static_cast<int>(SimTier::Mid)], budget = BudgetMs[static_cast<int>(SimTier::Mid)];
		if (mid > budget) midBatch = cs::Math::Max(1, midBatch / 2);
		else if (mid < budget * 0.5) midBatch = cs::Math::Min(4096, midBatch + cs::Math::Max(1, midBatch / 4));
		midAdmitted = 0;
		for (int t = 0; t < SimTierCount; t++) { frameMs[t] = 0; stepped[t] = 0; }
	}

	bool SimTiers::DueRetier(int index, int count) const
	{
		if (!active || count <= 0) return false;
		int start = retierCursor % count;
		return (index - start + count) % count < RetierPerFrame;
	}

	void SimTiers::Retier(int key, DVec3 helioKm)
	{
		if (!active) return;
		auto& e = entries[key];
		auto next = TierFor(NearestObserverKm(helioKm), e.Tier, NearKm, FarKm, Hysteresis);
		if (next == e.Tier) return;
		if (next < e.Tier) stats.Promotions++;
		else stats.Demotions++;
		// Into Mid from Near it was just stepped; from Far it is due at once.
		if (next == SimTier::Mid) e.LastStepS = e.Tier == SimTier::Near ? nowS : -1e300;
		e.Tier = next;
	}

	bool SimTiers::Steps(int key)
	{
		auto tier = TierOf(key);
		bool go = tier == SimTier::Near;
		if (tier == SimTier::Mid)
		{
			auto& e = entries[key];
			if (e.LastStepS == nowS) go = true;
			else if (midAdmitted < midBatch && nowS - e.LastStepS >= MidPeriodS)
			{
				e.LastStepS = nowS;
				midAdmitted++;
				go = true;
			}
		}
		if (go) stepped[static_cast<int>(tier)]++;
		return go;
	}

	double SimTiers::ClockS()
	{
		static const auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	void SimTiers::EndFrame()
	{
		stats.Frames++;
		stats.Observers = active ? observers.Count() : 0;
		stats.MidBatch = active ? midBatch : 0;
		for (int t = 0; t < SimTierCount; t++) stats.Fleets[t] = 0;
		if (active)
			for (const auto& kv : entries) stats.Fleets[static_cast<int>(kv.second.Tier)]++;
		for (int t = 0; t < SimTierCount; t++)
		{
			double ms = frameMs[t];
			stats.LastMs[t] = ms;
			stats.Stepped[t] = stepped[t];
			stats.AvgMs[t] = stats.Frames == 1 ? ms : stats.AvgMs[t] * 0.98 + ms * 0.02;
			stats.WorstMs[t] = cs::Math::Max(stats.WorstMs[t], ms);
			if (ms > BudgetMs[t]) stats.OverBudget[t]++;
			frameMs[t] = 0;
			stepped[t] = 0;
		}
	}

	void SimTiers::ResetStatistics() { stats = Stats(); }
}
