// US-T540 (not a C# source).

#include "Combat/BattleTuning.h"

#include "Combat/Battle.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& BattleTuning::EscapeKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "escapeKm", "Escape distance", 55, 20, 150, 1, "km",
			TuningScope::Simulation, "How far from a battle's anchor a unit is out of the fight and gone.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::HuntPlayerKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "huntPlayerKm", "Hunt the player within", 8, 0, 40, 0.5, "km",
			TuningScope::Simulation, "How near the player a dogfighter goes for them in preference to whatever else it was after.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::MineTriggerKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "mineTriggerKm", "Mine trigger distance", 0.12, 0.01, 1, 0.01, "km",
			TuningScope::Simulation, "How near a mine a hostile hull must come to set it off, beyond the hull's own size.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::FireCallWeightKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "fireCallWeightKm", "Call for fire weight", 40, 0, 120, 1, "km",
			TuningScope::Simulation, "How much nearer a hull called for fire scores in a gunner's choice of target.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::FallBackStandKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "fallBackStandKm", "Fall back station", 3, 0.5, 15, 0.5, "km",
			TuningScope::Simulation, "How far behind the caller, away from the enemy, the hulls falling back take station.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::CrippleCallWeightKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "crippleCallWeightKm", "Cripple call weight", 40, 0, 120, 1, "km",
			TuningScope::Simulation, "How much nearer a hull called to be crippled scores in a gunner's choice of target.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::WithdrawCoverKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "withdrawCoverKm", "Withdrawal spread", 8, 0, 30, 0.5, "km",
			TuningScope::Simulation, "How far the most and the least damaged of a withdrawing group spread along its way out.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::MergeKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "mergeKm", "Dogfight merge distance", 0.3, 0.05, 2, 0.05, "km",
			TuningScope::Simulation, "How close the merge is: inside it a dogfighter extends.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::ExtendKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "extendKm", "Dogfight extend distance", 1.6, 0.2, 6, 0.1, "km",
			TuningScope::Simulation, "How far a dogfighter extends past the merge before it comes round.");
		return s;
	}

	const cs::Ref<RangeSetting>& BattleTuning::TailKm()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("battle", "tailKm", "On its tail within", 2.5, 0.5, 8, 0.1, "km",
			TuningScope::Simulation, "How near a hostile behind a unit must be to count as on its tail.");
		return s;
	}

	double Battle::EscapeKm() { return BattleTuning::EscapeKm()->Value; }

	double Battle::HuntPlayerKm() { return BattleTuning::HuntPlayerKm()->Value; }

	double Battle::MineTriggerKm() { return BattleTuning::MineTriggerKm()->Value; }

	double Battle::FireCallWeightKm() { return BattleTuning::FireCallWeightKm()->Value; }

	double Battle::FallBackStandKm() { return BattleTuning::FallBackStandKm()->Value; }

	double Battle::CrippleCallWeightKm() { return BattleTuning::CrippleCallWeightKm()->Value; }

	double Battle::WithdrawCoverKm() { return BattleTuning::WithdrawCoverKm()->Value; }

	double Battle::MergeKm() { return BattleTuning::MergeKm()->Value; }

	double Battle::ExtendKm() { return BattleTuning::ExtendKm()->Value; }

	double Battle::TailKm() { return BattleTuning::TailKm()->Value; }
}
