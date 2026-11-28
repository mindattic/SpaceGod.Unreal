// US-T985: Populace's trade trust (docs/research/LightSociety-design.md, "Feedback"): each month the heads of every world's
// trading houses play a trust round and an ultimatum round with the next such head of their world, scored by the rule
// teacher; each world's mean outcome is heard as a TradeTrust push. Houses are walked in their own order only: the society is
// lockstep-deterministic.

#include "Systems/Civilization/Populace.h"

#include "Economy/Settlement.h"
#include "LightSociety/Teacher.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

#include <algorithm>
#include <vector>

namespace SpaceGod::Core
{
	namespace LS = MindAttic::Society;

	namespace
	{
		/// <summary>How strongly a month's dealings reach a world.</summary>
		constexpr float TradeMagnitude = 0.3f;

		struct Dealer
		{
			float edu, cls, age, agree, neuro;
		};

		Dealer DealerOf(const cs::Ref<Citizen>& c, const cs::Ref<Household>& h, const cs::Ref<Civics>& cv, double jd)
		{
			const double wealth = h->Wealth + (h->Business != nullptr ? h->Business->Capital : 0);
			auto has = [&](Trait t) { return (c->Traits & static_cast<int>(t)) != 0; };
			Dealer d;
			d.edu = static_cast<float>(cs::Math::Clamp(cv != nullptr ? cv->Learning : 0.3, 0.0, 1.0));
			d.cls = wealth < 1e3 ? 0.0f : wealth < 1e4 ? 0.25f : wealth < 1e5 ? 0.5f : wealth < 1e6 ? 0.75f : 1.0f;
			d.age = static_cast<float>(cs::Math::Clamp(c->AgeYears(jd) / 100, 0.0, 1.0));
			d.agree = 0.5f - (has(Trait::Cruel) ? 0.25f : 0.0f) - (has(Trait::Vengeful) ? 0.1f : 0.0f) + (has(Trait::Honest) ? 0.1f : 0.0f);
			d.neuro = 0.5f + (has(Trait::Reckless) ? 0.15f : 0.0f) + (has(Trait::Vengeful) ? 0.1f : 0.0f);
			return d;
		}

		/// <summary>A game row as LightSociety's games scenario builds one (Games.cpp GameRow).</summary>
		void Row(const Dealer& self, const Dealer& other, float amount, std::span<float> row)
		{
			std::fill(row.begin(), row.end(), 0.0f);
			row[LS::Feature::SelfEducation] = self.edu;
			row[LS::Feature::SelfClass] = self.cls;
			row[LS::Feature::SelfAge] = self.age;
			row[LS::Feature::SelfOpenness] = 0.5f;
			row[LS::Feature::SelfAgreeableness] = self.agree;
			row[LS::Feature::SelfNeuroticism] = self.neuro;
			row[LS::Feature::SelfTrust] = self.agree;
			row[LS::Feature::OtherEducation] = other.edu;
			row[LS::Feature::OtherClass] = other.cls;
			row[LS::Feature::OtherAge] = other.age;
			row[LS::Feature::Amount] = amount;
			row[LS::Feature::Reciprocity] = 0.5f;
		}

		float Play(LS::Op op, const Dealer& self, const Dealer& other, float amount)
		{
			float row[LS::FeatureCount];
			float out[LS::MaxOutputs] = {};
			Row(self, other, amount, row);
			LS::Teacher::Game(op, row, std::span<float>(out, LS::OutputWidth(op)));
			return op == LS::Op::UltimatumAnswer ? out[static_cast<int>(LS::Answer::Accept)] : std::clamp(out[0], 0.0f, 1.0f);
		}

		/// <summary>One pair's dealings in -1..1: what the trust round earned the trustor and whether the offer was taken.</summary>
		double Deal(const Dealer& a, const Dealer& b)
		{
			const float send = Play(LS::Op::TrustSend, a, b, 0.0f);
			const float back = Play(LS::Op::TrustReturn, b, a, send);
			const double trust = cs::Math::Clamp(send * (3.0 * back - 1.0) * 2.0, -1.0, 1.0);
			const float offer = Play(LS::Op::UltimatumOffer, a, b, 0.0f);
			const double ultimatum = 2.0 * Play(LS::Op::UltimatumAnswer, b, a, offer) - 1.0;
			return (trust + ultimatum) / 2;
		}
	}

	void Populace::Trade(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(OriginJd) || ReadDay() < 0) return;
		if (cs::Double::IsNaN(NextTradeJd)) NextTradeJd = cs::Math::Floor(jd) + Civilization::MonthDays;
		if (jd < NextTradeJd) return;
		const double dealt = NextTradeJd;
		NextTradeJd = cs::Math::Floor(jd) + Civilization::MonthDays;
		auto society = Society::Find(galaxy);
		if (society == nullptr) return;

		std::vector<std::vector<Dealer>> dealers(static_cast<size_t>(Groups.Count()));
		for (const auto& h : society->Houses)
		{
			if (h->Scattered || h->Business == nullptr || !h->Business->Trading()) continue;
			const int g = GroupOf(h->Home);
			auto head = h->Head();
			if (g < 0 || head == nullptr || !head->Alive()) continue;
			dealers[static_cast<size_t>(g)].push_back(DealerOf(head, h, civ.CivicsOf(h->Home), jd));
		}
		for (int g = 0; g < Groups.Count(); g++)
		{
			const auto& ds = dealers[static_cast<size_t>(g)];
			if (ds.size() < 2) continue;
			double sum = 0;
			for (size_t i = 0; i < ds.size(); i++) sum += Deal(ds[i], ds[(i + 1) % ds.size()]);
			const double mean = sum / static_cast<double>(ds.size());
			if (mean != 0) Hear(g, Topic::TradeTrust, static_cast<float>(cs::Math::Clamp(mean, -1.0, 1.0)), TradeMagnitude, dealt);
		}
	}
}
