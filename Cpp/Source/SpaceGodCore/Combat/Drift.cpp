#include "Combat/Drift.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/FlightModels.h"
#include "Settings/GameSetting.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	static cs::Ref<RangeSetting> DriftSeconds(const char* key, const char* label, double seconds, double min, double max, double step, const char* description)
	{
		return cs::New<RangeSetting>(key, SettingGroups::Dogfighting, label, seconds, min, max, step, description,
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), " s"); }), "flight model handling drift boost-out");
	}

	const cs::Ref<RangeSetting>& Drift::SpinUp()
	{
		static const cs::Ref<RangeSetting> setting = DriftSeconds("flight.drift.spinUp", "Drift: spin-up", 0.1, 0.03, 0.5, 0.01,
			"Held in drift, how long the attitude thrusters take to swing the nose up to its drift turn rate and to stop it.");
		return setting;
	}

	const cs::Ref<RangeSetting>& Drift::Turn()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("flight.drift.turn", SettingGroups::Dogfighting,
			"Drift: turn rate", 1.6, 1, 3, 0.05,
			"Held in drift, how fast the nose swings, times the craft's own top turn rate: with no drive fighting the turn, the attitude thrusters have the hull to themselves.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }), "flight model handling drift turn rate flip");
		return setting;
	}

	const cs::Ref<RangeSetting>& Drift::BoostOut()
	{
		static const cs::Ref<RangeSetting> setting = DriftSeconds("flight.drift.boostOut", "Drift: boost-out", 0.6, 0, 2, 0.05,
			"Once drift is let go, how long the afterburner stays lit along the new facing, with flight assist back on and the throttle at least full.");
		return setting;
	}

	const cs::Ref<RangeSetting>& Drift::BoostOutReach()
	{
		static const cs::Ref<RangeSetting> setting = DriftSeconds("flight.drift.boostOutReach", "Drift: boost-out reach", 0.8, 0.2, 3, 0.05,
			"Once drift is let go, how soon your craft makes most of its top speed along the new facing, whatever way it was coasting; the flight computer pushes harder than the drive alone when it must. Crews never drift.");
		return setting;
	}

	const cs::Ref<RangeSetting>& Drift::BoostOutReachShare()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("flight.drift.boostOutReachShare", SettingGroups::Dogfighting,
			"Drift: boost-out reach speed", 0.9, 0.5, 1, 0.05,
			"The share of top speed along the new facing the boost-out makes within the boost-out reach.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "flight model handling drift boost-out speed");
		return setting;
	}

	double Drift::SpinUpS() { return SpinUp()->Value; }

	double Drift::TurnFactor() { return Turn()->Value; }

	double Drift::BoostOutS() { return BoostOut()->Value; }

	double Drift::TurnRateDegS(double turnRateDegS) { return turnRateDegS * TurnFactor(); }

	void Drift::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (dt <= 0) return;
		// The attitude thrusters alone: assist's roll hold (so a roll stops when let go), no drive, no cruise.
		PilotInput in = input;
		in.FlightAssistOff = false;
		in.Boost = in.Cruise = false;
		in.Throttle = in.StrafeRight = in.StrafeUp = 0;
		double rate = TurnRateDegS(tuning.TurnRateDegS);
		double spinUp = SpinUpS();
		double spin = cs::Math::Max(hull.PitchYawAngularAccel, rate * Astro::Deg2Rad / spinUp);
		double roll = cs::Math::Max(hull.RollAngularAccel, tuning.RollRateDegS() * Astro::Deg2Rad / spinUp);
		auto rcs = HullDynamics(hull.MassKt, hull.MainAccel, hull.RetroAccel, hull.LateralAccel, hull.VerticalAccel, spin, roll);
		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, rcs, rate, tuning.RollRateDegS(), in, dt);
		// The boost charge refills while the drive is cold, and a cruise ramp drops.
		if (combat != nullptr) PilotFlight::DriveMultiplier(s, combat, tuning, in, forwardBeforeTurn, dt);
		// Barrelling on along the way it had: nothing but gravity changes it.
		PilotFlight::Integrate(s, DVec3::Zero, gravityKmS2, dt);
	}

	void Drift::Fly(const cs::Ref<IFlightModel>& model, PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (input.Drift || model == nullptr) Step(s, combat, hull, tuning, input, gravityKmS2, dt);
		else model->Step(s, combat, hull, tuning, input, gravityKmS2, dt);
	}

	bool DriftControl::Update(bool held, double dt)
	{
		if (held)
		{
			Active = true;
			BoostOutLeft = ReachLeft = 0;
			return false;
		}
		if (Active)
		{
			Active = false;
			BoostOutLeft = Drift::BoostOutS();
			ReachLeft = Drift::BoostOutReach()->Value;
			return true;
		}
		BoostOutLeft = cs::Math::Max(0.0, BoostOutLeft - cs::Math::Max(0.0, dt));
		ReachLeft = cs::Math::Max(0.0, ReachLeft - cs::Math::Max(0.0, dt));
		return false;
	}

	void DriftControl::Apply(PilotInput& input) const
	{
		if (Active)
		{
			input.Drift = true;
			return;
		}
		if (input.Piloted && ReachLeft > 0)
		{
			// The player's boost-out makes its share of top speed on the new facing by the deadline (US-T829).
			input.BoostOutReachS = ReachLeft;
			input.BoostOutReachShare = Drift::BoostOutReachShare()->Value;
			input.FlightAssistOff = false;
			input.Throttle = cs::Math::Max(input.Throttle, input.BoostOutReachShare);
		}
		if (BoostOutLeft <= 0) return;
		// The boost-out: snapped back to assisted flight in the new facing, the afterburner lit along it.
		input.Boost = true;
		input.FlightAssistOff = false;
		input.Throttle = cs::Math::Max(input.Throttle, 1.0);
	}
}
