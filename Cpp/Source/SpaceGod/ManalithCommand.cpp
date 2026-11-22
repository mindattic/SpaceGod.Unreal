// US-T458/US-T460: sg.Manalith, the Manalith's reference frame (docs/MANALITH_VISUAL_SPEC.md). Travels to Nightglass,
// pauses the simulation clock (the tumble and the shards run on real time, so they keep going while the orbit holds
// still) and holds the free camera 2.666 D from the Manalith (D spans 70% of the frame
// height) on its lit side, and at 20 s after the view first drew it takes the screenshot.
//   sg.Manalith [shot <file>] [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Systems/Galaxy.h"
#include "Systems/ManalithSystem.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

#include <cmath>
#include <memory>

using namespace SpaceGod::Core;
using namespace SpaceGod::UnityCompat;
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

	constexpr float Fov = 30.0f;
	constexpr double ShotAtSeconds = 20.0;

	struct Shot
	{
		FString file;
		bool quit = false, askedNewGame = false, travelled = false, shot = false;
		double started = FPlatformTime::Seconds(), arrived = -1, shotAt = -1;

		int Index(UnitySpaceApp& app) const
		{
			cs::Ref<BodyDefinition> b;
			return app.Catalog()->TryGet(ManalithSystem::SatelliteName, b) ? b->Index : -1;
		}

		void Pose(UnitySpaceApp& app, int i)
		{
			DVec3 at = app.BodyScenes()[i];
			double d = 2 * app.BodyRadii()[i];
			// The game eases its own field of view every frame (pace, cruise, helm), so frame D at 70% of the height at
			// the field of view it really draws with, rather than the spec rig's 30 degrees.
			float fov = app.UnityCamera ? app.UnityCamera->fieldOfView : Fov;
			double dist = d / 0.7 / (2 * std::tan(fov * 0.5 * 3.14159265358979 / 180));
			// From the star's side, raised a little and off to one side, so the key light falls upper left.
			DVec3 toStar = (app.BodyScenes()[0] - at).Normalized();
			DVec3 side = DVec3::Cross(toStar, DVec3(0, 1, 0)).Normalized();
			DVec3 eye = at + (toStar * 0.75 + side * 0.55 + DVec3(0, 0.35, 0)).Normalized() * dist;
			DVec3 look = (at - eye).Normalized();
			auto rot = Quaternion::LookRotation(Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), Vector3::up());
			app.SpaceCamera->SetPose(eye, rot);
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = App())
			{
				app->Clock->SetPaused(false);
			}
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Manalith: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}

		bool Tick()
		{
			auto* app = App();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - started > 180.0 ? Finish(TEXT("no game came up")) : true;
			}
			if (now - started > 300.0) return Finish(TEXT("timed out"));
			if (!travelled)
			{
				auto system = app->Galaxy->Realize(ManalithSystem::SystemName, app->Clock->Jd);
				if (system == nullptr) return Finish(TEXT("Nightglass would not build (is it charted?)"));
				if (!app->SpaceCamera->FocusTarget.IsNone()) app->SpaceCamera->ExitFocus();
				app->CheatTravelTo(system);
				travelled = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: travelling to %s, %.2f ly from Sol"), UTF8_TO_TCHAR(system->Name.c_str()),
					system->PositionLy.Length());
				return true;
			}
			bool travelling = app->Travel != nullptr && app->Travel->Active;
			if (travelling || app->ActiveSystem == nullptr || !ManalithSystem::ObservationOnly(app->ActiveSystem->Name)) return true;
			int i = Index(*app);
			if (i < 0) return Finish(TEXT("Nightglass has no Manalith"));
			if (arrived < 0)
			{
				arrived = now;
				app->Clock->SetPaused(true);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: arrived; Manalith drawn radius %.6g scene units"), app->BodyRadii()[i]);
			}
			Pose(*app, i);
			if (!shot && now - arrived >= ShotAtSeconds)
			{
				shot = true;
				shotAt = now;
				if (!file.IsEmpty())
				{
					FScreenshotRequest::RequestScreenshot(file, false, false);
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: screenshot %s"), *file);
				}
			}
			return !(shot && now - shotAt > 1.5) || Finish(nullptr);
		}
	};

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Shot>();
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->file = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float) { return run->Tick(); }));
	}

	FAutoConsoleCommand GManalithCommand(
		TEXT("sg.Manalith"),
		TEXT("US-T458/US-T460: sg.Manalith [shot <file>] [quit] travels to Nightglass and holds the camera on the Manalith as the "
			"shared spec frames it (D at 70% of the height at the game's own field of view, the simulation paused), screenshotting it 20 s in."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
