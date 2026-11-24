#pragma once
// US-T540 (not a C# source): a battle's distances as live tune.battle.* knobs (US-T501).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;

	/// <summary>
	/// The distances a battle is fought by, km, as knobs the owner turns live from the Developer screen (Simulation scope,
	/// so in co-op the host's value is everyone's). Each defaults to exactly the constant it replaced, so nothing changes
	/// until someone turns it; <see cref="Battle"/> reads them on every use (<see cref="Battle.EscapeKm"/> and the rest).
	/// </summary>
	class SPACEGODCORE_API BattleTuning
	{
	public:
		static const cs::Ref<RangeSetting>& EscapeKm();

		static const cs::Ref<RangeSetting>& HuntPlayerKm();

		static const cs::Ref<RangeSetting>& MineTriggerKm();

		static const cs::Ref<RangeSetting>& FireCallWeightKm();

		static const cs::Ref<RangeSetting>& FallBackStandKm();

		static const cs::Ref<RangeSetting>& CrippleCallWeightKm();

		static const cs::Ref<RangeSetting>& WithdrawCoverKm();

		static const cs::Ref<RangeSetting>& MergeKm();

		static const cs::Ref<RangeSetting>& ExtendKm();

		static const cs::Ref<RangeSetting>& TailKm();
	};
}
