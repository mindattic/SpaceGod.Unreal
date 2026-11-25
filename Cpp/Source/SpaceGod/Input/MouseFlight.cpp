// US-T736: mouse flight, aim or virtual stick, and auto-level (Input/MouseFlight.h).

#include "Input/MouseFlight.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	namespace
	{
		cs::Func<cs::String, double> Percent() { return [](double v) { return cs::Cat(cs::ToInt(cs::Math::Round(v * 100)), "%"); }; }
	}

	void MouseFlight::Declare()
	{
		ModeSetting(); DeadZone(); FullAt(); Exponent(); AutoLevel(); AutoLevelSeconds();
	}

	const cs::Ref<ChoiceSetting>& MouseFlight::ModeSetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("controls.mouseFlight", SettingGroups::Controls,
			"Mouse flight", cs::List<cs::String>{ "Aim", "Virtual stick" }, static_cast<int>(Mode::Aim),
			"Aim: the mouse aims and the craft turns after it. Virtual stick: the mouse moves a cursor about the centre of the "
			"screen and the nose turns at a rate that grows with its distance out; leave it in the middle to fly straight.",
			"mouse flight virtual stick cursor aim freelancer fighter")->Applying([](int) { Centre(); });
		return value;
	}

	const cs::Ref<RangeSetting>& MouseFlight::DeadZone()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.mouseFlight.deadZone", SettingGroups::Controls,
			"Virtual stick dead zone", 0.03, 0.0, 0.2, 0.01,
			"How far from the centre, as a share of the half-screen, the cursor goes before the nose starts to turn.",
			Percent(), "mouse flight virtual stick dead zone centre");
		return value;
	}

	const cs::Ref<RangeSetting>& MouseFlight::FullAt()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.mouseFlight.fullAt", SettingGroups::Controls,
			"Virtual stick full rate at", 0.4, 0.1, 1.0, 0.05,
			"How far out, as a share of the half-screen, the cursor turns the nose at the craft's full rate.",
			Percent(), "mouse flight virtual stick range full rate");
		return value;
	}

	const cs::Ref<RangeSetting>& MouseFlight::Exponent()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.mouseFlight.exponent", SettingGroups::Controls,
			"Virtual stick curve", 1.5, 1.0, 3.0, 0.1,
			"The response curve between the dead zone and full rate: 1 is straight, higher gives finer aim near the centre.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.0"); }), "mouse flight virtual stick curve exponent response");
		return value;
	}

	const cs::Ref<ToggleSetting>& MouseFlight::AutoLevel()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("controls.autoLevel", SettingGroups::Controls,
			"Auto-level", false, "With flight assist on and roll let go, a craft eases its roof back up to the system's north.",
			"auto level roll horizon fighter flight assist");
		return value;
	}

	const cs::Ref<RangeSetting>& MouseFlight::AutoLevelSeconds()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.autoLevel.seconds", SettingGroups::Controls,
			"Auto-level time", 1.0, 0.2, 5.0, 0.1, "How long auto-level takes to ease the roof back up.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.0"), " s"); }), "auto level roll time");
		return value;
	}

	bool MouseFlight::VirtualStick() { return ModeSetting()->Index == static_cast<int>(Mode::VirtualStick); }

	double MouseFlight::AutoLevelS() { return AutoLevel()->Value ? AutoLevelSeconds()->Value : 0; }

	Vector2 MouseFlight::Step(Vector2 lookDeg)
	{
		cursor = Vector2::ClampMagnitude(Vector2(cursor.x + lookDeg.x / LookDegPerHalfScreen, cursor.y + lookDeg.y / LookDegPerHalfScreen), 1.0f);
		return Shape(cursor);
	}

	Vector2 MouseFlight::Shape(Vector2 c)
	{
		float r = c.magnitude();
		float dead = static_cast<float>(DeadZone()->Value);
		float full = Mathf::Max(dead + 0.01f, static_cast<float>(FullAt()->Value));
		if (r <= dead) return Vector2::zero();
		float t = Mathf::Clamp01((r - dead) / (full - dead));
		float k = Mathf::Pow(t, static_cast<float>(Exponent()->Value)) / r;
		return Vector2(c.x * k, c.y * k);
	}
}
