#pragma once
// US-T590: who sounds like whom. Every speaker on the radio (a named pilot, a settlement's traffic control, an unnamed
// hull of a faction, the player's held hull) gets one stable voice, a pure function of who they are: the same pilot
// sounds the same in every session and on every co-op peer. Each faction speaks from its own disjoint slice of the
// libritts_r model's speakers, so two sides never share a voice, and the player has a slice of their own.
//
// View-local presentation: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and
// nothing draws from the simulation's random stream (only the stable hashes of Characters).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Faction;
	class NamedPilot;
	class Settlement;
	class Ship;

	/// <summary>The voice one speaker is read in (US-T590), for the local TTS.</summary>
	struct SPACEGODCORE_API VoiceSpec
	{
		/// <summary>The Piper model it is spoken with.</summary>
		cs::String EngineVoice = "libritts_r";
		/// <summary>The speaker in that model, 0..<see cref="VoiceCasting.Speakers"/>-1.</summary>
		int SpeakerId = 0;
		/// <summary>Piper's length scale: below 1 quicker, above 1 slower.</summary>
		double LengthScale = 1;
		/// <summary>The fallback SAPI voice, taken modulo the voices installed.</summary>
		int SapiIndex = 0;
		/// <summary>The fallback engine's pitch shift, -1..1 (SAPI's -10..10 over ten).</summary>
		double Pitch = 0;
	};

	/// <summary>
	/// Casts voices (US-T590) over one world's factions, in the fleet's own order (saved, so the same on every peer and
	/// in every session): faction i speaks from the i-th slice of the speakers below <see cref="PlayerFirst"/>.
	/// </summary>
	class SPACEGODCORE_API VoiceCasting final
	{
	public:
		explicit VoiceCasting(cs::List<cs::Ref<Faction>> factions);

		/// <summary>A named pilot's own voice, paced by their Chatter trait: loose talkers are quicker.</summary>
		VoiceSpec Of(const cs::Ref<NamedPilot>& pilot) const;
		/// <summary>A settlement's traffic control: one calm voice per settlement, from its owner's slice.</summary>
		VoiceSpec Of(const cs::Ref<Settlement>& settlement) const;
		/// <summary>An unnamed hull of <paramref name="faction"/>: the faction's fallback voice.</summary>
		VoiceSpec Of(const cs::Ref<Faction>& faction) const;
		/// <summary>The player, by the hull they hold, from the player's own slice.</summary>
		VoiceSpec Player(const cs::Ref<Ship>& held) const;

		/// <summary>The speakers in en_US-libritts_r-medium.</summary>
		static constexpr int Speakers = 904;
		/// <summary>The top speakers are kept for the player; the factions share the rest.</summary>
		static constexpr int PlayerSpeakers = 16;
		static constexpr int PlayerFirst = Speakers - PlayerSpeakers;

		/// <summary>Length scales for the quickest (Chatter 1) and slowest (Chatter 0) talkers, and for traffic control.</summary>
		static constexpr double QuickestLength = 0.9;
		static constexpr double SlowestLength = 1.15;
		static constexpr double ControlLength = 1.1;

	private:
		cs::List<cs::Ref<Faction>> factions;

		/// <summary>The faction's slice of the speakers: first id and how many (all below PlayerFirst when unknown).</summary>
		void SliceOf(const cs::Ref<Faction>& faction, int& first, int& count) const;
		VoiceSpec Cast(const cs::Ref<Faction>& faction, uint32_t hash, double lengthScale) const;
		static void Fallback(VoiceSpec& v, uint32_t hash);
	};
}
