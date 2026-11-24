#pragma once
// US-T559: Settings > Interface -- the crosshair's style and colour, a frame-rate counter, when targets carry their name and
// range, the off-screen target arrows and the hit markers. Declared here as PlayerOptions declares its groups
// (GameSettingsHost registers this source), kept per profile and never shared in co-op: each machine draws its own HUD.
// Every default is the HUD as it was before these rows, and a scripted run (PlayerOptions::ScriptedRun) reads the
// defaults whatever the profile says, so shots and digests stay alike.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod
{
	class InterfaceOptions
	{
	public:
		/// The crosshair's shape: the four ticks it always had, a dot, both, or a ring.
		static const cs::Ref<Core::ChoiceSetting>& CrosshairStyleSetting();
		/// The crosshair's colour (cyan as it always was).
		static const cs::Ref<Core::ChoiceSetting>& CrosshairColourSetting();
		/// A frames-per-second counter in a corner of the screen. Off by default.
		static const cs::Ref<Core::ToggleSetting>& ShowFpsCounter();
		/// Targets' names and ranges on screen: always, only near ones, or never.
		static const cs::Ref<Core::ChoiceSetting>& TargetLabels();
		/// Arrows at the screen's edge toward targets off it (US-T128). On by default.
		static const cs::Ref<Core::ToggleSetting>& OffscreenIndicators();
		/// The ticks round the reticle when a round lands (US-T257). On by default.
		static const cs::Ref<Core::ToggleSetting>& HitMarkers();
		/// US-T600: how far in from the screen's edges the HUD keeps (0% to 10%), for TVs that crop the picture and very
		/// wide screens. 0 by default: the HUD on the screen's edges as it always was.
		static const cs::Ref<Core::RangeSetting>& SafeZone();

		/// Builds every one of these settings (the declaration source GameSettingsHost registers).
		static void Declare();

		// ---------------------------------------------------------------- reads

		enum class Crosshair : int { Ticks = 0, Dot = 1, TicksAndDot = 2, Ring = 3 };
		static Crosshair CrosshairStyle();
		/// The crosshair's colour at <paramref name="alpha"/> (0.9, the HUD's own, unless given).
		static Color CrosshairColor(float alpha = 0.9f);

		/// Whether the frame-rate counter is drawn.
		static bool ShowsFps();

		enum class Labels : int { Always = 0, Near = 1, Off = 2 };
		static Labels LabelMode();
		/// Whether a target <paramref name="km"/> away carries its label, Near counting up to <paramref name="nearKm"/>.
		static bool LabelsAt(double km, double nearKm);

		/// Whether the off-screen target arrows are drawn.
		static bool ShowsOffscreenIndicators();
		/// Whether hit markers are drawn.
		static bool ShowsHitMarkers();

		/// The HUD's inset from each edge as a fraction of the screen's size (0 to 0.1).
		static float SafeZoneFraction();
	};
}
