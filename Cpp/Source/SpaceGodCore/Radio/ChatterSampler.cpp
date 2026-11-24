#include "Radio/ChatterSampler.h"

#include "Comms/Comms.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Information/LightSpeed.h"
#include "Radio/ChatterLines.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

#include <algorithm>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr double SecondsPerDay = 86400.0;
		constexpr double WindowDays = ChatterSampler::WindowSeconds / SecondsPerDay;
	}

	uint64_t ChatterSampler::Next()
	{
		uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	double ChatterSampler::Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }

	int ChatterSampler::Poisson(double mean)
	{
		if (mean <= 0) return 0;
		// Knuth; the means here are small (a few per window at most), and the count is capped by the window anyway.
		double limit = cs::Math::Exp(-mean);
		double p = Unit();
		int k = 0;
		while (p > limit && k < MaxPerWindow)
		{
			k++;
			p *= Unit();
		}
		return k;
	}

	void ChatterSampler::Tick(const cs::Ref<StarSystem>& system, int systemId, DVec3 listenerKm, double travelMarginKm, double nowJd, std::vector<RadioMessage>& out)
	{
		if (system == nullptr || system->World == nullptr || systemId < 0) return;
		if (world != system->World.get() || nowJd < nextJd - WindowDays)
		{
			world = system->World.get();
			nextJd = nowJd + WindowDays;
			return;
		}
		if (nowJd < nextJd) return;

		const RadioChannelSet& set = channels.For(system, systemId);
		for (int i = 0; i < MaxWindowsPerTick && nowJd >= nextJd; i++)
		{
			Sample(*system, set, systemId, listenerKm, travelMarginKm, nextJd - WindowDays, nextJd, Density);
			Flush(MaxPerWindow, out);
			nextJd += WindowDays;
		}
		if (nowJd >= nextJd) nextJd = nowJd + WindowDays;
	}

	void ChatterSampler::BackFill(const cs::Ref<StarSystem>& system, int systemId, DVec3 listenerKm, double travelMarginKm, double nowJd, std::vector<RadioMessage>& out)
	{
		if (system == nullptr || system->World == nullptr || systemId < 0) return;
		world = system->World.get();
		nextJd = nowJd + WindowDays;

		// Each group draws over the longest span (the station's), and keeps only what a front of its own class could
		// still be carrying: older ship talk has passed beyond reach.
		double span = LightSpeed::DelayDays(RadioPowerTable::MaxRangeKm(RadioPower::Station) + travelMarginKm);
		const RadioChannelSet& set = channels.For(system, systemId);
		Sample(*system, set, systemId, listenerKm, travelMarginKm, nowJd - span, nowJd, Density * BackFillDensity);
		double shipSpan = LightSpeed::DelayDays(RadioPowerTable::MaxRangeKm(RadioPower::Ship) + travelMarginKm);
		drawn.erase(std::remove_if(drawn.begin(), drawn.end(), [&](const RadioMessage& m) { return m.Power == RadioPower::Ship && m.SentJd < nowJd - shipSpan; }), drawn.end());
		Flush(MaxBackFill, out);
	}

	void ChatterSampler::Sample(const StarSystem& system, const RadioChannelSet& set, int systemId, DVec3 listenerKm, double travelMarginKm, double fromJd, double toJd, double density)
	{
		drawn.clear();
		if (density <= 0 || toJd <= fromJd) return;
		World& w = *system.World;
		double hours = (toJd - fromJd) * 24.0;

		auto emit = [&](int count, DVec3 origin, uint32_t channelId, RadioPower power, uint64_t emitterKey, int factionId, int arg0, auto situation)
		{
			for (int i = 0; i < count; i++)
			{
				RadioMessage m;
				m.Id = static_cast<uint32_t>(Next() >> 32);
				m.ChannelId = channelId;
				m.EmitterKey = emitterKey;
				m.FactionId = factionId;
				m.SystemId = systemId;
				m.OriginKm = origin;
				m.SentJd = fromJd + Unit() * (toJd - fromJd);
				m.Power = power;
				m.Situation = static_cast<uint16_t>(situation());
				m.Args[0] = arg0;
				drawn.push_back(m);
			}
		};

		if (w.Economy != nullptr && w.Ephemeris != nullptr)
		{
			double reach = RadioPowerTable::MaxRangeKm(RadioPower::Station) + travelMarginKm;
			for (const auto& st : w.Economy->Settlements)
			{
				if (st == nullptr || st->Fallen || st->Hidden) continue;
				int n = Poisson(StationPerHour * hours * density);
				if (n == 0) continue;
				const RadioChannel* traffic = set.TrafficOf(st);
				if (traffic == nullptr) continue;
				DVec3 at = w.Ephemeris->HelioAt(st->Body, toJd);
				if ((at - listenerKm).Length() > reach) continue;
				emit(n, at, traffic->Id, RadioPower::Station, ChatterLines::EmitterKey(CommsParty::OfStation(st)), -1, st->Id, [this]
				{
					static constexpr ChatterSituation Talk[] = { ChatterSituation::Clearance, ChatterSituation::Departure, ChatterSituation::Idle };
					return Talk[Next() % 3];
				});
			}
		}

		if (w.Fleet != nullptr && w.Sensors != nullptr)
		{
			const auto& ships = w.Fleet->Ships();
			int total = ships.Count();
			int scan = cs::Math::Min(total, MaxShipsPerWindow);
			if (scan > 0)
			{
				if (shipCursor >= total) shipCursor = 0;
				double scale = static_cast<double>(total) / scan;
				double reach = RadioPowerTable::MaxRangeKm(RadioPower::Ship) + travelMarginKm;
				for (int i = 0; i < scan; i++)
				{
					const cs::Ref<Ship>& s = ships[(shipCursor + i) % total];
					if (s == nullptr || s->Destroyed || s->GateDeparted || s->Scrapped || s->Derelict || s->Planetside()) continue;

					bool pirate = s->Faction != nullptr && s->Faction->IsPirate;
					bool patrol = !pirate && (s->Role == ShipRole::Patrol || s->Role == ShipRole::Escort);
					bool trader = !pirate && !patrol && s->Role == ShipRole::Trader && s->CargoDestination != nullptr;
					double perHour = pirate ? PiratePerHour : patrol ? PatrolPerHour : trader ? TraderPerHour : CivilPerHour;
					int n = Poisson(perHour * hours * density * scale);
					if (n == 0) continue;
					DVec3 at = w.Sensors->HelioOf(s, toJd);
					if ((at - listenerKm).Length() > reach) continue;

					uint64_t key = ChatterLines::EmitterKey(CommsParty::OfShip(s));
					int faction = s->Faction != nullptr ? w.Fleet->Factions().IndexOf(s->Faction) : -1;
					const RadioChannel* net = pirate || patrol ? set.FleetOf(s->Faction) : nullptr;
					const RadioChannel* traffic = trader ? set.TrafficOf(s->CargoDestination) : nullptr;
					if (pirate && net != nullptr)
						emit(n, at, net->Id, RadioPower::Ship, key, faction, -1, [this] { return Next() % 2 == 0 ? ChatterSituation::PirateTaunt : ChatterSituation::Idle; });
					else if (patrol && net != nullptr)
						emit(n, at, net->Id, RadioPower::Ship, key, faction, -1, [this] { return Next() % 2 == 0 ? ChatterSituation::PatrolCheckIn : ChatterSituation::Idle; });
					else if (traffic != nullptr && Next() % 3 == 0)
						emit(n, at, traffic->Id, RadioPower::Ship, key, faction, s->CargoDestination->Id, [] { return ChatterSituation::DockingRequest; });
					else
						emit(n, at, set.Civil().Id, RadioPower::Ship, key, faction, -1, [] { return ChatterSituation::Idle; });
				}
				shipCursor = (shipCursor + scan) % total;
			}
		}
	}

	void ChatterSampler::Flush(int cap, std::vector<RadioMessage>& out)
	{
		int n = static_cast<int>(drawn.size());
		if (n > cap)
		{
			for (int i = 0; i < cap; i++)
				std::swap(drawn[i], drawn[i + static_cast<int>(Next() % static_cast<uint64_t>(n - i))]);
			drawn.resize(cap);
		}
		std::sort(drawn.begin(), drawn.end(), [](const RadioMessage& a, const RadioMessage& b) { return a.SentJd < b.SentJd; });
		out.insert(out.end(), drawn.begin(), drawn.end());
		drawn.clear();
	}
}
