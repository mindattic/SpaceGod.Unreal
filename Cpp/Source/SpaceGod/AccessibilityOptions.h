#pragma once
// US-T634: Settings > Accessibility's colour-blind filter: which deficiency (Off, Protanopia, Deuteranopia, Tritanopia),
// how strong, and whether it corrects the picture for it or simulates it. Slate's colour vision deficiency pass tints the
// whole back buffer, so the world, the HUD and every menu alike; Off leaves the frame untouched. A view setting: each
// machine's own, never shared in co-op; a scripted run (PlayerOptions::ScriptedRun) keeps the default.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod
{
	/// C#: [SettingsSource] -- registered with the settings registry by GameSettingsHost, after PlayerOptions.
	class AccessibilityOptions
	{
	public:
		static const cs::Ref<Core::ChoiceSetting>& ColorBlindMode();
		static const cs::Ref<Core::RangeSetting>& ColorBlindStrength();
		static const cs::Ref<Core::ToggleSetting>& ColorBlindCorrect();

		/// Builds every row in page order (the settings source GameSettingsHost adds).
		static void Declare();
	};
}
