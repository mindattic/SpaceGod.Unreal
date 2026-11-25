// US-T730: sg.Flyby, the scale flyby. sg.Flyby [ship]: a cinematic camera skims a hull (named, else the selected ship, else
// the longest hull present) at a true speed along a spline over its drawn bounds, one pass bow to stern down one flank and
// the next down the other, in metres of the hull as drawn (ScaleModel::DrawnLengthKm, US-T715). sg.Flyby photo [ship]: the
// clock held and the HUD off, the free camera left beside the bow to fly round it (its speed follows the nearest surface).
// sg.Flyby off ends either. Camera and drawing only: nothing in the simulation reads it.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "UnityCompat/UnityRendering.h"
#include "Views/ShipView.h"

#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

#include <array>
#include <memory>

namespace SpaceGod
{
	bool UnitySpaceApp::ShipHullFrame(const cs::Ref<Core::Ship>& ship, Vector3& centre, Vector3& extents, double& length)
	{
		if (ship == nullptr || ship->Id < 0 || ship->Id >= shipViews.Count() || !shipViews[ship->Id] || ship->Id >= shipLength.Count()) return false;
		UnityCompat::Bounds box = shipViews[ship->Id]->HullBox();
		if (box.extents.x <= 0 || box.extents.z <= 0) return false;
		centre = box.center;
		extents = box.extents;
		length = shipLength[ship->Id];
		return length > 0;
	}
}

using namespace SpaceGod::Core;
using SpaceGod::UnitySpaceApp;

namespace
{
	TAutoConsoleVariable<float> CVarFlybySpeed(TEXT("sg.Flyby.Speed"), 120.0f,
		TEXT("US-T730: the flyby's speed along the hull, true metres per real second."));
	TAutoConsoleVariable<float> CVarFlybyClearance(TEXT("sg.Flyby.Clearance"), 25.0f,
		TEXT("US-T730: how far off the hull's drawn bounds the flyby skims, true metres."));

	UnitySpaceApp* FlybyApp()
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

	/// One flyby or photo session; the ticker ends when <c>Live</c> goes false.
	struct FlybyRun
	{
		static constexpr int Samples = 64;
		cs::Ref<Ship> Hull;
		bool Photo = false;
		bool Live = true;
		bool WasPaused = false, HadHud = true;
		int Pass = 0;
		double Along = 0; // hull lengths travelled on this pass
		std::array<SpaceGod::Vector3, 5> Knots{};
		std::array<double, Samples + 1> Arc{};
	};

	std::shared_ptr<FlybyRun>& FlybyCurrent()
	{
		static std::shared_ptr<FlybyRun> run;
		return run;
	}

	SpaceGod::Vector3 FlybyCatmullRom(SpaceGod::Vector3 p0, SpaceGod::Vector3 p1, SpaceGod::Vector3 p2, SpaceGod::Vector3 p3, float t)
	{
		float t2 = t * t, t3 = t2 * t;
		return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
	}

	/// The chain through the knots (ends doubled), at <paramref name="u"/> in [0, 1].
	SpaceGod::Vector3 FlybyPathAt(const FlybyRun& run, float u)
	{
		const auto& k = run.Knots;
		constexpr int spans = static_cast<int>(std::tuple_size<decltype(FlybyRun::Knots)>::value) - 1;
		float s = SpaceGod::Mathf::Clamp01(u) * spans;
		int i = SpaceGod::Mathf::Min(static_cast<int>(s), spans - 1);
		float t = s - i;
		return FlybyCatmullRom(k[i > 0 ? i - 1 : 0], k[i], k[i + 1], k[i + 2 <= spans ? i + 2 : spans], t);
	}

