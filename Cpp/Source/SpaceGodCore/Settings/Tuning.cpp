// US-T533: live-tuning knobs (Tuning.h).

#include "Settings/Tuning.h"

#include "Settings/GameSetting.h"

#include <set>

namespace SpaceGod::Core
{
	namespace
	{
		std::set<cs::String>& SimulationKeys()
		{
			static std::set<cs::String> keys;
			return keys;
		}

		cs::Func<cs::String, double> Display(const cs::String& unit)
		{
			if (unit.empty()) return nullptr;
			if (unit == "x") return cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); });
			return cs::Func<cs::String, double>([unit](double v) { return cs::Cat(cs::Fmt(v, "0.##"), " ", unit); });
		}
	}

	cs::Ref<RangeSetting> Tuning::Knob(const cs::String& area, const cs::String& name, const cs::String& label, double defaultValue, double min,
		double max, double step, const cs::String& unit, TuningScope scope, const cs::String& description)
	{
		cs::String key = cs::Cat("tune.", area, ".", name);
		if (scope == TuningScope::Simulation) SimulationKeys().insert(key);
		return cs::New<RangeSetting>(key, Group(area), label, defaultValue, min, max, step, description, Display(unit), cs::Cat("tuning developer ", area));
	}

	cs::String Tuning::Group(const cs::String& area) { return cs::Cat(GroupPrefix, area); }

	bool Tuning::IsTuningGroup(const cs::String& group) { return group.rfind(GroupPrefix, 0) == 0; }

	bool Tuning::IsSimulationScope(const cs::String& key) { return SimulationKeys().count(key) > 0; }
}
