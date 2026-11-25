#include "Combat/Dogfighting.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/FireCones.h"
#include "Combat/FlightModels.h"
#include "Combat/PilotTuning.h"
#include "Combat/Fighters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(FighterRole v)
	{
		switch (v)
		{
			case FighterRole::Interceptor: return "Interceptor";
			case FighterRole::HeavyFighter: return "HeavyFighter";
			case FighterRole::Bomber: return "Bomber";
			case FighterRole::SpaceSuperiority: return "SpaceSuperiority";
			case FighterRole::Recon: return "Recon";
			case FighterRole::AssaultShuttle: return "AssaultShuttle";
		}
		return "";
	}

	const char* ToString(WeaponGroup v)
	{
		switch (v)
		{
			case WeaponGroup::Guns: return "Guns";
			case WeaponGroup::Missiles: return "Missiles";
			case WeaponGroup::Torpedoes: return "Torpedoes";
			case WeaponGroup::Rockets: return "Rockets";
			case WeaponGroup::Hive: return "Hive";
		}
		return "";
	}

	const char* ToString(PowerSystem v)
	{
		switch (v)
		{
			case PowerSystem::Weapons: return "Weapons";
			case PowerSystem::Shields: return "Shields";
			case PowerSystem::Engines: return "Engines";
		}
		return "";
	}

	const char* ToString(WingOrder v)
	{
		switch (v)
		{
			case WingOrder::FormUp: return "FormUp";
			case WingOrder::AttackMyTarget: return "AttackMyTarget";
			case WingOrder::CoverMe: return "CoverMe";
			case WingOrder::EngageAtWill: return "EngageAtWill";
			case WingOrder::Disable: return "Disable";
			case WingOrder::Ignore: return "Ignore";
			case WingOrder::ReturnToCarrier: return "ReturnToCarrier";
		}
		return "";
	}

	// ---------------------------------------------------------------- FighterCraft

	/// The type's statics, set up by FighterCraft::TypeInit in C#'s textual order.
	struct FighterCraftStatics
	{
		cs::Dictionary<FighterRole, cs::Ref<FighterCraft>> Crafts;
		cs::List<FighterRole> SquadronMix;
		cs::Ref<FighterCraft> Interceptor, SpaceSuperiority, HeavyFighter, Bomber, Recon, AssaultShuttle;
		bool Started = false;

		static FighterCraftStatics& Get()
		{
			static FighterCraftStatics s;
			return s;
		}
	};

	namespace
	{
		cs::Ref<SubsystemDef> Def(SubsystemKind kind, double x, double y, double z, bool facesAft = false, cs::Nullable<DVec3> coneAxis = std::nullopt, cs::Nullable<double> coneHalfDeg = std::nullopt)
		{
			return cs::New<SubsystemDef>(kind, x, y, z, facesAft, coneAxis, coneHalfDeg);
		}

		// new SubsystemDef(...) { Weapon = WeaponSpec.DogfightMissile, MaxHp = 30 }
		cs::Ref<SubsystemDef> MissileDef(double x, double y, double z)
		{
			auto d = Def(SubsystemKind::MissileLauncher, x, y, z, false, FireCones::Bow, 20);
			d->Weapon = WeaponSpec::DogfightMissile();
			d->MaxHp = 30;
			return d;
		}

		cs::Ref<CombatSpec> NewSpec(double hull, double shield, double shieldRegen, double armor, double maxSpeed, double accel, double turnRateDegS, cs::List<cs::Ref<SubsystemDef>> layout)
		{
			auto s = cs::New<CombatSpec>();
			s->Hull = hull;
			s->Shield = shield;
			s->ShieldRegen = shieldRegen;
			s->Armor = armor;
			s->MaxSpeed = maxSpeed;
			s->Accel = accel;
			s->TurnRateDegS = turnRateDegS;
			s->Layout = std::move(layout);
			return s;
		}
	}

	void FighterCraft::TypeInit()
	{
		auto& st = FighterCraftStatics::Get();
		if (st.Started) return;
		st.Started = true;

		// US-T180 (two nests of hornets): every combat craft below flies 1.4x the speed and thrust and 1.25x the turn it had
		// (the shuttle excepted), the same for each so the interceptor is still the quickest and the bomber the slowest; and
		// 2.5x the hull and shield (1.5x the regeneration), so a squadron lives through more than one merge: at the old plate
		// a pair of the opening salvoes and a few seconds of flak wiped whole squadrons out within a minute, and the nest
		// emptied.
		st.SquadronMix = { FighterRole::Interceptor, FighterRole::SpaceSuperiority, FighterRole::Bomber, FighterRole::HeavyFighter };

		st.Interceptor = Add(FighterRole::Interceptor, "interceptor", ShipClass::Interceptor, 0.016, 0.012, 500, 1.6, 6,
			NewSpec(/*Hull*/ 175, /*Shield*/ 75, /*ShieldRegen*/ 3, /*Armor*/ 0, /*MaxSpeed*/ 0.49, /*Accel*/ 0.35, /*TurnRateDegS*/ 150,
			{
				Def(SubsystemKind::FighterGuns, 0.2, 0, 0.4, false, FireCones::Bow, 14),
				Def(SubsystemKind::FighterGuns, -0.2, 0, 0.4, false, FireCones::Bow, 14),
				MissileDef(0, -0.1, 0.1),
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}), /*huntsFightersKm:*/ 14, /*screensBombers:*/ true);

		/// <summary>The balanced workhorse (U.SPC-T484): a little slower and wider in the turn than the interceptor, tougher,
		/// and with twice its missiles, as ready to strafe a hull as to fight a craft.</summary>
		st.SpaceSuperiority = Add(FighterRole::SpaceSuperiority, "fighter", ShipClass::Fighter, 0.018, 0.018, 325, 1.55, 5.5,
			NewSpec(250, 110, 3.75, 0.5, 0.45, 0.31, 125,
			{
				Def(SubsystemKind::FighterGuns, 0.22, 0, 0.4, false, FireCones::Bow, 14),
				Def(SubsystemKind::FighterGuns, -0.22, 0, 0.4, false, FireCones::Bow, 14),
				MissileDef(0.12, -0.1, 0.1),
				MissileDef(-0.12, -0.1, 0.1),
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}));

		st.HeavyFighter = Add(FighterRole::HeavyFighter, "heavy fighter", ShipClass::Gunship, 0.02, 0.025, 200, 1.5, 5,
			NewSpec(350, 150, 4.5, 1, 0.42, 0.28, 112,
			{
				Def(SubsystemKind::FighterGuns, 0.25, 0, 0.4, false, FireCones::Bow, 14),
				Def(SubsystemKind::FighterGuns, -0.25, 0, 0.4, false, FireCones::Bow, 14),
				MissileDef(0.15, -0.1, 0.1),
				MissileDef(-0.15, -0.1, 0.1),
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}));

		st.Bomber = Add(FighterRole::Bomber, "bomber", ShipClass::Bomber, 0.024, 0.03, 150, 1.4, 5,
			NewSpec(325, 125, 3, 1, 0.36, 0.21, 100,
			{
				Def(SubsystemKind::FighterGuns, 0, 0.05, 0.4, false, FireCones::Bow, 14),
				Def(SubsystemKind::Torpedo, 0, -0.1, 0),
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}));

		/// <summary>The recon / EW fighter (U.SPC-T485): an interceptor's pace, a light gun for its own defence, flares, and
		/// the rest of it sensors and jammers (<see cref="ElectronicWarfare"/>). It never hunts fighters: it holds off its
		/// mark, jamming and painting.</summary>
		st.Recon = Add(FighterRole::Recon, "recon fighter", ShipClass::Recon, 0.017, 0.014, 450, 1.6, 7,
			NewSpec(160, 100, 3.75, 0, 0.48, 0.34, 138,
			{
				Def(SubsystemKind::FighterGuns, 0, -0.05, 0.4, false, FireCones::Bow, 14),
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}), /*huntsFightersKm:*/ 0);

		/// <summary>The boarding shuttle (U.SPC-T486): slow, plated and shielded to get its team across, no guns, flares to
		/// shake a missile off. Its pilots fly it to a prize, never into a fight (<see cref="BoardingShuttles"/>).</summary>
		st.AssaultShuttle = Add(FighterRole::AssaultShuttle, "assault shuttle", ShipClass::AssaultShuttle, 0.03, 0.04, 90, 1.3, 4,
			NewSpec(200, 70, 2, 2, 0.2, 0.1, 60,
			{
				Def(SubsystemKind::Countermeasures, 0, 0, -0.4, /*facesAft:*/ true),
				Def(SubsystemKind::ShieldGenerator, 0, 0, -0.3),
			}), /*huntsFightersKm:*/ 0);
	}

	FighterCraft::FighterCraft(FighterRole role, cs::String name, ShipClass cls, cs::Ref<CombatSpec> spec, double lengthKm, double boostMultiplier, double boostSeconds)
	{
		Role = role;
		Name = std::move(name);
		Class = cls;
		Spec = std::move(spec);
		LengthKm = lengthKm;
		BoostMultiplier = boostMultiplier;
		BoostSeconds = boostSeconds;
	}

	FlightFamily FighterCraft::Family() const { return FlightModelRegistry::Default()->FamilyOf(Class); }

	PilotTuning FighterCraft::Tuning(double engines) const
	{
		return PilotTuning(Spec->MaxSpeed * engines, Spec->TurnRateDegS, BoostMultiplier, BoostSeconds, 1, 0);
	}

	cs::Ref<IFlightModel> FighterCraft::Model() const { return FlightModelRegistry::Default()->For(Class); }

	cs::List<FighterRole>& FighterCraft::SquadronMix()
	{
		TypeInit();
		return FighterCraftStatics::Get().SquadronMix;
	}

	FighterRole FighterCraft::RoleOfSquadron(int squadron)
	{
		auto& mix = SquadronMix();
		return mix[((squadron % mix.Count()) + mix.Count()) % mix.Count()];
	}

	void FighterCraft::Register(const cs::Ref<FighterCraft>& craft)
	{
		TypeInit();
		if (!craft) throw cs::ArgumentNullException("craft");
		FighterCraftStatics::Get().Crafts[craft->Role] = craft;
	}

	cs::Ref<FighterCraft> FighterCraft::Of(FighterRole role)
	{
		TypeInit();
		return FighterCraftStatics::Get().Crafts.At(role);
	}

	const cs::Ref<FighterCraft>& FighterCraft::Interceptor() { TypeInit(); return FighterCraftStatics::Get().Interceptor; }
	const cs::Ref<FighterCraft>& FighterCraft::SpaceSuperiority() { TypeInit(); return FighterCraftStatics::Get().SpaceSuperiority; }
	const cs::Ref<FighterCraft>& FighterCraft::HeavyFighter() { TypeInit(); return FighterCraftStatics::Get().HeavyFighter; }
	const cs::Ref<FighterCraft>& FighterCraft::Bomber() { TypeInit(); return FighterCraftStatics::Get().Bomber; }
	const cs::Ref<FighterCraft>& FighterCraft::Recon() { TypeInit(); return FighterCraftStatics::Get().Recon; }
	const cs::Ref<FighterCraft>& FighterCraft::AssaultShuttle() { TypeInit(); return FighterCraftStatics::Get().AssaultShuttle; }

	cs::Ref<FighterCraft> FighterCraft::Add(FighterRole role, cs::String name, ShipClass cls, double lengthKm, double massKt, double angularAccel, double boost, double boostSeconds, cs::Ref<CombatSpec> spec,
		double huntsFightersKm, bool screensBombers)
	{
		spec->DesignName = name;
		spec->LengthKm = lengthKm;
		spec->MassKt = massKt;
		spec->MainThrustMN = spec->Accel * massKt * 1000;
		spec->RetroThrustMN = spec->MainThrustMN * 0.75;
		spec->LateralThrustMN = spec->VerticalThrustMN = spec->MainThrustMN * 0.5;
		spec->BoostSpeedMultiplier = boost;
		spec->BoostDurationS = boostSeconds;
		spec->PitchYawTorqueMNKm = angularAccel * spec->PitchYawInertia() * 1000;
		spec->RollTorqueMNKm = angularAccel * 2 * spec->RollInertia() * 1000;
		auto craft = cs::New<FighterCraft>(role, name, cls, spec, lengthKm, boost, boostSeconds);
		craft->HuntsFightersKm = huntsFightersKm;
		craft->ScreensBombers = screensBombers;
		Register(craft);
		return craft;
	}

	cs::Ref<FighterCraft> FighterCraft::OfClass(ShipClass cls)
	{
		TypeInit();
		for (const auto& c : FighterCraftStatics::Get().Crafts.Values())
			if (c->Class == cls) return c;
		return nullptr;
	}

	// ---------------------------------------------------------------- weapon groups

	cs::Dictionary<SubsystemKind, WeaponGroup>& WeaponGroups::ByKind()
	{
		static cs::Dictionary<SubsystemKind, WeaponGroup> byKind = []
		{
			cs::Dictionary<SubsystemKind, WeaponGroup> d;
			d[SubsystemKind::FighterGuns] = WeaponGroup::Guns;
			d[SubsystemKind::Autocannon] = WeaponGroup::Guns;
			d[SubsystemKind::LightRailgun] = WeaponGroup::Guns;
			d[SubsystemKind::MissileLauncher] = WeaponGroup::Missiles;
			d[SubsystemKind::Torpedo] = WeaponGroup::Torpedoes;
			// The full arsenal (US-T101).
			d[SubsystemKind::VulcanCannon] = WeaponGroup::Guns;
			d[SubsystemKind::RocketPod] = WeaponGroup::Rockets;
			d[SubsystemKind::HiveLauncher] = WeaponGroup::Hive;
			return d;
		}();
		return byKind;
	}

	void WeaponGroups::Assign(SubsystemKind kind, WeaponGroup group) { ByKind()[kind] = group; }

	cs::Nullable<WeaponGroup> WeaponGroups::Of(SubsystemKind kind)
	{
		WeaponGroup g{};
		if (ByKind().TryGetValue(kind, g)) return g;
		// A save from before the full arsenal (US-T101) restores the table without its mounts: they keep their groups.
		switch (kind)
		{
			case SubsystemKind::VulcanCannon: return WeaponGroup::Guns;
			case SubsystemKind::RocketPod: return WeaponGroup::Rockets;
			case SubsystemKind::HiveLauncher: return WeaponGroup::Hive;
			default: return std::nullopt;
		}
	}

	cs::List<WeaponGroup> WeaponGroups::Secondaries(const cs::Ref<BattleUnit>& u)
	{
		cs::List<WeaponGroup> list;
		// Enum.GetValues(typeof(WeaponGroup)), in value order.
		for (WeaponGroup g : { WeaponGroup::Guns, WeaponGroup::Missiles, WeaponGroup::Torpedoes, WeaponGroup::Rockets, WeaponGroup::Hive })
		{
			if (g == WeaponGroup::Guns) continue;
			for (const auto& s : u->Combat->Subsystems)
				if (!s->Destroyed() && Of(s->Kind()) == g)
				{
					list.Add(g);
					break;
				}
		}
		return list;
	}

	int WeaponGroups::Ammo(const cs::Ref<BattleUnit>& u, WeaponGroup g)
	{
		if (g == WeaponGroup::Guns) return -1;
		int n = 0;
		for (const auto& s : u->Combat->Subsystems)
			if (!s->Destroyed() && Of(s->Kind()) == g) n += s->Ammo;
		return n;
	}

	cs::String WeaponGroups::Label(WeaponGroup g)
	{
		// The homing missiles named as such, beside the dumb-fire rockets (US-T101).
		switch (g)
		{
			case WeaponGroup::Guns: return "guns";
			case WeaponGroup::Missiles: return "homing missiles";
			case WeaponGroup::Rockets: return "dumb-fire rockets";
			case WeaponGroup::Hive: return "hive missiles";
			default: return "torpedoes";
		}
	}

	// ---------------------------------------------------------------- energy

	bool PowerDistribution::Shift(PowerSystem s)
	{
		int i = static_cast<int>(s);
		if (pips[i] >= MaxPips) return false;
		int from = -1;
		for (int k = 0; k < pips.Count(); k++)
			if (k != i && pips[k] > 0 && (from < 0 || pips[k] > pips[from])) from = k;
		if (from < 0) return false;
		pips[from]--;
		pips[i]++;
		revision++;
		return true;
	}

	void PowerDistribution::Balance()
	{
		int before = Code();
		for (int k = 0; k < pips.Count(); k++) pips[k] = EvenPips;
		Touched(before);
	}

	bool PowerDistribution::Set(int weapons, int shields, int engines)
	{
		if (weapons + shields + engines != TotalPips || Max(weapons, Max(shields, engines)) > MaxPips || Min(weapons, Min(shields, engines)) < 0)
			return false;
		int before = Code();
		pips[0] = weapons;
		pips[1] = shields;
		pips[2] = engines;
		Touched(before);
		return true;
	}

	void PowerDistribution::AllTo(PowerSystem s)
	{
		int before = Code();
		for (int k = 0; k < pips.Count(); k++) pips[k] = 1;
		pips[static_cast<int>(s)] = MaxPips;
		Touched(before);
	}

	bool PowerDistribution::SetCode(int code)
	{
		if (code < 0 || code >= 125) return false;
		return Set(code / 25, code / 5 % 5, code % 5);
	}

	cs::String PowerDistribution::PresetName() const
	{
		if (IsEven()) return "BALANCED";
		for (int k = 0; k < pips.Count(); k++)
			if (pips[k] == MaxPips)
			{
				bool rest = true;
				for (int j = 0; j < pips.Count(); j++) if (j != k && pips[j] != 1) rest = false;
				if (rest) return cs::Cat("ALL ", cs::Strings::ToUpperInvariant(Core::ToString(static_cast<PowerSystem>(k))));
			}
		return cs::String();
	}

	double PowerDistribution::FactorOf(const cs::Ref<BattleUnit>& u, PowerSystem s) { return u->Power ? u->Power->Factor(s) : 1; }

	cs::String PowerDistribution::ToString() const { return cs::Cat("W", pips[0], " S", pips[1], " E", pips[2]); }

	const cs::Ref<RangeSetting>& BoostPool::ChargeSeconds()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.boost-charge", SettingGroups::Dogfighting,
			"Boost charge", 5, 2, 30, 0.5,
			"How long a full boost pool lasts a small craft under your hands, at an even share of power.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "boost afterburner energy pool dogfight");
		return setting;
	}

	const cs::Ref<RangeSetting>& BoostPool::RunnerChargeSeconds()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.boost-charge-runner", SettingGroups::Dogfighting,
			"Boost charge (runner)", 10, 2, 60, 0.5,
			"How long a full boost pool lasts a blockade runner (the Courier) under your hands, at an even share of power.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "boost afterburner energy pool runner courier");
		return setting;
	}

	const cs::Ref<RangeSetting>& BoostPool::RefillSeconds()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.boost-refill", SettingGroups::Dogfighting,
			"Boost refill", 6, 1, 60, 0.5,
			"How long an empty boost pool takes to refill at an even share of power; power to the engines refills it faster, power taken off them slower.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "boost afterburner energy pool recharge pips engines");
		return setting;
	}

	const cs::Ref<RangeSetting>& BoostPool::TopSpeedSway()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.engine-pips-speed", SettingGroups::Dogfighting,
			"Engine power on top speed", 0.4, 0, 1, 0.05,
			"How much of the engines' power swing reaches top speed under your hands: at 40% the most power is +20% speed and none is -20%; thrust and boost refill always take the full swing.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "power pips engines speed distribution");
		return setting;
	}

	double BoostPool::PoolSeconds(FlightFamily family)
	{
		return family == FlightFamily::Runner ? RunnerChargeSeconds()->Value : ChargeSeconds()->Value;
	}

	double BoostPool::RefillSecondsAt(double engines) { return RefillSeconds()->Value / Max(0.1, engines); }

	double BoostPool::TopSpeedFactor(double engines) { return 1 + (engines - 1) * TopSpeedSway()->Value; }

	cs::Ref<RangeSetting> AssistThrottle::Seconds(const char* key, const char* label, double seconds, const char* what)
	{
		return cs::New<RangeSetting>(key, SettingGroups::Dogfighting, label, seconds, 0.5, 20, 0.1,
			cs::Cat("With flight assist on, how long it takes under your hands to ", what, "; the flight computer pushes harder than the drive alone when it must. Crews fly on the hull's own thrust."),
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "flight model handling throttle acceleration assist");
	}

	cs::Ref<RangeSetting> AssistThrottle::HeavySeconds(const char* key, const char* label, double seconds, const char* what)
	{
		return cs::New<RangeSetting>(key, SettingGroups::Dogfighting, label, seconds, 0.5, 20, 0.1,
			cs::Cat("With flight assist on, how long a heavy fighter takes under your hands to ", what, "; its flight computer eases no faster than this, so it feels its weight (the boost still kicks). Crews fly on the hull's own ease."),
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "flight model handling throttle acceleration assist heavy fighter");
	}

	const cs::Ref<RangeSetting>& AssistThrottle::ToTop(FlightFamily family)
	{
		static const cs::Ref<RangeSetting> light = Seconds("flight.lightFighter.toTop", "Light fighters: 0 to top speed", 2.5, "reach top speed from rest");
		static const cs::Ref<RangeSetting> interceptor = Seconds("flight.interceptor.toTop", "Interceptors: 0 to top speed", 2.2, "reach top speed from rest");
		static const cs::Ref<RangeSetting> runner = Seconds("flight.runner.toTop", "Couriers: 0 to top speed", 3, "reach top speed from rest");
		static const cs::Ref<RangeSetting> bomber = Seconds("flight.bomber.toTop", "Bombers: 0 to top speed", 5, "reach top speed from rest");
		static const cs::Ref<RangeSetting> heavy = HeavySeconds("flight.heavyFighter.toTop", "Heavy fighters: 0 to top speed", 3.5, "reach top speed from rest");
		static const cs::Ref<RangeSetting> none;
		switch (family)
		{
			case FlightFamily::LightFighter: return light;
			case FlightFamily::Interceptor: return interceptor;
			case FlightFamily::Runner: return runner;
			case FlightFamily::HeavyFighter: return heavy;
			case FlightFamily::Bomber: return bomber;
			default: return none;
		}
	}

	const cs::Ref<RangeSetting>& AssistThrottle::ToStop(FlightFamily family)
	{
		static const cs::Ref<RangeSetting> light = Seconds("flight.lightFighter.toStop", "Light fighters: top speed to stop", 2, "stop from top speed");
		static const cs::Ref<RangeSetting> interceptor = Seconds("flight.interceptor.toStop", "Interceptors: top speed to stop", 1.8, "stop from top speed");
		static const cs::Ref<RangeSetting> runner = Seconds("flight.runner.toStop", "Couriers: top speed to stop", 3, "stop from top speed");
		static const cs::Ref<RangeSetting> bomber = Seconds("flight.bomber.toStop", "Bombers: top speed to stop", 4.5, "stop from top speed");
		static const cs::Ref<RangeSetting> heavy = HeavySeconds("flight.heavyFighter.toStop", "Heavy fighters: top speed to stop", 3, "stop from top speed");
		static const cs::Ref<RangeSetting> none;
		switch (family)
		{
			case FlightFamily::LightFighter: return light;
			case FlightFamily::Interceptor: return interceptor;
			case FlightFamily::Runner: return runner;
			case FlightFamily::HeavyFighter: return heavy;
			case FlightFamily::Bomber: return bomber;
			default: return none;
		}
	}

	const cs::Ref<RangeSetting>& AssistThrottle::LightFighterRoll()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("flight.lightFighter.roll", SettingGroups::Dogfighting,
			"Light fighters: roll rate", 300, 90, 540, 5,
			"With flight assist on, how fast a light fighter rolls under your hands (its catalog roll if that is faster), so a roll-and-pull is one quick move. Crews roll at the catalog's rate.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " deg/s"); }), "flight model handling light fighter roll");
		return setting;
	}

	double AssistThrottle::ToTopS(FlightFamily family)
	{
		auto& s = ToTop(family);
		return s != nullptr ? s->Value : 0;
	}

	double AssistThrottle::ToStopS(FlightFamily family)
	{
		auto& s = ToStop(family);
		return s != nullptr ? s->Value : 0;
	}

	double AssistThrottle::RollDegS(FlightFamily family) { return family == FlightFamily::LightFighter ? LightFighterRoll()->Value : 0; }

	cs::Ref<RangeSetting> AssistThrottle::SpinUpSeconds(const char* key, const char* label, double seconds)
	{
		return cs::New<RangeSetting>(key, SettingGroups::Dogfighting, label, seconds, 0.05, 2, 0.01,
			"With flight assist off, how long the attitude thrusters take under your hands to reach the top turn and roll rate; the turn still stops when you let go of the stick, and only the thrust is left Newtonian. Crews turn on the hull's own torque.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), " s"); }), "flight model handling assist off newtonian spin-up rotation");
	}

	const cs::Ref<RangeSetting>& AssistThrottle::SpinUp(FlightFamily family)
	{
		static const cs::Ref<RangeSetting> light = SpinUpSeconds("flight.lightFighter.assistOffSpinUp", "Light fighters: assist-off spin-up", 0.08);
		static const cs::Ref<RangeSetting> interceptor = SpinUpSeconds("flight.interceptor.assistOffSpinUp", "Interceptors: assist-off spin-up", 0.08);
		static const cs::Ref<RangeSetting> runner = SpinUpSeconds("flight.runner.assistOffSpinUp", "Couriers: assist-off spin-up", 0.15);
		static const cs::Ref<RangeSetting> heavy = SpinUpSeconds("flight.heavyFighter.assistOffSpinUp", "Heavy fighters: assist-off spin-up", 0.15);
		static const cs::Ref<RangeSetting> bomber = SpinUpSeconds("flight.bomber.assistOffSpinUp", "Bombers: assist-off spin-up", 0.25);
		static const cs::Ref<RangeSetting> hauler = SpinUpSeconds("flight.hauler.assistOffSpinUp", "Haulers: assist-off spin-up", 0.4);
		static const cs::Ref<RangeSetting> none;
		switch (family)
		{
			case FlightFamily::LightFighter: return light;
			case FlightFamily::Interceptor: return interceptor;
			case FlightFamily::Runner: return runner;
			case FlightFamily::HeavyFighter: return heavy;
			case FlightFamily::Bomber: return bomber;
			case FlightFamily::Hauler: return hauler;
			default: return none;
		}
	}

	double AssistThrottle::SpinUpS(FlightFamily family)
	{
		auto& s = SpinUp(family);
		return s != nullptr ? s->Value : 0;
	}

	const cs::Ref<RangeSetting>& AssistThrottle::BoostKick()
	{
		static const cs::Ref<RangeSetting> setting = Seconds("flight.light.boostKick", "Light craft: boost kick", 1,
			"reach most of boost speed once the boost fires (light fighters, interceptors and couriers; the heavy fighter has its own kick)");
		return setting;
	}

	const cs::Ref<RangeSetting>& AssistThrottle::BoostKickShare()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("flight.light.boostKickShare", SettingGroups::Dogfighting,
			"Light craft: boost kick share", 0.9, 0.5, 1, 0.05,
			"The share of boost speed the boost kick reaches in its time.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "flight model handling boost kick");
		return setting;
	}

	const cs::Ref<RangeSetting>& AssistThrottle::BoostBleed()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("flight.light.boostBleed", SettingGroups::Dogfighting,
			"Light craft: boost bleed", 2, 0.5, 20, 0.1,
			"With flight assist on, how long the boost's extra speed takes to bleed back to top speed once you let go of boost, so a boosted run carries through (light fighters, interceptors and couriers). Pulling the throttle back still brakes at full push. Crews brake at once.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "flight model handling boost bleed");
		return setting;
	}

	bool AssistThrottle::LightFamily(FlightFamily family)
	{
		return family == FlightFamily::LightFighter || family == FlightFamily::Interceptor || family == FlightFamily::Runner;
	}

	double AssistThrottle::BoostKickS(FlightFamily family) { return LightFamily(family) ? BoostKick()->Value : 0; }

	double AssistThrottle::BoostBleedS(FlightFamily family) { return LightFamily(family) ? BoostBleed()->Value : 0; }

	// ---------------------------------------------------------------- countermeasures

	const cs::Ref<RangeSetting>& Countermeasures::Effectiveness()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.countermeasures", SettingGroups::Dogfighting,
			"Countermeasure effectiveness", 0.65, 0, 1, 0.05,
			"The chance a flare-and-chaff cloud draws off each missile homing on the craft that threw it (a torpedo is fooled less than half as often).",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "flares chaff decoy missile dogfight");
		return setting;
	}

	const cs::Ref<ToggleSetting>& Countermeasures::AiSpoofLocks()
	{
		static const cs::Ref<ToggleSetting> setting = cs::New<ToggleSetting>("dogfight.aiSpoofLocks", SettingGroups::Dogfighting,
			"AI pilots spoof missile locks", true,
			"An enemy pilot whose tail the player's seeker is locking up throws flares and chaff to break the lock, not only at a missile already inbound.",
			"flares chaff decoy missile lock seek dogfight");
		return setting;
	}

	double Countermeasures::SeduceChance(MissileKind kind)
	{
		// A hive round and its seekers are fooled as a missile is; a dumb-fire rocket has no seeker to fool (US-T101).
		return kind == MissileKind::Missile || kind == MissileKind::Interceptor || kind == MissileKind::Hive || kind == MissileKind::HiveMicro ? Effectiveness()->Value
			: kind == MissileKind::Torpedo || kind == MissileKind::Antimatter ? Effectiveness()->Value * TorpedoShare : 0;
	}

	// ---------------------------------------------------------------- capital-ship weak points

	WeakPointSpec::WeakPointSpec(SubsystemKind kind, cs::String label, DVec3 localAxis, double coneDeg, double multiplier, int priority)
	{
		Kind = kind;
		Label = std::move(label);
		LocalAxis = localAxis.Normalized();
		ConeDeg = coneDeg;
		Multiplier = multiplier;
		Priority = priority;
	}

	cs::Dictionary<SubsystemKind, cs::Ref<WeakPointSpec>>& WeakPoints::Specs()
	{
		static cs::Dictionary<SubsystemKind, cs::Ref<WeakPointSpec>> specs;
		// The static constructor: runs once, after the field initialiser, before the first use.
		static bool registered = false;
		if (!registered)
		{
			registered = true;
			Register(cs::New<WeakPointSpec>(SubsystemKind::Reactor, "reactor vent", DVec3(0, -0.6, -0.8), 55, 4, 3));
			Register(cs::New<WeakPointSpec>(SubsystemKind::ShieldGenerator, "shield emitter", DVec3(0, 1, 0), 60, 3, 2));
			Register(cs::New<WeakPointSpec>(SubsystemKind::Engine, "engine bells", DVec3(0, 0, -1), 50, 2.5, 1));
		}
		return specs;
	}

	const cs::Ref<RangeSetting>& WeakPoints::Damage()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.weakPointDamage", SettingGroups::Dogfighting,
			"Weak point damage", 1, 0, 3, 0.25,
			"How much harder a small craft hits a capital ship's exposed reactor, engines and shield emitters: 0 takes the bonus away, 1 is as designed.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }), "reactor engines shield attack run bomber dogfight");
		return setting;
	}

	const cs::Ref<ToggleSetting>& WeakPoints::AiGoesForThem()
	{
		static const cs::Ref<ToggleSetting> setting = cs::New<ToggleSetting>("dogfight.aiWeakPoints", SettingGroups::Dogfighting,
			"AI bombers go for weak points", true,
			"Carrier bombers fly their runs onto a capital ship's exposed reactor, engines and shield emitters, as the player can.",
			"bomber torpedo reactor attack run dogfight");
		return setting;
	}

	void WeakPoints::Register(const cs::Ref<WeakPointSpec>& spec) { Specs()[spec->Kind] = spec; }

	cs::Ref<WeakPointSpec> WeakPoints::SpecOf(SubsystemKind kind)
	{
		cs::Ref<WeakPointSpec> s;
		return Specs().TryGetValue(kind, s) ? s : nullptr;
	}

	bool WeakPoints::IsCapital(const cs::Ref<BattleUnit>& u) { return u != nullptr && u->Kind != UnitKind::Fighter && u->LengthKm >= MinCapitalKm; }

	bool WeakPoints::IsSmallCraft(const cs::Ref<BattleUnit>& u) { return u != nullptr && (u->Kind == UnitKind::Fighter || u->LengthKm <= MaxSmallCraftKm); }

	DVec3 WeakPoints::Axis(const cs::Ref<BattleUnit>& target, const cs::Ref<WeakPointSpec>& spec)
	{
		DVec3 right, up;
		target->Basis(right, up);
		auto a = spec->LocalAxis;
		return (right * a.X + up * a.Y + target->Forward * a.Z).Normalized();
	}

	bool WeakPoints::Exposed(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 incoming)
	{
		if (sub == nullptr || sub->Destroyed() || !IsCapital(target) || incoming.LengthSquared() < 1e-18) return false;
		auto spec = SpecOf(sub->Kind());
		if (spec == nullptr) return false;
		return DVec3::Dot(-incoming.Normalized(), Axis(target, spec)) >= cs::Math::Cos(spec->ConeDeg * Astro::Deg2Rad);
	}

	bool WeakPoints::ExposedFrom(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 from)
	{
		return sub != nullptr && Exposed(target, sub, target->PointOf(sub->Def) - from);
	}

	double WeakPoints::Factor(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 incoming)
	{
		if (!Exposed(target, sub, incoming)) return 1;
		return Max(1.0, 1 + (SpecOf(sub->Kind())->Multiplier - 1) * Damage()->Value);
	}

	cs::List<cs::Ref<Subsystem>> WeakPoints::On(const cs::Ref<BattleUnit>& target)
	{
		cs::List<cs::Ref<Subsystem>> list;
		if (!IsCapital(target)) return list;
		for (const auto& s : target->Combat->Subsystems)
			if (!s->Destroyed() && SpecOf(s->Kind()) != nullptr) list.Add(s);
		list.Sort([](const cs::Ref<Subsystem>& a, const cs::Ref<Subsystem>& b)
		{
			int p = cs::CompareTo(SpecOf(b->Kind())->Priority, SpecOf(a->Kind())->Priority);
			return p != 0 ? p : cs::CompareTo(a->Index, b->Index);
		});
		return list;
	}

	cs::Ref<Subsystem> WeakPoints::Preferred(const cs::Ref<BattleUnit>& target)
	{
		cs::Ref<Subsystem> fallback = nullptr;
		for (const auto& s : On(target))
		{
			if (s->Kind() == SubsystemKind::ShieldGenerator && s->Shield <= 0)
			{
				if (!fallback) fallback = s;
				continue;
			}
			return s;
		}
		return fallback;
	}

	cs::Ref<Subsystem> WeakPoints::BestFrom(const cs::Ref<BattleUnit>& target, DVec3 from)
	{
		for (const auto& s : On(target))
			if (ExposedFrom(target, s, from)) return s;
		return nullptr;
	}

	cs::Ref<Subsystem> WeakPoints::Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step)
	{
		auto list = On(target);
		if (list.Count() == 0) return nullptr;
		int i = current == nullptr ? -1 : list.IndexOf(current);
		if (i < 0) return step >= 0 ? list[0] : list[list.Count() - 1];
		return list[((i + step) % list.Count() + list.Count()) % list.Count()];
	}

	cs::String WeakPoints::Label(const cs::Ref<Subsystem>& s)
	{
		if (s == nullptr) return cs::String();
		auto spec = SpecOf(s->Kind());
		return spec ? spec->Label : cs::Strings::ToLowerInvariant(SubsystemDef::Name(s->Kind()));
	}

	// ---------------------------------------------------------------- strike torpedoes

	const cs::Ref<RangeSetting>& StrikeTorpedoes::BareHullDamage()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.torpedoBareHull", SettingGroups::Dogfighting,
			"Torpedoes on a bare hull", 3, 1, 6, 0.25,
			"How much harder a bomber's torpedo hits a capital ship whose shield on that side is already down: bombers knock the shields down, then torpedo. 1 takes the bonus away.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }), "bomber torpedo shields capital dogfight");
		return setting;
	}

	bool StrikeTorpedoes::IsStrike(const cs::Ref<Missile>& m) { return m != nullptr && m->Kind == MissileKind::Torpedo && m->SmallCraft; }

	double StrikeTorpedoes::Factor(const cs::Ref<BattleUnit>& target, bool shieldDown)
	{
		return shieldDown && WeakPoints::IsCapital(target) ? Max(1.0, BareHullDamage()->Value) : 1;
	}

	bool StrikeTorpedoes::ShieldDownToward(const cs::Ref<BattleUnit>& target, DVec3 from)
	{
		auto g = target->Combat->EmitterFor(target->ShieldStrike(from));
		return g == nullptr || g->Shield <= 0;
	}

	// ---------------------------------------------------------------- wingmen

	cs::List<cs::Ref<BattleUnit>> Wing::Members(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead)
	{
		cs::List<cs::Ref<BattleUnit>> list;
		auto f = lead ? lead->Fighter : nullptr;
		if (f == nullptr) return list;
		for (const auto& o : b->Units)
			if (o != lead && o->Present() && o->Kind == UnitKind::Fighter && o->Fighter != nullptr && o->Fighter->Carrier == f->Carrier && o->Fighter->Squadron == f->Squadron)
				list.Add(o);
		return list;
	}

	int Wing::Command(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead, WingOrder order)
	{
		if ((lead ? lead->Fighter : nullptr) == nullptr) return 0;
		auto members = Members(b, lead);
		lead->Fighter->Lead = nullptr;
		auto marked = order == WingOrder::Disable || order == WingOrder::Ignore ? lead->Target : nullptr;
		for (const auto& m : members)
		{
			auto f = m->Fighter;
			f->Lead = lead;
			f->OrderTarget = marked;
			if (f->Order != order && f->Phase != FighterPhase::Return)
			{
				// A fresh start on the new order, not the tail of an old run.
				f->Phase = FighterPhase::Approach;
				f->PhaseTime = 0;
				f->RunEntry = DVec3::Zero;
			}
			f->Order = order;
		}
		lead->Fighter->Order = order;
		return members.Count();
	}

	cs::Ref<BattleUnit> Wing::Threat(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		for (const auto& o : b->Units)
		{
			if (!b->Hostile(lead, o) || o->Surrendering || o->Captured || !o->Combat->Armed()) continue;
			auto to = o->Pos - lead->Pos;
			double d = to.Length();
			if (d > CoverKm || d < 1e-9) continue;
			// On its tail counts double.
			double score = DVec3::Dot(lead->Forward, to / d) < -0.3 ? d * 0.5 : d;
			if (o->Kind != UnitKind::Fighter) score += CoverKm;
			if (score < bestScore)
			{
				bestScore = score;
				best = o;
			}
		}
		return best;
	}

	bool Wing::HoldsFire(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		auto f = u != nullptr ? u->Fighter : nullptr;
		if (f == nullptr || f->Lead == nullptr || o == nullptr || f->OrderTarget != o) return false;
		return f->Order == WingOrder::Ignore || (f->Order == WingOrder::Disable && o->Combat->Disabled());
	}

	cs::String Wing::Label(WingOrder o)
	{
		switch (o)
		{
			case WingOrder::AttackMyTarget: return "attack my target";
			case WingOrder::CoverMe: return "cover me";
			case WingOrder::EngageAtWill: return "engage at will";
			case WingOrder::Disable: return "disable my target";
			case WingOrder::Ignore: return "ignore my target";
			case WingOrder::ReturnToCarrier: return "return to carrier";
			default: return "form up";
		}
	}
}

// ObjectGraph (docs/PERSISTENCE.md, "Statics"): C#'s static FighterCraft.Crafts lives in this file's FighterCraftStatics,
// so its save declaration is here (the others are in Persistence/Registrations/Statics.cpp). A public accessor first, so
// the type's statics are set up as C#'s type initialiser sets them.
#include "Persistence/ObjectGraph.h"

namespace SpaceGod::Core
{
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "Crafts", (static_cast<void>(FighterCraft::AssaultShuttle()), FighterCraftStatics::Get().Crafts))
}
