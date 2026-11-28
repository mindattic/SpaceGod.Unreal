// US-T963: sg.Inhabit, the proof of inhabiting the galaxy's hulls (US-T951), in Sol, one PASS/FAIL line and a screenshot per
// step: (1) ride along in a hauler and watch its trip's phases advance; (2) take its helm with P while riding, let go, and
// see it replan and fly on to its booked market; (3) at a lawful patrol's helm, engage a pirate lying in wait beside a lane
// (or one under way) and be paid for the kill; (4) inhabit a station through the selection's station fallback; (5) at a
// pirate hull's helm with a projector, pulse a lane and take the ransomed cargo into the hold as plunder; (6) turn a pirate
// hull's guns on its own clan and sail with the Renegades. Each step stages what it needs and fails on a timeout with a
// reason, then the run goes on to the next; a summary line closes it.
//   sg.Inhabit [shot <dir>] [quit]   (screenshots inhabit-*.png; without shot, Saved/Screenshots)

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "PlayerOptions.h"
#include "SimClock.h"
#include "Smoke/ScriptedDevice.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/LaneInterdictors.h"
#include "Combat/Morale.h"
#include "Combat/War.h"
#include "Economy/Settlement.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;
using SpaceGod::Input::Key;

namespace
{
	constexpr int StepCount = 6;
	const TCHAR* const StepTitles[StepCount + 1] = { TEXT("setup"), TEXT("ride a hauler"), TEXT("take and hand back its helm"),
		TEXT("patrol vs lurker"), TEXT("inhabit a station"), TEXT("pirate pulse and ransom"), TEXT("turncoat") };

	AUnitySpaceAppActor* RunningActor()
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

	UnitySpaceApp* RunningApp()
	{
		auto* actor = RunningActor();
		return actor != nullptr ? actor->GetApp() : nullptr;
	}

