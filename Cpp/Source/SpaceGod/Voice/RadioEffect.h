#pragma once

#include "Audio/SynthSound.h"

#include <cstdint>

namespace SpaceGod::Voice
{
	/// The radio sound on a spoken line (US-T589; design in US-T574), put on the PCM on TtsService's worker before the
	/// clip is cached, never on the game thread: a 300-3400 Hz band-pass (two biquads), a soft clip, a static bed at
	/// about -30 dB and a 40 ms squelch click before and after the line. Its noise comes from its own generator seeded
	/// by the line's cache key, so a line always sounds the same and nothing draws from the simulation's random stream.
	namespace RadioEffect
	{
		/// Part of the cache key (VoiceCache::EffectVersion): bump it when the sound below changes.
		inline constexpr int Version = 2;

		inline constexpr float LowHz = 300.0f;
		inline constexpr float HighHz = 3400.0f;
		inline constexpr float StaticDb = -30.0f;
		inline constexpr float SquelchSeconds = 0.04f;

		/// The line with the radio sound on it, 80 ms longer; an invalid clip stays as it is.
		void Apply(Audio::SoundClip& clip, std::uint64_t seed);

		/// One 40 ms squelch click with the radio band-pass and soft clip on it (US-T684: an urgent line cutting the one
		/// playing), rendered once on first use and shared after; always the same click.
		const Audio::SoundClip& SquelchClip();
	}
}
