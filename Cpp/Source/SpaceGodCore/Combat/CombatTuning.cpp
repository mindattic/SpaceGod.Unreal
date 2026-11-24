// US-T539 (not a C# source).

#include "Combat/CombatTuning.h"

#include "Combat/CombatSpecs.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& CombatTuning::WeaponDamage()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "weaponDamage", "Weapon damage", 1, 0, 5, 0.05, "x",
			TuningScope::Simulation, "What every weapon's hit does to a ship, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::WeaponRange()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "weaponRange", "Weapon range", 1, 0.1, 5, 0.05, "x",
			TuningScope::Simulation, "How far every weapon reaches, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::RateOfFire()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "rateOfFire", "Rate of fire", 1, 0.1, 5, 0.05, "x",
			TuningScope::Simulation, "How fast every weapon reloads between shots, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::ProjectileSpeed()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "projectileSpeed", "Projectile speed", 1, 0.1, 5, 0.05, "x",
			TuningScope::Simulation, "How fast every gun's rounds and every missile fly, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::ShieldStrength()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "shieldStrength", "Shield strength", 1, 0.1, 5, 0.05, "x",
			TuningScope::Simulation, "How much every shield field holds off before it falls, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::ShieldRegen()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "shieldRegen", "Shield regeneration", 1, 0, 5, 0.05, "x",
			TuningScope::Simulation, "How fast every shield field regrows, times this.");
		return s;
	}

	const cs::Ref<RangeSetting>& CombatTuning::HullStrength()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("combat", "hullStrength", "Hull strength", 1, 0.1, 5, 0.05, "x",
			TuningScope::Simulation, "How much punishment every hull takes before it breaks, times this.");
		return s;
	}

	double CombatTuning::RangeKm(const cs::Ref<WeaponSpec>& w) { return w->RangeKm * WeaponRange()->Value; }

	double CombatTuning::MuzzleSpeedKmS(const cs::Ref<WeaponSpec>& w) { return w->MuzzleSpeedKmS * ProjectileSpeed()->Value; }

	double CombatTuning::MissileSpeedKmS(const cs::Ref<WeaponSpec>& w) { return w->MissileSpeedKmS * ProjectileSpeed()->Value; }
}
