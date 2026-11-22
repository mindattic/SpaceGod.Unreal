// US-T459: sg.Tether, the Ascension Tether seen and screenshotted. In Sol, holds the free camera side-on to the tether
// (the whole ribbon from the Galapagos to the counterweight in frame), shoots, toggles the scale and shoots again.
//   sg.Tether [shot <dir>] [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "Views/TetherView.h"

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

	struct Run
	{
		FString dir;
		bool quit = false, askedNewGame = false;
		int stage = 0;
		double started = FPlatformTime::Seconds(), stageAt = -1;

		/// Side-on to the tether, far enough for the whole of it (or close on the anchor when <paramref name="close"/>).
		void Pose(UnitySpaceApp& app, bool close)
		{
			cs::Ref<BodyDefinition> earth;
			if (!app.Catalog()->TryGet("Earth", earth)) return;
			int i = earth->Index;
			auto rel = app.Ephemeris()->Relative(i);
			auto orbitNormal = DVec3::Cross(rel, app.Ephemeris()->RelativeVelocityKmS(i));
			DVec3 north, meridian;
			BodyOrientation::Frame(earth, app.Clock->Jd, -rel, orbitNormal, north, meridian);
			double lat = SpaceGod::Views::TetherView::AnchorLatDeg * Astro::Deg2Rad, lon = SpaceGod::Views::TetherView::AnchorLonDeg * Astro::Deg2Rad;
			DVec3 east = DVec3::Cross(north, meridian);
			DVec3 upE = (meridian * (std::cos(lat) * std::cos(lon)) + east * (std::cos(lat) * std::sin(lon)) + north * std::sin(lat)).Normalized();
			DVec3 up(upE.X, upE.Z, upE.Y);
			DVec3 northU = DVec3(north.X, north.Z, north.Y).Normalized();
			DVec3 c = app.BodyScenes()[i];
			double r = app.BodyRadii()[i];
			double topD = app.Scale()->LocalDistance(earth, earth->RadiusKm + SpaceGod::Views::TetherView::LengthKm);
			DVec3 eye, at;
			if (close)
			{
				at = c + up * (r * 1.02);
				eye = at + northU * (r * 0.25) + up * (r * 0.12);
			}
			else
			{
				at = c + up * (topD * 0.5);
				eye = at + northU * (topD * 1.3);
			}
			DVec3 look = (at - eye).Normalized();
			app.SpaceCamera->SetPose(eye, Quaternion::LookRotation(Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)),
				Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z))));
		}

		void Shot(const TCHAR* name)
		{
			if (dir.IsEmpty()) return;
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("tether-%s.png"), name));
			FScreenshotRequest::RequestScreenshot(file, false, false);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Tether: screenshot %s"), *file);
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tether: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Tether: done")); }
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
			if (stageAt < 0) stageAt = now;
			double t = now - stageAt;
			// 0: wide, 1: close, 2: other scale wide, 3: other scale close.
			if (!app->SpaceCamera->FocusTarget.IsNone()) app->SpaceCamera->ExitFocus();
			Pose(*app, stage % 2 == 1);
			if (t < 4.0) return true;
			static const TCHAR* names[] = { TEXT("wide"), TEXT("close"), TEXT("wide-other-scale"), TEXT("close-other-scale") };
			Shot(names[stage]);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Tether: %s, scale blend %.2f, Earth radius %.4g, tether top %.4g scene units"), names[stage],
				app->Scale()->Blend(), app->BodyRadii()[0], 0.0);
			stage++;
			stageAt = now;
			if (stage == 2) app->ToggleScale();
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

	FAutoConsoleCommand GTetherCommand(
		TEXT("sg.Tether"),
		TEXT("US-T459: sg.Tether [shot <dir>] [quit] holds the camera side-on to the Ascension Tether (the Galapagos to its "
			"counterweight) and close on its Anchor Station, screenshotting both, then toggles the scale and shoots both again."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
