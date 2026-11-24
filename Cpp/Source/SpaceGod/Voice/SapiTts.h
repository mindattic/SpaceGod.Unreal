#pragma once

#include "Voice/TtsEngine.h"

#include <atomic>
#include <vector>

namespace SpaceGod::Voice
{
	/// The Windows speech fallback (US-T598) when Piper is missing or has given up: SAPI 5 speaking into a memory stream
	/// as 22.05 kHz PCM16 mono, on TtsService's worker thread (COM is initialised there, multithreaded, on first use).
	/// The voice is the installed voice at VoiceSpec::SapiIndex modulo how many there are, with VoiceSpec::Pitch and the
	/// length scale as SAPI's pitch and rate. Not on Windows, or with no voice installed, it is never available.
	class SapiTts final : public ITtsEngine
	{
	public:
		~SapiTts() override;

		const char* Name() const override { return "sapi"; }
		cs::String Version() const override { return "5"; }
		bool Available() override;
		Audio::SoundClip Synthesize(const TtsRequest& request) override;
		void Abort() override { Aborted = true; }

		/// The rate SAPI is asked to speak at.
		static constexpr int SampleRate = 22050;
		/// How long one line may take before it is given up.
		static constexpr double TimeoutSeconds = 30.0;

	private:
		bool Started = false;
		bool Usable = false;
		/// ISpVoice.
		void* Voice = nullptr;
		/// ISpObjectToken, one per installed voice.
		std::vector<void*> Voices;
		std::atomic<bool> Aborted { false };

		void Start();
	};
}
