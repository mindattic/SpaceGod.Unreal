// US-T650: the radio's stats group and its sg.Radio stats snapshot (see RadioStats.h).

#include "Radio/RadioStats.h"

#include "Voice/TtsService.h"

DEFINE_STAT(STAT_SpaceGodRadio_Tick);
DEFINE_STAT(STAT_SpaceGodRadio_HostTick);
DEFINE_STAT(STAT_SpaceGodRadio_Advance);
DEFINE_STAT(STAT_SpaceGodRadio_Rebase);
DEFINE_STAT(STAT_SpaceGodRadio_Sampler);
DEFINE_STAT(STAT_SpaceGodRadio_Events);
DEFINE_STAT(STAT_SpaceGodRadio_Reception);
DEFINE_STAT(STAT_SpaceGodRadio_Arbiter);
DEFINE_STAT(STAT_SpaceGodRadio_Digest);
DEFINE_STAT(STAT_SpaceGodRadio_TtsTick);
DEFINE_STAT(STAT_SpaceGodRadio_TtsSynth);
DEFINE_STAT(STAT_SpaceGodRadio_LogSize);
DEFINE_STAT(STAT_SpaceGodRadio_HeapSize);
DEFINE_STAT(STAT_SpaceGodRadio_Arrivals);
DEFINE_STAT(STAT_SpaceGodRadio_Digested);
DEFINE_STAT(STAT_SpaceGodRadio_TtsQueue);
DEFINE_STAT(STAT_SpaceGodRadio_CacheHits);
DEFINE_STAT(STAT_SpaceGodRadio_CacheMisses);
DEFINE_STAT(STAT_SpaceGodRadio_MeanSynthMs);

namespace SpaceGod::Radio
{
	RadioStats& RadioStats::Get()
	{
		static RadioStats stats;
		return stats;
	}

	const char* RadioStats::StageName(RadioStage stage)
	{
		switch (stage)
		{
		case RadioStage::Tick: return "radio tick (all)";
		case RadioStage::HostTick: return "host tick";
		case RadioStage::Advance: return "receiver advance";
		case RadioStage::Rebase: return "receiver rebase";
		case RadioStage::Sampler: return "chatter sampler";
		case RadioStage::Events: return "chatter events";
		case RadioStage::Reception: return "reception";
		case RadioStage::Arbiter: return "voice arbiter";
		case RadioStage::Digest: return "digest";
		case RadioStage::TtsTick: return "tts tick";
		default: return "?";
		}
	}

	void RadioStats::BeginFrame()
	{
		LastCycles = Cycles;
		Cycles.fill(0);
	}

	void RadioStats::Publish()
	{
		const Voice::TtsService::Stats tts = Voice::TtsService::GetStats();
		SET_DWORD_STAT(STAT_SpaceGodRadio_LogSize, static_cast<uint32>(LogSize));
		SET_DWORD_STAT(STAT_SpaceGodRadio_HeapSize, static_cast<uint32>(HeapSize));
		SET_DWORD_STAT(STAT_SpaceGodRadio_Arrivals, static_cast<uint32>(Arrivals));
		SET_DWORD_STAT(STAT_SpaceGodRadio_Digested, static_cast<uint32>(Digested));
		SET_DWORD_STAT(STAT_SpaceGodRadio_TtsQueue, static_cast<uint32>(tts.QueueDepth));
		SET_DWORD_STAT(STAT_SpaceGodRadio_CacheHits, static_cast<uint32>(tts.CacheHits));
		SET_DWORD_STAT(STAT_SpaceGodRadio_CacheMisses, static_cast<uint32>(tts.CacheMisses));
		SET_DWORD_STAT(STAT_SpaceGodRadio_MeanSynthMs, static_cast<uint32>(tts.MeanSynthMs + 0.5));
		(void)tts;
	}

	double RadioStats::LastMs(RadioStage stage) const
	{
		return FPlatformTime::ToMilliseconds64(LastCycles[static_cast<size_t>(stage)]);
	}
}