	FString Str(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		struct Entry
		{
			FString name;
			int group = 0;
			double limit = 60.0;
			Step step;
		};
		std::vector<Entry> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false, scripted = false;
		FString dir;
		SpaceGod::Smoke::ScriptedDevice Keys;
		FString outcome[StepCount + 1];
		bool passed[StepCount + 1] = {};

		void Script()
		{
			auto* a = RunningActor();
			if (a == nullptr) return;
			if (!a->bScriptedInput) SpaceGod::Input::InputDevice::current().Clear();
			a->bScriptedInput = true;
			scripted = true;
		}

		void Unscript()
		{
			if (!scripted) return;
			scripted = false;
			SpaceGod::Input::InputDevice::current().Clear();
			if (auto* a = RunningActor()) a->bScriptedInput = false;
		}

		void Shot(UnitySpaceApp& app, int group, const TCHAR* tag)
		{
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("inhabit-%d-%s.png"), group, tag));
			auto ship = app.IsPiloting() ? app.Piloted.Ship : app.RiddenShip();
			auto seat = app.HeldSeat();
			FString who = ship != nullptr ? Str(ship->Name) : seat != nullptr ? Str(seat->Name()) : FString();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: shot %s: %s%s, warp %s"), *file,
				app.IsPiloting() ? TEXT("at the helm of ") : app.IsRiding() ? TEXT("riding in ") : seat != nullptr ? TEXT("seated in ") : TEXT("the god's view"),
				*who, *Str(app.Clock->Warp().Label));
			FScreenshotRequest::RequestScreenshot(file, true, false);
		}

		void Pass(int group, const FString& detail)
		{
			passed[group] = true;
			outcome[group] = detail;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: PASS %d %s: %s"), group, StepTitles[group], *detail);
		}

		/// A step's check failed or timed out: FAIL with the reason, a screenshot of where it stood, on to the next step.
		bool Fail(int group, const FString& reason)
		{
			if (group <= 0) return Finish(*reason);
			outcome[group] = reason;
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Inhabit: FAIL %d %s: %s"), group, StepTitles[group], *reason);
			Unscript();
			if (auto* app = RunningApp()) Shot(*app, group, TEXT("fail"));
			while (at < steps.size() && steps[at].group <= group) at++;
			stepStart = -1;
			return true;
		}

		bool Tick()
		{
			auto* app = RunningApp();
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
			auto& e = steps[at];
			bool done = false;
			try { done = e.step(*app, now - stepStart); }
			catch (const std::exception& ex) { return Fail(e.group, FString(UTF8_TO_TCHAR(ex.what()))); }
			if (done)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: %s"), *e.name);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > e.limit) return Fail(e.group, FString::Printf(TEXT("timed out at '%s'"), *e.name));
			if (scripted)
			{
				Keys.Write();
				Keys.EndFrame();
			}
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			Unscript();
			int ok = 0;
			FString line;
			for (int g = 1; g <= StepCount; g++)
			{
				if (passed[g]) ok++;
				else if (outcome[g].IsEmpty()) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Inhabit: FAIL %d %s: not reached"), g, StepTitles[g]); }
				line += FString::Printf(TEXT("%s%d %s"), g > 1 ? TEXT(", ") : TEXT(""), g, passed[g] ? TEXT("PASS") : TEXT("FAIL"));
			}
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Inhabit: %s"), failure); }
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: summary %d/%d passed (%s)"), ok, StepCount, *line);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: done"));
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Scene
	{
		// 1, 2: the hauler ridden and the trip it is on.
		cs::Ref<Ship> hauler;
		ShipPhase phase = ShipPhase::Parked;
		double phaseStart = 0;
		int advances = 0, booked = -1;
		FString phases;
		// 3: the patrol, the pirate it engages, the fight, the pay.
		cs::Ref<Ship> patrol, lurker;
		cs::Ref<Battle> fight;
		cs::Ref<Faction> juris;
		double credits = 0, standing = 0;
		bool lurking = false;
		// 4: the station inhabited.
		cs::String station;
		// 5: the raider, the laden rider it pulses out of the lane, where.
		cs::Ref<Ship> raider, rider;
		double progress = 0, plunder = 0;
		DVec3 side;
		FString lane;
		// 6: the turncoat and its clanmate.
		cs::Ref<Ship> turncoat, clanmate;
	};

	bool Alive(const cs::Ref<Ship>& s) { return s != nullptr && !s->Destroyed && !s->GateDeparted && !s->Derelict; }

	/// A hauler to ride: a belt works' freighter, else a laden trader, else any trader, flying (not in a fight, not down at a
	/// port) and the soonest due to change phase; <paramref name="offLane"/> passes over one riding a lane.
	cs::Ref<Ship> PickHauler(UnitySpaceApp& app, bool offLane)
	{
		double jd = app.Clock->Jd;
		cs::Ref<Ship> best;
		int bestTier = 99;
		double bestLeft = 1e18;
		for (const auto& s : app.Fleet()->Ships())
		{
			if (!Alive(s) || s->Battle != nullptr || s->IsEscort() || s->Planetside() || s->Faction == nullptr || s->Faction->IsPirate) continue;
			if (offLane && s->Phase == ShipPhase::Lane) continue;
			int tier = s->Role == ShipRole::Hauler ? 0 : s->Role == ShipRole::Trader && s->HasCargo ? 1 : s->Role == ShipRole::Trader ? 2 : 99;
			double left = s->PhaseEndJd - jd;
			if (tier == 99 || left <= 0 || !app.CanRideAlong(s)) continue;
			if (tier < bestTier || (tier == bestTier && left < bestLeft))
			{
				best = s;
				bestTier = tier;
				bestLeft = left;
			}
		}
		return best;
	}

	/// The warp that gets the ship's current phase over in about <paramref name="seconds"/> of real time.
	void WarpThroughPhase(UnitySpaceApp& app, const cs::Ref<Ship>& s, double seconds)
	{
		double left = cs::Math::Max(s->PhaseEndJd - app.Clock->Jd, 1.0 / 1440);
		app.Clock->SetWarp(SimClock::WarpToCover(left * 86400, seconds));
	}

	/// Whether the ship's trip moved on to another phase since last looked at, noting it.
	bool PhaseAdvanced(const cs::Ref<Ship>& s, const cs::Ref<Scene>& scene)
	{
		if (s->Phase == scene->phase && cs::Math::Abs(s->PhaseStartJd - scene->phaseStart) < 1e-9) return false;
		scene->phases += FString::Printf(TEXT(" -> %s"), UTF8_TO_TCHAR(ToString(s->Phase)));
		scene->phase = s->Phase;
		scene->phaseStart = s->PhaseStartJd;
		scene->advances++;
		return true;
	}

	void Watch(const cs::Ref<Ship>& s, const cs::Ref<Scene>& scene)
	{
		scene->phase = s->Phase;
		scene->phaseStart = s->PhaseStartJd;
		scene->advances = 0;
		scene->phases = FString(UTF8_TO_TCHAR(ToString(s->Phase)));
	}

	int PlayerSide(const cs::Ref<Battle>& b, const cs::Ref<Ship>& pilot)
	{
		for (const auto& u : b->Units)
			if (u->PlayerFlown && u->Ship == pilot && !u->Dead && !u->Captured) return u->Side;
		return -1;
	}

	/// The helm's guns, scripted: every unit still in the fight against the pilot's side is hit until it breaks up.
	int HitOtherSide(const cs::Ref<Battle>& b, int side)
	{
		int hit = 0;
		for (const auto& u : b->Units)
			if (u->Side != side && u->Present() && u->Combat != nullptr)
			{
				b->ApplyImpact(u, u->Pos, 1e9);
				hit++;
			}
		return hit;
	}

	bool BattleOnAt(const cs::Ref<World>& world, int body)
	{
		for (const auto& b : world->War->Battles)
			if (!b->Over && b->Body == body) return true;
		return false;
	}

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Run>();
		run->dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->dir = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;
		}
		Run* r = run.get();
		auto scene = cs::New<Scene>();
		int group = 0;
		auto push = [&run, &group](FString name, Run::Step step, double limit = 60.0)
		{
			run->steps.push_back({ name, group, limit, std::move(step) });
		};
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		// Each step starts from the god's view: any seat let go, any ride ended, the scripted keys back to the player's.
		auto begin = [&push, &group, r](int g)
		{
			group = g;
			push(FString::Printf(TEXT("step %d (%s): from the god's view"), g, StepTitles[g]), [r](UnitySpaceApp& app, double t)
			{
				r->Unscript();
				if (app.AtControls()) app.PickCommand();
				if (app.IsRiding()) app.StopRideAlong();
				return (t >= 1.5 && !app.AtControls() && !app.IsRiding() && !app.PossessionShowing()) || t > 8.0;
			}, 15.0);
		};
		auto shot = [&push, &group, r](const TCHAR* tag)
		{
			int g = group;
			FString name(tag);
			push(FString::Printf(TEXT("shot %d-%s"), g, tag), [r, g, name](UnitySpaceApp& app, double)
			{
				r->Shot(app, g, *name);
				return true;
			});
			push(TEXT("shot written"), [](UnitySpaceApp&, double t) { return t >= 1.0; });
		};

		// ---- setup: Sol, the embody limits waived as Free possession waives them (a scripted run ignores the setting itself)
		push(TEXT("game up"), wait(5.0));
		push(TEXT("staged"), [](UnitySpaceApp& app, double)
		{
			if (app.World() == nullptr || app.World()->Sol == nullptr || app.World()->War == nullptr) throw std::runtime_error("no Sol to run in");
			app.SkyLeaveHelm();
			SpaceGodInfluence::Of(app.World())->Waived = true; // never saved
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: Free possession is %s in the settings; this run waives the embody limits itself"),
				SpaceGod::PlayerOptions::FreePossession()->Value ? TEXT("on") : TEXT("off"));
			return true;
		});

		// ---- 1: ride along in a hauler (Ctrl+P, US-T954) and watch its trip move from phase to phase
		begin(1);
		push(TEXT("riding a hauler"), [scene](UnitySpaceApp& app, double)
		{
			auto h = PickHauler(app, false);
			if (h == nullptr) throw std::runtime_error("no hauler or trader in flight to ride along in");
			app.Select(Target::OfShip(h));
			app.StartRideAlong(h);
			if (!app.IsRiding() || app.RiddenShip() != h) throw std::runtime_error("the ride along would not start");
			scene->hauler = h;
			Watch(h, scene);
			return true;
		}, 10.0);
		push(TEXT("phases advance"), [scene, r](UnitySpaceApp& app, double t)
		{
			auto h = scene->hauler;
			if (!Alive(h)) throw std::runtime_error("the hauler was lost");
			bool advanced = PhaseAdvanced(h, scene);
			if (!app.IsRiding())
			{
				if (scene->advances == 0) throw std::runtime_error("the ride ended before the trip moved on");
				r->Pass(1, FString::Printf(TEXT("%s: %s (the ride ended there)"), *Str(h->Name), *scene->phases));
				return true;
			}
			if (advanced || t < 0.1) WarpThroughPhase(app, h, 10.0);
			if (scene->advances >= 2 || (scene->advances == 1 && t > 45.0))
			{
				r->Pass(1, FString::Printf(TEXT("riding the %s (%s): %s"), *Str(h->Name), UTF8_TO_TCHAR(ToString(h->Role)), *scene->phases));
				return true;
			}
			return false;
		}, 80.0);
		shot(TEXT("ride"));

		// ---- 2: P while riding takes its helm (US-T955); let go, it rides on and flies its job again (US-T956)
		begin(2);
		push(TEXT("riding a hauler clear of the lanes"), [scene](UnitySpaceApp& app, double)
		{
			auto h = scene->hauler;
			if (!Alive(h) || h->Planetside() || h->Battle != nullptr) h = scene->hauler = PickHauler(app, true);
			if (h == nullptr) throw std::runtime_error("no hauler in flight to ride along in");
			if (!app.IsRiding() || app.RiddenShip() != h)
			{
				if (app.CanRideAlong(h)) app.StartRideAlong(h);
				return false;
			}
			// Riding a lane, the helm would be taken mid-ride: wait it out.
			if (h->Phase == ShipPhase::Lane)
			{
				WarpThroughPhase(app, h, 8.0);
				return false;
			}
			app.Clock->SetWarp(SimClock::DefaultWarpIndex);
			return true;
		}, 60.0);
		push(TEXT("ride settled"), wait(2.0));
		push(TEXT("P pressed"), [scene, r](UnitySpaceApp& app, double)
		{
			auto h = scene->hauler;
			if (!app.IsRiding() || app.RiddenShip() != h) throw std::runtime_error("the ride ended before P was pressed");
			scene->booked = h->HasCargo && h->CargoDestination != nullptr ? h->CargoDestination->Body : -1;
			r->Script();
			r->Keys.Tap(Key::P);
			return true;
		});
		push(TEXT("P took the ridden helm"), [scene](UnitySpaceApp& app, double)
		{
			return app.IsPiloting() && app.Piloted.Ship == scene->hauler;
		}, 10.0);
		push(TEXT("at the helm"), wait(3.0));
		push(TEXT("let go"), [r](UnitySpaceApp& app, double)
		{
			r->Unscript();
			app.PickCommand();
			return true;
		});
		push(TEXT("riding on in it"), [scene](UnitySpaceApp& app, double)
		{
			if (app.IsRiding() && app.RiddenShip() == scene->hauler)
			{
				Watch(scene->hauler, scene);
				return true;
			}
			return false;
		}, 10.0);
		push(TEXT("replanned and flying its job"), [scene, r](UnitySpaceApp& app, double t)
		{
			auto h = scene->hauler;
			if (!Alive(h)) throw std::runtime_error("the hauler was lost");
			int at = h->Orbit != nullptr ? h->Orbit->Body : -1;
			bool toBooked = scene->booked < 0 || scene->booked == at || h->DestinationBody == scene->booked || h->FinalBody == scene->booked;
			if (!toBooked) throw std::runtime_error(TCHAR_TO_UTF8(*FString::Printf(TEXT("let go laden for body %d, it is bound for %d instead"), scene->booked, h->DestinationBody)));
			bool advanced = PhaseAdvanced(h, scene);
			if (advanced || t < 0.1) WarpThroughPhase(app, h, 10.0);
			if (scene->advances == 0) return false;
			r->Pass(2, FString::Printf(TEXT("the %s handed back, rode on, %s%s"), *Str(h->Name),
				scene->booked >= 0 ? *FString::Printf(TEXT("bound for its booked body %d: "), scene->booked) : TEXT("replanned: "), *scene->phases));
			return true;
		}, 70.0);
		shot(TEXT("handed-back"));

		// ---- 3: a lawful patrol's helm against a pirate lying in wait beside a lane (US-T957), paid for the kill (US-T958)
		begin(3);
		push(TEXT("patrol taken beside a pirate"), [scene](UnitySpaceApp& app, double t)
		{
			auto world = app.World();
			double jd = app.Clock->Jd;
			app.Clock->SetWarp(SimClock::WarpToCover(6 * 3600.0, 1.0));
			cs::Ref<Ship> mark;
			cs::Ref<Faction> juris;
			for (const auto& s : app.Fleet()->Ships())
			{
				if (!Alive(s) || s->Faction == nullptr || !s->Faction->IsPirate || s->IsEscort() || s->Battle != nullptr || !s->Ambushing() || s->LaneLeg.Lane == nullptr) continue;
				auto j = world->Law->JurisdictionOf(s->LaneLeg);
				if (j == nullptr || j->IsPirate) continue;
				mark = s;
				juris = j;
				break;
			}
			// None lying in wait for a while: a pirate under way is engaged where it flies instead.
			if (mark == nullptr && t > 25.0)
				for (const auto& s : app.Fleet()->Ships())
				{
					if (!Alive(s) || s->Faction == nullptr || !s->Faction->IsPirate || s->IsEscort() || s->Battle != nullptr || s->Phase != ShipPhase::Transit) continue;
					auto j = world->Law->JurisdictionAt(s->FrameBody);
					if (j == nullptr || j->IsPirate) continue;
					mark = s;
					juris = j;
					break;
				}
			// None found quickly: one is staged, a pirate hull lying in wait beside a lane under a lawful jurisdiction, as step 5
			// puts its raider beside a rider's lane, so the step never rests on where the world's pirates happen to be.
			if (mark == nullptr && t > 8.0)
			{
				for (const auto& host : app.Fleet()->Ships())
				{
					if (!Alive(host) || host->Phase != ShipPhase::Lane || host->LaneLeg.Lane == nullptr || host->Battle != nullptr) continue;
					auto j = world->Law->JurisdictionOf(host->LaneLeg);
					if (j != world->Sol) continue; // the patrol's own law, whose pay the step looks for
					double p = Fleet::LaneProgress(host, jd);
					if (p < 0.1 || p > 0.9) continue;
					int root = app.Catalog()->Root()->Index;
					auto side = DVec3::Cross(app.Fleet()->LanePoint(host, jd, cs::Math::Min(p + 0.01, 1.0)) - app.Fleet()->LanePoint(host, jd, p), DVec3::UnitZ);
					auto off = (side.LengthSquared() > 0 ? side.Normalized() : DVec3::UnitX) * 3000.0;
					auto at = app.Fleet()->LanePoint(host, jd, p) + off - app.Ephemeris()->Helio(root);
					auto pirate = app.Fleet()->SpawnAt("Inhabit Mark", ShipClass::Destroyer, world->Pirates, root, at, off.Normalized() * -1.0, jd);
					if (pirate == nullptr) throw std::runtime_error("the staged pirate would not spawn");
					pirate->LaneLeg = host->LaneLeg;
					pirate->LaneFromOffset = host->LaneFromOffset;
					pirate->LaneToOffset = host->LaneToOffset;
					pirate->LaneStar = host->LaneStar;
					pirate->LaneShape = host->LaneShape;
					pirate->LaneLengthKm = host->LaneLengthKm;
					pirate->LaneHold = p;
					pirate->LaneHoldSide = off;
					pirate->PhaseStartJd = jd;
					pirate->PhaseEndJd = jd + 5.0;
					pirate->Phase = ShipPhase::Lurk;
					mark = pirate;
					juris = j;
					break;
				}
			}
			if (mark == nullptr) return false;
			bool lurking = mark->Ambushing();
			// The fight beside a lane is anchored off the system's root: the patrol is put in that frame, so its helm and the fight agree.
			int frame = lurking ? app.Catalog()->Root()->Index : mark->FrameBody;
			auto markAt = app.Fleet()->StateOf(mark, jd).HelioKm;
			auto offset = markAt - app.Ephemeris()->Helio(frame) + DVec3::UnitZ * 800.0;
			auto patrol = app.Fleet()->SpawnAt("Inhabit Patrol", ShipClass::Destroyer, world->Sol, frame, offset, DVec3::UnitZ * -1.0, jd);
			if (patrol == nullptr) throw std::runtime_error("the patrol would not spawn");
			app.PickSeat(Embodiables::HelmOf(world, patrol));
			scene->patrol = patrol;
			scene->lurker = mark;
			scene->lurking = lurking;
			scene->juris = juris;
			return true;
		}, 75.0);
		push(TEXT("at the patrol's helm"), [scene](UnitySpaceApp& app, double) { return app.IsPiloting() && app.Piloted.Ship == scene->patrol; }, 10.0);
		push(TEXT("weapons free on the pirate"), [scene](UnitySpaceApp& app, double t)
		{
			if (t < 1.0) return false;
			auto world = app.World();
			double jd = app.Clock->Jd;
			auto patrol = scene->patrol, mark = scene->lurker;
			if (!Alive(mark) || mark->Battle != nullptr) throw std::runtime_error("the pirate was gone or already fighting before the patrol opened fire");
			int frame = scene->lurking ? app.Catalog()->Root()->Index : mark->FrameBody;
			auto here = app.Fleet()->StateOf(patrol, jd).HelioKm;
			auto forward = (app.Fleet()->StateOf(mark, jd).HelioKm - here).Normalized();
			auto rec = world->Law->RecordOf(patrol);
			scene->credits = patrol->Credits;
			scene->standing = rec != nullptr ? rec->StandingWith(scene->juris) : 0;
			// A pirate hull the authority is paid to be rid of, not one with a bounty on its captain (that is paid by the bounty instead,
			// and the staged hull lies where a rider sees the fight): whatever bounty the fight's end posts on it is lifted at once.
			world->Law->Committed += [mark](cs::Ref<Crime> c)
			{
				if (c != nullptr && c->Offender != nullptr && c->Offender->Captain == mark) { c->Offender->Bounty.Clear(); c->Offender->BountyUntil.Clear(); }
			};
			auto fight = world->War->TryStartPlayerAttack(patrol, mark, frame, here - app.Ephemeris()->Helio(frame), forward, jd);
			if (fight == nullptr || fight->UnitOf(mark) == nullptr) throw std::runtime_error("the attack on the pirate was refused");
			scene->fight = fight;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: %s: the %s engages the %s %s"), *Str(fight->Name), *Str(patrol->Name),
				scene->lurking ? TEXT("lurker") : TEXT("pirate under way"), *Str(mark->Name));
			return true;
		}, 10.0);
		push(TEXT("the pirate broken up and the fight over"), [scene](UnitySpaceApp& app, double t)
		{
			auto b = scene->fight;
			if (b->Over) return true;
			if (t < 2.0) return false;
			int side = PlayerSide(b, scene->patrol);
			if (side < 0) throw std::runtime_error("the patrol is out of the fight");
			HitOtherSide(b, side);
			return false;
		}, 60.0);
		push(TEXT("paid"), [scene, r](UnitySpaceApp& app, double t)
		{
			auto patrol = scene->patrol;
			auto rec = app.World()->Law->RecordOf(patrol);
			double paid = patrol->Credits - scene->credits;
			double standing = (rec != nullptr ? rec->StandingWith(scene->juris) : 0) - scene->standing;
			if (paid > 0 && standing > 0)
			{
				r->Pass(3, FString::Printf(TEXT("the %s killed the %s (%s): %s pays %.0f cr, standing +%.1f"), *Str(patrol->Name), *Str(scene->lurker->Name),
					scene->lurking ? TEXT("lying in wait") : TEXT("under way"), *Str(scene->juris->Name), paid, standing));
				return true;
			}
			// The law settles it when the fight ends, which the hold-to-rewind window can hold back (US-T419).
			if (t < 14.0) return false;
			// A wanted captain is paid for by the bounty on them, not by the authority's pirate pay.
			if (paid > 0)
			{
				r->Pass(3, FString::Printf(TEXT("the %s killed the %s: %.0f cr paid (a bounty: standing with %s unchanged)"), *Str(patrol->Name),
					*Str(scene->lurker->Name), paid, *Str(scene->juris->Name)));
				return true;
			}
			throw std::runtime_error(TCHAR_TO_UTF8(*FString::Printf(TEXT("the fight is over but %s paid nothing (standing %+.1f)"), *Str(scene->juris->Name), standing)));
		}, 25.0);
		shot(TEXT("patrol"));

		// ---- 4: a station inhabited out of a fight, through the selection's station fallback (US-T953)
		begin(4);
		push(TEXT("station inhabited"), [scene](UnitySpaceApp& app, double)
		{
			auto infra = app.World()->Infrastructure;
			if (infra == nullptr) throw std::runtime_error("no infrastructure in this system");
			for (const auto& s : infra->Structures)
			{
				if (s == nullptr || s->Settlement == nullptr) continue;
				auto target = Target::OfStructure(s);
				auto seat = app.EmbodiableAt(target);
				if (seat == nullptr || !seat->CanEmbody()) continue;
				app.Select(target);
				app.PickSeat(seat);
				scene->station = seat->Name();
				return true;
			}
			throw std::runtime_error("no station with a settlement to inhabit");
		}, 10.0);
		push(TEXT("in the station"), [scene, r](UnitySpaceApp& app, double t)
		{
			auto held = std::dynamic_pointer_cast<StationSeat>(app.HeldSeat());
			if (held == nullptr || held->Name() != scene->station) return false;
			if (t < 4.0) return false;
			r->Pass(4, FString::Printf(TEXT("inhabiting %s (%s view)"), *Str(held->Name()),
				app.Perceiving() == UnitySpaceApp::PerceptionKind::Panorama ? TEXT("panoramic") : TEXT("not yet panoramic")));
			return true;
		}, 15.0);
		shot(TEXT("station"));

		// ---- 5: a pirate hull with a projector (as a sandbox spawn fits it, US-T983) pulses a lane (US-T960, US-T961) and
		// the laden ship it pulls out hands over its cargo into the helm's hold (US-T962)
		begin(5);
		push(TEXT("raider taken beside a laden lane rider"), [scene](UnitySpaceApp& app, double)
		{
			auto world = app.World();
			double jd = app.Clock->Jd;
			app.Clock->SetWarp(SimClock::WarpToCover(3 * 3600.0, 1.0));
			cs::Ref<Ship> rider;
			double p = 0;
			for (const auto& s : app.Fleet()->Ships())
			{
				if (!Alive(s) || s->Phase != ShipPhase::Lane || s->LaneLeg.Lane == nullptr || s->Battle != nullptr || s->Faction == nullptr || s->Faction->IsPirate) continue;
				if (!s->HasCargo || s->CargoTonnes <= 0 || s->LaneSurged) continue;
				double at = Fleet::LaneProgress(s, jd);
				if (at < 0.15 || at > 0.85) continue;
				rider = s;
				p = at;
				break;
			}
			if (rider == nullptr) return false;
			auto side = DVec3::Cross(app.Fleet()->LanePoint(rider, jd, cs::Math::Min(p + 0.01, 1.0)) - app.Fleet()->LanePoint(rider, jd, p), DVec3::UnitZ);
			scene->side = (side.LengthSquared() > 0 ? side.Normalized() : DVec3::UnitX) * 3000.0;
			int root = app.Catalog()->Root()->Index;
			auto at = app.Fleet()->LanePoint(rider, jd, p) + scene->side - app.Ephemeris()->Helio(root);
			auto raider = app.Fleet()->SpawnAt("Inhabit Raider", ShipClass::Destroyer, world->Pirates, root, at, scene->side.Normalized() * -1.0, jd);
			if (raider == nullptr) throw std::runtime_error("the raider would not spawn");
			raider->Ports = world->FreeShipPorts();
			if (!LaneInterdictors::CanCarry(ShipClass::Destroyer) || !LaneInterdictors::Fit(raider)) throw std::runtime_error("the raider could not be fitted with a field projector");
			app.PickSeat(Embodiables::HelmOf(world, raider));
			scene->raider = raider;
			scene->rider = rider;
			return true;
		}, 75.0);
		push(TEXT("at the raider's helm"), [scene](UnitySpaceApp& app, double) { return app.IsPiloting() && app.Piloted.Ship == scene->raider; }, 10.0);
		push(TEXT("field pulse"), [scene](UnitySpaceApp& app, double t)
		{
			if (t < 1.0) return false;
			auto world = app.World();
			double jd = app.Clock->Jd;
			auto raider = scene->raider, rider = scene->rider;
			if (!Alive(rider) || rider->Phase != ShipPhase::Lane || rider->LaneLeg.Lane == nullptr) throw std::runtime_error("the laden rider left the lane before the pulse");
			double p = Fleet::LaneProgress(rider, jd);
			// The hull lies beside the rider's lane, where it is, as one dropped out of that lane lies (what the helm's bar pulses from).
			raider->LaneLeg = rider->LaneLeg;
			raider->LaneFromOffset = rider->LaneFromOffset;
			raider->LaneToOffset = rider->LaneToOffset;
			raider->LaneStar = rider->LaneStar;
			raider->LaneShape = rider->LaneShape;
			raider->LaneLengthKm = rider->LaneLengthKm;
			raider->LaneHold = p;
			raider->LaneHoldSide = scene->side;
			scene->plunder = raider->PlunderTonnes();
			scene->progress = p;
			scene->lane = Str(rider->LaneLeg.Lane->Name);
			auto result = world->War->PlayerPulse(raider, rider->LaneLeg, p, scene->side, jd);
			static const TCHAR* const names[] = { TEXT("ok"), TEXT("no projector"), TEXT("recharging"), TEXT("nobody near enough on the lane"), TEXT("unable") };
			if (result != War::PulseResult::Ok)
				throw std::runtime_error(TCHAR_TO_UTF8(*FString::Printf(TEXT("the pulse on the %s lane failed: %s"), *Str(rider->LaneLeg.Lane->Name), names[static_cast<int>(result)])));
			if (raider->Battle == nullptr || raider->Battle->UnitOf(rider) == nullptr) throw std::runtime_error("the pulse did not pull the laden rider into a fight with the raider");
			scene->fight = raider->Battle;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Inhabit: %s: the %s pulled out the %s (%.0f t aboard)"), *Str(scene->fight->Name), *Str(raider->Name), *Str(rider->Name), rider->CargoTonnes);
			return true;
		}, 10.0);
		// The hold-to-rewind window (US-T417) keeps a surrender's terms back for HoldRewindTuning::WindowS lived seconds so a
		// hold can still undo them; the plunder reaches the hold once that window has run out, so the step waits for it.
		auto offered = std::make_shared<bool>(false);
		push(TEXT("ransom into the hold"), [scene, r, offered](UnitySpaceApp& app, double t)
		{
			if (t < 1.0) return false;
			if (!*offered)
			{
				auto b = scene->fight;
				auto u = b->UnitOf(scene->rider);
				if (u == nullptr || !u->Present()) throw std::runtime_error("the laden rider is out of the fight before it could be ransomed");
				// It strikes its colours (its morale's call, made here) and the helm answers 1, its cargo.
				b->Surrender(u);
				if (!b->OfferSurrenderTerms(u, SurrenderTerms::RansomCargo)) throw std::runtime_error("the ransom terms were refused");
				*offered = true;
			}
			double taken = scene->raider->PlunderTonnes() - scene->plunder;
			if (taken <= 0) return false;
			r->Pass(5, FString::Printf(TEXT("the %s pulsed the %s lane at %.2f and took %.0f t from the %s into its plunder"), *Str(scene->raider->Name),
				*scene->lane, scene->progress, taken, *Str(scene->rider->Name)));
			return true;
		}, 40.0);
		shot(TEXT("plunder"));

		// ---- 6: a pirate hull's guns on its own clan: cast out, it sails with the Renegades (US-T959)
		begin(6);
		push(TEXT("turncoat at the helm, weapons free on its clanmate"), [scene](UnitySpaceApp& app, double)
		{
			auto world = app.World();
			double jd = app.Clock->Jd;
			for (const auto& s : Embodiables::StationsIn(world))
			{
				if (s == nullptr) continue;
				auto pose = s->PoseAt(jd);
				if (BattleOnAt(world, pose.FrameBody)) continue;
				auto out = pose.OffsetKm.Normalized();
				auto side = DVec3::Cross(out, DVec3::UnitZ);
				if (side.LengthSquared() < 1e-9) side = DVec3::Cross(out, DVec3::UnitX);
				side = side.Normalized();
				auto pirates = world->Pirates;
				auto turncoat = app.Fleet()->SpawnAt("Inhabit Turncoat", ShipClass::Battleship, pirates, pose.FrameBody, pose.OffsetKm - side * 200.0, side, jd);
				auto clanmate = app.Fleet()->SpawnAt("Inhabit Clanmate", ShipClass::Destroyer, pirates, pose.FrameBody, pose.OffsetKm + side * 200.0, side * -1.0, jd);
				if (turncoat == nullptr || clanmate == nullptr) throw std::runtime_error("the pirate hulls would not spawn");
				app.PickSeat(Embodiables::HelmOf(world, turncoat));
				if (!(app.IsPiloting() && app.Piloted.Ship == turncoat)) throw std::runtime_error("the turncoat's helm would not be taken");
				auto fight = world->War->TryStartPlayerAttack(turncoat, clanmate, pose.FrameBody, pose.OffsetKm - side * 200.0, side, jd);
				if (fight == nullptr || fight->UnitOf(clanmate) == nullptr) throw std::runtime_error("the attack on its own clan was refused");
				scene->turncoat = turncoat;
				scene->clanmate = clanmate;
				scene->fight = fight;
				return true;
			}
			throw std::runtime_error("no station body out of a fight to stage the turncoat at");
		}, 10.0);
		push(TEXT("the clanmate broken up and the fight over"), [scene](UnitySpaceApp& app, double t)
		{
			auto b = scene->fight;
			if (b->Over) return true;
			if (t < 2.0) return false;
			int side = PlayerSide(b, scene->turncoat);
			if (side < 0) throw std::runtime_error("the turncoat is out of the fight");
			HitOtherSide(b, side);
			return false;
		}, 60.0);
		push(TEXT("cast out"), [scene, r](UnitySpaceApp&, double t)
		{
			auto f = scene->turncoat->Faction;
			if (f != nullptr && f->Name == "Renegades")
			{
				r->Pass(6, FString::Printf(TEXT("the %s fought its own clan and sails with the Renegades"), *Str(scene->turncoat->Name)));
				return true;
			}
			if (t < 3.0) return false;
			throw std::runtime_error(TCHAR_TO_UTF8(*FString::Printf(TEXT("the fight is over and the %s still sails with %s"), *Str(scene->turncoat->Name), f != nullptr ? *Str(f->Name) : TEXT("nobody"))));
		}, 10.0);
		shot(TEXT("turncoat"));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GInhabitCommand(
		TEXT("sg.Inhabit"),
		TEXT("US-T963: sg.Inhabit [shot <dir>] [quit] proves inhabiting in Sol, one PASS/FAIL line and screenshot per step: ride a hauler, "
			"take and hand back its helm, a patrol's kill of a pirate lurker paid, a station inhabited, a raider's lane pulse and "
			"ransom into its hold, a turncoat cast out to the Renegades (inhabit-*.png)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
