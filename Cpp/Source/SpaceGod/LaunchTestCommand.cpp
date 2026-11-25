// US-T783: sg.LaunchTest -- the mass launch scene the frame-rate pass is measured on: ten carriers off an empty world, every
// squadron thrown out of launch tubes in waves by the launch sequencer (US-T779), the camera off one carrier's beam. It logs
// the launch's progress and the frame times a second at a time and a summary at the end; docs/TrueScale.md says what to
// measure alongside it (stat unit, stat gpu, an Insights trace).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/FlightDecks.h"
#include "Combat/War.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <memory>

using namespace SpaceGod::Core;

namespace
{
	SpaceGod::UnitySpaceApp* RunningApp()
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

	SpaceGod::Quaternion LaunchLook(DVec3 from, DVec3 at)
	{
		auto d = (at - from);
		if (d.LengthSquared() < 1e-24) d = DVec3(0, 0, 1);
		d = d.Normalized();
		return SpaceGod::Quaternion::LookRotation(SpaceGod::Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)), SpaceGod::Vector3::up());
	}

	/// <summary>One run of sg.LaunchTest, ticked once a frame.</summary>
	struct LaunchRun
	{
		FString Scale = TEXT("game");
		bool Quit = false, KeepScale = false;
		/// <summary>Carriers a side (ten capitals at the default five), launch tubes each, and how long it is watched.</summary>
		int Carriers = 5, Tubes = 20;
		double WatchSeconds = 40;

		double Started = FPlatformTime::Seconds();
		double WatchStart = -1, LastTick = -1, SecondStart = -1;
		bool AskedNewGame = false, Scaled = false;
		cs::Ref<Battle> Fight;
		cs::Ref<BattleUnit> Watched;

		// Frame times (ms) over the last second and over the whole watch.
		int Frames = 0, SecondFrames = 0, Over16 = 0, Over33 = 0;
		double SumMs = 0, WorstMs = 0, SecondSumMs = 0, SecondWorstMs = 0;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!AskedNewGame && now - Started > 2.0)
				{
					AskedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - Started < 180.0 || Finish(TEXT("no game came up"));
			}
			if (now - Started < 0.5) return true;
			if (!Scaled)
			{
				double goal = Scale == TEXT("game") ? 1.0 : 0.0;
				if (!KeepScale && app->ScaleGoal != goal) app->ToggleScale();
				if (!KeepScale && app->ScaleAnimating()) return true;
				Scaled = true;
			}
			if (Fight == nullptr)
			{
				Fight = Start(*app);
				if (Fight == nullptr) return Finish(TEXT("the launch did not start"));
				WatchStart = LastTick = SecondStart = now;
				int craft = 0;
				for (const auto& u : Fight->Units)
					if (u->Kind == UnitKind::Fighter) craft++;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.LaunchTest: %s, %d units, %d craft launching, %d tubes a carrier"),
					UTF8_TO_TCHAR(Fight->Name.c_str()), Fight->Units.Count(), craft, Tubes);
				return true;
			}
			double ms = (now - LastTick) * 1000.0;
			LastTick = now;
			Frames++;
			SecondFrames++;
			SumMs += ms;
			SecondSumMs += ms;
			WorstMs = cs::Math::Max(WorstMs, ms);
			SecondWorstMs = cs::Math::Max(SecondWorstMs, ms);
			if (ms > 1000.0 / 60.0) Over16++;
			if (ms > 1000.0 / 30.0) Over33++;
			Frame(*app);
			if (now - SecondStart >= 1.0)
			{
				LogSecond(now - WatchStart);
				SecondStart = now;
				SecondFrames = 0;
				SecondSumMs = SecondWorstMs = 0;
			}
			if (now - WatchStart >= WatchSeconds || Fight->Over) return Finish(nullptr);
			return true;
		}

		/// <summary>Both fleets' carriers off Uranus, every carrier given the tubes for the run (built into its decks now, the
		/// setting then put back as it was), and the fight watched from the first; null if it did not start.</summary>
		cs::Ref<Battle> Start(SpaceGod::UnitySpaceApp& app)
		{
			auto uranus = (*app.Catalog())["Uranus"];
			int body = uranus != nullptr ? uranus->Index : 0;
			cs::List<ShipClass> fleet;
			for (int i = 0; i < Carriers; i++) fleet.Add(ShipClass::Carrier);
			const auto& tubes = FlightDecks::LaunchTubes();
			double was = tubes->Value;
			tubes->Set(Tubes);
			auto battle = SpaceGod::SceneTour::TriggerSkirmish(app, body, app.World()->Sol, app.World()->Reach, fleet);
			if (battle != nullptr)
				for (const auto& u : battle->Units)
					if (u->Kind == UnitKind::Ship) FlightDecks::Of(u);
			tubes->Set(was);
			if (battle == nullptr) return nullptr;
			for (const auto& u : battle->Units)
				if (u->Kind == UnitKind::Ship && Watched == nullptr) Watched = u;
			app.Clock->SetPaused(false);
			app.WatchBattle(battle);
			return battle;
		}

		/// <summary>Off the watched carrier's starboard beam and a little above it, back far enough to hold the hull and the
		/// first join points ahead of it.</summary>
		void Frame(SpaceGod::UnitySpaceApp& app)
		{
			if (Watched == nullptr || !Watched->Present()) return;
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			DVec3 right, up;
			Watched->Basis(right, up);
			double back = Watched->LengthKm * 1.8 + 2.0;
			auto eye = app.BattleScene(Fight, Watched->Pos + right * back + up * (back * 0.35) - Watched->Forward * (back * 0.3));
			auto at = app.BattleScene(Fight, Watched->Pos + Watched->Forward * (Watched->LengthKm * 0.8));
			app.SpaceCamera->SetPose(eye, LaunchLook(eye, at));
		}

		void LogSecond(double t)
		{
			int waiting = 0, launching = 0, flying = 0, onDeck = 0;
			for (const auto& u : Fight->Units)
			{
				if (u->Kind != UnitKind::Fighter || u->Dead) continue;
				auto v = FlightDecks::VisitOf(u);
				if (v == nullptr) { if (u->Present()) flying++; continue; }
				if (v->Stage == DeckStage::OnDeck) onDeck++;
				else if (v->Stage == DeckStage::Launching) (Fight->Seconds < v->Release ? waiting : launching)++;
				else flying++;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.LaunchTest: t=%.0f battle %.1f s: %d at the breech, %d in the tubes, %d out, %d on deck; frames %d, %.2f ms mean, %.2f ms worst"),
				t, Fight->Seconds, waiting, launching, flying, onDeck, SecondFrames, SecondFrames > 0 ? SecondSumMs / SecondFrames : 0.0, SecondWorstMs);
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.LaunchTest: %s"), failure); }
			else
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.LaunchTest: done: %d frames, %.2f ms mean (%.1f fps), %.2f ms worst, %d over 16.7 ms, %d over 33.3 ms"),
					Frames, Frames > 0 ? SumMs / Frames : 0.0, SumMs > 0 ? Frames * 1000.0 / SumMs : 0.0, WorstMs, Over16, Over33);
			}
			if (Quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// US-T783: <c>sg.LaunchTest [game|real|here] [carriers=N] [tubes=N] [seconds=N] [quit]</c>: N carriers a side (5: ten
	/// capitals, 200 craft) off Uranus launch every squadron out of N launch tubes each (20) and are watched for N seconds (40).
	FAutoConsoleCommand GLaunchTestCommand(
		TEXT("sg.LaunchTest"),
		TEXT("US-T783: mass launch perf scene: sg.LaunchTest [game|real|here] [carriers=N] [tubes=N] [seconds=N] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
		{
			auto run = std::make_shared<LaunchRun>();
			for (const FString& a : args)
			{
				if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) run->Scale = a.ToLower();
				else if (a.Equals(TEXT("here"), ESearchCase::IgnoreCase)) run->KeepScale = true;
				else if (a.StartsWith(TEXT("carriers="), ESearchCase::IgnoreCase)) run->Carriers = FMath::Max(1, FCString::Atoi(*a.Mid(9)));
				else if (a.StartsWith(TEXT("tubes="), ESearchCase::IgnoreCase)) run->Tubes = FMath::Max(0, FCString::Atoi(*a.Mid(6)));
				else if (a.StartsWith(TEXT("seconds="), ESearchCase::IgnoreCase)) run->WatchSeconds = FMath::Max(1.0, FCString::Atod(*a.Mid(8)));
				else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->Quit = true;
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}));
}
