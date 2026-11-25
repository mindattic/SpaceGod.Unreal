// US-T825: the joystick or HOTAS (Input/Joystick.h).

#include "Input/Joystick.h"
#include "PlayerOptions.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	void JoystickDevice::Clear()
	{
		for (float& a : axes) a = 0;
	}

	JoystickDevice& JoystickDevice::current()
	{
		static JoystickDevice device;
		return device;
	}

	float AxisCurve::Apply(float raw, float inner, float outer, float exponent, bool invert)
	{
		float v = Mathf::Clamp(invert ? -raw : raw, -1.0f, 1.0f);
		float m = Mathf::Abs(v);
		if (m <= inner) return 0;
		float full = Mathf::Max(inner + 0.0001f, 1.0f - outer);
		float t = Mathf::Clamp01((m - inner) / (full - inner));
		if (exponent != 1.0f) t = Mathf::Pow(t, exponent);
		return v < 0 ? -t : t;
	}

	namespace
	{
		struct JoyAxisRow
		{
			const char* key;
			const char* label;
			int source;
			double inner;
			const char* keywords;
		};

		// The defaults: X yaws and Y pitches (FreeSpace's layout); roll, strafe and the throttle wait to be given an axis.
		const JoyAxisRow JoyAxisRows[static_cast<int>(JoyAxis::Count)] =
		{
			{ "pitch", "pitch", 2, 0.05, "joystick hotas flight stick axis pitch" },
			{ "yaw", "yaw", 1, 0.05, "joystick hotas flight stick axis yaw rudder twist" },
			{ "roll", "roll", 0, 0.05, "joystick hotas flight stick axis roll bank twist" },
			{ "strafeX", "strafe left/right", 0, 0.05, "joystick hotas axis strafe lateral thruster" },
			{ "strafeY", "strafe up/down", 0, 0.05, "joystick hotas axis strafe vertical rise sink thruster" },
			{ "throttle", "throttle", 0, 0.0, "joystick hotas axis throttle lever slider speed" },
		};

		struct JoyAxisSettings
		{
			cs::Ref<ChoiceSetting> source;
			cs::Ref<ToggleSetting> invert;
			cs::Ref<RangeSetting> inner, outer, exponent;
		};

		cs::Func<cs::String, double> JoyPercent() { return [](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::ToInt(cs::Math::Round(v * 100)), "%"); }; }

		const JoyAxisSettings& SettingsOf(JoyAxis axis)
		{
			static JoyAxisSettings built[static_cast<int>(JoyAxis::Count)];
			static bool made = false;
			if (!made)
			{
				made = true;
				for (int i = 0; i < static_cast<int>(JoyAxis::Count); i++)
				{
					const JoyAxisRow& row = JoyAxisRows[i];
					cs::String key = cs::Cat("controls.joystick.", row.key);
					cs::String label = cs::Cat("Joystick ", row.label);
					built[i].source = cs::New<ChoiceSetting>(key, SettingGroups::Controls, cs::Cat(label, " axis"),
						cs::List<cs::String>{ "None", "Axis 1", "Axis 2", "Axis 3", "Axis 4", "Axis 5", "Axis 6", "Axis 7", "Axis 8" }, row.source,
						cs::Cat("Which joystick or HOTAS axis flies the ", row.label, "; None leaves it to the keys, mouse and pad."), row.keywords);
					built[i].invert = cs::New<ToggleSetting>(cs::Cat(key, ".invert"), SettingGroups::Controls, cs::Cat("Invert ", label), false,
						cs::Cat("The ", row.label, " axis reads the other way."), row.keywords);
					built[i].inner = cs::New<RangeSetting>(cs::Cat(key, ".deadZone"), SettingGroups::Controls, cs::Cat(label, " dead zone"),
						row.inner, 0.0, 0.3, 0.01, "How far the axis moves before it counts: raise it if a worn stick drifts on its own.",
						JoyPercent(), row.keywords);
					built[i].outer = cs::New<RangeSetting>(cs::Cat(key, ".outerDeadZone"), SettingGroups::Controls, cs::Cat(label, " outer dead zone"),
						0.0, 0.0, 0.1, 0.01, "The last of the axis's travel that already reads full, for a stick that never quite reaches its stop.",
						JoyPercent(), row.keywords);
					built[i].exponent = cs::New<RangeSetting>(cs::Cat(key, ".exponent"), SettingGroups::Controls, cs::Cat(label, " curve"),
						1.0, 1.0, 3.0, 0.1, "The response past the dead zone: 1 follows the stick, higher keeps small moves small for fine control.",
						cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.0"); }), row.keywords);
				}
			}
			return built[static_cast<int>(axis)];
		}
	}

	const cs::Ref<ChoiceSetting>& Joystick::Source(JoyAxis axis) { return SettingsOf(axis).source; }
	const cs::Ref<ToggleSetting>& Joystick::Invert(JoyAxis axis) { return SettingsOf(axis).invert; }
	const cs::Ref<RangeSetting>& Joystick::InnerDeadZone(JoyAxis axis) { return SettingsOf(axis).inner; }
	const cs::Ref<RangeSetting>& Joystick::OuterDeadZone(JoyAxis axis) { return SettingsOf(axis).outer; }
	const cs::Ref<RangeSetting>& Joystick::Exponent(JoyAxis axis) { return SettingsOf(axis).exponent; }

	void Joystick::Declare()
	{
		SettingsOf(JoyAxis::Pitch);
	}

	bool Joystick::Bound(JoyAxis axis)
	{
		return JoystickDevice::current().present && Source(axis)->Index > 0;
	}

	float Joystick::Read(JoyAxis axis)
	{
		const bool throttle = axis == JoyAxis::Throttle;
		if (!Bound(axis)) return throttle ? -1.0f : 0.0f;
		int source = Mathf::Clamp(Source(axis)->Index, 1, JoystickDevice::AxisCount) - 1;
		float raw = JoystickDevice::current().axes[source];
		bool scripted = PlayerOptions::ScriptedRun();
		bool invert = !scripted && Invert(axis)->Value;
		float inner = scripted ? 0.0f : static_cast<float>(InnerDeadZone(axis)->Value);
		float outer = scripted ? 0.0f : static_cast<float>(OuterDeadZone(axis)->Value);
		float exponent = scripted ? 1.0f : static_cast<float>(Exponent(axis)->Value);
		if (!throttle) return AxisCurve::Apply(raw, inner, outer, exponent, invert);
		// The lever: its full travel, -1..1, as 0..1 from the idle stop.
		float lever = Mathf::Clamp01((raw + 1.0f) * 0.5f);
		return AxisCurve::Apply(invert ? 1.0f - lever : lever, inner, outer, exponent, false);
	}
}
