// US-T481: the hand-off settle (design in US-T480, diagnosis in US-T478), view only: nothing here touches the simulation.
//
// When World()->PilotedShip changes (a take, a switch to another hull, a let-go) two things jump in the scene:
//  - the hull whose piloted-ness flipped: taken, it is drawn at the helm's pinned point (PilotedScene); let go, back on its
//    schedule. The camera follows shipScene (TargetScene), so carrying the hull carries the chase with it.
//  - the other free hulls: their keep-clear sidesteps (Fleet::DrawnOffsetOf) re-solve a Fleet::Update or two later, as
//    IsHeld and DrawnViewHull switch over, so they land a frame late (+17.7 then -26 units in sg.Feel).
// Each jump is carried as a scene offset added to the raw place and eased out (time constant HandOffEaseSeconds).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Pursuit.h"
#include "Infrastructure/Infrastructure.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		constexpr int HandOffWindowFrames = 8;     // frames (dt > 0) the sidestep re-solves are absorbed after a hand-off
		constexpr double HandOffEaseSeconds = 0.5; // the carry's decay time constant
		constexpr float HandOffMaxStep = 1.0f / 30.0f; // a hitch eases no further than a 30 Hz frame

		template <class T> void HandOffResize(cs::List<T>& a, int n) { a.Raw().resize(static_cast<size_t>(n)); }

		/// Each hull's offset to draw: its own carry, plus its leader's for an escort (it rides its leader's place).
		cs::List<DVec3> HandOffApplied(const cs::List<cs::Ref<Ship>>& ships, const cs::List<DVec3>& carry, int n)
		{
			cs::List<DVec3> applied;
			HandOffResize(applied, n);
			for (int pass = 0; pass < 2; pass++)
				for (int i = 0; i < n; i++)
				{
					const auto& ship = ships[i];
					if (ship->IsEscort() != (pass == 1)) continue;
					applied[i] = carry[i];
					if (pass == 1 && ship->Leader != nullptr && ship->Leader->Id >= 0 && ship->Leader->Id < n) applied[i] += applied[ship->Leader->Id];
				}
			return applied;
		}
	}

	void UnitySpaceApp::ResetHandOffSettle()
	{
		handOffCarry = cs::List<DVec3>();
		handOffLastDrawn = cs::List<DVec3>();
		handOffLastStep = cs::List<DVec3>();
		handOffLastDt = 0;
		handOffLastOff =cs::List<DVec3>();
		handOffLastOffValid = cs::List<bool>();
		handOffSettled = cs::List<DVec3>();
		handOffLastPiloted = nullptr;
		handOffFrames = 0;
		handOffPrimed = false;
	}

	void UnitySpaceApp::SettleHandOff(float dt)
	{
		const auto& ships = Fleet()->Ships();
		int n = cs::Math::Min(ships.Count(), shipScene.Count());
		if (handOffCarry.Count() != n)
		{
			int had = handOffLastDrawn.Count();
			HandOffResize(handOffCarry, n);
			HandOffResize(handOffLastDrawn, n);
			HandOffResize(handOffLastStep, n);
			// A hull new to the list has no last drawn place: take this frame's, so its first step reads as none.
			for (int i = had; i < n; i++)
			{
				handOffLastDrawn[i] = shipScene[i];
				handOffLastStep[i] = DVec3::Zero;
			}
			HandOffResize(handOffLastOff, n);
			HandOffResize(handOffLastOffValid, n);
			HandOffResize(handOffSettled, n);
		}
		auto piloted = World()->PilotedShip;
		bool stepping = dt > 0;

		if (stepping)
		{
			double keep = std::exp(-Mathf::Min(dt, HandOffMaxStep) / HandOffEaseSeconds);
			for (int i = 0; i < n; i++)
			{
				handOffCarry[i] = handOffCarry[i] * keep;
				if (handOffCarry[i].LengthSquared() < 1e-40) handOffCarry[i] = DVec3::Zero;
			}
		}

		bool handOff = stepping && handOffPrimed && piloted != handOffLastPiloted;
		if (handOff)
		{
			handOffFrames = HandOffWindowFrames;
			// The hull taken or let go goes on from where it was drawn last frame by the step it made on the last stepping
			// frame, scaled to this frame's dt (US-T490: at real scale the hulls' shared orbital motion moves the scene
			// several hull radii a frame; pinning last frame's point froze it for a frame while everything round it drifted,
			// and that drift is not a body moving, so carrying it with a body did nothing), then eases to its new place.
			auto flipped = [&](const cs::Ref<Ship>& s)
			{
				if (s == nullptr || s->Destroyed || s->Id < 0 || s->Id >= n) return;
				DVec3 from = handOffLastDrawn[s->Id];
				if (handOffLastDt > 0) from += handOffLastStep[s->Id] * (static_cast<double>(dt) / handOffLastDt);
				handOffCarry[s->Id] = from - shipScene[s->Id];
			};
			flipped(handOffLastPiloted);
			flipped(piloted);
		}

		bool window = handOffFrames > 0;
		for (int i = 0; i < n; i++)
		{
			auto ship = ships[i];
			// A free hull's sidestep, as UpdateShips adds it: not the hull flown, a pursuer, an escort (it rides its
			// leader), one in a fight (the battle places it) or one in or holding off a berth (posed by the station).
			bool free = !ship->Destroyed && ship != piloted && !ship->IsEscort() && ship->Battle == nullptr
				&& !(World()->Pursuit != nullptr && World()->Pursuit->Active() && World()->Pursuit->Of(ship) != nullptr);
			if (free && ship->Phase == ShipPhase::Parked && World()->Infrastructure != nullptr)
			{
				int berth = -1, place = -1;
				if (World()->Infrastructure->BerthedAt(ship, berth) != nullptr || World()->Infrastructure->HoldingAt(ship, place) != nullptr) free = false;
			}
			if (!free)
			{
				handOffLastOffValid[i] = false;
				continue;
			}
			auto evasion = Fleet()->DrawnOffsetOf(ship);
			auto off = evasion.LengthSquared() > 0 ? evasion.SwapYZ() * KmToSceneNear(ship, shipStates[i].HelioKm) : DVec3::Zero;
			// Only on a frame that steps: an extra UpdateShips(0) mid-frame saw the same solve.
			if (stepping)
			{
				if (window && handOffLastOffValid[i]) handOffCarry[i] += handOffLastOff[i] - off;
				handOffLastOff[i] = off;
				handOffLastOffValid[i] = true;
			}
		}
		if (stepping && handOffFrames > 0) handOffFrames--;

		auto applied = HandOffApplied(ships, handOffCarry, n);
		for (int i = 0; i < n; i++)
		{
			shipScene[i] += applied[i];
			handOffSettled[i] = shipScene[i];
			if (stepping || !handOffPrimed)
			{
				handOffLastStep[i] = stepping && handOffPrimed ? shipScene[i] - handOffLastDrawn[i] : DVec3::Zero;
				handOffLastDrawn[i] = shipScene[i];
			}
		}
		if (stepping || !handOffPrimed)
		{
			handOffLastDt = stepping && handOffPrimed ? dt : 0;
			handOffLastPiloted = piloted;
			handOffPrimed = true;
		}
	}

	void UnitySpaceApp::ReapplyHandOffSettle()
	{
		// An UpdateShips run since the settle (TargetScene's, for a ship spawned mid-frame) drew the raw places again:
		// put the carry back on each hull it rewrote, leaving the ones it did not.
		const auto& ships = Fleet()->Ships();
		int n = cs::Math::Min(cs::Math::Min(handOffCarry.Count(), shipScene.Count()), ships.Count());
		bool any = false;
		for (int i = 0; i < n && !any; i++) any = handOffCarry[i] != DVec3::Zero;
		if (!any) return;
		auto applied = HandOffApplied(ships, handOffCarry, n);
		for (int i = 0; i < n; i++)
		{
			if (applied[i] == DVec3::Zero || shipScene[i] == handOffSettled[i]) continue;
			shipScene[i] += applied[i];
			handOffSettled[i] = shipScene[i];
			// The drawn place moved since the settle recorded it: the step it made this frame moved with it.
			if (i < handOffLastStep.Count()) handOffLastStep[i] += shipScene[i] - handOffLastDrawn[i];
			handOffLastDrawn[i] = shipScene[i];
		}
	}
}
