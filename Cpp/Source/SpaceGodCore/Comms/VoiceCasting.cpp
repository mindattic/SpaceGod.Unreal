// US-T590: see VoiceCasting.h.

#include "Comms/VoiceCasting.h"

#include "Comms/Pilots.h"
#include "Economy/Settlement.h"
#include "Ships.h"

#include <algorithm>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr uint32_t PilotSalt = 0x50494c54u, ControlSalt = 0x41544331u, FactionSalt = 0x46414354u, PlayerSalt = 0x504c4159u;

		uint32_t Identity(uint32_t salt, int id, const cs::String& name)
		{
			return Characters::Mix(salt ^ Characters::Mix(static_cast<uint32_t>(id) * 2654435761u) ^ Characters::HashText(name));
		}
	}

	VoiceCasting::VoiceCasting(cs::List<cs::Ref<Faction>> factions) : factions(std::move(factions)) {}

	void VoiceCasting::SliceOf(const cs::Ref<Faction>& faction, int& first, int& count) const
	{
		first = 0;
		count = PlayerFirst;
		const int n = factions.Count();
		if (faction == nullptr || n == 0) return;
		for (int i = 0; i < n; i++)
		{
			if (factions[i] != faction) continue;
			const int size = PlayerFirst / n;
			first = i * size;
			count = size;
			return;
		}
	}

	void VoiceCasting::Fallback(VoiceSpec& v, uint32_t hash)
	{
		const uint32_t h = Characters::Mix(hash ^ 0x53415049u);
		v.SapiIndex = static_cast<int>(h & 0xffffu);
		v.Pitch = ((h >> 16) & 0xffu) / 255.0 * 2 - 1;
	}

	VoiceSpec VoiceCasting::Cast(const cs::Ref<Faction>& faction, uint32_t hash, double lengthScale) const
	{
		int first, count;
		SliceOf(faction, first, count);
		VoiceSpec v;
		v.SpeakerId = first + static_cast<int>(hash % static_cast<uint32_t>(std::max(1, count)));
		v.LengthScale = lengthScale;
		Fallback(v, hash);
		return v;
	}

	VoiceSpec VoiceCasting::Of(const cs::Ref<NamedPilot>& pilot) const
	{
		if (pilot == nullptr) return VoiceSpec();
		const double chatter = std::clamp(pilot->Chatter, 0.0, 1.0);
		return Cast(pilot->Faction, Identity(PilotSalt, pilot->Id, pilot->Name), SlowestLength + (QuickestLength - SlowestLength) * chatter);
	}

	VoiceSpec VoiceCasting::Of(const cs::Ref<Settlement>& settlement) const
	{
		if (settlement == nullptr) return VoiceSpec();
		return Cast(settlement->Owner, Identity(ControlSalt, settlement->Id, settlement->Name), ControlLength);
	}

	VoiceSpec VoiceCasting::Of(const cs::Ref<Faction>& faction) const
	{
		if (faction == nullptr) return VoiceSpec();
		return Cast(faction, Identity(FactionSalt, 0, faction->Name), 1);
	}

	VoiceSpec VoiceCasting::Player(const cs::Ref<Ship>& held) const
	{
		const uint32_t hash = held != nullptr ? Identity(PlayerSalt, held->Id, held->Name) : Characters::Mix(PlayerSalt);
		VoiceSpec v;
		v.SpeakerId = PlayerFirst + static_cast<int>(hash % static_cast<uint32_t>(PlayerSpeakers));
		Fallback(v, hash);
		return v;
	}
}
