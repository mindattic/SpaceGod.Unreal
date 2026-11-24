#pragma once
// US-T539 (not a C# source): global combat multipliers as live tune.combat.* knobs (US-T501).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class WeaponSpec;

	/// <summary>
	/// Global combat multipliers the owner turns live from the Developer screen (Simulation scope, so in co-op the
	/// host's value is everyone's). Each defaults to exactly 1, so nothing changes until someone turns it. A battle
	/// reads them on every use: weapon damage and hull and shield strength where a hit lands
	/// (<see cref="Battle.ApplyDamage"/>), rate of fire where a weapon's reload ticks down, range and projectile speed
	/// wherever a weapon's reach or a round's or missile's speed is read, and shield regeneration where emitters regrow
	/// (<see cref="ShipCombat.RegenShields"/>).
	/// </summary>
	class SPACEGODCORE_API CombatTuning
	{
	public:
		static const cs::Ref<RangeSetting>& WeaponDamage();

		static const cs::Ref<RangeSetting>& WeaponRange();

		static const cs::Ref<RangeSetting>& RateOfFire();

		static const cs::Ref<RangeSetting>& ProjectileSpeed();

		static const cs::Ref<RangeSetting>& ShieldStrength();

		static const cs::Ref<RangeSetting>& ShieldRegen();

		static const cs::Ref<RangeSetting>& HullStrength();

		/// <summary>How far <paramref name="w"/> reaches, times <see cref="WeaponRange"/>.</summary>
		static double RangeKm(const cs::Ref<WeaponSpec>& w);

		/// <summary><paramref name="w"/>'s round speed, times <see cref="ProjectileSpeed"/> (0 stays 0: a beam).</summary>
		static double MuzzleSpeedKmS(const cs::Ref<WeaponSpec>& w);

		/// <summary><paramref name="w"/>'s missile speed, times <see cref="ProjectileSpeed"/>.</summary>
		static double MissileSpeedKmS(const cs::Ref<WeaponSpec>& w);
	};
}
