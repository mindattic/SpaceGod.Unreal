// US-T559: Settings > Interface (InterfaceOptions.h).

#include "InterfaceOptions.h"

#include "PlayerOptions.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// A scripted run draws the factory HUD whatever the profile (or the shipped defaults) say.
		int Choice(const cs::Ref<ChoiceSetting>& s) { return PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Index; }
		bool Toggle(const cs::Ref<ToggleSetting>& s) { return PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value; }
	}

	const cs::Ref<ChoiceSetting>& InterfaceOptions::CrosshairStyleSetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("interface.crosshairStyle", SettingGroups::Interface, "Crosshair",
			cs::List<cs::String>{ "Four ticks", "Dot", "Ticks and dot", "Ring" }, 0,
			"The shape of the helm's crosshair, where the stick points the hull.",
			"crosshair reticle aim sight cross dot ring circle");
		return value;
	}

	const cs::Ref<ChoiceSetting>& InterfaceOptions::CrosshairColourSetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("interface.crosshairColour", SettingGroups::Interface, "Crosshair colour",
			cs::List<cs::String>{ "Cyan", "White", "Green", "Yellow", "Magenta" }, 0,
			"The colour the crosshair is drawn in.",
			"crosshair reticle colour color cyan white green yellow magenta");
		return value;
	}

	const cs::Ref<ToggleSetting>& InterfaceOptions::ShowFpsCounter()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("interface.showFps", SettingGroups::Interface, "Show FPS counter", false,
			"Frames per second in a corner of the screen.", "fps frame rate framerate counter performance");
		return value;
	}

	const cs::Ref<ChoiceSetting>& InterfaceOptions::TargetLabels()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("interface.targetLabels", SettingGroups::Interface, "Target labels",
			cs::List<cs::String>{ "Always", "Near only", "Off" }, 0,
			"Targets' names and ranges beside their markers: always, only for the ones close by, or never.",
			"labels names markers tags range targets clutter");
		return value;
	}

	const cs::Ref<ToggleSetting>& InterfaceOptions::OffscreenIndicators()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("interface.offscreenIndicators", SettingGroups::Interface, "Off-screen indicators", true,
			"Arrows at the screen's edge toward targets out of view.", "offscreen off-screen arrows edge pointer indicator direction");
		return value;
	}

	const cs::Ref<ToggleSetting>& InterfaceOptions::HitMarkers()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("interface.hitMarkers", SettingGroups::Interface, "Hit markers", true,
			"Ticks round the reticle when your fire lands.", "hit marker confirm feedback reticle ticks");
		return value;
	}

	const cs::Ref<RangeSetting>& InterfaceOptions::SafeZone()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("interface.safeZone", SettingGroups::Interface, "HUD safe zone", 0, 0, 10, 1,
			"How far in from the screen's edges the HUD keeps, for TVs that crop the picture and very wide screens.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"safe zone area margin inset border edge tv overscan ultrawide hud");
		return value;
	}

	const cs::Ref<RangeSetting>& InterfaceOptions::VelocityMarkerAngle()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("interface.velocityMarkerAngle", SettingGroups::Interface,
			"Velocity marker angle", 5, 0, 30, 1,
			"With flight assist off or in drift, how far the craft's track swings off its nose before the prograde and retrograde markers show.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "°"); }),
			"velocity vector prograde retrograde marker track drift flight assist off slide angle hud");
		return value;
	}

	const cs::Ref<RangeSetting>& InterfaceOptions::WingCommsMenuTime()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("interface.wingCommsMenuTime", SettingGroups::Interface,
			"Wing comms menu time", 5, 2, 15, 1,
			"How long a fighter seat's wing comms menu (Shift+Z) waits for a number key before it closes.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }),
			"wing comms menu orders wingmen squadron radial timeout close seconds");
		return value;
	}

	const cs::Ref<ChoiceSetting>& InterfaceOptions::ChatterDensitySetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("radio.chatterDensity", SettingGroups::Interface, "Chatter density",
			cs::List<cs::String>{ "Off", "Low", "Normal", "High" }, 2,
			"How busy the radio's ambient talk is: traffic control, patrols, traders and raiders between the news.",
			"chatter density radio ambient talk traffic busy quiet noise comms");
		return value;
	}

	const cs::Ref<ToggleSetting>& InterfaceOptions::RadioSubtitles()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("radio.subtitles", SettingGroups::Interface, "Radio subtitles", true,
			"The radio log: what is heard on the air, written bottom left.",
			"radio subtitles captions log text chatter comms transcript");
		return value;
	}

	const cs::Ref<RangeSetting>& InterfaceOptions::MaxVoices()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("comms.maxVoices", SettingGroups::Interface, "Max voices", 1, 1, 3, 1,
			"How many radio lines may be spoken over one another at once.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); }),
			"max voices voice radio spoken overlap parallel simultaneous comms speech");
		return value;
	}

	const cs::Ref<RangeSetting>& InterfaceOptions::MinRadioPriority()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("radio.minPriority", SettingGroups::Interface, "Minimum radio priority", 0, 0, 3, 1,
			"Radio lines below this are only counted into the channel summaries: 0 hears all the chatter, 3 only distress.",
			cs::Func<cs::String, double>([](double v)
			{
				switch (cs::ToInt(cs::Math::Round(v)))
				{
				case 1: return cs::String("1 (no chatter)");
				case 2: return cs::String("2 (important)");
				case 3: return cs::String("3 (distress only)");
				default: return cs::String("0 (everything)");
				}
			}),
			"minimum radio priority filter chatter distress important urgent comms digest");
		return value;
	}

	void InterfaceOptions::Declare()
	{
		CrosshairStyleSetting(); CrosshairColourSetting(); ShowFpsCounter(); TargetLabels(); OffscreenIndicators(); HitMarkers(); SafeZone(); VelocityMarkerAngle(); WingCommsMenuTime();
		ChatterDensitySetting(); RadioSubtitles(); MaxVoices(); MinRadioPriority();
	}

	// ---------------------------------------------------------------- reads

	InterfaceOptions::Crosshair InterfaceOptions::CrosshairStyle() { return static_cast<Crosshair>(Choice(CrosshairStyleSetting())); }

	Color InterfaceOptions::CrosshairColor(float alpha)
	{
		switch (Choice(CrosshairColourSetting()))
		{
		case 1: return Color(0.95f, 0.95f, 0.95f, alpha);
		case 2: return Color(0.45f, 0.95f, 0.6f, alpha);
		case 3: return Color(1.0f, 0.9f, 0.35f, alpha);
		case 4: return Color(1.0f, 0.45f, 0.95f, alpha);
		default: return Color(0.55f, 0.95f, 1.0f, alpha); // the HUD's CrosshairColor
		}
	}

	bool InterfaceOptions::ShowsFps() { return Toggle(ShowFpsCounter()); }

	InterfaceOptions::Labels InterfaceOptions::LabelMode() { return static_cast<Labels>(Choice(TargetLabels())); }

	bool InterfaceOptions::LabelsAt(double km, double nearKm)
	{
		switch (LabelMode())
		{
		case Labels::Off: return false;
		case Labels::Near: return km <= nearKm;
		default: return true;
		}
	}

	bool InterfaceOptions::ShowsOffscreenIndicators() { return Toggle(OffscreenIndicators()); }

	bool InterfaceOptions::ShowsHitMarkers() { return Toggle(HitMarkers()); }

	float InterfaceOptions::SafeZoneFraction()
	{
		const auto& s = SafeZone();
		double pct = PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value;
		return static_cast<float>(cs::Math::Max(0.0, cs::Math::Min(10.0, pct)) / 100.0);
	}

	double InterfaceOptions::VelocityMarkerCos()
	{
		const auto& s = VelocityMarkerAngle();
		double deg = PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value;
		return cs::Math::Cos(cs::Math::Max(0.0, cs::Math::Min(30.0, deg)) * cs::Math::PI / 180.0);
	}

	float InterfaceOptions::WingCommsMenuS()
	{
		const auto& s = WingCommsMenuTime();
		double v = PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value;
		return static_cast<float>(cs::Math::Max(2.0, cs::Math::Min(15.0, v)));
	}

	double InterfaceOptions::ChatterDensity()
	{
		switch (Choice(ChatterDensitySetting()))
		{
		case 0: return 0.0;
		case 1: return 0.4;
		case 3: return 2.0;
		default: return 1.0;
		}
	}

	bool InterfaceOptions::ShowsRadioSubtitles() { return Toggle(RadioSubtitles()); }

	int InterfaceOptions::RadioVoices()
	{
		const auto& s = MaxVoices();
		double v = PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value;
		return cs::Math::Max(1, cs::Math::Min(3, cs::ToInt(cs::Math::Round(v))));
	}

	int InterfaceOptions::RadioMinPriority()
	{
		const auto& s = MinRadioPriority();
		double v = PlayerOptions::ScriptedRun() ? s->FactoryDefault : s->Value;
		return cs::Math::Max(0, cs::Math::Min(3, cs::ToInt(cs::Math::Round(v))));
	}
}
