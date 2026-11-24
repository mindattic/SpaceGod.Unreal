#pragma once
// US-T646: how the local listener receives a message (US-T578). Clarity compares the flux EIRP / (4 pi d^2) with the
// noise floor (Clean, Noisy with dropped words, Unreadable); Score weighs priority, a line addressed to the listener's
// unit, the listener's own faction, the tuned primary over monitored channels, and proximity; Route sends the arrival to
// Spoken (the voice), Subtitled (the radio log) or Digest (counted only). O(1) per arrival; Garble is O(text) and only
// for lines shown.
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and nothing draws
// from the simulation RNG (dropouts hash the message id).

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"
#include "Radio/RadioTuner.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Faction;

	enum class RadioClarity : uint8_t
	{
		Clean,
		/// <summary>Shown with dropped words, spoken with more static.</summary>
		Noisy,
		/// <summary>Counted only.</summary>
		Unreadable
	};
	SPACEGODCORE_API const char* ToString(RadioClarity v);

	enum class RadioRoute : uint8_t
	{
		/// <summary>Not a channel of the listener's set: neither shown nor counted.</summary>
		Ignored,
		/// <summary>Counted into its channel's summary only.</summary>
		Digest,
		/// <summary>Shown in the radio log.</summary>
		Subtitled,
		/// <summary>Shown and handed to the voice.</summary>
		Spoken
	};
	SPACEGODCORE_API const char* ToString(RadioRoute v);

	/// <summary>Who is listening. Channels null: no tuner state yet, every message counts as heard.</summary>
	struct RadioListener
	{
		const RadioChannelSet* Channels = nullptr;
		const RadioTuner* Tuner = nullptr;
		cs::Ref<Faction> ListenerFaction;
		int32_t FactionId = -1;
		/// <summary>The listener's unit (ship or pilot key) a message may be addressed to; 0 for none.</summary>
		uint64_t UnitKey = 0;
		/// <summary>US-T704: lines below this priority go to the digest (Settings > Interface > Minimum radio priority).</summary>
		int MinPriority = 0;
	};

	/// <summary>One arrival's reception.</summary>
	struct RadioReceptionResult
	{
		RadioClarity Clarity = RadioClarity::Clean;
		RadioAcceptance Acceptance = RadioAcceptance::Heard;
		double Score = 0;
		RadioRoute Route = RadioRoute::Digest;
	};

	class SPACEGODCORE_API RadioReception
	{
	public:
		/// <summary>Signal-to-noise at or above which a line is Clean (within half the class's reach).</summary>
		static constexpr double CleanSnr = 4.0;
		/// <summary>Signal-to-noise at or above which a line is Noisy rather than Unreadable (within 0.8 of the reach).</summary>
		static constexpr double NoisySnr = 1.5625;
		/// <summary>The share of a Noisy line's words dropped.</summary>
		static constexpr double NoisyDropout = 0.3;
		/// <summary>Score at or above which a line is spoken, and at or above which it is shown.</summary>
		static constexpr double SpokenScore = 4.0;
		static constexpr double SubtitledScore = 1.0;
		static constexpr double AddressedWeight = 4.0;
		static constexpr double OwnFactionWeight = 2.0;
		static constexpr double PrimaryWeight = 2.0;

		/// <summary>The flux at <paramref name="distanceKm"/> over the noise floor.</summary>
		static double SignalToNoise(const RadioMessage& message, double distanceKm);
		static RadioClarity Clarity(const RadioMessage& message, double distanceKm);

		/// <summary>
		/// The text with about NoisyDropout of its words dropped, each run of dropped words shown as "...". The words dropped
		/// are a pure function of <paramref name="messageId"/> and their position.
		/// </summary>
		static cs::String Garble(const cs::String& text, uint32_t messageId);

		/// <summary>The priority weight (1, 2, 4, 8) times the addressed, faction, primary and proximity factors.</summary>
		static double Score(const RadioMessage& message, double distanceKm, const RadioListener& listener);

		/// <summary>Clarity, the tuner's Accepts, the listener's minimum priority and the score thresholds. O(1).</summary>
		static RadioReceptionResult Route(const RadioMessage& message, double distanceKm, const RadioListener& listener);
	};
}
