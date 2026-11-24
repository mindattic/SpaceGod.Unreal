#include "Radio/RadioChannels.h"

#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

#include <array>
#include <unordered_set>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr int ChannelKindCount = static_cast<int>(RadioChannelKind::Count);
		constexpr uint32_t SettlementSalt = 0x53544c4du, FactionSalt = 0x46414354u, SystemSalt = 0x53595354u;

		/// <summary>A kind's band: its first frequency, its channel spacing and how many channels it holds, kHz.</summary>
		struct RadioBand
		{
			uint32_t FirstKHz;
			uint32_t StepKHz;
			uint32_t Slots;
		};

		// Guard 121.5 alone; Traffic the airband 118-137; Civil marine VHF 156-162; Fleet military UHF 225-400;
		// Pirate the 27 MHz citizens' band; Relay the S-band deep-space downlink 2200-2290.
		constexpr std::array<RadioBand, ChannelKindCount> Bands = { {
			{ 121500, 25, 1 },
			{ 118000, 25, 760 },
			{ 156025, 25, 240 },
			{ 225000, 25, 7000 },
			{ 26965, 10, 45 },
			{ 2200000, 500, 180 },
		} };

		constexpr uint32_t GuardKHz = 121500;

		const RadioBand& BandOf(RadioChannelKind kind)
		{
			int i = static_cast<int>(kind);
			return Bands[i < 0 || i >= ChannelKindCount ? 0 : i];
		}

		uint32_t SlotKHz(const RadioBand& band, uint32_t slot) { return band.FirstKHz + band.StepKHz * (slot % band.Slots); }

		uint32_t PreferredSlot(RadioChannelKind kind, uint32_t ownerKey)
		{
			return Characters::Mix(ownerKey ^ (0x9e3779b9u * (static_cast<uint32_t>(kind) + 1))) % BandOf(kind).Slots;
		}

		uint64_t Combine(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; }
	}

	const char* ToString(RadioChannelKind v)
	{
		switch (v)
		{
		case RadioChannelKind::Guard: return "Guard";
		case RadioChannelKind::Traffic: return "Traffic";
		case RadioChannelKind::Civil: return "Civil";
		case RadioChannelKind::Fleet: return "Fleet";
		case RadioChannelKind::Pirate: return "Pirate";
		case RadioChannelKind::Relay: return "Relay";
		default: return "Unknown";
		}
	}

	const RadioChannel* RadioChannelSet::Find(uint32_t id) const
	{
		auto it = byId.find(id);
		return it == byId.end() ? nullptr : &channels[it->second];
	}

	const RadioChannel* RadioChannelSet::TrafficOf(const cs::Ref<Settlement>& settlement) const
	{
		auto it = byOwner.find(settlement.get());
		return it == byOwner.end() ? nullptr : &channels[it->second];
	}

	const RadioChannel* RadioChannelSet::FleetOf(const cs::Ref<Faction>& faction) const
	{
		auto it = byOwner.find(faction.get());
		return it == byOwner.end() ? nullptr : &channels[it->second];
	}

	uint32_t RadioChannels::KeyOf(const Settlement& settlement)
	{
		return Characters::Mix(SettlementSalt ^ Characters::Mix(static_cast<uint32_t>(settlement.Id) * 2654435761u) ^ Characters::HashText(settlement.Name));
	}

	uint32_t RadioChannels::KeyOf(const Faction& faction) { return Characters::Mix(FactionSalt ^ Characters::HashText(faction.Name)); }

	uint32_t RadioChannels::KeyOf(const StarSystem& system) { return Characters::Mix(SystemSalt ^ Characters::HashText(system.Name)); }

	uint32_t RadioChannels::IdOf(RadioChannelKind kind, uint32_t ownerKey)
	{
		uint32_t id = Characters::Mix(ownerKey * 31u + static_cast<uint32_t>(kind) * 0x85ebca6bu + 1u);
		return id == 0 ? 1u : id;
	}

	double RadioChannels::FrequencyMhz(RadioChannelKind kind, uint32_t ownerKey)
	{
		if (kind == RadioChannelKind::Guard) return GuardMhz;
		uint32_t khz = SlotKHz(BandOf(kind), PreferredSlot(kind, ownerKey));
		if (khz == GuardKHz) khz += BandOf(kind).StepKHz;
		return khz / 1000.0;
	}

	uint64_t RadioChannels::Stamp(const StarSystem& system)
	{
		uint64_t h = Combine(0xcbf29ce484222325ull, reinterpret_cast<uintptr_t>(system.World.get()));
		const World* world = system.World.get();
		if (world == nullptr) return h;
		if (world->Economy != nullptr)
		{
			const auto& settlements = world->Economy->Settlements;
			h = Combine(h, static_cast<uint64_t>(settlements.Count()));
			for (int i = 0; i < settlements.Count(); i++)
			{
				const Settlement* s = settlements[i].get();
				if (s == nullptr) continue;
				h = Combine(h, reinterpret_cast<uintptr_t>(s));
				h = Combine(h, (static_cast<uint64_t>(static_cast<uint32_t>(s->Id)) << 1) | (s->Hidden ? 1u : 0u));
			}
		}
		if (world->Fleet != nullptr)
		{
			const auto& factions = world->Fleet->Factions();
			h = Combine(h, static_cast<uint64_t>(factions.Count()));
			for (int i = 0; i < factions.Count(); i++)
			{
				const Faction* f = factions[i].get();
				if (f == nullptr) continue;
				h = Combine(h, (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(f)) << 1) | (f->IsPirate ? 1u : 0u));
			}
		}
		return h;
	}

	void RadioChannels::Build(RadioChannelSet& set, const StarSystem& system, int systemId)
	{
		set.channels.clear();
		set.byId.clear();
		set.byOwner.clear();
		std::unordered_set<uint32_t> taken;

		auto add = [&](RadioChannelKind kind, uint32_t ownerKey, int32_t settlementId, cs::String name, const void* owner)
		{
			RadioChannel c;
			c.Kind = kind;
			c.OwnerKey = ownerKey;
			c.SettlementId = settlementId;
			c.SystemId = systemId;
			c.Name = std::move(name);
			c.Encrypted = kind == RadioChannelKind::Fleet;

			uint32_t id = IdOf(kind, ownerKey);
			while (set.byId.count(id) != 0) id = Characters::Mix(id + 1u) | 1u;
			c.Id = id;

			if (kind == RadioChannelKind::Guard)
				c.FrequencyMhz = GuardMhz;
			else
			{
				const RadioBand& band = BandOf(kind);
				const uint32_t first = PreferredSlot(kind, ownerKey);
				uint32_t khz = 0;
				for (uint32_t k = 0; k < band.Slots; k++)
				{
					khz = SlotKHz(band, first + k);
					if (khz != GuardKHz && taken.count(khz) == 0) break;
				}
				taken.insert(khz);
				c.FrequencyMhz = khz / 1000.0;
			}

			const int index = static_cast<int>(set.channels.size());
			set.byId.emplace(c.Id, index);
			if (owner != nullptr) set.byOwner.emplace(owner, index);
			set.channels.push_back(std::move(c));
		};

		const uint32_t systemKey = KeyOf(system);
		add(RadioChannelKind::Guard, 0, -1, "Guard", nullptr);
		add(RadioChannelKind::Civil, systemKey, -1, system.Name + " Civil", nullptr);
		add(RadioChannelKind::Relay, systemKey, -1, system.Name + " Relay", nullptr);

		const World* world = system.World.get();
		if (world == nullptr) return;
		if (world->Economy != nullptr)
		{
			for (const auto& s : world->Economy->Settlements)
				if (s != nullptr && !s->Hidden)
					add(RadioChannelKind::Traffic, KeyOf(*s), s->Id, s->Name + " Traffic", s.get());
		}
		if (world->Fleet != nullptr)
		{
			for (const auto& f : world->Fleet->Factions())
				if (f != nullptr)
					add(f->IsPirate ? RadioChannelKind::Pirate : RadioChannelKind::Fleet, KeyOf(*f), -1,
						f->Name + (f->IsPirate ? " Band" : " Fleet"), f.get());
		}
	}

	const RadioChannelSet& RadioChannels::For(const cs::Ref<StarSystem>& system, int systemId)
	{
		static const RadioChannelSet empty = []
		{
			RadioChannelSet s;
			RadioChannel guard;
			guard.Id = IdOf(RadioChannelKind::Guard, 0);
			guard.FrequencyMhz = GuardMhz;
			guard.Name = "Guard";
			for (int i = 0; i < 3; i++) s.channels.push_back(guard);
			s.byId.emplace(guard.Id, 0);
			return s;
		}();
		if (system == nullptr) return empty;

		const uint64_t stamp = Stamp(*system) ^ static_cast<uint64_t>(static_cast<uint32_t>(systemId));
		auto [it, added] = sets.try_emplace(system.get());
		RadioChannelSet& set = it->second;
		if (added || set.version != stamp)
		{
			Build(set, *system, systemId);
			set.version = stamp;
		}
		return set;
	}
}
