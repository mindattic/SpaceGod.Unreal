#include "Combat/PilotTuning.h"
#include "Combat/ShipCatalog.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>US-T535: one pilot-control family's flight multipliers, each a live knob defaulting to 1.</summary>
		struct FlightKnobs
		{
			cs::Ref<RangeSetting> TopSpeed, TurnRate, Roll, Boost, BoostDuration, Cruise, CruiseRamp;
		};

		FlightKnobs DeclareFlightKnobs(const cs::String& family, const cs::String& who)
		{
			auto knob = [&](const char* name, const char* label, const char* what)
			{
				return Tuning::Knob("flight", cs::Cat(family, ".", name), cs::Cat(who, ": ", label), 1, 0.25, 4, 0.05, "x", TuningScope::Simulation,
					cs::Cat(what, " of ", who, ", times this."));
			};
			FlightKnobs k;
			k.TopSpeed = knob("topSpeed", "top speed", "The speed flight assist holds");
			k.TurnRate = knob("turnRate", "turn rate", "The fastest pitch and yaw");
			k.Roll = knob("roll", "roll", "The roll rate, on top of the turn rate,");
			k.Boost = knob("boost", "boost multiplier", "The boost's thrust and speed multiplier");
			k.BoostDuration = knob("boostDuration", "boost duration", "How long a full boost charge lasts");
			k.Cruise = knob("cruise", "cruise multiplier", "The cruise drive's thrust and speed multiplier");
			k.CruiseRamp = knob("cruiseRamp", "cruise ramp", "The seconds the cruise drive takes to spool up");
			return k;
		}

		const FlightKnobs& FlightKnobsOf(PilotControl control)
		{
			static const FlightKnobs freeAim = DeclareFlightKnobs("freeAim", "free-aim fighters");
			static const FlightKnobs directNose = DeclareFlightKnobs("directNose", "direct-nose fighters");
			static const FlightKnobs hauler = DeclareFlightKnobs("hauler", "haulers");
			static const FlightKnobs helm = DeclareFlightKnobs("helm", "capital ships");
			switch (control)
			{
				case PilotControl::FreeAim: return freeAim;
				case PilotControl::DirectNose: return directNose;
				case PilotControl::Hauler: return hauler;
				case PilotControl::Helm: break;
			}
			return helm;
		}
	}

	const char* ToString(PilotControl v)
	{
		switch (v)
		{
			case PilotControl::FreeAim: return "FreeAim";
			case PilotControl::DirectNose: return "DirectNose";
			case PilotControl::Hauler: return "Hauler";
			case PilotControl::Helm: return "Helm";
		}
		return "";
	}

	const cs::Dictionary<ShipClass, PilotTuning>& PilotTuning::ByClass()
	{
		// Every hull's numbers are authored once, in the ship catalog's table (US-T193, Combat/ShipCatalog.cpp): each
		// class's tuning is its row there, its roll as a factor of its turn so a flight model that narrows the turn
		// narrows the roll with it.
		static const cs::Dictionary<ShipClass, PilotTuning> byClass = []
		{
			cs::Dictionary<ShipClass, PilotTuning> d;
			for (ShipClass c : ShipCatalog::Classes())
			{
				HullHandling h;
				if (!ShipCatalog::TryHandling(c, h)) continue;
				double roll = h.TurnRateDegS > 0 ? h.RollRateDegS / h.TurnRateDegS : DefaultRollFactor;
				d[c] = PilotTuning(h.TopSpeedKmS, h.TurnRateDegS, h.BoostSpeedMultiplier, h.BoostDurationS, h.CruiseSpeedMultiplier, h.CruiseRampS, roll);
			}
			return d;
		}();
		return byClass;
	}

	PilotTuning PilotTuning::Of(ShipClass c)
	{
		PilotTuning t;
		if (!ByClass().TryGetValue(c, t)) t = PilotTuning(0.1, 20, 1.3, 20, 6, 3.0);
		const FlightKnobs& k = FlightKnobsOf(ControlOf(c));
		t.MaxSpeed *= k.TopSpeed->Value;
		t.TurnRateDegS *= k.TurnRate->Value;
		t.RollFactor *= k.Roll->Value;
		t.BoostSpeedMultiplier *= k.Boost->Value;
		t.BoostDurationS *= k.BoostDuration->Value;
		t.CruiseSpeedMultiplier *= k.Cruise->Value;
		t.CruiseRampS *= k.CruiseRamp->Value;
		return t;
	}

	PilotControl PilotTuning::ControlOf(ShipClass c) { return ShipCatalog::ControlOf(c); }
}
