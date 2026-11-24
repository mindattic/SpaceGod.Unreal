#pragma once
// US-T544: the Display group's own rows beside the resolution, window mode and VSync of the Display page: brightness (the
// display gamma), HDR output where the screen supports it with its peak brightness, and a frame cap while the window
// is in the background. Each goes into effect the moment it changes and whenever a profile is loaded. A view setting:
// each machine's own, never shared in co-op; a scripted run (PlayerOptions::ScriptedRun) keeps the defaults.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod
{
	/// C#: [SettingsSource] -- registered with the settings registry by GameSettingsHost.
	class DisplayExtras
	{
	public:
		/// The group these rows are declared in; the Display page draws it, not the settings nav.
		static constexpr const char* Group = "Display";

		static const cs::Ref<Core::RangeSetting>& Brightness();
		static const cs::Ref<Core::ToggleSetting>& HdrOutput();
		static const cs::Ref<Core::RangeSetting>& HdrPeakNits();
		static const cs::Ref<Core::ChoiceSetting>& BackgroundFrameCap();

		/// Builds every row in page order (the settings source GameSettingsHost adds).
		static void Declare();

		/// Whether the screen the game is on can take HDR output.
		static bool HdrSupported();
	};
}
