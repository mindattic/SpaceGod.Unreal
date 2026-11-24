#pragma once

#include "Comms/VoiceCasting.h"
#include "Cs/Cs.h"

namespace SpaceGod::Voice
{
	/// One comms line to read aloud.
	struct SpokenLine
	{
		/// Who says it (the log, the audit).
		cs::String Who;
		/// UTF-8.
		cs::String Text;
		/// The default voice until the speaker's own is cast (Core's VoiceCasting).
		Core::VoiceSpec Voice;
		/// Higher is synthesized first.
		int Priority = 0;
		/// Whether the radio sound is put on it.
		bool Radio = true;
	};

	/// Comms read aloud (US-T589; design in US-T574): lines are synthesized through TtsService as they are queued and
	/// played one after another on AudioBus::Voice with GameAudio::Play2D. A line whose clip is not ready ReadyWithin
	/// seconds after its turn comes, or could not be spoken, is skipped: its text is already in the comms log. The game
	/// thread never waits on speech. Silent and empty while GameAudio is disabled (a scripted run). View-local.
	class CommsVoice
	{
	public:
		/// Queues a line; when Capacity lines wait already, the oldest that has not started is dropped.
		static void Speak(SpokenLine line);
		/// Game thread, every frame, after TtsService::Tick: starts the next line when the one playing has ended.
		static void Tick();
		/// Drops every waiting line (a scene change); the one playing ends on its own.
		static void Stop();

		static constexpr double ReadyWithin = 1.5;
		/// The pause between two lines.
		static constexpr double Gap = 0.25;
		static constexpr int Capacity = 8;
	};
}
