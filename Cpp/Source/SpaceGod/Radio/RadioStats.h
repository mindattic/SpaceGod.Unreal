#pragma once
// US-T650: the radio's profiling hooks (US-T580). STATGROUP_SpaceGodRadio ("stat SpaceGodRadio") holds a cycle counter per
// stage (host tick, receiver Advance and Rebase, chatter sampler, event chatter, reception, voice arbiter, digests, TTS)
// and dword counters (log size, heap size, arrivals this frame, digested, TTS queue depth, cache hits and misses, mean
// synth ms). SG_RADIO_SCOPE(Stage) opens the stage's cycle counter, an Unreal Insights CPU scope and a plain timer whose
// last-frame time sg.Radio stats prints (stats compile out of shipping builds; the timer does not). The Core radio files
// stay free of Unreal headers: the game reads their plain counters into these stats. View-local and read-only.

#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Stats/Stats.h"

#include <array>
#include <cstdint>

DECLARE_STATS_GROUP(TEXT("SpaceGodRadio"), STATGROUP_SpaceGodRadio, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Radio tick (all)"), STAT_SpaceGodRadio_Tick, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RadioHost::Tick"), STAT_SpaceGodRadio_HostTick, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Receiver Advance"), STAT_SpaceGodRadio_Advance, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Receiver Rebase"), STAT_SpaceGodRadio_Rebase, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ChatterSampler"), STAT_SpaceGodRadio_Sampler, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ChatterEvents"), STAT_SpaceGodRadio_Events, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RadioReception"), STAT_SpaceGodRadio_Reception, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("VoiceArbiter"), STAT_SpaceGodRadio_Arbiter, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RadioDigest"), STAT_SpaceGodRadio_Digest, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("TtsService::Tick"), STAT_SpaceGodRadio_TtsTick, STATGROUP_SpaceGodRadio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("TTS synth (worker)"), STAT_SpaceGodRadio_TtsSynth, STATGROUP_SpaceGodRadio, );

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Log size"), STAT_SpaceGodRadio_LogSize, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Heap size"), STAT_SpaceGodRadio_HeapSize, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Arrivals this frame"), STAT_SpaceGodRadio_Arrivals, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Digested (pending)"), STAT_SpaceGodRadio_Digested, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("TTS queue depth"), STAT_SpaceGodRadio_TtsQueue, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("TTS cache hits"), STAT_SpaceGodRadio_CacheHits, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("TTS cache misses"), STAT_SpaceGodRadio_CacheMisses, STATGROUP_SpaceGodRadio, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("TTS mean synth ms"), STAT_SpaceGodRadio_MeanSynthMs, STATGROUP_SpaceGodRadio, );

namespace SpaceGod::Radio
{
	/// <summary>The game-thread stages timed for sg.Radio stats (the TTS worker's synth is reported by TtsService).</summary>
	enum class RadioStage : int
	{
		Tick,
		HostTick,
		Advance,
		Rebase,
		Sampler,
		Events,
		Reception,
		Arbiter,
		Digest,
		TtsTick,
		Count
	};

	/// <summary>Game thread: the latest frame's time per stage and the counters published to the stats group.</summary>
	struct RadioStats
	{
		/// <summary>Cycles64 per stage, accumulated since BeginFrame.</summary>
		std::array<uint64_t, static_cast<size_t>(RadioStage::Count)> Cycles{};
		/// <summary>The previous frame's, as printed.</summary>
		std::array<uint64_t, static_cast<size_t>(RadioStage::Count)> LastCycles{};
		uint64_t LogSize = 0;
		int HeapSize = 0;
		int Arrivals = 0;
		uint64_t Digested = 0;

		static RadioStats& Get();
		static const char* StageName(RadioStage stage);
		/// <summary>Moves this frame's stage times to LastCycles (the start of RadioTick).</summary>
		void BeginFrame();
		/// <summary>Sets the dword counters (with TtsService's) in the stats group.</summary>
		void Publish();
		double LastMs(RadioStage stage) const;
	};

	/// <summary>Adds the scope's Cycles64 to its stage.</summary>
	class RadioStageTimer
	{
	public:
		explicit RadioStageTimer(RadioStage stage) : slot(stage), start(FPlatformTime::Cycles64()) {}
		~RadioStageTimer() { RadioStats::Get().Cycles[static_cast<size_t>(slot)] += FPlatformTime::Cycles64() - start; }
		RadioStageTimer(const RadioStageTimer&) = delete;
		RadioStageTimer& operator=(const RadioStageTimer&) = delete;

	private:
		RadioStage slot;
		uint64_t start;
	};
}

/// One per scope: the stage's cycle counter, its Insights CPU scope and its sg.Radio stats timer.
#define SG_RADIO_SCOPE(Stage) \
	SCOPE_CYCLE_COUNTER(STAT_SpaceGodRadio_##Stage); \
	TRACE_CPUPROFILER_EVENT_SCOPE(SpaceGodRadio_##Stage); \
	::SpaceGod::Radio::RadioStageTimer RadioStageTimer_##Stage(::SpaceGod::Radio::RadioStage::Stage)
