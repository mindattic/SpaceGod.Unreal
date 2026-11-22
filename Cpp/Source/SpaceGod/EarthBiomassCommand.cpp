// US-T461: sg.EarthBiomass, the Biomass on Earth seen and screenshotted. In Sol, moves the clock to the next daylight hour over
// Huntsville, holds the free camera over the patch from orbit (wide, then close), shoots, toggles the scale and shoots again.
//   sg.EarthBiomass [shot <dir>] [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"
#include "Ephemeris.h"
#include "ScaleModel.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <cmath>
#include <memory>

using namespace SpaceGod::Core;
using namespace SpaceGod::UnityCompat;
using SpaceGod::UnitySpaceApp;

namespace SpaceGodEarthBiomassCommand
{
	constexpr double CentreLat = 34.9, CentreLon = -86.9;

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

	/// The patch's outward direction (ecliptic) at <paramref name="jd"/>, and the Earth's north there.
	DVec3 Up(UnitySpaceApp& app, const cs::Ref<BodyDefinition>& earth, double jd, DVec3& north)
	{
		int i = earth->Index;
		auto rel = app.Ephemeris()->Relative(i);
		auto orbitNormal = DVec3::Cross(rel, app.Ephemeris()->RelativeVelocityKmS(i));
		DVec3 meridian;
		BodyOrientation::Frame(earth, jd, -rel, orbitNormal, north, meridian);
		double lat = CentreLat * Astro::Deg2Rad, lon = CentreLon * Astro::Deg2Rad;
		DVec3 east = DVec3::Cross(north, meridian);
		return (meridian * (std::cos(lat) * std::cos(lon)) + east * (std::cos(lat) * std::sin(lon)) + north * std::sin(lat)).Normalized();
	}

	struct Run
	{
		FString dir;
		bool quit = false, askedNewGame = false, daylit = false;
		int stage = 0;
		double started = FPlatformTime::Seconds(), stageAt = -1;

		void ToDaylight(UnitySpaceApp& app, const cs::Ref<BodyDefinition>& earth)
		{
			// Local mid-morning over the patch: the hour (of the next 24) whose sun is highest there.
			auto sunDir = (-app.Ephemeris()->Relative(earth->Index)).Normalized();
			double best = -2, bestJd = app.Clock->Jd;
			for (int h = 0; h < 24; h++)
			{
				double jd = app.Clock->Jd + h / 24.0;
				DVec3 north;
				double d = DVec3::Dot(Up(app, earth, jd, north), sunDir);
				if (d > best) { best = d; bestJd = jd; }
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.EarthBiomass: clock moved %.1f h to put the sun over the patch (cos %.2f)"), (bestJd - app.Clock->Jd) * 24, best);
			app.Clock->Jd = bestJd;
		}

		void Pose(UnitySpaceApp& app, const cs::Ref<BodyDefinition>& earth, bool close)
		{
			DVec3 north;
			DVec3 upE = Up(app, earth, app.Clock->Jd, north);
			DVec3 up(upE.X, upE.Z, upE.Y);
			DVec3 northU = DVec3(north.X, north.Z, north.Y).Normalized();
			DVec3 c = app.BodyScenes()[earth->Index];
			double r = app.BodyRadii()[earth->Index];
			DVec3 at = c + up * r;
			DVec3 eye = close ? at + up * (r * 0.22) - northU * (r * 0.08) : at + up * (r * 1.6) - northU * (r * 0.3);
			DVec3 look = (at - eye).Normalized();
			app.SpaceCamera->SetPose(eye, Quaternion::LookRotation(Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)),
				Vector3(static_cast<float>(northU.X), static_cast<float>(northU.Y), static_cast<float>(northU.Z))));
		}

		void Shot(const TCHAR* name)
		{
			if (dir.IsEmpty()) return;
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("biomass-%s.png"), name));
			FScreenshotRequest::RequestScreenshot(file, false, false);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.EarthBiomass: screenshot %s"), *file);
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.EarthBiomass: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.EarthBiomass: done")); }
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
			if (now - started > 240.0) return Finish(TEXT("timed out"));
			if (!app->IsSol()) return Finish(TEXT("not in Sol"));
			cs::Ref<BodyDefinition> earth;
			if (!app->Catalog()->TryGet("Earth", earth)) return Finish(TEXT("no Earth"));
			if (!daylit) { ToDaylight(*app, earth); daylit = true; }
			if (stageAt < 0) stageAt = now;
			double t = now - stageAt;
			// 0: wide, 1: close, 2: other scale wide, 3: other scale close.
			if (!app->SpaceCamera->FocusTarget.IsNone()) app->SpaceCamera->ExitFocus();
			Pose(*app, earth, stage % 2 == 1);
			if (t < 4.0) return true;
			static const TCHAR* names[] = { TEXT("wide"), TEXT("close"), TEXT("wide-other-scale"), TEXT("close-other-scale") };
			Shot(names[stage]);
			stage++;
			daylit = false; // each shot in daylight over the patch, however fast the clock runs
			stageAt = now;
			if (stage == 2) { app->ToggleScale(); daylit = false; }
			if (stage == 4) return Finish(nullptr);
			return true;
		}
	};

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Run>();
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->dir = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float) { return run->Tick(); }));
	}

	FAutoConsoleCommand GBiomassCommand(
		TEXT("sg.EarthBiomass"),
		TEXT("US-T461: sg.EarthBiomass [shot <dir>] [quit] moves the clock to daylight over the Biomass (the old south-east US), "
			"holds the camera over it from orbit, wide and close, screenshotting both, then toggles the scale and shoots both again."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
