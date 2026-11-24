#include "Radio/ChatterEvents.h"

#include "BodyCatalog.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Radio/ChatterLines.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>The settlement on the event's body that is on the map, or null.</summary>
		const Settlement* SettlementAt(const World& world, int body)
		{
			if (body < 0 || world.Economy == nullptr) return nullptr;
			for (const auto& st : world.Economy->Settlements)
				if (st != nullptr && !st->Hidden && st->Body == body) return st.get();
			return nullptr;
		}

		/// <summary>The index in the fleet's factions of the first one the event names, or -1.</summary>
		int FactionNamed(const World& world, const cs::String& text)
		{
			if (world.Fleet == nullptr) return -1;
			const auto& factions = world.Fleet->Factions();
			for (int i = 0; i < factions.Count(); i++)
				if (factions[i] != nullptr && !cs::Strings::IsNullOrEmpty(factions[i]->Name) && cs::Strings::Contains(text, factions[i]->Name)) return i;
			return -1;
		}
	}

	void ChatterEvents::Pump(const cs::Ref<StarSystem>& at, int systemId, double jd, std::vector<Chatter>& out)
	{
		if (at == nullptr || at->World == nullptr || systemId < 0) return;
		const World& w = *at->World;
		const auto& events = w.Events;
		if (world != &w || jd < cursorJd)
		{
			world = &w;
			cursorJd = events.Count() > 0 ? cs::Math::Max(events[events.Count() - 1].Jd, jd) : jd;
			readAtCursor = 0;
			for (int i = events.Count() - 1; i >= 0 && events[i].Jd == cursorJd; i--) readAtCursor++;
			return;
		}

		int first = events.Count();
		while (first > 0 && events.Count() - first < MaxBacklog && events[first - 1].Jd > cursorJd) first--;
		int atCursor = 0;
		while (first > 0 && events.Count() - first < MaxBacklog && events[first - 1].Jd == cursorJd)
		{
			first--;
			atCursor++;
		}
		first += cs::Math::Min(readAtCursor, atCursor);
		int last = cs::Math::Min(events.Count(), first + MaxPerPump);
		if (first >= last) return;

		const RadioChannelSet& set = channels.For(at, systemId);
		for (int i = first; i < last; i++)
		{
			Translate(*at, set, systemId, i, out);
			double e = events[i].Jd;
			if (e > cursorJd)
			{
				cursorJd = e;
				readAtCursor = 1;
			}
			else if (e == cursorJd) readAtCursor++;
		}
	}

	void ChatterEvents::Translate(const StarSystem& at, const RadioChannelSet& set, int systemId, int eventIndex, std::vector<Chatter>& out) const
	{
		const World& w = *at.World;
		const WorldEvent& e = w.Events[eventIndex];

		RadioMessage m;
		m.SystemId = systemId;
		m.SentJd = e.Jd;
		if (at.Ephemeris != nullptr && at.Ephemeris->Catalog != nullptr && e.Body >= 0 && e.Body < at.Ephemeris->Catalog->Count())
			m.OriginKm = at.Ephemeris->Helio(e.Body);
		const Settlement* port = SettlementAt(w, e.Body);
		if (port != nullptr) m.Args[0] = port->Id;

		auto add = [&out](RadioMessage message, uint32_t channelId, RadioPower power, uint8_t priority, ChatterSituation situation, const cs::String& text)
		{
			message.ChannelId = channelId;
			message.Power = power;
			message.Priority = priority;
			message.Situation = static_cast<uint16_t>(situation);
			out.push_back({ message, text });
		};
		auto net = [&](int faction) -> const RadioChannel*
		{
			if (faction < 0) return nullptr;
			return set.FleetOf(w.Fleet->Factions()[faction]);
		};

		switch (e.Kind)
		{
		case WorldEventKind::Battle:
		{
			int faction = FactionNamed(w, e.Text);
			if (const RadioChannel* fleet = net(faction))
			{
				RadioMessage callout = m;
				callout.FactionId = faction;
				add(callout, fleet->Id, RadioPower::Ship, 2, ChatterSituation::BattleCallout, {});
			}
			add(m, set.Guard().Id, RadioPower::Station, 2, ChatterSituation::ContactReport, e.Text);
			break;
		}
		case WorldEventKind::Capture:
		{
			int faction = FactionNamed(w, e.Text);
			if (const RadioChannel* fleet = net(faction))
			{
				m.FactionId = faction;
				add(m, fleet->Id, RadioPower::Ship, 1, ChatterSituation::BattleOver, e.Text);
			}
			break;
		}
		case WorldEventKind::Loss:
			add(m, set.Guard().Id, RadioPower::Ship, 3, ChatterSituation::Distress, {});
			break;
		case WorldEventKind::Launch:
		{
			const RadioChannel* traffic = nullptr;
			if (port != nullptr)
				for (const auto& st : w.Economy->Settlements)
					if (st.get() == port)
					{
						traffic = set.TrafficOf(st);
						break;
					}
			add(m, traffic != nullptr ? traffic->Id : set.Civil().Id, RadioPower::Station, 0, ChatterSituation::Departure, {});
			break;
		}
		case WorldEventKind::Law:
		{
			int faction = FactionNamed(w, e.Text);
			const RadioChannel* fleet = net(faction);
			if (fleet != nullptr) m.FactionId = faction;
			add(m, fleet != nullptr ? fleet->Id : set.Civil().Id, RadioPower::Station, 1, ChatterSituation::BountyNotice, e.Text);
			break;
		}
		case WorldEventKind::Economy:
		case WorldEventKind::Market:
			add(m, set.Civil().Id, RadioPower::Station, 0, ChatterSituation::TradeQuote, e.Text);
			break;
		default:
			break;
		}
	}
}
