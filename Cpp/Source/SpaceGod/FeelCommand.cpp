// US-T463: sg.Feel, the flow between the god's view and the helm, measured frame by frame. A fighter and a battleship are
// put just ahead of the free camera; then, on scripted keys and mouse: the god focuses the fighter and orbits it; takes
// its helm (the fly-in); flies it (thrust, a mouse turn, a roll, a boost); lets go; swims the free camera and stops;
// takes the battleship's helm and turns it on the keys; goes straight from that seat to the fighter's; lets go again.
// Every frame writes one CSV row: the step, dt, the camera's mode, its scene position and its forward and up, the field
// of view, what is held, the show, and both hulls' scene positions and radii. tools/feel-report.py reads the rows and
// names every jolt (a frame whose turn, move or zoom breaks from its neighbours), with the step it fell in.
//   sg.Feel [game|real] [csv <file>] [quit]   (csv default Saved/Logs/feel-<scale>.csv)

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Smoke/ScriptedDevice.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h"
#include "Embodiment/ShipHelm.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;
using SpaceGod::Input::Key;
using SpaceGod::Input::MouseButton;

namespace
{
	AUnitySpaceAppActor* FeelActor()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor;
		}
		return nullptr;
	}

	struct Scene
	{
		cs::Ref<Ship> fighter, battleship;
	};

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds(), last = -1;
		bool askedNewGame = false, quit = false, scripted = false;
		SpaceGod::Smoke::ScriptedDevice Keys;
		cs::Ref<Scene> scene = cs::New<Scene>();
		FString csv;
		TArray<FString> rows;

		void Script()
		{
			auto* a = FeelActor();
			if (a == nullptr) return;
			if (!a->bScriptedInput) SpaceGod::Input::InputDevice::current().Clear();
			a->bScriptedInput = true;
			scripted = true;
		}

		// US-T473: full double precision. At real scale the scene sits ~1e5 out and a fighter is 5e-5 across, so %.9g's
		// 1e-4 step was two fighter radii: every frame read as a 20-radius position pop.
		static FString V(DVec3 v) { return FString::Printf(TEXT("%.17g,%.17g,%.17g"), v.X, v.Y, v.Z); }
		static FString V(SpaceGod::Vector3 v) { return FString::Printf(TEXT("%.6f,%.6f,%.6f"), v.x, v.y, v.z); }

		void Sample(UnitySpaceApp& app, double now)
		{
			double dt = last < 0 ? 0 : now - last;
			last = now;
			auto* cam = app.SpaceCamera.get();
			auto fwd = cam->Rotation * SpaceGod::Vector3::forward();
			auto up = cam->Rotation * SpaceGod::Vector3::up();
			auto shipCols = [&](const cs::Ref<Ship>& s) -> FString
			{
				if (s == nullptr || s->Destroyed) return TEXT("0,0,0,0");
				auto t = Target::OfShip(s);
				return FString::Printf(TEXT("%s,%.17g"), *V(app.TargetScene(t)), app.TargetRadius(t));
			};
			const TCHAR* held = app.IsPiloting() ? (app.Piloted.Ship == scene->fighter ? TEXT("fighter") : TEXT("battleship")) : TEXT("god");
			rows.Add(FString::Printf(TEXT("\"%s\",%.5f,%s,%d,%s,%s,%s,%.4f,%s,%d,%.4f,%d,%.6g,%s,%s"),
				at < steps.size() ? *steps[at].first : TEXT("end"), dt, UTF8_TO_TCHAR(SpaceGod::Cameras::ToString(cam->Mode)), cam->PilotLocked ? 1 : 0,
				*V(cam->ScenePosition), *V(fwd), *V(up), app.UnityCamera ? app.UnityCamera->fieldOfView : 0.0f, held,
				app.PossessionShowing() ? 1 : 0, app.PossessionT(), cam->PilotView.has_value() ? 1 : 0, cam->CurrentSpeed,
				*shipCols(scene->fighter), *shipCols(scene->battleship)));
		}

		bool Tick()
		{
			auto* actor = FeelActor();
			auto* app = actor ? actor->GetApp() : nullptr;
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
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (scene->fighter != nullptr) Sample(*app, now);
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Feel: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 90.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			if (scripted)
			{
				Keys.Write();
				Keys.EndFrame();
			}
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Feel: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Feel: done, %d frames to %s"), rows.Num(), *csv); }
			if (scripted)
			{
				SpaceGod::Input::InputDevice::current().Clear();
				if (auto* a = FeelActor()) a->bScriptedInput = false;
			}
			rows.Insert(TEXT("step,dt,mode,locked,cx,cy,cz,fx,fy,fz,ux,uy,uz,fov,held,show,showT,pilotView,swimSpeed,"
				"ax,ay,az,ar,bx,by,bz,br"), 0);
			FFileHelper::SaveStringArrayToFile(rows, *csv);
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Run>();
		bool real = false;
		run->csv.Empty();
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("real"), ESearchCase::IgnoreCase)) real = true;
			else if (args[i].Equals(TEXT("game"), ESearchCase::IgnoreCase)) real = false;
			else if (args[i].Equals(TEXT("csv"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->csv = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;
		}
		if (run->csv.IsEmpty()) run->csv = FPaths::Combine(FPaths::ProjectLogDir(), real ? TEXT("feel-real.csv") : TEXT("feel-game.csv"));
		auto scene = run->scene;
		Run* r = run.get();
		auto push = [r](FString name, Run::Step step) { r->steps.push_back({ name, std::move(step) }); };
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		// Keys held for a while, then let go (the step ends when they are).
		auto hold = [r](std::vector<Key> keys, double seconds, SpaceGod::Vector2 mousePerFrame = SpaceGod::Vector2(0, 0), bool rightButton = false)
		{
			return [r, keys, seconds, mousePerFrame, rightButton](UnitySpaceApp&, double t)
			{
				bool done = t >= seconds;
				for (Key k : keys)
				{
					if (done) r->Keys.Release(k);
					else r->Keys.Hold(k);
				}
				if (rightButton)
				{
					if (done) r->Keys.ReleaseButton(MouseButton::Right);
					else r->Keys.HoldButton(MouseButton::Right);
				}
				if (!done) r->Keys.mouseDelta = mousePerFrame;
				return done;
			};
		};

		push(TEXT("game up"), wait(5.0));
		push(TEXT("staged"), [r, scene, real](UnitySpaceApp& app, double)
		{
			r->Script();
			app.SkyLeaveHelm();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			if ((app.ScaleGoal > 0.5) == real) app.ToggleScale();
			SpaceGodInfluence::Of(app.World())->Waived = true; // a debug run: the embody limits lifted (never saved)
			auto* cam = app.SpaceCamera.get();
			int frame = cam->FrameAt(cam->ScenePosition);
			auto offsetKm = app.Scale()->InverseLocalOffset((*app.Catalog())[frame], cam->ScenePosition - app.BodyScene(frame));
			auto f3 = cam->Rotation * SpaceGod::Vector3::forward();
			auto r3 = cam->Rotation * SpaceGod::Vector3::right();
			auto forward = DVec3(f3.x, f3.y, f3.z).SwapYZ().Normalized();
			auto right = DVec3(r3.x, r3.y, r3.z).SwapYZ().Normalized();
			double jd = app.Clock->Jd;
			auto fleet = app.Fleet();
			auto sol = app.World()->Sol;
			scene->fighter = fleet->SpawnAt("Feel Fighter", ShipClass::Fighter, sol, frame, offsetKm + forward * 2.0, forward, jd);
			scene->battleship = fleet->SpawnAt("Feel Battleship", ShipClass::Battleship, sol, frame, offsetKm + forward * 8.0 + right * 4.0, forward, jd);
			if (!scene->fighter || !scene->battleship) throw std::runtime_error("a hull would not spawn");
			app.Clock->SetWarp(0);
			return true;
		});
		push(TEXT("scale settled"), [](UnitySpaceApp& app, double t) { return t >= 1.0 && !app.ScaleAnimating(); });

		// ---- the god: focus the fighter, orbit it with the right button
		push(TEXT("god focus fighter"), [scene](UnitySpaceApp& app, double t)
		{
			if (t == 0 || app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->FocusOn(Target::OfShip(scene->fighter));
			return t >= 2.5;
		});
		push(TEXT("god orbit"), hold({}, 1.5, SpaceGod::Vector2(6, 1.5f), true));
		push(TEXT("god idle"), wait(1.0));

		// ---- the fighter's helm: the fly-in, then flying
		push(TEXT("take fighter"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->fighter));
			return true;
		});
		push(TEXT("fighter fly-in"), wait(3.0));
		push(TEXT("fighter thrust"), hold({ Key::W }, 2.0));
		push(TEXT("fighter turn"), hold({ Key::W }, 1.5, SpaceGod::Vector2(9, 0)));
		push(TEXT("fighter pitch"), hold({ Key::W }, 1.0, SpaceGod::Vector2(0, 6)));
		push(TEXT("fighter roll"), hold({ Key::LeftCtrl, Key::D }, 1.2));
		push(TEXT("fighter boost"), hold({ Key::W, Key::LeftShift }, 1.5));
		push(TEXT("fighter coast"), wait(1.0));

		// ---- let go: back to the god's free camera, then a swim and a stop
		push(TEXT("let go fighter"), [](UnitySpaceApp& app, double)
		{
			app.PickCommand();
			return true;
		});
		push(TEXT("god after fighter"), wait(3.0));
		push(TEXT("god swim"), hold({ Key::W }, 2.0));
		push(TEXT("god stop"), wait(1.5));
		push(TEXT("god look"), hold({}, 1.0, SpaceGod::Vector2(-8, 0), true));
		push(TEXT("god still"), wait(1.0));

		// ---- the battleship's helm: a capital turned on the keys
		push(TEXT("take battleship"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->battleship));
			return true;
		});
		push(TEXT("battleship fly-in"), wait(3.5));
		push(TEXT("battleship ahead"), hold({ Key::W }, 0.2));
		push(TEXT("battleship turn"), hold({ Key::A }, 3.0));
		push(TEXT("battleship look"), hold({}, 1.0, SpaceGod::Vector2(7, 0)));
		push(TEXT("battleship settle"), wait(1.5));

		// ---- seat to seat, then let go
		push(TEXT("switch to fighter"), [scene](UnitySpaceApp& app, double)
		{
			app.PickSeat(Embodiables::HelmOf(app.World(), scene->fighter));
			return true;
		});
		push(TEXT("fighter again"), wait(3.0));
		push(TEXT("let go again"), [](UnitySpaceApp& app, double)
		{
			app.PickCommand();
			return true;
		});
		push(TEXT("god at the end"), wait(3.0));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GFeelCommand(
		TEXT("sg.Feel"),
		TEXT("US-T463: sg.Feel [game|real] [csv <file>] [quit] flies the god's view into a fighter's helm and a battleship's and out "
			"again on scripted keys, writing the camera frame by frame to a CSV (tools/feel-report.py names the jolts)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
