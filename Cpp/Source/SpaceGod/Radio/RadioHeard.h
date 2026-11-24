#pragma once
// US-T674: what the local listener has heard on the radio, as lines to show (the HUD's radio log, the Logs screen's Radio
// tab). A sink on the app's RadioHost, held by the app after the host so it is gone first; the HUD only reads it. Each
// arrival is realised once, when heard: "[<channel> <MHz>] <sender>: <text>  - sent <light time> ago, <range>".
// View-local like the host: nothing here enters the simulation or saves.

#include "Comms/VoiceCasting.h"
#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioHost.h"

#include <deque>
#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class World;
}

namespace SpaceGod::Radio
{
	class RadioHeard final : public IRadioSink
	{
	public:
		/// <summary>Lines kept, oldest dropped first.</summary>
		static constexpr int Capacity = 200;

		/// <summary>How a line is coloured: PlayerOptions' Hostile, Wary, Friendly or Neutral colour.</summary>
		/// <summary>Digest (US-T703): a RadioDigest summary line, shown neutral and dimmed.</summary>
		enum class Tone : uint8_t { Neutral, Hostile, Wary, Friendly, Digest };
		/// <summary>US-T703: real seconds between two spoken digest lines.</summary>
		static constexpr float DigestSpeakSeconds = 120.0f;

		struct HeardLine
		{
			cs::String Text;
			Tone Kind = Tone::Neutral;
			double ArrivalJd = 0;
			/// <summary>Time::unscaledTime when it was heard.</summary>
			float Heard = 0;
		};

		explicit RadioHeard(UnitySpaceApp& app);
		~RadioHeard() override;
		RadioHeard(const RadioHeard&) = delete;
		RadioHeard& operator=(const RadioHeard&) = delete;

		void OnRadioArrival(const RadioHost& host, const Core::RadioArrival& arrival, const Core::RadioReceptionResult& reception) override;

		/// <summary>Oldest first.</summary>
		const std::deque<HeardLine>& Lines() const { return lines; }
		/// <summary>The clock went back (a rewind or a load): drops the lines that arrived after <paramref name="jd"/>.</summary>
		void DropAfter(double jd);
		void Clear() { lines.clear(); }
		/// <summary>
		/// US-T703: logs each digest summary taken at game <paramref name="jd"/> (Tone::Digest) and speaks one (a distress first,
		/// else the largest Total) at the lowest score, only when the voice is idle, nothing waits and DigestSpeakSeconds have
		/// passed since the last.
		/// </summary>
		void AddDigest(const std::vector<Core::RadioDigestLine>& digest, double jd);

	private:
		/// <summary>US-T685: the voice a message's emitter is cast in (VoiceCasting).</summary>
		static Core::VoiceSpec VoiceOf(const cs::Ref<Core::World>& world, const Core::RadioMessage& m);

		UnitySpaceApp& app;
		Core::RadioChannels channels;
		std::deque<HeardLine> lines;
		/// <summary>Time::unscaledTime of the last spoken digest line; none yet when negative.</summary>
		float lastDigestSpoken = -1.0f;
	};
}