	/// Lays this pass's knots over the hull's bounds (root frame, hull lengths): ahead of the bow and high, down the flank a
	/// clearance off the box, out past the stern. Odd passes take the other flank, under the hull. Then its arc lengths.
	void FlybyLayPass(FlybyRun& run, SpaceGod::Vector3 c, SpaceGod::Vector3 e, double metresPerHull)
	{
		float side = run.Pass % 2 == 0 ? 1.0f : -1.0f;
		float clear = static_cast<float>(CVarFlybyClearance.GetValueOnGameThread() / metresPerHull);
		float x = e.x + clear, y = e.y + clear;
		run.Knots[0] = c + SpaceGod::Vector3(side * x * 0.4f, side * (y + clear * 4), e.z + 0.25f);
		run.Knots[1] = c + SpaceGod::Vector3(side * x, side * y * 0.7f, e.z * 0.75f);
		run.Knots[2] = c + SpaceGod::Vector3(side * x, side * e.y * 0.2f, 0);
		run.Knots[3] = c + SpaceGod::Vector3(side * x, side * y * 0.7f, -e.z * 0.75f);
		run.Knots[4] = c + SpaceGod::Vector3(side * x * 0.4f, side * (y + clear * 4), -e.z - 0.25f);
		run.Arc[0] = 0;
		auto prev = FlybyPathAt(run, 0);
		for (int i = 1; i <= FlybyRun::Samples; i++)
		{
			auto p = FlybyPathAt(run, static_cast<float>(i) / FlybyRun::Samples);
			run.Arc[i] = run.Arc[i - 1] + (p - prev).magnitude();
			prev = p;
		}
		run.Along = 0;
	}

	/// The path parameter <paramref name="d"/> hull lengths along this pass.
	float FlybyParamAt(const FlybyRun& run, double d)
	{
		int lo = 0, hi = FlybyRun::Samples;
		while (hi - lo > 1)
		{
			int mid = (lo + hi) / 2;
			(run.Arc[mid] <= d ? lo : hi) = mid;
		}
		double span = run.Arc[hi] - run.Arc[lo];
		double f = span > 0 ? (d - run.Arc[lo]) / span : 0;
		return static_cast<float>((lo + f) / FlybyRun::Samples);
	}

	SpaceGod::Core::DVec3 FlybyToD(SpaceGod::Vector3 v) { return DVec3(v.x, v.y, v.z); }

	void FlybyEnd(UnitySpaceApp* app, FlybyRun& run)
	{
		if (!run.Live) return;
		run.Live = false;
		if (app != nullptr)
		{
			app->ShowHud = run.HadHud;
			if (run.Photo) app->Clock->SetPaused(run.WasPaused);
		}
	}

