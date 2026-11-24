#include "AccessibilityOptions.h"

// Before the engine's headers: UnityMath.h (through PlayerOptions.h) declares a member the engine's PI macro would rewrite.
#include "PlayerOptions.h"
#include "Settings/GameSetting.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// The rows' values as last put into effect (written by the Applying callbacks only, see DisplayExtras.cpp).
		int mode = 0;
		int strength = 100;
		bool correct = true;
		int captions = 0;
		int captionSize = 1;
		int captionBackground = 60;

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

	const cs::Ref<ChoiceSetting>& AccessibilityOptions::Captions()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("access.captions", PlayerOptions::Accessibility,
			"Captions", cs::List<cs::String>{ "Off", "Comms", "Comms and sounds" }, 0,
			"Shows what is said over comms as captions; with sounds, the interface's alerts (a hail, a contract, credits) too.",
			"captions subtitles deaf hearing comms sounds alerts text")
			->Applying([](int i) { captions = i; });
		return value;
	}

	const cs::Ref<ChoiceSetting>& AccessibilityOptions::CaptionSize()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("access.captionSize", PlayerOptions::Accessibility,
			"Caption size", cs::List<cs::String>{ "Small", "Medium", "Large" }, 1,
			"How large captions are drawn.",
			"captions subtitles size text font large")
			->Applying([](int i) { captionSize = i; });
		return value;
	}

	const cs::Ref<RangeSetting>& AccessibilityOptions::CaptionBackground()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.captionBackground", PlayerOptions::Accessibility,
			"Caption background", 60, 0, 100, 10,
			"How dark the box behind captions is (0% draws no box).",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"captions subtitles background box opacity contrast")
			->Applying([](double v) { captionBackground = cs::ToInt(cs::Math::Round(v)); });
		return value;
	}

	AccessibilityOptions::CaptionLevel AccessibilityOptions::CaptionsShown()
	{
		return PlayerOptions::ScriptedRun() ? CaptionLevel::Off : static_cast<CaptionLevel>(cs::Math::Max(0, cs::Math::Min(2, captions)));
	}

	AccessibilityOptions::CaptionScale AccessibilityOptions::CaptionsScale()
	{
		return PlayerOptions::ScriptedRun() ? CaptionScale::Medium : static_cast<CaptionScale>(cs::Math::Max(0, cs::Math::Min(2, captionSize)));
	}

	float AccessibilityOptions::CaptionsBackdrop()
	{
		return (PlayerOptions::ScriptedRun() ? 60 : cs::Math::Max(0, cs::Math::Min(100, captionBackground))) / 100.0f;
	}

	void AccessibilityOptions::Declare()
	{
		ColorBlindMode(); ColorBlindStrength(); ColorBlindCorrect();
		Captions(); CaptionSize(); CaptionBackground();
	}
}
