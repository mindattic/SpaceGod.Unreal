#include "Voice/TtsService.h"

#include "Audio/GameAudio.h"
#include "Voice/PiperTts.h"
#include "Voice/SapiTts.h"

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
		};

		struct Result
		{
			TtsId Id = 0;
			Audio::SoundClip Clip;
		};

		class Worker final : public FRunnable
		{
		public:
			FCriticalSection Lock;
			/// Guarded by Lock; at most Capacity.
			TArray<Job> Waiting;
			/// Worker in, game thread out.
			TQueue<Result, EQueueMode::Spsc> Done;
			FEvent* Wake = FPlatformProcess::GetSynchEventFromPool(false);
			std::atomic<bool> Stopping { false };
			std::unique_ptr<ITtsEngine> Piper = std::make_unique<PiperTts>();
			/// US-T598: when Piper is missing or has given up.
			std::unique_ptr<ITtsEngine> Sapi = std::make_unique<SapiTts>();

			~Worker() override { FPlatformProcess::ReturnSynchEventToPool(Wake); }

			uint32 Run() override
			{
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
					Result result { job.Id, {} };
					if (Piper->Available()) result.Clip = Piper->Synthesize(job.Request);
					else if (Sapi->Available()) result.Clip = Sapi->Synthesize(job.Request);
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
		};

		/// Game thread only.
		struct State
		{
			std::unique_ptr<Worker> Runnable;
			FRunnableThread* Thread = nullptr;
			TMap<TtsId, TtsDone> Callbacks;
			/// Evicted on the game thread: answered at the next Tick.
			TArray<TtsId> Evicted;
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
	}

	TtsId TtsService::Request(TtsRequest request, TtsDone done)
	{
		if (!Audio::GameAudio::Enabled()) return 0;
		Worker* worker = Started();
		if (!worker) return 0;
		State& s = S();
		const TtsId id = ++s.Next;
		{
			FScopeLock scope(&worker->Lock);
			if (worker->Waiting.Num() >= Capacity)
			{
				int32 lowest = 0;
				for (int32 i = 1; i < worker->Waiting.Num(); ++i)
					if (worker->Waiting[i].Request.Priority < worker->Waiting[lowest].Request.Priority) lowest = i;
				if (worker->Waiting[lowest].Request.Priority >= request.Priority) return 0;
				s.Evicted.Add(worker->Waiting[lowest].Id);
				worker->Waiting.RemoveAt(lowest);
			}
			worker->Waiting.Add(Job { id, MoveTemp(request) });
		}
		s.Callbacks.Add(id, MoveTemp(done));
		worker->Wake->Trigger();
		return id;
	}

	void TtsService::Cancel(TtsId id)
	{
		State& s = S();
		if (!s.Callbacks.Remove(id) || !s.Runnable) return;
		FScopeLock scope(&s.Runnable->Lock);
		s.Runnable->Waiting.RemoveAll([id](const Job& job) { return job.Id == id; });
	}

	void TtsService::Tick()
	{
		State& s = S();
		if (!s.Runnable) return;
		for (const TtsId id : s.Evicted)
			if (TtsDone done; s.Callbacks.RemoveAndCopyValue(id, done) && done) done(id, {});
		s.Evicted.Reset();
		Result result;
		while (s.Runnable && s.Runnable->Done.Dequeue(result))
			if (TtsDone done; s.Callbacks.RemoveAndCopyValue(result.Id, done) && done) done(result.Id, result.Clip);
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
	}
}
