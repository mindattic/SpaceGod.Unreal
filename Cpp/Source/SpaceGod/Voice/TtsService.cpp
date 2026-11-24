#include "Voice/TtsService.h"

#include "Audio/GameAudio.h"
#include "Voice/PiperTts.h"
#include "Voice/SapiTts.h"
#include "Voice/VoiceCache.h"

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"

#include <atomic>
#include <memory>

namespace SpaceGod::Voice
{
	namespace
	{
		struct Job
		{
			TtsId Id = 0;
			TtsRequest Request;
			/// As the game thread saw the engine when it was queued (0 when not known yet).
			VoiceKey Key = 0;
		};

		struct Result
		{
			TtsId Id = 0;
			/// Under the engine that answered it (0 when none could).
			VoiceKey Key = 0;
			Audio::SoundClip Clip;
		};

		class Worker final : public FRunnable
		{
		public:
			FCriticalSection Lock;
			/// Guarded by Lock; at most Capacity.
			TArray<Job> Waiting;
			/// Guarded by Lock: VoiceCache::EngineIdOf the engine the worker speaks with, empty until it has chosen.
			cs::String EngineId;
			/// Worker in, game thread out.
			TQueue<Result, EQueueMode::Spsc> Done;
			FEvent* Wake = FPlatformProcess::GetSynchEventFromPool(false);
			std::atomic<bool> Stopping { false };
			std::unique_ptr<ITtsEngine> Piper = std::make_unique<PiperTts>();
			/// US-T598: when Piper is missing or has given up.
			std::unique_ptr<ITtsEngine> Sapi = std::make_unique<SapiTts>();

			~Worker() override { FPlatformProcess::ReturnSynchEventToPool(Wake); }

			/// The engine that speaks now (Piper, else SAPI), its id published for the game thread's keys.
			ITtsEngine* Choose()
			{
				ITtsEngine* engine = Piper->Available() ? Piper.get() : Sapi->Available() ? Sapi.get() : nullptr;
				if (engine != Chosen)
				{
					Chosen = engine;
					ChosenId = engine ? VoiceCache::EngineIdOf(*engine) : cs::String();
					FScopeLock scope(&Lock);
					EngineId = ChosenId;
				}
				return engine;
			}

			uint32 Run() override
			{
				VoiceCache::Prune();
				Choose();
				while (!Stopping)
				{
					Job job;
					{
						FScopeLock scope(&Lock);
						int32 best = INDEX_NONE;
						for (int32 i = 0; i < Waiting.Num(); ++i)
							if (best == INDEX_NONE || Waiting[i].Request.Priority > Waiting[best].Request.Priority) best = i;
						if (best != INDEX_NONE)
						{
							job = MoveTemp(Waiting[best]);
							Waiting.RemoveAt(best);
						}
					}
					if (!job.Id)
					{
						Wake->Wait(100);
						continue;
					}
					Result result { job.Id, 0, {} };
					if (ITtsEngine* engine = Choose())
					{
						result.Key = VoiceCache::KeyOf(ChosenId, job.Request);
						if (!VoiceCache::Load(result.Key, result.Clip))
						{
							result.Clip = engine->Synthesize(job.Request);
							if (result.Clip.IsValid() && !Stopping) VoiceCache::Store(result.Key, result.Clip);
						}
					}
					Done.Enqueue(MoveTemp(result));
				}
				return 0;
			}

			void Stop() override
			{
				Stopping = true;
				Piper->Abort();
				Sapi->Abort();
				Wake->Trigger();
			}

		private:
			/// Worker thread only.
			ITtsEngine* Chosen = nullptr;
			cs::String ChosenId;
		};

		/// Game thread only.
		struct State
		{
			std::unique_ptr<Worker> Runnable;
			FRunnableThread* Thread = nullptr;
			TMap<TtsId, TtsDone> Callbacks;
			/// Evicted on the game thread: answered at the next Tick.
			TArray<TtsId> Evicted;
			/// Memory hits: answered at the next Tick.
			TArray<Result> Hits;
			/// Queued and in-flight requests and prefetches with a known key, so a prefetch is not spoken twice.
			TMap<TtsId, VoiceKey> Keys;
			VoiceCache Cache;
			TtsId Next = 0;
			bool ExitHooked = false;
		};

		State& S()
		{
			static State s;
			return s;
		}

