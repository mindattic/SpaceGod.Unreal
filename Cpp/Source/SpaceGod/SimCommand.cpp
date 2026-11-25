// US-T724: sg.Sim, the sim tiers of the active system's fleet: how many fleets are Near, Mid and Far, how many were stepped
// last frame, and the game-thread time each tier took against its budget (last, average, worst, frames over).
//   sg.Sim                          the figures
//   sg.Sim near|far <km>            the tier edges
//   sg.Sim budget near|mid|far <ms> a tier's budget
//   sg.Sim reset                    clears the figures

#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"

#include "Fleet.h"
#include "SimTier.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

using namespace SpaceGod::Core;
using SpaceGod::UnitySpaceApp;

namespace
{
	UnitySpaceApp* App()
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

	int TierArg(const FString& s)
	{
		if (s.Equals(TEXT("near"), ESearchCase::IgnoreCase)) return static_cast<int>(SimTier::Near);
		if (s.Equals(TEXT("mid"), ESearchCase::IgnoreCase)) return static_cast<int>(SimTier::Mid);
		if (s.Equals(TEXT("far"), ESearchCase::IgnoreCase)) return static_cast<int>(SimTier::Far);
		return -1;
	}

	void Sim(const TArray<FString>& args, FOutputDevice& out)
	{
		auto* app = App();
		if (app == nullptr || app->Fleet() == nullptr)
		{
			out.Logf(TEXT("sg.Sim: no game running"));
			return;
		}
		SimTiers& tiers = app->Fleet()->Tiers;
		if (args.Num() >= 2 && args[0].Equals(TEXT("near"), ESearchCase::IgnoreCase)) tiers.NearKm = FCString::Atod(*args[1]);
		else if (args.Num() >= 2 && args[0].Equals(TEXT("far"), ESearchCase::IgnoreCase)) tiers.FarKm = FCString::Atod(*args[1]);
		else if (args.Num() >= 3 && args[0].Equals(TEXT("budget"), ESearchCase::IgnoreCase) && TierArg(args[1]) >= 0)
			tiers.BudgetMs[TierArg(args[1])] = FCString::Atod(*args[2]);
		else if (args.Num() >= 1 && args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase)) tiers.ResetStatistics();
		else if (args.Num() > 0)
		{
			out.Logf(TEXT("sg.Sim: usage: sg.Sim [near|far <km> | budget near|mid|far <ms> | reset]"));
			return;
		}

		const auto& st = tiers.Statistics();
		out.Logf(TEXT("sg.Sim: %s, %d observer(s); Near within %.0f km, Far beyond %.0f km (hysteresis %.0f%%); Mid every %.2f s, batch %d; %lld frame(s)"),
			tiers.Active() ? TEXT("tiered") : TEXT("idle (no observer: every fleet Near)"), st.Observers, tiers.NearKm, tiers.FarKm,
			tiers.Hysteresis * 100, tiers.MidPeriodS, st.MidBatch, static_cast<long long>(st.Frames));
		out.Logf(TEXT("sg.Sim: promotions %d, demotions %d"), st.Promotions, st.Demotions);
		for (int t = 0; t < SimTierCount; t++)
		{
			out.Logf(TEXT("  %-4s fleets %5d  stepped %5d  last %6.3f ms  avg %6.3f ms  worst %7.3f ms  worker %6.3f ms  budget %5.2f ms  over %lld%s"),
				UTF8_TO_TCHAR(SimTierName(static_cast<SimTier>(t))), st.Fleets[t], st.Stepped[t], st.LastMs[t], st.AvgMs[t], st.WorstMs[t], st.WorkerMs[t],
				tiers.BudgetMs[t], static_cast<long long>(st.OverBudget[t]), st.AvgMs[t] > tiers.BudgetMs[t] ? TEXT("  OVER BUDGET") : TEXT(""));
		}
		out.Logf(TEXT("sg.Sim: ms are game-thread time: Far the schedule's event queue and the tier sweep, Mid its batch's gather and merge (the solve runs on a worker: worker ms, last batch), Near the sidestep solve and the separation"));
	}

	FAutoConsoleCommand GSimCommand(TEXT("sg.Sim"),
		TEXT("US-T724: sg.Sim [near|far <km> | budget near|mid|far <ms> | reset] reports the fleets' sim tiers (Near every frame, Mid ~1 Hz "
			"batched, Far event-driven) and each tier's game-thread time against its budget."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&Sim));
}