	bool FlybyTick(const std::shared_ptr<FlybyRun>& run, float dt)
	{
		if (!run->Live) return false;
		UnitySpaceApp* app = FlybyApp();
		if (app == nullptr || !app->TargetValid(SpaceGod::Target::OfShip(run->Hull))) { FlybyEnd(app, *run); return false; }
		if (run->Photo) return true;
		SpaceGod::Vector3 c, e;
		double length = 0;
		if (!app->ShipHullFrame(run->Hull, c, e, length)) return true;
		double metresPerHull = ScaleModel::DrawnLengthKm(run->Hull) * 1000.0;
		if (metresPerHull <= 0) return true;
		if (run->Arc[FlybyRun::Samples] <= 0) FlybyLayPass(*run, c, e, metresPerHull);
		run->Along += CVarFlybySpeed.GetValueOnGameThread() * dt / metresPerHull;
		if (run->Along >= run->Arc[FlybyRun::Samples]) { run->Pass++; FlybyLayPass(*run, c, e, metresPerHull); }

		auto target = SpaceGod::Target::OfShip(run->Hull);
		auto rot = app->TargetRotation(target);
		auto root = app->TargetScene(target);
		float u = FlybyParamAt(*run, run->Along);
		float ahead = FlybyParamAt(*run, run->Along + 0.08);
		auto eyeLocal = FlybyPathAt(*run, u);
		// Down the path, drawn a third of the way in to the hull's spine abreast of the point ahead.
		auto aheadLocal = FlybyPathAt(*run, ahead);
		auto spine = SpaceGod::Vector3(c.x, c.y, aheadLocal.z);
		auto aimLocal = SpaceGod::Vector3::Lerp(aheadLocal, spine, 0.33f);
		auto eye = root + FlybyToD(rot * (eyeLocal * static_cast<float>(length)));
		auto look = rot * (aimLocal - eyeLocal);
		if (look.sqrMagnitude() <= 0) return true;
		app->SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(look.normalized(), rot * SpaceGod::Vector3::up()));
		return true;
	}

	cs::Ref<Ship> FlybyPickHull(UnitySpaceApp& app, const FString& name)
	{
		if (name.IsEmpty() && app.Selected.IsShip() && app.TargetValid(app.Selected)) return app.Selected.Ship;
		cs::Ref<Ship> best;
		double bestKm = 0;
		for (const auto& ship : app.Fleet()->Ships())
		{
			if (ship == nullptr || !app.TargetValid(SpaceGod::Target::OfShip(ship))) continue;
			if (!name.IsEmpty() && !FString(UTF8_TO_TCHAR(ship->Name.c_str())).Contains(name)) continue;
			double km = ScaleModel::DrawnLengthKm(ship);
			if (best == nullptr || km > bestKm) { best = ship; bestKm = km; }
		}
		return best;
	}

	void StartFlyby(const TArray<FString>& args)
	{
		UnitySpaceApp* app = FlybyApp();
		if (auto& old = FlybyCurrent()) { FlybyEnd(app, *old); old.reset(); }
		bool photo = args.Num() > 0 && args[0].Equals(TEXT("photo"), ESearchCase::IgnoreCase);
		if (args.Num() > 0 && args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)) return;
		if (app == nullptr) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Flyby: no game running")); return; }
		FString name;
		for (int i = photo ? 1 : 0; i < args.Num(); i++) name += (name.IsEmpty() ? TEXT("") : TEXT(" ")) + args[i];
		auto hull = FlybyPickHull(*app, name);
		if (hull == nullptr) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Flyby: no hull here%s%s"), name.IsEmpty() ? TEXT("") : TEXT(" named "), *name); return; }

		auto run = std::make_shared<FlybyRun>();
		run->Hull = hull;
		run->Photo = photo;
		run->HadHud = app->ShowHud;
		run->WasPaused = app->Clock->Paused();
		app->SkyLeaveHelm();
		if (!app->SpaceCamera->FocusTarget.IsNone()) app->SpaceCamera->ExitFocus();
		app->ShowHud = false;
		if (photo)
		{
			app->Clock->SetPaused(true);
			// Beside the bow, a clearance off the flank, looking aft along it: the free camera from there on.
			SpaceGod::Vector3 c, e;
			double length = 0;
			double metresPerHull = ScaleModel::DrawnLengthKm(hull) * 1000.0;
			if (app->ShipHullFrame(hull, c, e, length) && metresPerHull > 0)
			{
				FlybyLayPass(*run, c, e, metresPerHull);
				auto target = SpaceGod::Target::OfShip(hull);
				auto rot = app->TargetRotation(target);
				auto eyeLocal = run->Knots[1];
				auto look = rot * (SpaceGod::Vector3(c.x, c.y, -e.z) - eyeLocal);
				app->SpaceCamera->SetPose(app->TargetScene(target) + FlybyToD(rot * (eyeLocal * static_cast<float>(length))),
					SpaceGod::Quaternion::LookRotation(look.normalized(), rot * SpaceGod::Vector3::up()));
			}
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Flyby: %s %s, %.0f m drawn"), photo ? TEXT("photo at") : TEXT("skimming"),
			UTF8_TO_TCHAR(hull->Name.c_str()), ScaleModel::DrawnLengthKm(hull) * 1000.0);
		FlybyCurrent() = run;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float dt) { return FlybyTick(run, dt); }));
	}

	FAutoConsoleCommand GFlybyCommand(
		TEXT("sg.Flyby"),
		TEXT("US-T730: sg.Flyby [ship]: skim a hull (named, else the selected ship, else the longest here) at sg.Flyby.Speed true m/s, "
			"sg.Flyby.Clearance m off its drawn bounds; sg.Flyby photo [ship]: clock held, HUD off, free camera beside the bow; sg.Flyby off."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { StartFlyby(args); }));
}
