// US-T833: the subsystems a pilot can pick on the hull locked (see Combat/SubsystemTargets.h).

#include "Combat/SubsystemTargets.h"

#include "Combat/Battle.h"
#include "Combat/BroadcastArrays.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"

namespace SpaceGod::Core
{
	namespace
	{
		bool PickableHardpoints(const cs::Ref<BattleUnit>& target)
		{
			return target != nullptr && target->Kind != UnitKind::Fighter && target->Combat != nullptr;
		}

		void AddPickableKind(cs::List<cs::Ref<Subsystem>>& list, const cs::Ref<BattleUnit>& target, SubsystemKind kind)
		{
			for (const auto& s : target->Combat->Subsystems)
				if (!s->Destroyed() && s->Kind() == kind && !list.Contains(s)) list.Add(s);
		}
	}

	cs::List<cs::Ref<Subsystem>> SubsystemTargets::On(const cs::Ref<BattleUnit>& target)
	{
		if (target == nullptr) return cs::List<cs::Ref<Subsystem>>();
		auto list = WeakPoints::On(target);
		if (PickableHardpoints(target))
		{
			AddPickableKind(list, target, SubsystemKind::Bridge);
			AddPickableKind(list, target, SubsystemKind::Hangar);
			for (const auto& s : target->Combat->Subsystems)
				if (!s->Destroyed() && s->Def->IsWeapon() && !list.Contains(s)) list.Add(s);
		}
		auto array = BroadcastArrays::Of(target);
		if (array != nullptr && !array->Destroyed()) list.Add(array);
		return list;
	}

	bool SubsystemTargets::Any(const cs::Ref<BattleUnit>& target)
	{
		if (target == nullptr) return false;
		if (BroadcastArrays::Carries(target) && !BroadcastArrays::Silenced(target)) return true;
		if (!PickableHardpoints(target)) return false;
		for (const auto& s : target->Combat->Subsystems)
			if (!s->Destroyed() && (s->Def->IsWeapon() || s->Kind() == SubsystemKind::Bridge || s->Kind() == SubsystemKind::Hangar)) return true;
		return WeakPoints::On(target).Count() > 0;
	}

	cs::Ref<Subsystem> SubsystemTargets::Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step)
	{
		auto list = On(target);
		if (list.Count() == 0) return nullptr;
		int i = current == nullptr ? -1 : list.IndexOf(current);
		if (i < 0) return step >= 0 ? list[0] : list[list.Count() - 1];
		return list[((i + step) % list.Count() + list.Count()) % list.Count()];
	}

	cs::Ref<Subsystem> SubsystemTargets::Struck(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& hit, DVec3 at, const cs::Ref<Subsystem>& nearest)
	{
		if (shooter == nullptr || hit == nullptr || !shooter->PlayerControlled() || hit != shooter->PlayerLockTarget) return nearest;
		auto pick = shooter->PlayerSubTarget;
		if (pick == nullptr || pick == nearest || pick->Destroyed()) return nearest;
		if (BroadcastArrays::Is(hit, pick)) return BroadcastArrays::Struck(shooter, hit, at, nearest);
		if (!PickableHardpoints(hit) || !hit->Combat->Subsystems.Contains(pick)) return nearest;
		double d = (hit->PointOf(pick->Def) - at).Length();
		if (d > hit->LengthKm * ReachShare) return nearest;
		if (nearest != nullptr && d > NearestFactor * (hit->PointOf(nearest->Def) - at).Length()) return nearest;
		return pick;
	}
}
