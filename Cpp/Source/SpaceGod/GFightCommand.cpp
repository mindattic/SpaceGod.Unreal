// US-T922: sg.GFight, a probe of what G (the scale toggle) does to a dogfight: the seat, the lock, the chase distance and the
// lead pip, logged every frame across four toggles.

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Gunnery.h"
#include "Embodiment/FighterSeat.h"
#include "Target.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

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

	struct Run
	{
		using Step = std::function<bool(SpaceGod::UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1;
		double started = FPlatformTime::Seconds();
		FString phase = TEXT("start");
		bool quit = false;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GFight: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.GFight: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.GFight: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	FString Describe(SpaceGod::UnitySpaceApp& app, const FString& phase)
	{
		auto me = app.PilotUnit();
		auto ship = app.Piloted.Ship;
		auto cam = app.SpaceCamera;
		double ratio = -1;
		if (cam != nullptr && cam->PilotLocked)
		{
			double r = app.TargetRadius(cam->FocusTarget);
			ratio = r > 0 ? (cam->ScenePosition - app.TargetScene(cam->FocusTarget)).Length() / r : -2;
		}
		FString lock = TEXT("none"), pip = TEXT("none"), gun = TEXT("none");
		if (me != nullptr && me->PlayerLockTarget != nullptr)
		{
			auto on = me->PlayerLockTarget;
			lock = FString::Printf(TEXT("%s(%.2f)"), UTF8_TO_TCHAR(on->Name().c_str()), me->LockProgress);
			bool grouped = false;
			double speed = SpaceGod::Core::Battle::PlayerGunSpeedKmS(me, grouped);
			int mounts = 0, down = 0;
			if (me->Combat != nullptr) for (const auto& o : me->Combat->Subsystems) { mounts++; if (o->Destroyed()) down++; }
			gun = FString::Printf(TEXT("%.2f km/s %d mounts %d down"), speed, mounts, down);
			if (speed > 0 && ship != nullptr && ship->Battle != nullptr)
			{
				auto lead = SpaceGod::Core::Gunnery::LeadPoint(me->Pos, me->Vel, on->Pos, on->Vel, speed);
				SpaceGod::Vector2 gui;
				float depth = 0;
				bool onScreen = app.ToGui(app.BattleScene(ship->Battle, lead), gui, depth);
				pip = FString::Printf(TEXT("%s(%.0f,%.0f)"), onScreen ? TEXT("on") : TEXT("off"), gui.x, gui.y);
			}
		}
		return FString::Printf(TEXT("%s goal %.0f piloting %d held %d ship %s battle %d lock %s pip %s gun %s chase %.2f"),
			*phase, app.ScaleGoal, app.IsPiloting() ? 1 : 0, app.HeldSeat() != nullptr ? 1 : 0,
			ship != nullptr ? UTF8_TO_TCHAR(ship->Name.c_str()) : TEXT("none"), ship != nullptr && ship->Battle != nullptr ? 1 : 0,
			*lock, *pip, *gun, ratio);
	}

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Run>();
		for (const FString& a : args) if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;

		run->steps.push_back({ TEXT("demo started"), [](SpaceGod::UnitySpaceApp& app, double t)
		{
			if (t < 0.01) app.StartDogfightDemo(false);
			return t > 0.5;
		} });
		run->steps.push_back({ TEXT("piloting in a battle"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			return app.IsPiloting() && app.Piloted.Ship != nullptr && app.Piloted.Ship->Battle != nullptr && app.PilotUnit() != nullptr;
		} });
		run->steps.push_back({ TEXT("settled"), [](SpaceGod::UnitySpaceApp&, double t) { return t >= 3.0; } });
		run->steps.push_back({ TEXT("lock taken"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			auto me = app.PilotUnit();
			if (me == nullptr || me->PlayerLockTarget != nullptr) return true;
			if (auto f = cs::As<SpaceGod::Core::FighterSeat>(app.HeldSeat())) f->CycleLock(f->Unit->Forward);
			else app.ProbeLockHelmAttacker(); // US-T932: the demo's hull is flown from the helm, not a fighter seat
			return false;
		} });
		run->steps.push_back({ TEXT("lock held"), [](SpaceGod::UnitySpaceApp&, double t) { return t >= 2.0; } });
		for (int i = 0; i < 4; i++)
		{
			FString tag = FString::Printf(TEXT("toggle%d"), i + 1);
			run->steps.push_back({ tag + TEXT(" begun"), [run, tag](SpaceGod::UnitySpaceApp& app, double)
			{
				run->phase = tag;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GFight: before %s"), *Describe(app, tag));
				app.ToggleScale();
				return true;
			} });
			run->steps.push_back({ tag + TEXT(" animated"), [](SpaceGod::UnitySpaceApp& app, double t) { return t > 0.2 && !app.ScaleAnimating(); } });
			run->steps.push_back({ tag + TEXT(" settled"), [tag](SpaceGod::UnitySpaceApp& app, double t)
			{
				if (t < 3.0) return false;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GFight: after %s"), *Describe(app, tag + TEXT("-after")));
				return true;
			} });
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try
			{
				if (auto* app = RunningApp())
					if (run->at >= 2 && run->at < run->steps.size()) UE_LOG(LogSpaceGod, Display, TEXT("sg.GFight: frame %s"), *Describe(*app, run->phase));
				return run->Tick();
			}
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T922: <c>sg.GFight [quit]</c>: the dogfight demo, a lock taken, then four scale toggles, each frame's seat, lock,
	/// chase distance and lead pip logged.
	FAutoConsoleCommand GGFightCommand(
		TEXT("sg.GFight"),
		TEXT("US-T922: sg.GFight [quit] starts the dogfight demo, locks a target and toggles the scale four times, logging the seat, "
			"lock, chase distance and lead pip every frame."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
