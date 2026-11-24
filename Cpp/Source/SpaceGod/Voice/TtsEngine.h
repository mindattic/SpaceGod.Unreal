#pragma once
// US-T587: a local text-to-speech engine (docs: US-T574). Engines run on TtsService's worker thread only; the game
// thread never calls into one and never waits on synthesis.

#include "Audio/SynthSound.h"
#include "Comms/VoiceCasting.h"
#include "Cs/Cs.h"

namespace SpaceGod::Voice
{
	/// One line to speak.
	struct TtsRequest
	{
		/// UTF-8.
		cs::String Text;
		/// Who speaks it (Core's VoiceCasting): the engine voice, speaker id, length scale and SAPI fallback index.
		Core::VoiceSpec Voice;
		/// Higher is spoken first; the lowest is dropped first when the queue is full.
		int Priority = 0;
		/// Whether the radio sound is put on it.
		bool Effect = true;
	};

	/// A speech synthesizer, called on the worker thread only.
	class ITtsEngine
	{
	public:
		virtual ~ITtsEngine() = default;

		/// For the log and the cache key.
		virtual const char* Name() const = 0;
		virtual cs::String Version() const = 0;
		/// Whether Synthesize can speak at all (installed, not given up).
		virtual bool Available() = 0;
		/// The line as mono PCM16 at the engine's own rate; an invalid clip when it could not be spoken.
		virtual Audio::SoundClip Synthesize(const TtsRequest& request) = 0;
		/// Any thread: a Synthesize in progress gives up now, and every later one does nothing (exit).
		virtual void Abort() {}
	};
}
