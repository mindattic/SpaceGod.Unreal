// US-T252: the player's own options (PlayerOptions.h).

#include "PlayerOptions.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "MindAtticHeadless.h"
#include "Input/KeyBindings.h" // US-T249
#include "Settings/GameSetting.h"
#include "UI/Hud.h"
#include "UnityCompat/UnityGui.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		cs::Func<cs::String, double> Percent()
		{
			return cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); });
		}

		// Colour-blind safe: orange against sky blue (Okabe-Ito's, lifted for a dark screen), told apart under every common
		// form of colour blindness; neutral stays grey.
		Color Hostile(bool colourBlind, float alpha) { return colourBlind ? Color(1.0f, 0.55f, 0.1f, alpha) : Color(1.0f, 0.4f, 0.35f, alpha); }
		Color Friendly(bool colourBlind, float alpha) { return colourBlind ? Color(0.35f, 0.72f, 1.0f, alpha) : Color(0.45f, 0.95f, 0.6f, alpha); }
		Color Neutral(bool colourBlind, float alpha) { return colourBlind ? Color(0.86f, 0.86f, 0.86f, alpha) : Color(0.85f, 0.85f, 0.8f, alpha); }

		/// The stance palette into the HUD's own colours (its markers, edge arrows, target card and roster read them). Takes the
		/// palette rather than reading StancePalette(): it runs from inside that setting's own first construction (US-T320).
		void PaintHud(bool colourBlind)
		{
			UI::Hud::SetStanceColours(Hostile(colourBlind, 0.95f), Friendly(colourBlind, 0.95f), Neutral(colourBlind, 0.9f));
		}
	}

	const cs::List<cs::String>& PlayerOptions::PlayerGroups()
	{
		// US-T249: Key bindings right under Controls (the nav indents it there). US-T570: Graphics and Sound lead.
		static const cs::List<cs::String> value{ SettingGroups::Graphics, SettingGroups::Audio, Gameplay, SettingGroups::Multiplayer, SettingGroups::Controls,
			Input::KeyBindings::Group, SettingGroups::Interface, Accessibility };
		return value;
	}

	bool PlayerOptions::IsPlayerGroup(const cs::String& group) { return PlayerGroups().Contains(group); }

	// ---------------------------------------------------------------- Gameplay

	const cs::Ref<ChoiceSetting>& PlayerOptions::AutosaveInterval()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.autosaveInterval", Gameplay, "Autosave",
			cs::List<cs::String>{ "Off", "Every 2 minutes", "Every 5 minutes", "Every 10 minutes", "Every 15 minutes", "Every 30 minutes" }, 2,
			"How often the game saves itself while you play (it always saves on quitting and on the way back to the main menu).",
			"save autosave interval minutes");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::AutosaveSlots()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.autosaveSlots", Gameplay, "Autosaves kept",
			cs::List<cs::String>{ "1", "3", "5", "10" }, 1,
			"How many autosaves take turns before the oldest is overwritten (fewer kept leaves the extra ones on disk).",
			"save autosave slots kept rotation count");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::AutosaveOnDock()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.autosaveOnDock", Gameplay, "Autosave on docking", false,
			"An autosave each time the helm is cleared in at a port.", "save autosave dock docking port berth");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::AutosaveBeforeJump()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.autosaveBeforeJump", Gameplay, "Autosave before a jump", false,
			"An autosave right before a gate jump, an FTL charge or a trip to another star.", "save autosave jump gate ftl travel");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::DistanceUnits()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.distanceUnits", Gameplay, "Distances",
			cs::List<cs::String>{ "km, then AU", "Always km", "AU when far" }, 0,
			"How distances are written: km up close and AU across a system, km everywhere, or AU from a tenth of the Moon's "
			"distance out (a fight's ranges stay in km).",
			"units km kilometres kilometers au astronomical metric");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::Notices()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.notices", Gameplay, "Toasts and news",
			cs::List<cs::String>{ "Everything", "Important only", "Quiet" }, 0,
			"Everything; Important only keeps battles, captures and diplomacy on the news ticker and drops the autosave notice; "
			"Quiet hides the ticker too (the news screen keeps every report).",
			"notifications toast ticker news verbosity messages spam");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::ConfirmQuit()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.confirmQuit", Gameplay, "Confirm before quitting", true,
			"The pause menu's Main Menu and Quit ask before they leave the game.", "quit exit leave confirm ask");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::LayoutWithSituation()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.layoutWithSituation", Gameplay, "Switch HUD layout with the situation", false,
			"The HUD's drawers take the Combat layout in a fight and the Commerce layout docked, and your chosen layout again after (US-T441).",
			"hud layout preset drawers combat commerce docked situation automatic");
		return value;
	}

	const cs::Ref<RangeSetting>& PlayerOptions::CameraShake()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("gameplay.cameraShake", Gameplay, "Camera shake", 100, 0, 100, 5,
			"How hard the view shakes under thrust, a broadside and a hit (0% holds it still).", Percent(),
			"shake camera rumble motion kick shudder");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::PauseOnFocusLoss()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.pauseOnFocusLoss", Gameplay, "Pause when the window loses focus", false,
			"Switching away from the game opens the pause menu (single player only: a co-op session runs on).",
			"pause focus alt tab background minimize window");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::Clock()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.clockFormat", Gameplay, "Clock",
			cs::List<cs::String>{ "24-hour", "12-hour" }, 0,
			"How times of day are written on the HUD and the screens: 14:05, or 2:05 PM.",
			"clock time hour 12 24 am pm military format");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::Dates()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.dateOrder", Gameplay, "Date order",
			cs::List<cs::String>{ "Year-month-day", "Day/month/year", "Month/day/year" }, 0,
			"How dates are written on the HUD and the screens: 2026-11-24, 24/11/2026 or 11/24/2026.",
			"date order format day month year iso us european calendar");
		return value;
	}

	// ---------------------------------------------------------------- Accessibility

	const cs::Ref<RangeSetting>& PlayerOptions::UiScale()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.uiScale", Accessibility, "UI scale", 100, 75, 200, 5,
			"Every screen, menu and the HUD drawn larger or smaller.", Percent(), "interface size zoom big small hud menu dpi")
			->Applying([](double v) { GUI::uiScale = ScriptedRun() ? 1.0f : static_cast<float>(v / 100.0); });
		return value;
	}

	const cs::Ref<RangeSetting>& PlayerOptions::TextSize()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.textSize", Accessibility, "Text size", 100, 80, 150, 5,
			"Every piece of text drawn larger or smaller, on top of the UI scale.", Percent(), "font size big large readable")
			->Applying([](double v) { GUI::textScale = ScriptedRun() ? 1.0f : static_cast<float>(v / 100.0); });
		return value;
	}

	const cs::Ref<RangeSetting>& PlayerOptions::HudOpacity()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.hudOpacity", Accessibility, "HUD opacity", 100, 30, 100, 5,
			"How solid the HUD's overlays are drawn over the view (full screens and menus stay solid).", Percent(),
			"transparency transparent alpha fade overlay");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::StancePalette()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("access.stancePalette", Accessibility, "Stance colours",
			cs::List<cs::String>{ "Red, grey, green", "Colour-blind safe" }, 0,
			"Hostile, neutral and friendly in comms, markers, edge arrows and the roster: red, grey and green, or orange, grey and blue.",
			"colourblind colorblind color colour deuteranopia protanopia palette hostile friendly")
			->Applying([](int palette) { PaintHud(palette == 1); });
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::ReduceFlashes()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("access.reduceFlashes", Accessibility, "Reduce flashes and shake", false,
			"Gate and drive flashes, damage pulses at the screen's edge and the hyperspace toll's heartbeat kept faint and still, "
			"and menus no longer slide in.", "photosensitive epilepsy flash strobe motion shake vignette pulse");
		return value;
	}

	void PlayerOptions::Declare()
	{
		AutosaveInterval(); AutosaveSlots(); AutosaveOnDock(); AutosaveBeforeJump(); DistanceUnits(); Notices(); ConfirmQuit(); LayoutWithSituation(); CameraShake(); PauseOnFocusLoss(); Clock(); Dates();
		UiScale(); TextSize(); HudOpacity(); StancePalette(); ReduceFlashes();
	}

	// ---------------------------------------------------------------- reads

	float PlayerOptions::AutosaveSeconds()
	{
		static const float minutes[] = { 0, 2, 5, 10, 15, 30 };
		int i = AutosaveInterval()->Index;
		return i >= 0 && i < 6 ? minutes[i] * 60.0f : 300.0f;
	}

	int PlayerOptions::AutosavesKept()
	{
		static const int kept[] = { 1, 3, 5, 10 };
		int i = AutosaveSlots()->Index;
		return !ScriptedRun() && i >= 0 && i < 4 ? kept[i] : 3;
	}

	bool PlayerOptions::AutosavesOnDock() { return !ScriptedRun() && AutosaveOnDock()->Value; }

	bool PlayerOptions::AutosavesBeforeJump() { return !ScriptedRun() && AutosaveBeforeJump()->Value; }

	PlayerOptions::Units PlayerOptions::DistanceMode() { return static_cast<Units>(DistanceUnits()->Index); }

	bool PlayerOptions::WriteAu(double km, double autoFromKm)
	{
		switch (DistanceMode())
		{
		case Units::Kilometres: return false;
		case Units::Au: return km >= 38440.0; // a tenth of the Moon's distance: past any fight's ranges
		default: return km >= autoFromKm;
		}
	}

	cs::String PlayerOptions::KmText(double km)
	{
		if (km >= 1e9) return cs::Cat(cs::Fmt(km / 1e9, "0.00"), " billion km");
		if (km >= 1e7) return cs::Cat(cs::Fmt(km / 1e6, "0.0"), " million km");
		return cs::Cat(cs::Fmt(km, "N0"), " km");
	}

	PlayerOptions::NoticeLevel PlayerOptions::NoticeMode() { return static_cast<NoticeLevel>(Notices()->Index); }

	float PlayerOptions::HudAlpha() { return cs::Math::Max(0.3f, cs::Math::Min(1.0f, static_cast<float>(HudOpacity()->Value / 100.0))); }

	bool PlayerOptions::ColourBlindStances() { return StancePalette()->Index == 1; }

	Color PlayerOptions::HostileColor(float alpha) { return Hostile(ColourBlindStances(), alpha); }

	Color PlayerOptions::FriendlyColor(float alpha) { return Friendly(ColourBlindStances(), alpha); }

	Color PlayerOptions::NeutralColor(float alpha) { return Neutral(ColourBlindStances(), alpha); }

	Color PlayerOptions::WaryColor(float alpha) { return ColourBlindStances() ? Color(0.95f, 0.9f, 0.35f, alpha) : Color(1.0f, 0.78f, 0.35f, alpha); }

	bool PlayerOptions::Calm() { return ReduceFlashes()->Value; }

	float PlayerOptions::FlashScale() { return Calm() ? 0.2f : 1.0f; }

	float PlayerOptions::ShakeScale()
	{
		return ScriptedRun() ? 1.0f : cs::Math::Max(0.0f, cs::Math::Min(1.0f, static_cast<float>(CameraShake()->Value / 100.0)));
	}

	bool PlayerOptions::PausesOnFocusLoss() { return !ScriptedRun() && PauseOnFocusLoss()->Value; }

	ClockFormat PlayerOptions::ClockMode() { return !ScriptedRun() && Clock()->Index == 1 ? ClockFormat::TwelveHour : ClockFormat::TwentyFourHour; }

	DateOrder PlayerOptions::DateMode()
	{
		int i = Dates()->Index;
		return !ScriptedRun() && i >= 0 && i <= 2 ? static_cast<DateOrder>(i) : DateOrder::YearMonthDay;
	}

	cs::String PlayerOptions::Date(const cs::DateTime& t) { return DateText::Date(t, DateMode()); }

	cs::String PlayerOptions::Time(const cs::DateTime& t, bool seconds) { return DateText::Time(t, ClockMode(), seconds); }

	cs::String PlayerOptions::DateAndTime(const cs::DateTime& t, bool seconds, const char* gap)
	{
		return DateText::DateAndTime(t, DateMode(), ClockMode(), seconds, gap);
	}

	cs::String PlayerOptions::MonthDay(const cs::DateTime& t, bool year, bool padDay) { return DateText::MonthDay(t, DateMode(), year, padDay); }

	bool PlayerOptions::ScriptedRun()
	{
		static const bool value = []
		{
			if (FMindAtticHeadless::IsActive()) return true;
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false))
				if (token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase)
					|| token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
					return true;
			return false;
		}();
		return value;
	}
}
