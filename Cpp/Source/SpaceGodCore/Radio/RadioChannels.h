#pragma once
// US-T639: a star system's radio channels (US-T576). Guard (distress, everyone monitors it), one Traffic channel per
// known settlement, one Civil and one Relay (interstellar bulletins) per system, one encrypted Fleet channel per faction
// and one Pirate channel per raider clan. Frequencies are a pure function of kind and owner, inside each kind's band, so
// a channel keeps its number across sessions and on every peer. A system's set is built once and rebuilt only when its
// version stamp (settlement and faction counts and ids) changes; lookups by id or owner are O(1).
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and nothing draws
// from the simulation's random stream.

#include "Cs/Cs.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace SpaceGod::Core
{
	class Faction;
	class Settlement;
	class StarSystem;

	enum class RadioChannelKind : uint8_t
	{
		Guard,
		Traffic,
		Civil,
		Fleet,
		Pirate,
		Relay,
		Count
	};
	SPACEGODCORE_API const char* ToString(RadioChannelKind v);

	/// <summary>One channel of a system, as plain data.</summary>
	struct SPACEGODCORE_API RadioChannel
	{
		/// <summary>Stable from kind and owner (never 0), unique within its system's set.</summary>
		uint32_t Id = 0;
		RadioChannelKind Kind = RadioChannelKind::Guard;
		/// <summary>The owner's stable key: the settlement (Traffic), the faction (Fleet, Pirate), the system (Civil, Relay); 0 for Guard.</summary>
		uint32_t OwnerKey = 0;
		/// <summary>The owning settlement's Id for a Traffic channel, else -1.</summary>
		int32_t SettlementId = -1;
		int32_t SystemId = -1;
		double FrequencyMhz = 0;
		/// <summary>What the tuner shows, such as "Ceres Traffic".</summary>
		cs::String Name;
		/// <summary>A faction's fleet net: outsiders hear only that there is traffic.</summary>
		bool Encrypted = false;
	};

	/// <summary>The channels of one system, built by <see cref="RadioChannels.For"/>.</summary>
	class SPACEGODCORE_API RadioChannelSet final
	{
	public:
		/// <summary>Every channel: Guard first, then Civil, Relay, the settlements' Traffic and the factions' Fleet or Pirate.</summary>
		const std::vector<RadioChannel>& All() const { return channels; }

		/// <summary>The channel with this id, or null.</summary>
		const RadioChannel* Find(uint32_t id) const;

		const RadioChannel& Guard() const { return channels[0]; }
		const RadioChannel& Civil() const { return channels[1]; }
		const RadioChannel& Relay() const { return channels[2]; }

		/// <summary>The settlement's traffic control channel; null for a hidden haven or a settlement of another system.</summary>
		const RadioChannel* TrafficOf(const cs::Ref<Settlement>& settlement) const;

		/// <summary>The faction's own net: its encrypted Fleet channel, or a raider clan's Pirate channel; null if not here.</summary>
		const RadioChannel* FleetOf(const cs::Ref<Faction>& faction) const;

		/// <summary>The version stamp the set was built at.</summary>
		uint64_t Version() const { return version; }

	private:
		friend class RadioChannels;

		std::vector<RadioChannel> channels;
		std::unordered_map<uint32_t, int> byId;
		std::unordered_map<const void*, int> byOwner;
		uint64_t version = 0;
	};

	/// <summary>Builds and caches each system's <see cref="RadioChannelSet"/>.</summary>
	class SPACEGODCORE_API RadioChannels final
	{
	public:
		/// <summary>Guard's fixed frequency, MHz.</summary>
		static constexpr double GuardMhz = 121.5;

		/// <summary>
		/// The system's channels, rebuilt only when its <see cref="Stamp"/> has changed since the last call. The reference
		/// stays valid until the next For on the same system or <see cref="Clear"/>.
		/// </summary>
		const RadioChannelSet& For(const cs::Ref<StarSystem>& system, int systemId);

		/// <summary>A cheap version stamp of what the channels are built from: the settlements (ids, hidden) and factions.</summary>
		static uint64_t Stamp(const StarSystem& system);

		/// <summary>The channel id of a kind and owner key before any in-system collision is resolved (never 0).</summary>
		static uint32_t IdOf(RadioChannelKind kind, uint32_t ownerKey);

		/// <summary>The kind's preferred frequency for an owner key, inside its band, MHz (Guard always 121.5).</summary>
		static double FrequencyMhz(RadioChannelKind kind, uint32_t ownerKey);

		static uint32_t KeyOf(const Settlement& settlement);
		static uint32_t KeyOf(const Faction& faction);
		static uint32_t KeyOf(const StarSystem& system);

		/// <summary>Drops every cached set.</summary>
		void Clear() { sets.clear(); }

	private:
		static void Build(RadioChannelSet& set, const StarSystem& system, int systemId);

		std::unordered_map<const StarSystem*, RadioChannelSet> sets;
	};
}
