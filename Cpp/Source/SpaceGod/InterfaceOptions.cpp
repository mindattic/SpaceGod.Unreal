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

	void InterfaceOptions::Declare()
	{
		CrosshairStyleSetting(); CrosshairColourSetting(); ShowFpsCounter(); TargetLabels(); OffscreenIndicators(); HitMarkers();
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
}
