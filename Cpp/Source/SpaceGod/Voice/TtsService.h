#pragma once

#include "Voice/TtsEngine.h"

#include <climits>
#include <functional>

namespace SpaceGod::Voice
{
	/// A request taken by TtsService: 0 is none.
	using TtsId = int;
	/// Called on the game thread with the spoken line, or an invalid clip when it was dropped or could not be spoken.
	using TtsDone = std::function<void(TtsId id, const Audio::SoundClip& clip)>;

	/// Speech off the game thread (US-T587; design in US-T574): one worker thread at below-normal priority takes the
	/// highest-priority request from a bounded queue and answers it from the voice cache (US-T660: memory on the game
	/// thread, Saved/VoiceCache on the worker) or else synthesizes it (Piper when it is available, else SAPI) and caches
	/// it; results come back through a lock-free queue that Tick drains on the game thread. The game thread never waits
	/// on the engine, its process, pipe or files. Nothing runs while GameAudio is disabled (a scripted run, no device,
	/// muted). View-local: nothing here touches the simulation, the lockstep frames, saves or the digests.
	class TtsService
	{
	public:
		/// Queues a line; <paramref name="done"/> is called from a later Tick. Returns 0, and never calls it, when
		/// GameAudio is disabled or the queue is full of requests of higher priority. A request it evicts (the lowest,
		/// the oldest of those) is answered with an invalid clip.
		static TtsId Request(TtsRequest request, TtsDone done);
		/// Speaks a line ahead of need into the cache, below every Request (PrefetchPriority); nothing when it is cached
		/// in memory, already queued or being spoken, or GameAudio is disabled.
		static void Prefetch(TtsRequest request);
		/// Drops a request: its callback is never called (the line being spoken is still spoken, then discarded).
		static void Cancel(TtsId id);
		/// Game thread, every frame: hands the finished lines to their callbacks.
		static void Tick();
		/// Stops the worker and its engine (exit); a later Request starts them again.
		static void Shutdown();
		/// US-T649: Settings > Sound > Voice engine: 0 Auto (Piper when available, else SAPI), 1 Piper only, 2 Windows
		/// (SAPI) only. The worker picks it up before its next line; the cache key follows the engine that speaks.
		static void SetEngine(int engine);

		/// US-T650: plain counters for STATGROUP_SpaceGodRadio and sg.Radio stats (any thread may have added to them).
		struct Stats
		{
			/// Requests waiting for the worker now.
			int QueueDepth = 0;
			/// Answered from the memory or disk cache, since the start.
			unsigned long long CacheHits = 0;
			/// Synthesized, since the start.
			unsigned long long CacheMisses = 0;
			/// The mean time of a synthesis (with its effect), ms.
			double MeanSynthMs = 0;
		};
		static Stats GetStats();

		/// The most requests waiting at once.
		static constexpr int Capacity = 16;
		static constexpr int PrefetchPriority = INT_MIN;
	};
}
