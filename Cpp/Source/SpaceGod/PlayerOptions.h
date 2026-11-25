#pragma once
// US-T252: the player's own options, as against the design tunables the game's systems declare. Two settings groups of
// their own -- Gameplay (autosave interval, distance units, how chatty toasts and the news ticker are, a confirmation
// before leaving) and Accessibility (UI scale, text size, HUD opacity, a colour-blind safe stance palette, fewer screen
// flashes) -- declared here as every setting is (GameSettingsHost registers this source), kept per profile like any
// other, and read by the places they touch through the helpers below. The settings menu (UI/PauseMenu) lists the player
// groups first (PlayerGroups) and folds the rest under "Advanced (simulation)".

#include "Cs/Cs.h"
#include "DateText.h"
#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class ToggleSetting;
	class RangeSetting;
}

namespace SpaceGod
{
	class PlayerOptions
	{
	public:
		/// The two groups of player options (Core's SettingGroups names the design ones).
		static constexpr const char* Gameplay = "Gameplay";
		static constexpr const char* Accessibility = "Accessibility";

		/// The groups the settings menu lists first, in its order (its own Display page goes after Graphics); every other
		/// group is design tuning, folded under "Advanced (simulation)".
		static const cs::List<cs::String>& PlayerGroups();
		static bool IsPlayerGroup(const cs::String& group);

		// ---------------------------------------------------------------- Gameplay

		/// Off, or 2 / 5 / 10 / 15 / 30 real minutes between autosaves (5 before the player changes it).
		static const cs::Ref<Core::ChoiceSetting>& AutosaveInterval();
		/// US-T616: 1 / 3 / 5 / 10 autosaves taking turns (3 before the player changes it).
		static const cs::Ref<Core::ChoiceSetting>& AutosaveSlots();
		/// US-T616: an autosave on docking. Off by default.
		static const cs::Ref<Core::ToggleSetting>& AutosaveOnDock();
		/// US-T616: an autosave right before a jump. Off by default.
		static const cs::Ref<Core::ToggleSetting>& AutosaveBeforeJump();
		/// How long distances are written: km then AU as the readout sees fit, always km, or AU wherever it is not a fight's range.
		static const cs::Ref<Core::ChoiceSetting>& DistanceUnits();
		/// Toasts and the news ticker: everything, important only, or quiet.
		static const cs::Ref<Core::ChoiceSetting>& Notices();
		/// The pause menu's Main Menu and Quit ask first.
		static const cs::Ref<Core::ToggleSetting>& ConfirmQuit();
		/// US-T441: the HUD's layout follows the situation (a fight: Combat; docked: Commerce; else the chosen preset). Off by default.
		static const cs::Ref<Core::ToggleSetting>& LayoutWithSituation();
		/// US-T555: every camera shake at this strength (0% to 100%, 100% as it always was).
		static const cs::Ref<Core::RangeSetting>& CameraShake();
		/// US-T840: the camera feel (head-look, G lean, boost kick, hit jolt, chase lag) at this strength (0% off to 200%).
		static const cs::Ref<Core::RangeSetting>& CameraFeel();
		/// US-T555: the pause menu opens when the window loses focus, outside co-op. Off by default.
		static const cs::Ref<Core::ToggleSetting>& PauseOnFocusLoss();
		/// US-T617: times of day in 24-hour (as always) or 12-hour form.
		static const cs::Ref<Core::ChoiceSetting>& Clock();
		/// US-T617: dates as year-month-day (as always), day/month/year or month/day/year.
		static const cs::Ref<Core::ChoiceSetting>& Dates();

		// ---------------------------------------------------------------- Accessibility

		/// Every IMGUI screen and the HUD drawn this much larger (75% to 200%).
		static const cs::Ref<Core::RangeSetting>& UiScale();
		/// Every font this much larger (80% to 150%), on top of the UI scale.
		static const cs::Ref<Core::RangeSetting>& TextSize();
		/// The HUD's overlays drawn at this opacity (30% to 100%); full screens and menus stay solid.
		static const cs::Ref<Core::RangeSetting>& HudOpacity();
		/// Hostile, neutral and friendly in red / grey / green, or a colour-blind safe orange / grey / blue.
		static const cs::Ref<Core::ChoiceSetting>& StancePalette();
		/// Screen washes, edge pulses and the hyperspace toll's heartbeat kept faint and still; transitions do not move.
		static const cs::Ref<Core::ToggleSetting>& ReduceFlashes();

		/// Builds every one of these settings (the declaration source GameSettingsHost registers).
		static void Declare();

		// ---------------------------------------------------------------- reads

		/// Real seconds between autosaves; 0 when they are off.
		static float AutosaveSeconds();
		/// How many autosave slots take turns (3 in a scripted run).
		static int AutosavesKept();
		/// Autosave on docking / before a jump is on (never in a scripted run).
		static bool AutosavesOnDock();
		static bool AutosavesBeforeJump();

		enum class Units : int { Auto = 0, Kilometres = 1, Au = 2 };
		static Units DistanceMode();
		/// Whether a readout that would switch to AU at <paramref name="autoFromKm"/> writes <paramref name="km"/> in AU.
		static bool WriteAu(double km, double autoFromKm);
		/// A distance in km with no AU: whole km, "12.3 million km" from ten million, "1.23 billion km" from a billion.
		static cs::String KmText(double km);

		enum class NoticeLevel : int { Everything = 0, Important = 1, Quiet = 2 };
		static NoticeLevel NoticeMode();

		/// The HUD's opacity, 0.3 to 1.
		static float HudAlpha();

		/// The stance palette's colours (the HUD's markers, arrows and roster; comms).
		static bool ColourBlindStances();
		static Color HostileColor(float alpha = 1);
		static Color FriendlyColor(float alpha = 1);
		static Color NeutralColor(float alpha = 1);
		/// Comms' "wary" between neutral and hostile.
		static Color WaryColor(float alpha = 1);

		/// Reduce flashes is on.
		static bool Calm();
		/// What a flash or screen wash is multiplied by: 1, or a faint share of it when Reduce flashes is on.
		static float FlashScale();
		/// What a camera shake is multiplied by: Camera shake as 0..1 (1 in a scripted run).
		static float ShakeScale();
		/// What the camera feel is multiplied by: Camera feel as 0..2 (1 in a scripted run).
		static float FeelScale();
		/// Pause when the window loses focus is on (never in a scripted run).
		static bool PausesOnFocusLoss();

		/// US-T617: Clock and Date order (24-hour, year-month-day in a scripted run).
		static Core::ClockFormat ClockMode();
		static Core::DateOrder DateMode();
		/// A date, a time of day, or both, as Clock and Date order write them (Core::DateText's forms).
		static cs::String Date(const cs::DateTime& t);
		static cs::String Time(const cs::DateTime& t, bool seconds = false);
		static cs::String DateAndTime(const cs::DateTime& t, bool seconds = false, const char* gap = " ");
		static cs::String MonthDay(const cs::DateTime& t, bool year = false, bool padDay = true);

		/// A run that is driven and photographed (the smoke, shots, audits, the demo, captures, headless) draws at 100% scale and
		/// text whatever the profile says, so every shot is alike.
		static bool ScriptedRun();
	};
}
