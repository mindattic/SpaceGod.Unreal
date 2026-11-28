// US-T984: Populace's feedback (docs/research/LightSociety-design.md, "Feedback"): the readout moves the worlds. Each day
// read, support for the flag stirs a world's approval and unrest; each month a poll is published per world; a mood crossing
// into a band publishes what the people now feel. Groups are walked in their own order only: the society is lockstep-deterministic.

#include "Systems/Civilization/Populace.h"

#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>A mood enters a band at this and leaves it below <see cref="LeaveBand"/>, so a mood on the edge publishes once.</summary>
		constexpr double EnterBand = 0.5;
		constexpr double LeaveBand = 0.3;

		struct Band
		{
			Topic topic;
			/// <summary>The way the mood enters the band (+1 or -1), or 0 for either way.</summary>
			int sign;
			SocialKind kind;
		};

		constexpr Band BandSpecs[Populace::BandCount] = {
			{Topic::PolitySupport, 0, SocialKind::OpinionShift},
			{Topic::PirateFear, 1, SocialKind::Panic},
			{Topic::TradeTrust, -1, SocialKind::TrustCrisis},
			{Topic::GodFaith, 1, SocialKind::Revival},
		};

		/// <summary>The band a mood stands in (-1, 0 or 1) given the one it stood in.</summary>
		int BandOf(int was, double mood, int sign)
		{
			int now = was;
			if (mood >= EnterBand) now = 1;
			else if (mood <= -EnterBand) now = -1;
			else if (was > 0 && mood < LeaveBand) now = 0;
			else if (was < 0 && mood > -LeaveBand) now = 0;
			return sign != 0 && now != sign ? 0 : now;
		}
	}

	const cs::Ref<RangeSetting>& Populace::FeedbackGain()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("light", "feedbackGain", "Opinion feedback", 0.02, 0, 0.2, 0.001, "/day",
			TuningScope::Simulation, "How far a day of the people's support for (or against) their flag moves its approval, and half that the other way its unrest.");
		return s;
	}

	const cs::Ref<RangeSetting>& Populace::FeedbackCap()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("light", "feedbackCap", "Opinion feedback cap", 0.01, 0, 0.1, 0.001, "/day",
			TuningScope::Simulation, "The most a day of the people's opinion moves a world's approval.");
		return s;
	}

	void Populace::Feedback(Civilization& civ, Galaxy& galaxy, double jd)
	{
		const int day = ReadDay();
		if (day < 0 || cs::Double::IsNaN(OriginJd)) return;
		// A society built afresh reads from its day 0 again.
		if (day < FedDay) FedDay = 0;
		while (Bands.Count() < Groups.Count() * BandCount) Bands.Add(0);

		if (day > FedDay)
		{
			const int days = day - FedDay;
			FedDay = day;
			const double on = OriginJd + day;
			const double gain = FeedbackGain()->Value, cap = FeedbackCap()->Value;
			for (int g = 0; g < Groups.Count(); g++)
			{
				const auto& s = Groups[g];
				if (s == nullptr) continue;
				const double support = Mood(s, Topic::PolitySupport);
				if (support != 0)
				{
					const double step = cs::Math::Clamp(gain * support, -cap, cap) * days;
					civ.Stir(s, -0.5 * step, step);
				}
				for (int b = 0; b < BandCount; b++)
				{
					const double mood = Mood(s, BandSpecs[b].topic);
					if (mood == 0) continue;
					const int i = g * BandCount + b;
					const int was = Bands[i];
					const int now = BandOf(was, mood, BandSpecs[b].sign);
					Bands[i] = now;
					if (now == 0 || now == was) continue;
					auto e = Civilization::Make(BandSpecs[b].kind, on, s, nullptr);
					e->Amount = mood;
					civ.Publish(galaxy, e);
				}
			}
		}

		if (cs::Double::IsNaN(NextPollJd)) NextPollJd = cs::Math::Floor(jd) + Civilization::MonthDays;
		if (jd < NextPollJd) return;
		const double polled = NextPollJd;
		NextPollJd = cs::Math::Floor(jd) + Civilization::MonthDays;
		for (const auto& s : Groups)
		{
			if (s == nullptr || Mood(s, Topic::PolitySupport) == 0) continue;
			auto e = Civilization::Make(SocialKind::PollPublished, polled, s, nullptr);
			e->Amount = Share(s, Topic::PolitySupport);
			civ.Publish(galaxy, e);
		}
	}
}
