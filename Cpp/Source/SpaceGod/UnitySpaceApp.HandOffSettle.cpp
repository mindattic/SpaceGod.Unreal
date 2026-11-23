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

	int UnitySpaceApp::HandOffAnchorBody(const cs::Ref<Ship>& ship, DVec3 helio)
	{
		// The body a hull's drawn place rides, picked as KmToSceneNear does: its ring's body while queued or planetside,
		// else its orbit's, the root outside that body's neighbourhood.
		// The hull flown is drawn at PilotedScene, which rides the helm's frame body.
		if (ship == nullptr) return -1;
		if (ship == World()->PilotedShip && IsPiloting())
			return pilotOffsetKm().Length() <= ScaleModel::NeighbourhoodKm((*Catalog())[pilotFrameBody]) ? pilotFrameBody : 0;
		auto c = ship->Controller();
		if (c == nullptr) return -1;
		int b = c->Ring != nullptr && (c->Phase == ShipPhase::Queued || c->Planetside()) ? c->Ring->Body : (c->Orbit ? c->Orbit->Body : 0);
		if ((helio - Ephemeris()->Helio(b)).Length() > ScaleModel::NeighbourhoodKm((*Catalog())[b])) b = 0;
		return b;
	}

	void UnitySpaceApp::ResetHandOffSettle()
	{
		handOffCarry = cs::List<DVec3>();
		handOffLastDrawn = cs::List<DVec3>();
		handOffLastAnchor = cs::List<int>();
		handOffLastAnchorScene = cs::List<DVec3>();
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
			HandOffResize(handOffCarry, n);
			HandOffResize(handOffLastDrawn, n);
			HandOffResize(handOffLastAnchor, n);
			HandOffResize(handOffLastAnchorScene, n);
			for (int i = 0; i < n; i++) handOffLastAnchor[i] = -1;
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
			// The hull taken or let go stays where it was drawn last frame, carried on with the body it rode (at real scale
			// the planet's orbit moves the whole neighbourhood several hull radii a frame: pinning last frame's scene point
			// would freeze it for a frame while everything round it drifts), and eases to its new place from there.
			auto flipped = [&](const cs::Ref<Ship>& s)
			{
				if (s == nullptr || s->Destroyed || s->Id < 0 || s->Id >= n) return;
				DVec3 from = handOffLastDrawn[s->Id];
				int anchor = handOffLastAnchor[s->Id];
				if (anchor >= 0 && anchor < bodyScene.Count()) from += bodyScene[anchor] - handOffLastAnchorScene[s->Id];
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
				handOffLastDrawn[i] = shipScene[i];
				int anchor = i < shipStates.Count() ? HandOffAnchorBody(ships[i], shipStates[i].HelioKm) : -1;
				if (anchor >= bodyScene.Count()) anchor = -1;
				handOffLastAnchor[i] = anchor;
				handOffLastAnchorScene[i] = anchor >= 0 ? bodyScene[anchor] : DVec3::Zero;
			}
		}
		if (stepping || !handOffPrimed)
		{
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
			handOffLastDrawn[i] = shipScene[i];
		}
	}
}
