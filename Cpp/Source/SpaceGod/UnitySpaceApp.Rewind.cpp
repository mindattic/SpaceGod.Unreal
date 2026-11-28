// US-T99 (docs/REWIND.md, phases 2 and 3): time reversal's keyframe ring in the game, the frame log, seeking by replay
// and the timeline bar. Members are declared in UnitySpaceApp.Rewind.inl; the ring itself is Core's RewindRing
// (Persistence/RewindRing.h).

#include "UnitySpaceApp.h"
#include "SpaceGodLog.h" // US-T940: AUDIT STEP marks
#include "PlayerOptions.h"
#include "UI/Hud.h"
#include "UI/GalaxyMapScreen.h" // US-T492: an order replayed
#include "UI/UiMotion.h"
#include "Coop/CoopPeer.h"
#include "Input/Gamepad.h"
#include "Input/InputDevice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <exception>
#include <functional>

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/War.h"
#include "Embodiment/MechWalker.h" // US-T493: a mech order replayed
#include "Persistence/InterventionLog.h"
#include "Persistence/SaveGame.h"
#include "Session/Commands.h"
#include "Systems/DiscoveryLog.h" // US-T497
#include "Systems/Frontier.h" // US-T497
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"

#include "Bootstrap.h"
#include "Camera/StarTravel.h"
#include "ActiveProfile.h"
#include "Shared/Profiles/ProfileStore.h"
#include "Shared/Profiles/SharedIo.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// "3 min", "5.2 h", "12 days": how far back a keyframe is, in game time.
		cs::String RewindSpan(double s)
		{
			s = std::fabs(s);
			if (s < 120) return cs::Cat(cs::Fmt(s, "F0"), " s");
			if (s < 2 * 3600) return cs::Cat(cs::Fmt(s / 60, "F0"), " min");
			if (s < 2 * 86400) return cs::Cat(cs::Fmt(s / 3600, "F1"), " h");
			if (s < 2 * 365.25 * 86400) return cs::Cat(cs::Fmt(s / 86400, "F1"), " days");
			return cs::Cat(cs::Fmt(s / (365.25 * 86400), "F1"), " years");
		}

		cs::String RewindMb(size_t bytes) { return cs::Cat(cs::Fmt(bytes / (1024.0 * 1024.0), "F1"), " MB"); }
	}

	bool UnitySpaceApp::RewindBattle() const
	{
		const auto& w = World();
		return !Piloted.IsNone() || (w != nullptr && w->War != nullptr && w->War->Battles.Count() > 0);
	}

	void UnitySpaceApp::UpdateRewind()
	{
		if (!rewindInit)
		{
			rewindInit = true;
			rewindEnabled = !Bootstrap::HasCommandLineFlag("-norewind") && !Bootstrap::HasCommandLineFlag("-demo") && !Bootstrap::HasCommandLineFlag("-capture");
			// US-T484: keyframes past the RAM budget spill to the profile's saves folder (no profile: RAM thinning only).
			try
			{
				if (auto profile = ActiveProfile::Current()) Rewind.SpillFolder = Shared::Io::Combine(profile->SavesFolder(), "Rewind");
			}
			catch (const std::exception& e)
			{
				Debug::LogWarning(cs::Cat("[Rewind] no spill folder: ", e.what()));
			}
		}
		if (Clock == nullptr || Galaxy == nullptr)
		{
			rewindFrameStarted = false;
			return;
		}
		RewindNoteSessionTime();

		double wall = Time::realtimeSinceStartupAsDouble;
		if (rewindReplaying)
		{
			// This frame was the logged one: on to the next, until the seek arrives.
			RewindReplayNote();
			if (rewindReplayNext >= rewindReplayStop || rewindReplayMismatches > 0) FinishRewindReplay(false); // US-T487: a drift gates it
			return;
		}
		LogRewindFrame();

		if (pendingRewindId >= 0 || pendingRewindBackS >= 0 || pendingRewindSeekS >= 0)
		{
			int id = pendingRewindId;
			double backS = pendingRewindBackS, seekS = pendingRewindSeekS;
			pendingRewindId = -1;
			pendingRewindBackS = -1;
			pendingRewindSeekS = -1;
			if (id >= 0) RewindTo(id);
			else if (seekS >= 0) RewindSeek(seekS);
			else RewindBack(backS);
			return;
		}
		if (Input.Rewind)
		{
			ShowRewindBar(8);
			if (!rewindEnabled && Rewind.Keyframes().empty()) ShowToast("Rewind is off in this run (-norewind, -demo, -capture, or a keyframe failed)");
			else
			{
				// Pressed again soon after a rewind: one further back from the moment rewound to, not from now.
				const RewindKeyframe* headK = Rewind.Head() >= 0 && wall - Rewind.HeadWallS() < RewindStepWindowS ? Rewind.Find(Rewind.Head()) : nullptr;
				const RewindKeyframe* k = Rewind.Before(headK != nullptr ? headK->SessionS : rewindSessionS - 1.0);
				if (k != nullptr) RewindTo(k->Id);
				else ShowToast(Rewind.Keyframes().empty() ? "Nothing to rewind to yet" : "This is as far back as the session goes");
			}
			return;
		}

		// Only a moment a save could be made of: not between the stars, under FTL or latched to a hulk (SaveBlocker).
		if (!rewindEnabled || !SaveBlocker().empty() || !Rewind.Due(rewindSessionS, wall, RewindBattle())) return;
		// US-T410: a keyframe books what the hold rewind holds back (a save must hold it), which a hold could not then undo:
		// while a fresh booking waits, the keyframe waits too (at most the window).
		if (HoldRewindDelaysKeyframe()) return;
		try
		{
			const RewindKeyframe& k = Rewind.Capture(CaptureSave(), rewindSessionS, Clock->Jd, wall, RewindBattle());
			// Phase 3: the frame this keyframe's world steps into next, where a seek from it starts replaying.
			rewindKeySerial[k.Id] = rewindNextSerial;
			for (auto it = rewindKeySerial.begin(); it != rewindKeySerial.end();)
				it = Rewind.Find(it->first) == nullptr ? rewindKeySerial.erase(it) : std::next(it);
			// US-T497: the system it was taken in, for the scrub preview.
			rewindKeySystem[k.Id] = RewindKeySystem{ k.SessionS, RewindWatchedSystem() };
			for (auto it = rewindKeySystem.begin(); it != rewindKeySystem.end();)
				it = Rewind.Find(it->first) == nullptr ? rewindKeySystem.erase(it) : std::next(it);
		}
		catch (const std::exception& e)
		{
			// A keyframe that cannot be taken costs the rewind, never the game: keyframing stops for this run rather than
			// walking the world again every frame (the keyframes already kept can still be rewound to).
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Rewind keyframe failed: ", e.what()));
			rewindEnabled = false;
		}
	}

	void UnitySpaceApp::RewindNoteSessionTime()
	{
		// The timeline's game time: what the clock moved forward (a clock set back by hand does not run the session back).
		if (!std::isnan(rewindLastJd) && Clock->Jd > rewindLastJd) rewindSessionS += (Clock->Jd - rewindLastJd) * 86400.0;
		rewindLastJd = Clock->Jd;
	}

	void UnitySpaceApp::RewindReplayNote()
	{
		rewindFrameStarted = false;
		if (const RewindFrame* f = RewindFrameAt(rewindReplayNext))
		{
			// The rest of this frame (its adventures) reads the logged frame's.
			rewindAdventureSerial = f->Serial;
			rewindAdventureFromLog = true;
			RewindReplayOrders(rewindReplayNext - 1); // US-T492: the previous frame's orders, given after its world step
			RewindReplayInterventions(f->Serial, f->Intervened); // US-T488: what the player did on it
			if (Clock->Jd != f->JdAfter) rewindReplayDrift++;
			// The world itself, where the play kept a check: the replay is exact only while every one matches. US-T487: the
			// first that differs gates the replay (its callers finish it here): past it the world is no longer the play's.
			if (f->Check != 0)
			{
				rewindReplayChecks++;
				if (RewindWorldCheck() != f->Check && rewindReplayMismatches++ == 0)
				{
					rewindReplayFirstMismatchS = f->SessionS;
					rewindReplayFirstMismatchSerial = f->Serial;
					Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: the world check differs from the play's at frame serial ",
						f->Serial, " (", RewindSpan(f->SessionS), " into the session, after ", rewindReplayChecks - 1, " matched); the replay stops there"));
				}
			}
		}
		rewindReplayNext++;
	}

	void UnitySpaceApp::RewindFastForward()
	{
		// US-T486: a seek replays headless: as many logged frames as fit the wall budget run through the world steps of the
		// live frame only, in its order, and the engine frame then runs the next logged frame whole (its views drawn). The
		// last logged frame is always left to the whole frame, so the replay ends as it did (FinishRewindReplay there).
		if (!rewindReplaying || Clock == nullptr || Galaxy == nullptr || Input.Escape) return; // Esc: RewindFrameStart stops it
		Input.Handover(); // the logged frames had idle hands (a frame acted on ends the seek)
		auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<long long>(RewindFastForwardBudgetS * 1e6));
		auto system = ActiveSystem;
		while (rewindReplaying && rewindReplayNext + 1 < rewindReplayStop && std::chrono::steady_clock::now() < until)
		{
			const RewindFrame* f = RewindFrameAt(rewindReplayNext);
			if (f == nullptr) break;
			float dt = f->Dt;
			// As RewindFrameStart, then HandleCommands' UpdateRewind (the bookkeeping at the point the live log takes it).
			rewindAdventureSerial = -1;
			rewindAdventureFromLog = false;
			f->Before.ApplyTo(*Clock);
			RewindNoteSessionTime();
			RewindReplayNote();
			// As Update: the helm in a battle holds warp at real time, then the clock and the world step.
			if (!Piloted.IsNone() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr && Clock->WarpIndex > 0) Clock->SetWarp(0);
			Clock->Advance(dt);
			Galaxy->Update(Clock->Jd);
			SyncShipViews(); // a ship the world spawned gets its slots before the helm reads them
			UpdatePiloting(dt);
			UpdateSeat(dt);
			CoopUpdateAdventures(!Travel->Active && !InFtlCruise());
			DropLostTargets();
			UpdateCourseShip();
			// US-T487: this frame's world check differed from the play's: the fast-forward stops on it (the frame stepped
			// whole, so play goes on from the next), and the engine frame runs live.
			if (rewindReplayMismatches > 0)
			{
				FinishRewindReplay(false);
				break;
			}
			RewindReplayOrders(f->Serial); // US-T492: this frame's orders, after its world step as the play gave them
			// A star travel under way, or another system shown: the whole frame takes it from here (its views follow).
			if (Travel->Active || ActiveSystem != system) break;
		}
	}

	bool UnitySpaceApp::RewindBack(double backS)
	{
		const RewindKeyframe* k = Rewind.Back(rewindSessionS, backS);
		if (k == nullptr)
		{
			ShowToast("Nothing to rewind to yet");
			return false;
		}
		return RewindTo(k->Id);
	}

	bool UnitySpaceApp::RewindTo(int id)
	{
		// A rewind is a load in place, so it is the host's in co-op; a client's request through the lockstep stream (any
		// player may rewind, the owner's call of 2026-11-05) is still to come (docs/REWIND.md, Build phases).
		if (CoopIsClient())
		{
			ShowToast("In co-op only the host can rewind for now");
			return false;
		}
		if (Travel->Active || InFtlCruise() || Ftl->Stage != FtlStage::Idle)
		{
			ShowToast(SaveBlocker());
			return false;
		}
		const RewindKeyframe* k = Rewind.Find(id);
		if (k == nullptr)
		{
			ShowToast("That moment is no longer kept");
			return false;
		}
		double sessionS = k->SessionS, jd = k->Jd, back = rewindSessionS - k->SessionS;
		cs::Ref<SaveData> data;
		try
		{
			data = Rewind.Restore(id, Time::realtimeSinceStartupAsDouble);
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Rewind to keyframe ", id, " failed: ", e.what()));
			ShowToast(cs::Cat("Could not rewind: ", e.what()));
			return false;
		}
		if (data == nullptr)
		{
			ShowToast("Could not rewind: the keyframe holds no game");
			return false;
		}
		rewindRestoring = true;
		CoopLoadingHere([this, &data] { ApplyLoaded(data); }); // the host's world is everyone's: the co-op resync follows
		rewindRestoring = false;
		rewindSessionS = sessionS;
		rewindLastJd = Clock->Jd;
		// Phase 3: the log goes on from this keyframe's frame (the old future is dropped as play goes on); a keyframe older
		// than the log starts it afresh.
		auto serial = rewindKeySerial.find(id);
		if (serial != rewindKeySerial.end()) rewindNextSerial = serial->second;
		else
		{
			rewindFrames.clear();
			ClearInterventions(); // US-T475: what it held was the replaced future's
		}
		rewindReplaying = false;
		ShowRewindBar(8);
		ShowSaves = false;
		ShowToast(cs::Cat("Rewound ", RewindSpan(back), " to ", PlayerOptions::DateAndTime(Astro::ToDateTime(jd)),
			" UTC  (Shift+F8 again to go further back; play on and the old future is replaced)"));
		UI::UiMotion::FadeFromBlack();
		return true;
	}

	void UnitySpaceApp::RewindAfterLoad()
	{
		UE_LOG(LogSpaceGod, Display, TEXT("AUDIT STEP rewind.0 RewindAfterLoad")); // US-T940
		ClearHoldRewind(false); // US-T409: a load or a keyframe rewind: the hold rewind's tracks and resume points are another world's (US-T410: and its held-back bookings, thrown away with it)
		if (rewindRestoring) return;
		Rewind.Clear();
		rewindSessionS = 0;
		rewindLastJd = Clock != nullptr ? Clock->Jd : std::numeric_limits<double>::quiet_NaN();
		rewindFrames.clear();
		rewindKeySerial.clear();
		UE_LOG(LogSpaceGod, Display, TEXT("AUDIT STEP rewind.1 before ClearInterventions")); // US-T940
		ClearInterventions();
		rewindReplaying = false;
		rewindFrameStarted = false;
		UE_LOG(LogSpaceGod, Display, TEXT("AUDIT STEP rewind.2 done")); // US-T940
	}

	// ---------------------------------------------------------------- phase 3: the frame log and seeking by replay

	bool UnitySpaceApp::RewindFrameStart(float& dt)
	{
		rewindAdventureSerial = -1;
		rewindAdventureFromLog = false;
		rewindFrameStarted = Clock != nullptr;
		// US-T475: the session records into the intervention log while the frame log logs (single player, not replaying),
		// at the serial this frame will be logged with.
		if (session != nullptr) session->Interventions = &interventions;
		interventions.Recording = Clock != nullptr && !rewindReplaying && rewindEnabled && Coop::CoopPeer::Current() == nullptr;
		interventions.Tick = rewindNextSerial;
		if (interventions.Recording)
		{
			// Play went on from an earlier moment: what the log held from here on was the old future.
			interventions.DropFrom(rewindNextSerial);
			RecordClockIntervention();
		}
		if (Clock == nullptr) return false;
		if (!rewindReplaying)
		{
			rewindFrameDt = dt;
			rewindFrameClock = Coop::ClockState::Of(*Clock);
			rewindFrameSessionS = rewindSessionS;
			// Anything the player did this frame may have changed the world; the log cannot replay it yet, so a seek stops
			// short of it. A mouse moved with nothing held only hovers.
			const Input::InputDevice& device = Input::InputDevice::current();
			bool touched = device.present && (device.scrollY != 0 || device.leftButton.isPressed || device.rightButton.isPressed ||
				device.middleButton.isPressed || device.leftButton.wasReleasedThisFrame || device.rightButton.wasReleasedThisFrame);
			for (int k = 1; !touched && device.present && k < static_cast<int>(Input::Key::Count); k++)
			{
				const auto& key = device[static_cast<Input::Key>(k)];
				touched = key.isPressed || key.wasPressedThisFrame || key.wasReleasedThisFrame;
			}
			rewindFrameIntervened = touched || Input::GamepadDevice::current().Touched() || !Input.HandsIdle();
			// US-T488: the log replays the session's commands and sticks; the helm's keys and a cockpit's own keys it does not keep.
			rewindFrameUnlogged = rewindFrameIntervened && (!Piloted.IsNone() || Input.ToggleFlightAssist || Input.LockTarget ||
				Input.LockAttacker || Input.DropMine || Input.NumberKey >= 0);
			return false;
		}
		// Esc stops the replay where it is: that moment is as real as the one aimed at.
		const RewindFrame* f = RewindFrameAt(rewindReplayNext);
		if (Input.Escape || f == nullptr)
		{
			FinishRewindReplay(true);
			return true;
		}
		dt = f->Dt;
		f->Before.ApplyTo(*Clock);
		return true;
	}

	void UnitySpaceApp::ClearInterventions()
	{
		interventions.Clear();
		interventionWarp = interventionSlow = interventionPause = -1;
	}

	void UnitySpaceApp::RecordOrderIntervention(const cs::String& verb, const cs::String& args)
	{
		if (!interventions.Recording) return;
		Coop::CoopPeer* peer = Coop::CoopPeer::Current();
		int seat = peer != nullptr ? peer->LocalSeat() : session != nullptr ? session->LocalPlayer().Seat : 0;
		try { interventions.Add(interventions.Tick, InterventionKind::Order, seat, args.empty() ? verb : cs::Cat(verb, " ", args)); }
		catch (const std::exception&)
		{
			// As LocalSession::Record: a log that cannot take it costs the rewind, never the order (recording stops until
			// the next frame's RewindFrameStart).
			interventions.Recording = false;
		}
	}

	void UnitySpaceApp::RecordClockIntervention()
	{
		if (Clock == nullptr) return;
		int pause = Clock->PauseHeld() ? 1 : 0;
		const RewindFrame* previous = RewindFrameAt(rewindNextSerial - 1);
		bool setByHand = previous != nullptr && Clock->Jd != previous->JdAfter;
		if (!setByHand && Clock->WarpIndex == interventionWarp && Clock->SlowIndex == interventionSlow && pause == interventionPause) return;
		interventionWarp = Clock->WarpIndex;
		interventionSlow = Clock->SlowIndex;
		interventionPause = pause;
		// "<warp index> <slow index> <pause held> <jd>", the Jd exact (Wire's round-trip form).
		interventions.Add(rewindNextSerial, InterventionKind::Clock, -1, cs::Cat(Coop::Wire::I(interventionWarp), " ",
			Coop::Wire::I(interventionSlow), " ", Coop::Wire::I(pause), " ", Coop::Wire::D(Clock->Jd)));
	}

	void UnitySpaceApp::LogRewindFrame()
	{
		bool logging = rewindFrameStarted && rewindEnabled && Coop::CoopPeer::Current() == nullptr;
		rewindFrameStarted = false;
		if (!logging) return;
		// Play went on from an earlier moment: what the log held after it was the old future.
		while (!rewindFrames.empty() && rewindFrames.back().Serial >= rewindNextSerial) rewindFrames.pop_back();
		if (!rewindFrames.empty() && rewindFrames.back().Serial != rewindNextSerial - 1) rewindFrames.clear();
		RewindFrame f;
		f.Serial = rewindNextSerial++;
		f.SessionS = rewindFrameSessionS;
		f.Dt = rewindFrameDt;
		f.Before = rewindFrameClock;
		f.JdAfter = Clock->Jd;
		f.Intervened = rewindFrameIntervened;
		f.Unlogged = rewindFrameUnlogged;
		f.System = RewindWatchedSystem(); // US-T497
		if (f.Serial % RewindCheckEvery == 0) f.Check = RewindWorldCheck();
		rewindFrames.push_back(f);
		while (rewindFrames.size() > RewindMaxFrames) rewindFrames.pop_front();
		interventions.DropBefore(rewindFrames.front().Serial); // US-T475: the intervention log follows the frame log's horizon
		rewindAdventureSerial = f.Serial; // this frame's adventures, later in it, are noted on it
	}

	void UnitySpaceApp::RewindAdventures(bool wanted)
	{
		// A replayed frame runs the adventures it ran, round the point it ran them round: the camera a seek lands with
		// need not stand where it stood (co-op's client does the same with the host's FrameRecord).
		if (rewindAdventureFromLog)
		{
			if (const RewindFrame* f = RewindFrameAt(rewindAdventureSerial))
			{
				if (f->Adventures) World()->UpdateAdventures(Clock->Jd, f->AdventurePoint);
				return;
			}
		}
		// Live: exactly the original's line, and the frame logged this frame notes what it did.
		DVec3 at;
		if (wanted)
		{
			at = PlayerHelioKm();
			World()->UpdateAdventures(Clock->Jd, at);
		}
		if (rewindAdventureSerial >= 0 && !rewindFrames.empty() && rewindFrames.back().Serial == rewindAdventureSerial)
		{
			rewindFrames.back().Adventures = wanted;
			rewindFrames.back().AdventurePoint = at;
		}
	}

	unsigned long long UnitySpaceApp::RewindWorldCheck()
	{
		// FNV-1a over co-op's world digest (clock, every system's counts and treasuries, the watched system's ships and
		// settlements): what a desync check compares, so a replay that matches it re-lived the play as a client would.
		cs::String d = CoopDigest();
		unsigned long long h = 1469598103934665603ULL;
		for (unsigned char c : d)
		{
			h ^= c;
			h *= 1099511628211ULL;
		}
		return h == 0 ? 1 : h;
	}

	const UnitySpaceApp::RewindFrame* UnitySpaceApp::RewindFrameAt(long long serial) const
	{
		if (rewindFrames.empty() || serial < rewindFrames.front().Serial || serial > rewindFrames.back().Serial) return nullptr;
		return &rewindFrames[static_cast<size_t>(serial - rewindFrames.front().Serial)];
	}

	bool UnitySpaceApp::RewindSeek(double targetS)
	{
		targetS = std::max(0.0, targetS);
		const RewindKeyframe* k = Rewind.Before(targetS + 1e-6);
		if (k == nullptr)
		{
			ShowToast(Rewind.Keyframes().empty() ? "Nothing to rewind to yet" : "That is before the session began");
			return false;
		}
		int id = k->Id;
		double fromS = k->SessionS;
		auto serial = rewindKeySerial.find(id);
		long long start = serial != rewindKeySerial.end() ? serial->second : -1;
		if (!RewindTo(id)) return false;
		if (targetS <= fromS + 1e-3 || start < 0 || Coop::CoopPeer::Current() != nullptr) return true;

		// The frames to replay: from the keyframe's on, until the one that reaches the target, the first the player acted
		// on, a gap in the log, or the replay cap.
		long long stop = start;
		double wallS = 0;
		rewindReplayWhy = "";
		for (;; stop++)
		{
			const RewindFrame* f = RewindFrameAt(stop);
			if (f == nullptr) { rewindReplayWhy = "the log ends there"; break; }
			if (f->SessionS >= targetS - 1e-9) break;
			// US-T488: the player's commands and sticks replay from the intervention log (and camera-only input needs none);
			// the replay stops only where the player did what the log cannot replay. The world checks report any drift.
			if (f->Unlogged) { rewindReplayWhy = "you flew the helm or worked a cockpit there (not replayed yet)"; break; }
			if (const InterventionEntry* e = RewindUnreplayable(stop))
			{
				rewindReplayWhy = e->Kind == InterventionKind::Order ? cs::Cat("you gave an order there ('", e->Text, "', not replayed yet)")
					: cs::String("the log holds a command there that does not read back");
				break;
			}
			wallS += f->Dt; // US-T486: no cap but the log (RewindMaxFrames): the replay runs headless, faster than played
		}
		if (stop == start)
		{
			if (!rewindReplayWhy.empty()) ShowToast(cs::Cat("Rewound to the keyframe: ", rewindReplayWhy));
			return true;
		}
		rewindReplaying = true;
		rewindReplayStart = rewindReplayNext = start;
		rewindReplayStop = stop;
		rewindReplayTargetS = targetS;
		rewindReplayDrift = 0;
		rewindReplayChecks = rewindReplayMismatches = 0;
		rewindReplayFirstMismatchS = -1;
		rewindReplayFirstMismatchSerial = -1;
		rewindReplayOrdersDone = start - 1;
		// US-T488: each seat's stick as it stood when the keyframe's world stepped on (a control frame is kept only on change).
		rewindReplayControls.clear();
		for (const auto& e : interventions.Entries())
		{
			if (e.Tick >= start) break;
			if (e.Kind != InterventionKind::Control) continue;
			try { rewindReplayControls[e.Seat] = InterventionLog::CommandOf(e); }
			catch (const std::exception&) { rewindReplayControls.erase(e.Seat); }
		}
		ShowToast(cs::Cat("Replaying ",RewindSpan(wallS), " of play to the moment picked  (Esc stops here)"));
		return true;
	}

	const InterventionEntry* UnitySpaceApp::RewindUnreplayable(long long tick) const
	{
		const auto& entries = interventions.Entries();
		auto it = std::lower_bound(entries.begin(), entries.end(), tick, [](const InterventionEntry& e, long long t) { return e.Tick < t; });
		for (; it != entries.end() && it->Tick == tick; ++it)
		{
			if (it->Kind == InterventionKind::Clock) continue; // the logged frame's clock (RewindFrame::Before)
			if (it->Kind == InterventionKind::Order)
			{
				// US-T492: replayed peer-less (RewindReplayOrder), but for the verbs that stop the seek.
				auto space = it->Text.find(' ');
				if (!RewindOrderReplays(space == cs::String::npos ? it->Text : it->Text.substr(0, space))) return &*it;
				continue;
			}
			try
			{
				if (InterventionLog::CommandOf(*it) == nullptr) return &*it;
			}
			catch (const std::exception&) { return &*it; }
		}
		return nullptr;
	}

	void UnitySpaceApp::RewindReplayInterventions(long long tick, bool intervened)
	{
		if (session == nullptr) return;
		const auto& entries = interventions.Entries();
		auto it = std::lower_bound(entries.begin(), entries.end(), tick, [](const InterventionEntry& e, long long t) { return e.Tick < t; });
		bool applied = false;
		for (; it != entries.end() && it->Tick == tick; ++it)
		{
			if (it->Kind != InterventionKind::Control && it->Kind != InterventionKind::Command) continue;
			cs::Ref<IGameCommand> c;
			try { c = InterventionLog::CommandOf(*it); }
			catch (const std::exception& e)
			{
				Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: entry '", it->Text, "' does not read back: ", e.what()));
				continue;
			}
			if (it->Kind == InterventionKind::Control) rewindReplayControls[it->Seat] = c;
			else if (intervened && c != nullptr)
			{
				session->Submit(c);
				applied = true;
			}
		}
		if (applied) session->ApplyPending(); // Recording is off while replaying: nothing is logged again
	}

	bool UnitySpaceApp::RewindOrderReplays(const cs::String& verb)
	{
		// US-T492: the reset and the co-op smoke's verbs are not replayed (US-T493 replays spawn, practice and mech).
		return verb != "reset" && verb != "hrfight" && verb != "hrdrop" && verb != "hrborn";
	}

	void UnitySpaceApp::RewindReplayOrders(long long serial)
	{
		if (serial <= rewindReplayOrdersDone || serial < rewindReplayStart) return;
		rewindReplayOrdersDone = serial;
		const RewindFrame* f = RewindFrameAt(serial);
		if (f == nullptr || !f->Intervened) return;
		const auto& entries = interventions.Entries();
		auto it = std::lower_bound(entries.begin(), entries.end(), serial, [](const InterventionEntry& e, long long t) { return e.Tick < t; });
		std::vector<std::pair<int, cs::String>> orders; // copied: an order may touch the log
		for (; it != entries.end() && it->Tick == serial; ++it)
			if (it->Kind == InterventionKind::Order) orders.emplace_back(it->Seat, it->Text);
		for (const auto& o : orders)
		{
			try { RewindReplayOrder(o.first, o.second); }
			catch (const std::exception& e)
			{
				Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: order '", o.second, "' failed: ", e.what()));
			}
		}
	}

	void UnitySpaceApp::RewindReplayOrder(int seat, const cs::String& text)
	{
		namespace Wire = Coop::Wire;
		auto space = text.find(' ');
		cs::String verb = space == cs::String::npos ? text : text.substr(0, space);
		cs::String args = space == cs::String::npos ? cs::String() : text.substr(space + 1);
		std::vector<cs::String> a = args.empty() ? std::vector<cs::String>() : Wire::Split(args, ' ');
		auto joinFrom = [&a](size_t from)
		{
			cs::String s;
			for (size_t i = from; i < a.size(); i++) s = i > from ? cs::Cat(s, " ", a[i]) : a[i];
			return s;
		};
		if (!RewindOrderReplays(verb)) return; // the seek stops before it (RewindUnreplayable)
		// US-T493: a spawn's spot as CoopSpawns wrote it (frame, offset km, forward, then the camera's up frame).
		auto spawnAt = [&a](size_t at, std::function<void(int, DVec3, DVec3)> spawn)
		{
			int frame = Wire::ParseI(a.at(at));
			DVec3 offsetKm(Wire::ParseD(a.at(at + 1)), Wire::ParseD(a.at(at + 2)), Wire::ParseD(a.at(at + 3)));
			DVec3 forward(Wire::ParseD(a.at(at + 4)), Wire::ParseD(a.at(at + 5)), Wire::ParseD(a.at(at + 6)));
			if (a.size() > at + 7)
			{
				auto c = Wire::Split(a[at + 7], ',');
				CoopUpFrame = Quaternion(Wire::ParseF(c.at(0)), Wire::ParseF(c.at(1)), Wire::ParseF(c.at(2)), Wire::ParseF(c.at(3)));
			}
			try { spawn(frame, offsetKm, forward); }
			catch (...) { CoopUpFrame.reset(); throw; }
			CoopUpFrame.reset();
		};
		if (verb == "spawn" || verb == "practice")
		{
			// The helm let go first, as SpawnShip and SpawnPracticeFighter do before they log the order.
			ReleaseSeat();
			if (IsPiloting()) ExitPiloting();
			if (verb == "spawn")
			{
				auto cls = static_cast<ShipClass>(Wire::ParseI(a.at(0)));
				bool hostile = a.at(1) == "1";
				spawnAt(2, [this, cls, hostile](int frame, DVec3 offsetKm, DVec3 forward) { SpawnShipAt(cls, hostile, frame, offsetKm, forward); });
			}
			else
			{
				ShowSpawn = ShowGalaxyMap = ShowPicker = false;
				spawnAt(0, [this](int frame, DVec3 offsetKm, DVec3 forward) { PracticeFighterAt(frame, offsetKm, forward); });
			}
		}
		else if (verb == "mech")
		{
			// Only logged in co-op (DeployMech's CoopSends): DeployMech's single-player tail on the logged battle and unit.
			const auto& battles = World()->War->Battles;
			int b = Wire::ParseI(a.at(0)), u = Wire::ParseI(a.at(1));
			auto battle = b >= 0 && b < battles.Count() ? battles[b] : nullptr;
			auto unit = battle != nullptr && u >= 0 && u < battle->Units.Count() ? battle->Units[u] : nullptr;
			cs::String why;
			auto mech = MechWalker::Deploy(battle, unit, why);
			if (mech == nullptr) ShowToast(cs::Cat("Can't send out a mech: ", why));
			else if (!EmbodiableEquals(HeldNonHelm(), mech))
			{
				mechHeight = 0;
				TakeSeat(mech);
			}
		}
		else if (verb == "travel")
		{
			if (auto to = Galaxy->Find(args)) TravelTo(to);
		}
		else if (verb == "plot") PlotCourse(Galaxy->Find(args));
		else if (verb == "cruise") CruiseTo(CruiseTargetFromWire(args));
		else if (verb == "laneplot") PlotLaneRoute(Wire::ParseI(a.at(0)), Wire::ParseI(a.at(1)), joinFrom(2));
		else if (verb == "laneclear") ClearLaneRoute();
		else if (verb == "autopilot") EngageCourseAutopilot();
		else if (verb == "crew")
		{
			int id = Wire::ParseI(a.at(0));
			cs::Ref<Ship> ship = id >= 0 && id < Fleet()->Ships().Count() ? Fleet()->Ships()[id] : nullptr;
			auto to = Galaxy->Find(joinFrom(2));
			if (ship != nullptr && to != nullptr) StartCrewCourse(ship, to, a.at(1) == "1");
		}
		else if (verb == "tanker") CallTanker();
		else if (verb == "civ")
		{
			bool sent = false;
			cs::String done;
			cs::String why = CivHandAsk(args, sent, done);
			if (!why.empty()) ShowToast(cs::Cat("The SpaceGod's hand is refused: ", why));
		}
		else if (verb == "ftlplot")
		{
			cs::String name = joinFrom(1);
			auto system = Galaxy->Find(name);
			cs::Ref<FtlTarget> target = system != nullptr ? FtlTarget::OfStar(system) : nullptr;
			if (target == nullptr)
				for (const auto& star : Galaxy->NearbyStars)
					if (star.Name == name) { target = FtlTarget::OfStar(star); break; }
			if (target != nullptr) PlotFtlCourse(target, a.at(0) == "1");
		}
		else if (verb == "ftlclear") ClearFtlCourse();
		else if (verb == "ftlauto") EngageFtlAutopilot();
		else if (!hud->CoopExecute(verb, a) && !UI::GalaxyMapScreen::CoopExecute(this, verb, a))
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: unknown order '", verb, "'"));
	}

	bool UnitySpaceApp::RewindReplayedControl(EmbodimentInput& input) const
	{
		if (!rewindReplaying || session == nullptr) return false;
		auto found = rewindReplayControls.find(session->State()->SeatIdOf(session->LocalPlayer()));
		if (found == rewindReplayControls.end()) return false;
		auto control = cs::As<SeatControlCommand>(found->second);
		if (control == nullptr) return false;
		input = control->Input;
		return true;
	}

	void UnitySpaceApp::FinishRewindReplay(bool stopped)
	{
		rewindReplaying = false;
		rewindReplayControls.clear();
		// Play goes on from here: the frames after it are the old future, dropped as the log goes on.
		rewindNextSerial = rewindReplayNext;
		if (rewindReplayDrift > 0)
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: the clock differed from the log on ", rewindReplayDrift, " of ",
				rewindReplayNext - rewindReplayStart, " frames"));
		// Whether the replay re-lived the play: its world checks against the ones the play kept. US-T487: the first that
		// differs stops the replay there (RewindReplayNote's callers), so the verdict names that frame.
		bool drifted = rewindReplayMismatches > 0;
		cs::String exact = rewindReplayChecks == 0 ? cs::String("not checked (too short)")
			: !drifted ? cs::Cat("exact (", rewindReplayChecks, " checks matched)")
			: cs::Cat("stopped where it drifted from the play: frame serial ", rewindReplayFirstMismatchSerial, ", ",
				RewindSpan(rewindReplayFirstMismatchS), " into the session (", rewindReplayChecks - 1, " checks matched before it)");
		rewindLastVerdict = cs::Cat(rewindReplayNext - rewindReplayStart, " of ", rewindReplayStop - rewindReplayStart, " frames replayed, ", exact);
		if (drifted) Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay ", rewindLastVerdict));
		else Debug::Log(cs::Cat("[Unity.SpaceGod] Rewind replay ", rewindLastVerdict));
		cs::String when = PlayerOptions::DateAndTime(Astro::ToDateTime(Clock->Jd), true);
		cs::String tail = drifted ? cs::String("  (sg.Rewind list)") : cs::String();
		if (drifted) ShowToast(cs::Cat("Replay stopped at ", when, " UTC, short of the moment picked: the world drifted from the play at frame ",
			rewindReplayFirstMismatchSerial, tail));
		else if (stopped) ShowToast(cs::Cat("Replay stopped at ", when, " UTC", tail));
		else if (!rewindReplayWhy.empty()) ShowToast(cs::Cat("Replayed to ", when, " UTC, short of the moment picked: ", rewindReplayWhy, tail));
		else ShowToast(cs::Cat("Arrived at ", when, " UTC  (play on and the old future is replaced)", tail));
		ShowRewindBar(6);
	}

	void UnitySpaceApp::ShowRewindBar(double seconds)
	{
		rewindBarUntil = std::max(rewindBarUntil, Time::realtimeSinceStartupAsDouble + seconds);
	}

	void UnitySpaceApp::DrawRewindBar()
	{
		const auto& frames = Rewind.Keyframes();
		double wall = Time::realtimeSinceStartupAsDouble;
		if (frames.empty() || ScreenCleared || rewindReplaying) rewindScrubbing = false;
		if (frames.empty() || (!rewindReplaying && !rewindScrubbing && wall > rewindBarUntil) || ScreenCleared) return;
		if (rewindBarPanel == nullptr)
		{
			rewindBarBack = UI::Hud::Solid(Color(0.03f, 0.05f, 0.09f, 0.86f), Color(0.5f, 0.72f, 1.0f, 0.7f));
			rewindBarLog = UI::Hud::Solid(Color(0.3f, 0.55f, 0.9f, 0.45f), Color(0.3f, 0.55f, 0.9f, 0.45f));
			rewindBarKey = UI::Hud::Solid(Color(0.85f, 0.92f, 1.0f, 0.95f), Color(0.85f, 0.92f, 1.0f, 0.95f));
			rewindBarNow = UI::Hud::Solid(Color(1.0f, 0.78f, 0.3f, 1.0f), Color(1.0f, 0.78f, 0.3f, 1.0f));
			rewindBarHover = UI::Hud::Solid(Color(1.0f, 1.0f, 1.0f, 0.6f), Color(1.0f, 1.0f, 1.0f, 0.6f));
			rewindBarPanel = cs::New<GUIStyle>();
			rewindBarPanel->normal.background = rewindBarBack;
			rewindBarPanel->border = RectOffset(2, 2, 2, 2);
			rewindBarText = cs::New<GUIStyle>();
			rewindBarText->fontSize = 12;
			rewindBarText->alignment = TextAnchor::MiddleCenter;
			rewindBarText->normal.textColor = UI::Hud::Dim;
			rewindBarText->clipping = TextClipping::Clip;
		}

		double endS = std::max(rewindSessionS, frames.back()->SessionS);
		if (!rewindFrames.empty()) endS = std::max(endS, rewindFrames.back().SessionS);
		if (endS <= 0) endS = 1;
		float w = std::min(900.0f, Screen::width - 80.0f), h = 54;
		// US-T440: the rewind bar's drawer (an alert, always shown: at the top edge under the top bar, framed by it).
		auto panel = hud->Place(w, h);
		auto strip = Rect(panel.x + 12, panel.y + 8, w - 24, 14);
		auto xOf = [&](double s) { return strip.x + static_cast<float>(cs::Math::Clamp(s / endS, 0.0, 1.0)) * strip.width; };

		// The replayable stretch (the frame log), the keyframes, and where the session is now.
		if (!rewindFrames.empty())
		{
			float a = xOf(rewindFrames.front().SessionS), b = xOf(rewindFrames.back().SessionS);
			GUI::DrawTexture(Rect(a, strip.y + 4, std::max(2.0f, b - a), strip.height - 8), rewindBarLog);
		}
		for (const auto& k : frames) GUI::DrawTexture(Rect(xOf(k->SessionS) - 1, strip.y, k->Pinned ? 3.0f : 2.0f, strip.height), rewindBarKey);
		double nowS = rewindReplaying ? rewindReplayTargetS : rewindSessionS;
		GUI::DrawTexture(Rect(xOf(rewindSessionS) - 1.5f, strip.y - 3, 3, strip.height + 6), rewindBarNow);

		cs::String line;
		Event& e = Event::current();
		auto hit = Rect(strip.x, strip.y - 4, strip.width, strip.height + 8);
		if (rewindReplaying)
		{
			long long total = std::max<long long>(1, rewindReplayStop - rewindReplayStart);
			double done = static_cast<double>(rewindReplayNext - rewindReplayStart) / static_cast<double>(total);
			GUI::DrawTexture(Rect(xOf(nowS) - 1, strip.y - 3, 2, strip.height + 6), rewindBarHover);
			// US-T486: the now marker walks to the target as the fast-forward runs; the line counts the frames.
			line = cs::Cat("Fast-forwarding to ", RewindSpan(rewindReplayTargetS - frames.front()->SessionS), " into the session: ",
				cs::Fmt(done * 100, "F0"), "%  (", rewindReplayNext - rewindReplayStart, " of ", total, " frames; Esc stops here)");
		}
		else if (rewindScrubbing || hit.Contains(e.mousePosition))
		{
			// US-T457: press and drag along the bar to preview a moment (the bodies' analytic positions then, below), release
			// to seek there; released well off the bar, the scrub is dropped and nothing changes.
			ShowRewindBar(2);
			auto sAt = [&](float x) { return cs::Math::Clamp(static_cast<double>((x - strip.x) / strip.width) * endS, 0.0, endS); };
			double s = rewindScrubbing ? rewindScrubS : sAt(e.mousePosition.x);
			bool off = rewindScrubbing && std::abs(e.mousePosition.y - strip.center().y) > 80.0f;
			if (e.type == EventType::MouseDown && e.button == 0 && hit.Contains(e.mousePosition))
			{
				rewindScrubbing = true;
				rewindScrubS = s;
				e.Use();
			}
			else if (rewindScrubbing && e.type == EventType::MouseDrag)
			{
				rewindScrubS = s = sAt(e.mousePosition.x);
				e.Use();
			}
			else if (rewindScrubbing && e.type == EventType::MouseUp && e.button == 0)
			{
				rewindScrubbing = false;
				if (!off) RequestRewindSeek(rewindScrubS);
				e.Use();
			}
			GUI::DrawTexture(Rect(xOf(s) - 1, strip.y - 3, 2, strip.height + 6), rewindBarHover);
			const RewindKeyframe* k = Rewind.Before(s + 1e-6);
			bool exact = k != nullptr && rewindKeySerial.count(k->Id) != 0 && !rewindFrames.empty() && s <= rewindFrames.back().SessionS;
			if (rewindScrubbing)
			{
				bool logged = false;
				double jd = RewindJdAt(s, logged);
				line = off ? cs::String("Release here to drop the scrub (nothing changes)")
					: cs::Cat("Release to go back ", RewindSpan(rewindSessionS - s), " to ", PlayerOptions::DateAndTime(Astro::ToDateTime(jd)),
						logged ? "" : " (about)", " UTC", exact ? " (replayed from the keyframe before)" : " (lands on the keyframe before)");
				DrawRewindScrubPreview(panel, jd, logged, RewindSystemAt(s));
			}
			else
				line = cs::Cat("Click to go back ", RewindSpan(rewindSessionS - s), exact ? " (replayed from the keyframe before)" : " (lands on the keyframe before)",
					"  ·  drag to preview");
		}
		else
			line = cs::Cat("Timeline: ", frames.size(), " keyframes over ", RewindSpan(endS), ", the blue stretch replays to any moment  ·  Shift+F8 the last keyframe  ·  click a moment");
		GUI::Label(Rect(panel.x, strip.yMax() + 4, w, 22), line, rewindBarText);
	}

	double UnitySpaceApp::RewindJdAt(double sessionS, bool& logged) const
	{
		logged = false;
		if (!rewindFrames.empty() && sessionS >= rewindFrames.front().SessionS && sessionS <= rewindFrames.back().SessionS)
		{
			// The logged frame the moment falls in, between its clock before and after.
			auto next = std::upper_bound(rewindFrames.begin(), rewindFrames.end(), sessionS,
				[](double s, const RewindFrame& f) { return s < f.SessionS; });
			const RewindFrame& f = *std::prev(next);
			double span = next != rewindFrames.end() ? next->SessionS - f.SessionS : 0;
			double t = span > 0 ? cs::Math::Clamp((sessionS - f.SessionS) / span, 0.0, 1.0) : 1.0;
			logged = true;
			return f.Before.Jd + (f.JdAfter - f.Before.Jd) * t;
		}
		// Between the keyframes on either side, or the last one and now.
		const auto& frames = Rewind.Keyframes();
		double s0 = 0, jd0 = Clock != nullptr ? Clock->Jd : 0, s1 = rewindSessionS, jd1 = jd0;
		bool haveBefore = false;
		for (const auto& k : frames)
		{
			if (k->SessionS <= sessionS) { s0 = k->SessionS; jd0 = k->Jd; haveBefore = true; }
			else { s1 = k->SessionS; jd1 = k->Jd; break; }
		}
		if (!haveBefore) return frames.empty() ? jd1 : frames.front()->Jd;
		if (s1 <= s0) return jd0;
		return jd0 + (jd1 - jd0) * cs::Math::Clamp((sessionS - s0) / (s1 - s0), 0.0, 1.0);
	}

	int UnitySpaceApp::RewindWatchedSystem() const
	{
		if (Galaxy == nullptr || ActiveSystem == nullptr) return -1;
		return Galaxy->Systems.IndexOf(ActiveSystem);
	}

	cs::Ref<StarSystem> UnitySpaceApp::RewindSystemAt(double sessionS) const
	{
		int index = -1;
		if (!rewindFrames.empty() && sessionS >= rewindFrames.front().SessionS && sessionS <= rewindFrames.back().SessionS)
		{
			auto next = std::upper_bound(rewindFrames.begin(), rewindFrames.end(), sessionS,
				[](double s, const RewindFrame& f) { return s < f.SessionS; });
			index = std::prev(next)->System;
		}
		else if (const RewindKeyframe* k = Rewind.Before(sessionS + 1e-6))
		{
			auto it = rewindKeySystem.find(k->Id);
			if (it != rewindKeySystem.end() && it->second.SessionS == k->SessionS) index = it->second.System;
		}
		if (Galaxy != nullptr && index >= 0 && index < Galaxy->Systems.Count()) return Galaxy->Systems[index];
		return ActiveSystem;
	}

	void UnitySpaceApp::DrawRewindScrubPreview(const Rect& under, double jd, bool logged, const cs::Ref<StarSystem>& shown)
	{
		// US-T497: the system the moment was in, which a star trip since may have left; drawn from its own catalog and
		// ephemeris (HelioAt reads either without touching the frame).
		const cs::Ref<StarSystem>& system = shown != nullptr ? shown : ActiveSystem;
		if (Event::current().type != EventType::Repaint || system == nullptr || Clock == nullptr) return;
		bool watched = system == ActiveSystem;
		auto catalog = system->Catalog;
		auto eph = system->Ephemeris;
		if (catalog == nullptr || eph == nullptr) return;
		bool uncharted = !watched && Frontier::IsUncharted(*system);

		// The star(s) and what orbits them directly (a moon would sit on its world at this scale).
		struct Mark { DVec3 Then, Now; Color Tint; bool Star; };
		std::vector<Mark> marks;
		double reach = 0;
		for (int i = 0; i < catalog->Count(); i++)
		{
			auto b = (*catalog)[i];
			if (b == nullptr || !b->IsVisible()) continue;
			bool star = b->IsStar();
			if (watched ? BodyUncharted(i) : (uncharted && !star && !Discoveries().HasBody(system->Name, b->Name))) continue;
			auto parent = b->ParentIndex >= 0 && b->ParentIndex < catalog->Count() ? (*catalog)[b->ParentIndex] : nullptr;
			if (!star && (parent == nullptr || !parent->IsStar())) continue;
			Mark m{ eph->HelioAt(i, jd), eph->HelioAt(i, Clock->Jd), Color(b->Color.R, b->Color.G, b->Color.B, 1), star };
			if (!star) reach = std::max(reach, std::max(m.Then.Length(), m.Now.Length()));
			marks.push_back(m);
		}
		if (marks.empty()) return;
		if (reach <= 0) reach = 1;

		// US-T495: the ships where their flight plans put them then (a conic transfer, a burn, a lane ride, parked or
		// at a ring), read off each one's schedule alone (Fleet::PlannedHelioAt: no sidestep, nothing changed). A ship
		// whose current leg began after the moment has no plan recorded for it then and is left out. US-T497: only when the
		// moment was in the watched system: the fleet is that system's, and its ships were nowhere in the one shown.
		struct Hull { DVec3 Then, Now; bool HaveNow, Piloted; };
		std::vector<Hull> hulls;
		int unplanned = 0;
		cs::Ref<Core::Fleet> fleet;
		if (watched && World() != nullptr) fleet = Fleet();
		cs::Ref<Core::Ship> piloted;
		if (World() != nullptr) piloted = World()->PilotedShip;
		if (fleet != nullptr)
			for (const auto& ship : fleet->Ships())
			{
				if (ship == nullptr || ship->Destroyed) continue;
				Hull hull{};
				if (!fleet->PlannedHelioAt(ship, jd, hull.Then)) { unplanned++; continue; }
				hull.HaveNow = fleet->PlannedHelioAt(ship, Clock->Jd, hull.Now);
				hull.Piloted = ship == piloted;
				hulls.push_back(hull);
			}

		float side = 200;
		auto box = Rect(under.center().x - side / 2, under.yMax() + 6, side, side);
		GUI::Box(box, cs::String(), rewindBarPanel);
		Vector2 c = box.center();
		float radius = side / 2 - 10;
		// Square-root radial scale: the inner worlds stay apart while the outer ones still fit; the angle is the true one.
		auto at = [&](const DVec3& p)
		{
			double r = p.Length();
			if (r <= 0) return c;
			double k = cs::Math::Sqrt(cs::Math::Min(1.0, r / reach)) * radius / r;
			return Vector2(c.x + static_cast<float>(p.X * k), c.y - static_cast<float>(p.Y * k));
		};
		auto old = GUI::color;
		const auto& tex = Texture2D::whiteTexture();
		for (const auto& m : marks)
		{
			Vector2 now = at(m.Now), then = at(m.Then);
			float size = m.Star ? 7.0f : 4.0f;
			GUI::color = Color(m.Tint.r, m.Tint.g, m.Tint.b, 0.3f);
			GUI::DrawTexture(Rect(now.x - size / 2, now.y - size / 2, size, size), tex);
			GUI::color = Color(m.Tint.r, m.Tint.g, m.Tint.b, logged ? 1.0f : 0.75f);
			GUI::DrawTexture(Rect(then.x - size / 2, then.y - size / 2, size, size), tex);
		}
		// Ships over the bodies: small cyan ticks (the piloted hull gold and larger), dim now, bright then.
		for (const auto& hull : hulls)
		{
			Color tint = hull.Piloted ? Color(1.0f, 0.82f, 0.35f, 1) : Color(0.55f, 0.9f, 1.0f, 1);
			float size = hull.Piloted ? 4.0f : 2.0f;
			if (hull.HaveNow)
			{
				Vector2 now = at(hull.Now);
				GUI::color = Color(tint.r, tint.g, tint.b, 0.3f);
				GUI::DrawTexture(Rect(now.x - size / 2, now.y - size / 2, size, size), tex);
			}
			Vector2 then = at(hull.Then);
			GUI::color = Color(tint.r, tint.g, tint.b, logged ? 1.0f : 0.75f);
			GUI::DrawTexture(Rect(then.x - size / 2, then.y - size / 2, size, size), tex);
		}
		GUI::color = old;
		GUI::Label(Rect(box.x, box.yMax() - 20, box.width, 18), watched ? cs::Cat(system->Name, ": bright then, dim now")
			: cs::Cat(system->Name, " (before the trip): bright then, dim now"), rewindBarText);
		if (!hulls.empty() || unplanned > 0)
			GUI::Label(Rect(box.x, box.y + 2, box.width, 18), unplanned > 0 ? cs::Cat(hulls.size(), " ships on plan, ", unplanned, " not yet on theirs")
				: cs::Cat(hulls.size(), " ships on their plans"), rewindBarText);
	}

	cs::String UnitySpaceApp::DescribeRewind() const
	{
		const auto& frames = Rewind.Keyframes();
		cs::String text = cs::Cat("Rewind: ", frames.size(), " keyframes, RAM ", RewindMb(Rewind.Bytes()), " of ", RewindMb(Rewind.BudgetBytes),
			", disk ", RewindMb(Rewind.DiskBytes()), " of ", RewindMb(Rewind.DiskBudgetBytes),
			Rewind.Spilling() ? "" : (Rewind.SpillFolder.empty() ? " (no spill folder)" : " (spill off: a write failed)"),
			", now ", RewindSpan(rewindSessionS), " into the session", rewindEnabled ? "" : " (off in this run)");
		size_t acted = 0;
		for (const auto& f : rewindFrames) acted += f.Intervened ? 1 : 0;
		text += rewindFrames.empty() ? cs::String("\n  frame log: empty (co-op, or nothing played yet)")
			: cs::Cat("\n  frame log: ", rewindFrames.size(), " frames, ", RewindSpan(rewindFrames.back().SessionS - rewindFrames.front().SessionS),
				" of game time (from ", RewindSpan(rewindSessionS - rewindFrames.front().SessionS), " back), ", acted, " with your input");
		text += cs::Cat("\n  intervention log: ", interventions.Count(), " entries, ", interventions.ByteSize(), " bytes",
			interventions.Recording ? "" : " (not recording)");
		if (!rewindLastVerdict.empty()) text += cs::Cat("\n  last replay: ", rewindLastVerdict);
		for (const auto& k : frames)
		{
			text += cs::Cat("\n  #", k->Id, "  ", Astro::ToDateTime(k->Jd).ToString("yyyy-MM-dd HH:mm:ss"), "  ", RewindSpan(rewindSessionS - k->SessionS),
				rewindSessionS >= k->SessionS ? " back" : " ahead", "  ", RewindMb(k->StoredBytes()), k->Whole() ? " whole" : " delta", k->OnDisk() ? " [disk]" : "", "  capture ", cs::Fmt(k->CaptureMs, "F0"), " ms",
				k->Pinned ? "  [start]" : "", k->Battle ? "  [battle]" : "", k->Id == Rewind.Head() ? "  <- here" : "");
		}
		return text;
	}
}
