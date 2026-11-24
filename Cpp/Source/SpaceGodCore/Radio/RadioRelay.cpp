#include "Radio/RadioRelay.h"

#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Information/LightSpeed.h"
#include "Information/News.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "World.h"

#include <cstdint>

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>Where the relay beams news in: the market nearest the star (Newswire::InnerLightDays), else the system's first gate, else the star.</summary>
		DVec3 RelayOriginKm(const cs::Ref<StarSystem>& at)
		{
			DVec3 origin;
			double nearest = cs::Double::PositiveInfinity;
			for (const auto& st : at->World->Economy->Settlements)
			{
				if (st->Hidden || st->Kind == SettlementKind::Haven) continue;
				DVec3 p = at->Ephemeris->Helio(st->Body);
				double d = p.Length();
				if (d < nearest)
				{
					nearest = d;
					origin = p;
				}
			}
			if (!cs::Double::IsInfinity(nearest)) return origin;
			if (at->Gates().Count() > 0) return at->Gates()[0]->PositionKm;
			return origin;
		}
	}

	void RadioRelay::Bulletins(const cs::Ref<StarSystem>& at, int systemId, const SystemReport& report, std::vector<Bulletin>& out)
	{
		if (at == nullptr || report.Source == nullptr || report.Carrier == NewsCarrier::Charts) return;
		const auto& headlines = report.Headlines;
		double since = cs::Double::NegativeInfinity;
		auto seen = lastHeadlineJd.find(report.Source.get());
		if (seen != lastHeadlineJd.end()) since = seen->second;
		int first = headlines.Count();
		while (first > 0 && headlines.Count() - first < Newswire::HeadlinesPerReport && headlines[first - 1].Jd > since) first--;
		if (first == headlines.Count()) return;
		lastHeadlineJd[report.Source.get()] = headlines[headlines.Count() - 1].Jd;

		RadioMessage m;
		m.ChannelId = channels.For(at, systemId).Relay().Id;
		m.SystemId = systemId;
		m.OriginKm = RelayOriginKm(at);
		m.SentJd = report.ReceivedJd;
		m.Power = RadioPower::GuardBeacon;
		double ageSeconds = cs::Math::Max(0.0, (report.ReceivedJd - report.AsOfJd) * 86400.0);
		m.Args[ArgAgeSeconds] = static_cast<int32_t>(cs::Math::Min(ageSeconds, static_cast<double>(INT32_MAX)));
		for (int i = first; i < headlines.Count(); i++)
			out.push_back({ m, cs::Cat("relayed from ", report.Source->Name, ": ", headlines[i].Text) });
	}

	double RadioRelay::ReportAgeDays(const RadioMessage& message, double nowJd)
	{
		return message.Args[ArgAgeSeconds] / 86400.0 + cs::Math::Max(0.0, nowJd - message.SentJd);
	}

	cs::String RadioRelay::Label(const RadioMessage& message, double nowJd)
	{
		return cs::Cat("relayed, sent ", LightSpeed::Describe(ReportAgeDays(message, nowJd)), " ago");
	}
}
