// US-T656: sg.Radio, the radio host's state: the log's size, the receiver's heap, its overflow and skipped counts and the
// latest arrivals. sg.Radio emit <text> sends <text> on Guard from 1 AU off the listener (heard about 8.3 minutes later).

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioStats.h"
#include "Voice/TtsService.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using namespace SpaceGod::Core;

	SpaceGod::UnitySpaceApp* RadioRunningApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	void RadioEmit(SpaceGod::Radio::RadioHost& radio, const FString& text)
	{
		RadioMessage m;
		m.ChannelId = RadioChannels::IdOf(RadioChannelKind::Guard, 0);
		m.SystemId = radio.SystemId();
		m.OriginKm = radio.ListenerKm() + DVec3(Astro::AuKm, 0, 0);
		m.SentJd = radio.Jd();
		m.Power = RadioPower::GuardBeacon;
		m.Priority = 3;
		uint64_t index = radio.EmitText(m, cs::String(TCHAR_TO_UTF8(*text)));
		if (index == SpaceGod::Radio::RadioHost::NoIndex)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Radio: not sent (no listener, or between the stars)"));
		}
		else
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Radio: sent on Guard as #%llu from 1 AU"), static_cast<unsigned long long>(index));
		}
	}

	void RadioPrint(const SpaceGod::Radio::RadioHost& radio)
	{
		const RadioLog* log = radio.Log();
		const RadioReceiver& rx = radio.Receiver();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Radio: system %d, jd %.6f, log %llu, heap %d, overflow %llu, skipped %llu, heard %llu, rebases %llu"),
			radio.SystemId(), radio.Jd(), static_cast<unsigned long long>(log != nullptr ? log->Count() : 0), rx.Count(),
			static_cast<unsigned long long>(rx.OverflowCount()), static_cast<unsigned long long>(rx.SkippedCount()),
			static_cast<unsigned long long>(radio.HeardCount()), static_cast<unsigned long long>(radio.RebaseCount()));
		for (const auto& r : radio.Recent())
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Radio:   jd %.6f, %.0f km, %.1f s old: %s"), r.ArrivalJd, r.DistanceKm,
				r.AgeDays * Astro::SecondsPerDay, UTF8_TO_TCHAR(r.Text.c_str()));
	}

	/// US-T650: the latest frame's time per radio stage and the counters behind stat SpaceGodRadio.
	void RadioPrintStats()
	{
		using SpaceGod::Radio::RadioStage;
		using SpaceGod::Radio::RadioStats;
		const RadioStats& stats = RadioStats::Get();
		for (int i = 0; i < static_cast<int>(RadioStage::Count); i++)
		{
			const RadioStage stage = static_cast<RadioStage>(i);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Radio stats: %-18s %.4f ms"), UTF8_TO_TCHAR(RadioStats::StageName(stage)), stats.LastMs(stage));
		}
		const SpaceGod::Voice::TtsService::Stats tts = SpaceGod::Voice::TtsService::GetStats();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Radio stats: log %llu, heap %d, arrivals %d, digested %llu, tts queue %d, cache hits %llu, misses %llu, mean synth %.1f ms"),
			static_cast<unsigned long long>(stats.LogSize), stats.HeapSize, stats.Arrivals, static_cast<unsigned long long>(stats.Digested),
			tts.QueueDepth, tts.CacheHits, tts.CacheMisses, tts.MeanSynthMs);
	}

	FAutoConsoleCommand GRadioCommand(
		TEXT("sg.Radio"),
		TEXT("US-T656: the radio host's log, heap, overflow and skipped counts and latest arrivals; sg.Radio emit <text> sends <text> on Guard from 1 AU away; sg.Radio stats (US-T650) prints each stage's last-frame time and the radio counters (stat SpaceGodRadio shows them live)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
		{
			auto* app = RadioRunningApp();
			if (app == nullptr)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Radio: no game running"));
				return;
			}
			if (args.Num() > 0 && args[0].Equals(TEXT("stats"), ESearchCase::IgnoreCase))
			{
				RadioPrintStats();
				return;
			}
			if (args.Num() > 0 && args[0].Equals(TEXT("emit"), ESearchCase::IgnoreCase))
			{
				FString text;
				for (int i = 1; i < args.Num(); i++) text += (i > 1 ? TEXT(" ") : TEXT("")) + args[i];
				RadioEmit(app->GameRadio(), text.IsEmpty() ? FString(TEXT("Test on Guard")) : text);
				return;
			}
			RadioPrint(app->GameRadio());
		}));
}
