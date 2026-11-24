#pragma once

#include "Voice/RadioEffect.h"
#include "Voice/TtsEngine.h"

#include "CoreMinimal.h"

#include <cstdint>

namespace SpaceGod::Voice
{
	/// A spoken line's cache key (0 is none).
	using VoiceKey = std::uint64_t;

	/// Spoken lines kept for reuse (US-T588; design in US-T574). The key is a 64-bit FNV-1a hash of the engine (name and
	/// version), the voice, speaker id, length scale, effect version and the normalised text. An instance is the memory
	/// LRU (game thread only, at most MemoryBytes of samples); the static disk side keeps Saved/VoiceCache/<key>.wav
	/// (PCM16 mono) up to DiskBytes, oldest first out, and is called on TtsService's worker thread only.
	class VoiceCache
	{
	public:
		/// The radio sound's version, so clips spoken under an older effect are not reused.
		static constexpr int EffectVersion = RadioEffect::Version;
		static constexpr int64 MemoryBytes = 32ll * 1024 * 1024;
		static constexpr int64 DiskBytes = 256ll * 1024 * 1024;

		/// Trimmed, every run of whitespace one space.
		static cs::String Normalise(const cs::String& text);
		/// "name/version": the engine part of a key (worker thread: Version may look on disk).
		static cs::String EngineIdOf(ITtsEngine& engine);
		/// 0 when <paramref name="engineId"/> is empty (the engine not known yet).
		static VoiceKey KeyOf(const cs::String& engineId, const TtsRequest& request);

		/// Game thread: the clip, made the most recent; false when it is not held.
		bool Find(VoiceKey key, Audio::SoundClip& clip);
		/// Game thread: keeps it, dropping the least recently used past MemoryBytes.
		void Put(VoiceKey key, const Audio::SoundClip& clip);
		void Clear();

		/// Worker thread: Saved/VoiceCache/<key>.wav, touched so it is pruned last; false when absent or unreadable.
		static bool Load(VoiceKey key, Audio::SoundClip& clip);
		/// Worker thread: writes it, then prunes when the folder has grown past DiskBytes.
		static void Store(VoiceKey key, const Audio::SoundClip& clip);
		/// Worker thread: deletes the oldest files until the folder holds at most DiskBytes.
		static void Prune();

		/// A RIFF PCM16 mono WAV as a clip at its own rate; false when it is not one or is not all written yet.
		static bool ReadWav(const TArray<uint8>& bytes, Audio::SoundClip& clip);

	private:
		struct Entry
		{
			Audio::SoundClip Clip;
			int64 Bytes = 0;
		};
		TMap<VoiceKey, Entry> Clips;
		/// Least recently used first.
		TArray<VoiceKey> Order;
		int64 Held = 0;
	};
}
