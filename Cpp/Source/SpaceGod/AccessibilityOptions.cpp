#include "AccessibilityOptions.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"

#include "PlayerOptions.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// The rows' values as last put into effect (written by the Applying callbacks only, see DisplayExtras.cpp).
		int mode = 0;
		int strength = 100;
		bool correct = true;

		/// Hands the filter to Slate's renderer, which runs its pass over the back buffer only while a deficiency is chosen.
		void Apply()
		{
			if (!FSlateApplication::IsInitialized()) return;
			FSlateRenderer* renderer = FSlateApplication::Get().GetRenderer();
			if (renderer == nullptr) return;
			static const EColorVisionDeficiency types[] = { EColorVisionDeficiency::NormalVision, EColorVisionDeficiency::Protanope,
				EColorVisionDeficiency::Deuteranope, EColorVisionDeficiency::Tritanope };
			int chosen = PlayerOptions::ScriptedRun() ? 0 : mode;
			int severity = cs::Math::Max(0, cs::Math::Min(10, cs::ToInt(cs::Math::Round(strength / 10.0))));
			if (chosen == 0 || severity == 0) renderer->SetColorVisionDeficiencyType(EColorVisionDeficiency::NormalVision, 0, false, false);
			else renderer->SetColorVisionDeficiencyType(types[chosen], severity, correct, false);
		}
	}

	const cs::Ref<ChoiceSetting>& AccessibilityOptions::ColorBlindMode()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("access.colorBlindMode", PlayerOptions::Accessibility,
			"Colour-blind filter", cs::List<cs::String>{ "Off", "Protanopia", "Deuteranopia", "Tritanopia" }, 0,
			"A filter over the whole picture for red-weak (protanopia), green-weak (deuteranopia) or blue-weak (tritanopia) sight.",
			"colourblind colorblind color colour filter protanopia deuteranopia tritanopia daltonize vision")
			->Applying([](int i)
			{
				mode = i;
				Apply();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& AccessibilityOptions::ColorBlindStrength()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.colorBlindStrength", PlayerOptions::Accessibility,
			"Colour-blind filter strength", 100, 0, 100, 10,
			"How strongly the colour-blind filter acts (0% leaves the picture as it is).",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"colourblind colorblind filter strength severity intensity")
			->Applying([](double v)
			{
				strength = cs::ToInt(cs::Math::Round(v));
				Apply();
			});
		return value;
	}

	const cs::Ref<ToggleSetting>& AccessibilityOptions::ColorBlindCorrect()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("access.colorBlindCorrect", PlayerOptions::Accessibility,
			"Correct colours (off: simulate)", true,
			"On shifts the colours so they stay told apart under the chosen deficiency; off shows the picture as that deficiency sees it.",
			"colourblind colorblind correct simulate daltonize filter")
			->Applying([](bool on)
			{
				correct = on;
				Apply();
			});
		return value;
	}

	void AccessibilityOptions::Declare()
	{
		ColorBlindMode(); ColorBlindStrength(); ColorBlindCorrect();
	}
}
