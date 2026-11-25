#pragma once
// US-T833: the subsystems a pilot can pick on the hull locked (FighterSeat.CycleWeakPoint, key 6), and what the player's
// guns strike when they land by the pick.
//
// The cycle runs through the hull's weak points (WeakPoints.On, a destroyer and up), then its bridge and hangar, then its
// weapon mounts in hardpoint order, then its broadcast array while it stands (BroadcastArrays). Guided rounds home on any
// pick. A gun's hit that lands near the pick strikes it instead of the hardpoint nearest the hit, and the hull section it
// tears at is the pick's. Only the player's own fire on the hull locked reaches a pick: nothing changes for a fight the
// player is not in.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BattleUnit;
	class Subsystem;

	class SPACEGODCORE_API SubsystemTargets
	{
	public:
		/// <summary>How near the pick a gun's hit must land to strike it, in hull lengths.</summary>
		static constexpr double ReachShare = 0.2;

		/// <summary>How much farther from the hit than the nearest hardpoint the pick may stand and still be struck.</summary>
		static constexpr double NearestFactor = 2;

		/// <summary><paramref name="target"/>'s pickable subsystems in cycle order: its weak points, bridge, hangar, weapon
		/// mounts, then its broadcast array (made whole the first time it is asked for) while it stands.</summary>
		static cs::List<cs::Ref<Subsystem>> On(const cs::Ref<BattleUnit>& target);

		/// <summary>Whether <paramref name="target"/> has anything to pick, without making its broadcast array.</summary>
		static bool Any(const cs::Ref<BattleUnit>& target);

		/// <summary>The next (or previous) pick on <paramref name="target"/> after <paramref name="current"/>, wrapping. Null if
		/// there is nothing to pick.</summary>
		static cs::Ref<Subsystem> Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step);

		/// <summary>
		/// What a gun's hit at <paramref name="at"/> on <paramref name="hit"/> strikes, given the hardpoint nearest it
		/// (<paramref name="nearest"/>, or null): the pick, if <paramref name="shooter"/> is the player's craft with it picked
		/// on that locked hull and the hit lands within <see cref="ReachShare"/> of it and no more than
		/// <see cref="NearestFactor"/> times the nearest hardpoint's distance; else the hardpoint. A broadcast array picked
		/// keeps its own rule (<see cref="BroadcastArrays.Struck"/>).
		/// </summary>
		static cs::Ref<Subsystem> Struck(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& hit, DVec3 at, const cs::Ref<Subsystem>& nearest);
	};
}