		Worker* Started()
		{
			State& s = S();
			if (s.Thread) return s.Runnable.get();
			s.Runnable = std::make_unique<Worker>();
			s.Thread = FRunnableThread::Create(s.Runnable.get(), TEXT("SpaceGodTts"), 0, TPri_BelowNormal);
			if (!s.Thread)
			{
				s.Runnable.reset();
				return nullptr;
			}
			if (!s.ExitHooked)
			{
				s.ExitHooked = true;
				FCoreDelegates::OnEnginePreExit.AddStatic(&TtsService::Shutdown);
			}
			return s.Runnable.get();
		}

		VoiceKey KeyOf(Worker& worker, const TtsRequest& request)
		{
			cs::String engineId;
			{
				FScopeLock scope(&worker.Lock);
				engineId = worker.EngineId;
			}
			return VoiceCache::KeyOf(engineId, request);
		}

		/// Puts a job on the worker's queue; 0 when it is full of requests of higher priority.
		TtsId Enqueue(Worker& worker, TtsRequest request, VoiceKey key)
		{
			State& s = S();
			const TtsId id = ++s.Next;
			{
				FScopeLock scope(&worker.Lock);
				if (worker.Waiting.Num() >= TtsService::Capacity)
				{
					int32 lowest = 0;
					for (int32 i = 1; i < worker.Waiting.Num(); ++i)
						if (worker.Waiting[i].Request.Priority < worker.Waiting[lowest].Request.Priority) lowest = i;
					if (worker.Waiting[lowest].Request.Priority >= request.Priority) return 0;
					s.Evicted.Add(worker.Waiting[lowest].Id);
					worker.Waiting.RemoveAt(lowest);
				}
				worker.Waiting.Add(Job { id, MoveTemp(request), key });
			}
			if (key) s.Keys.Add(id, key);
			worker.Wake->Trigger();
			return id;
		}
	}

	TtsId TtsService::Request(TtsRequest request, TtsDone done)
	{
		if (!Audio::GameAudio::Enabled()) return 0;
		Worker* worker = Started();
		if (!worker) return 0;
		State& s = S();
		const VoiceKey key = KeyOf(*worker, request);
		Audio::SoundClip clip;
		TtsId id = 0;
		if (key && s.Cache.Find(key, clip))
		{
			id = ++s.Next;
			s.Hits.Add(Result { id, key, MoveTemp(clip) });
		}
		else id = Enqueue(*worker, MoveTemp(request), key);
		if (id) s.Callbacks.Add(id, MoveTemp(done));
		return id;
	}

	void TtsService::Prefetch(TtsRequest request)
	{
		if (!Audio::GameAudio::Enabled()) return;
		Worker* worker = Started();
		if (!worker) return;
		State& s = S();
		const VoiceKey key = KeyOf(*worker, request);
		Audio::SoundClip clip;
		if (key && (s.Cache.Find(key, clip) || s.Keys.FindKey(key))) return;
		request.Priority = PrefetchPriority;
		Enqueue(*worker, MoveTemp(request), key);
	}

	void TtsService::Cancel(TtsId id)
	{
		State& s = S();
		s.Keys.Remove(id);
		if (!s.Callbacks.Remove(id) || !s.Runnable) return;
		FScopeLock scope(&s.Runnable->Lock);
		s.Runnable->Waiting.RemoveAll([id](const Job& job) { return job.Id == id; });
	}

	void TtsService::Tick()
	{
		State& s = S();
		if (!s.Runnable) return;
		TArray<Result> hits = MoveTemp(s.Hits);
		for (const Result& hit : hits)
			if (TtsDone done; s.Callbacks.RemoveAndCopyValue(hit.Id, done) && done) done(hit.Id, hit.Clip);
		for (const TtsId id : s.Evicted)
		{
			s.Keys.Remove(id);
			if (TtsDone done; s.Callbacks.RemoveAndCopyValue(id, done) && done) done(id, {});
		}
		s.Evicted.Reset();
		Result result;
		while (s.Runnable && s.Runnable->Done.Dequeue(result))
		{
			s.Keys.Remove(result.Id);
			s.Cache.Put(result.Key, result.Clip);
			if (TtsDone done; s.Callbacks.RemoveAndCopyValue(result.Id, done) && done) done(result.Id, result.Clip);
		}
	}

	void TtsService::Shutdown()
	{
		State& s = S();
		if (s.Thread)
		{
			s.Thread->Kill(/*bShouldWait*/ true);
			delete s.Thread;
			s.Thread = nullptr;
		}
		s.Runnable.reset();
		s.Callbacks.Reset();
		s.Evicted.Reset();
		s.Hits.Reset();
		s.Keys.Reset();
	}
}
