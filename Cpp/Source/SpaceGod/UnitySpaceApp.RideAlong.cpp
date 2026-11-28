// US-T954: riding along in a ship that keeps doing its job (see UnitySpaceApp.RideAlong.inl, docs/Possession.md). View only:
// nothing here writes to the simulation, so the ship's schedule, its battles and the co-op digest go on as if nobody were aboard.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Coop/CoopPeer.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"
#include "Input/KeyBindings.h"
#include "Ships.h"
#include "SimClock.h"
#include "UI/Hud.h"

namespace SpaceGod
{
	using namespace Core;

	namespace
	{
		cs::String Lower(const char* s) { return cs::Strings::ToLowerInvariant(cs::String(s)); }

		/// <summary>What the ship is about: its job and the phase of it ("hauler · transit"), or the fight it is in.</summary>
		cs::String JobLine(const cs::Ref<Ship>& ship)
		{
			if (ship->Battle != nullptr) return "in battle";
			return cs::Cat(Lower(ToString(ship->Role)), " \xC2\xB7 ", Lower(ToString(ship->Phase)));
		}

		/// <summary>US-T955: the keys on a ride: P takes the helm, Esc is the god; back from the helm, either is the god.</summary>
		cs::String RideKeys(bool handedBack)
		{
			auto p = Input::KeyBindings::Keys(Input::KeyAction::Pilot);
			return handedBack ? cs::Cat(p, " or Esc back to god") : cs::Cat(p, " take the helm \xC2\xB7 Esc back to god");
		}
	}

	bool UnitySpaceApp::IsRiding() const { return rideAlong.IsShip() && rideAlong.Ship != nullptr && !rideAlong.Ship->Destroyed; }

	bool UnitySpaceApp::CanRideAlong(const cs::Ref<Ship>& ship)
	{
		return ship != nullptr && !ship->Destroyed && !ship->GateDeparted && !AtControls() && !IsRiding() && TargetValid(Target::OfShip(ship));
	}

	void UnitySpaceApp::ToggleRideAlong()
	{
		if (IsRiding())
		{
			StopRideAlong();
			return;
		}
		if (AtControls())
		{
			ShowToast(cs::Cat("Let go of the controls first (", Input::KeyBindings::Keys(Input::KeyAction::Pilot), ")"));
			return;
		}
		auto t = Selected.IsShip() ? Selected : Hovered.IsShip() ? Hovered : Target::None();
		if (!t.IsShip())
		{
			ShowToast("Select a ship to ride along in");
			return;
		}
		StartRideAlong(t.Ship);
	}

	void UnitySpaceApp::StartRideAlong(const cs::Ref<Ship>& ship)
	{
		if (!CanRideAlong(ship))
		{
			ShowToast("Can't ride along in that ship right now");
			return;
		}
		RememberCommand();
		rideAlong = Target::OfShip(ship);
		rideHandedBack = false;
		Select(rideAlong);
		SpaceCamera->EnterPilotChase(rideAlong, 6);
		BeginSeatTransition(-1); // the drift in, as for any seat; the warp is left as it is
		ShowToast(cs::Cat("Riding along in the ", ship->Name, ", ", Clock->Warp().Label, "  (", RideKeys(false), ")"));
	}

	void UnitySpaceApp::TakeRiddenHelm()
	{
		auto ship = RiddenShip();
		if (ship == nullptr) return;
		TakeSeat(Embodiables::HelmOf(World(), ship));
		// In co-op the take goes into the frames and the release does too: no ride to go back to there.
		if (Coop::CoopPeer::Current() == nullptr && IsPiloting() && Piloted.Ship == ship) helmFromRide = ship;
	}

	bool UnitySpaceApp::RideOnAfterHelm(const cs::String& why)
	{
		auto ship = helmFromRide;
		helmFromRide = nullptr;
		if (ship == nullptr || !why.empty() || IsSeated() || !IsPiloting() || !(Piloted.Ship == ship)) return false;
		if (ship->Destroyed || ship->GateDeparted || !TargetValid(Target::OfShip(ship))) return false;
		ExitPiloting(); // its crew has the helm back and its schedule picks up from here
		if (AtControls()) return false;
		rideAlong = Target::OfShip(ship);
		rideHandedBack = true;
		Select(rideAlong);
		SpaceCamera->PilotView = std::nullopt;
		SpaceCamera->PilotEye = nullptr;
		SpaceCamera->EnterPilotChase(rideAlong, 6);
		// The warp the ride ran at comes back as the energy leaves the helm; the god's view stays the one the ride remembered.
		int warp = commandWarp;
		BeginSeatTransition(warp);
		auto label = warp >= 0 && warp < SimClock::Levels().Count() ? SimClock::Levels()[warp].Label : Clock->Warp().Label;
		ShowToast(cs::Cat(ship->Name, " handed back to its crew: riding along, ", label, "  (", RideKeys(true), ")"));
		return true;
	}

	void UnitySpaceApp::StopRideAlong(const cs::String& why)
	{
		if (rideAlong.IsNone()) return;
		rideHandedBack = false;
		Target anchor = rideAlong;
		cs::String name = rideAlong.Ship != nullptr ? rideAlong.Ship->Name : cs::String();
		rideAlong = Target::None();
		SpaceCamera->ExitFocus();
		if (!commandFocus.IsNone() && !(commandFocus.IsShip() && commandFocus.Ship->Destroyed) && !commandFocus.IsUnit())
			SpaceCamera->FocusOn(commandFocus);
		else GlideBackToGodEye(anchor);
		BeginSeatTransition(-1);
		commandWarp = -1;
		commandFocus = Target::None();
		godEyeBody = -1;
		ShowToast(!why.empty() ? cs::Cat("The ", name, " ", why, ": back to the god's view") : cs::Cat("Left the ", name, ": back to the god's view"));
	}

	void UnitySpaceApp::UpdateRideAlong()
	{
		// US-T955: the ride's helm is only remembered while it is still the one flown here.
		if (helmFromRide != nullptr && !(IsPiloting() && Piloted.Ship == helmFromRide)) helmFromRide = nullptr;
		if (rideAlong.IsNone())
		{
			rideHandedBack = false;
			return;
		}
		// A seat taken from the ride (the picker, Fly this) is the seat's from here: the ride just ends.
		if (AtControls())
		{
			rideAlong = Target::None();
			return;
		}
		auto ship = rideAlong.Ship;
		if (ship == nullptr || ship->Destroyed) StopRideAlong("is gone");
		else if (ship->GateDeparted || !TargetValid(rideAlong)) StopRideAlong("is out of sight");
		else if (SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Free || !(SpaceCamera->FocusTarget == rideAlong))
		{
			// The camera was taken off the hull (a focus, a home key): the ride is over, the god's view where it now is.
			rideAlong = Target::None();
			commandWarp = -1;
			commandFocus = Target::None();
			godEyeBody = -1;
		}
	}

	void UnitySpaceApp::DrawRideAlongHud()
	{
		auto ship = RiddenShip();
		if (ship == nullptr || !ShowHud || ScreenCleared) return;
		if (rideStyle == nullptr)
		{
			rideStyle = cs::New<GUIStyle>();
			rideStyle->fontSize = 14;
			rideStyle->fontStyle = FontStyle::Bold;
			rideStyle->alignment = TextAnchor::MiddleCenter;
			rideStyle->normal.textColor = Color(0.75f, 0.88f, 1.0f);
			rideStyle->clipping = TextClipping::Clip;
		}
		float w = 760;
		GUI::Label(Rect((Screen::width - w) * 0.5f, 96, w, 22), cs::Cat("RIDING ALONG \xC2\xB7 ", ship->Name, " \xC2\xB7 ", JobLine(ship), "  (", RideKeys(rideHandedBack), ")"), rideStyle);
	}
}
