// US-T517: Settings' Developer page (PauseMenu.h): the Developer group, each Tuning group, then every other design group.

#include "UI/PauseMenu.h"

#include "Combat/PilotTuning.h"
#include "Combat/ShipCatalog.h"
#include "GameSettingsHost.h"
#include "PlayerOptions.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "Settings/Tuning.h"
#include "UI/UiInput.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <exception>
#include <optional>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// US-T518: a lock-in or unlock asked for and waiting on the confirm line, and what the last one did.
		struct LockAsk
		{
			bool unlock = false;
			cs::List<cs::String> targets; // each a group, a key, or empty for every setting
			cs::String question;
		};
		std::optional<LockAsk> asked;
		cs::String lockNotice;

		const char* BenchReminder = "  Simulation values moved: the bench digests move with them, so re-record Bench/expected-digests.json.";

		bool AnyMovesDigests(const cs::List<cs::Ref<GameSetting>>& settings)
		{
			for (const auto& s : settings)
				if (GameSettingsHost::MovesDigests(s->Key)) return true;
			return false;
		}

		/// Asks to lock in what of <paramref name="settings"/> is changed (<paramref name="targets"/> names them to LockIn).
		void AskLockIn(const cs::List<cs::Ref<GameSetting>>& settings, const cs::List<cs::String>& targets, const cs::String& where)
		{
			cs::List<cs::Ref<GameSetting>> changed;
			for (const auto& s : settings)
				if (!s->IsDefault()) changed.Add(s);
			if (changed.Count() == 0) return;
			lockNotice = "";
			asked = LockAsk{ false, targets, cs::Cat("Lock in ", changed.Count(), changed.Count() == 1 ? " change" : " changes", " ", where,
				" as the shipped defaults (", GameSettingsHost::ShippedDefaultsPath(), ")?", AnyMovesDigests(changed) ? BenchReminder : "") };
		}

		void AskUnlock(const cs::Ref<GameSetting>& setting)
		{
			lockNotice = "";
			asked = LockAsk{ true, { setting->Key }, cs::Cat("Unlock ", setting->Label, ", back to its factory default?",
				GameSettingsHost::MovesDigests(setting->Key) ? BenchReminder : "") };
		}

		void AnswerLock(bool yes)
		{
			if (!asked.has_value()) return;
			LockAsk ask = *asked;
			asked.reset();
			if (!yes) return;
			try
			{
				cs::List<cs::String> done;
				for (const auto& t : ask.targets)
					for (const auto& key : ask.unlock ? GameSettingsHost::Unlock(t) : GameSettingsHost::LockIn(t)) done.Add(key);
				bool moves = false;
				for (const auto& key : done) moves |= GameSettingsHost::MovesDigests(key);
				lockNotice = cs::Cat(done.Count(), done.Count() == 1 ? " setting " : " settings ", ask.unlock ? "unlocked." : "locked in.", moves ? BenchReminder : "");
			}
			catch (const std::exception& e)
			{
				lockNotice = cs::Cat("Nothing written: ", e.what());
			}
		}
	}

	bool PauseMenu::DeveloperShown()
	{
#if UE_BUILD_SHIPPING
		static const bool dev = FParse::Param(FCommandLine::Get(), TEXT("dev"));
		return dev;
#else
		return true;
#endif
	}

	cs::List<cs::String> PauseMenu::DeveloperGroups()
	{
		auto groups = GameSettingsHost::Registry()->Groups();
		cs::List<cs::String> order;
		if (groups.Contains(SettingGroups::Developer)) order.Add(SettingGroups::Developer);
		for (const auto& g : groups)
			if (Tuning::IsTuningGroup(g)) order.Add(g);
		for (const auto& g : groups)
			if (!order.Contains(g) && !PlayerOptions::IsPlayerGroup(g)) order.Add(g);
		return order;
	}

	void PauseMenu::OpenDeveloper()
	{
		static const bool declared = []
		{
			SettingsRegistry::DiscoverLoadedDeclarations();
			for (auto c : ShipCatalog::Classes()) PilotTuning::Of(c); // the flight knobs are declared on first use (US-T535)
			return true;
		}();
		(void)declared;
		auto groups = DeveloperGroups();
		ShowGroup(groups.Count() > 0 ? groups[0] : cs::String());
		navScroll = Vector2::zero();
		asked.reset();
		lockNotice = "";
		page = Page::Developer;
	}

	void PauseMenu::DrawDeveloper()
	{
		const auto& registry = GameSettingsHost::Registry();
		auto groups = DeveloperGroups();
		if (group.empty() || !groups.Contains(group)) group = groups.Count() > 0 ? groups[0] : cs::String();

		float w = Mathf::Min(1120.0f, Screen::width - 40.0f), h = Mathf::Min(640.0f, Screen::height - 40.0f);
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "DEVELOPER", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 340, 18), "Flags and tuning knobs, live  ·  Esc to go back to Settings", small);

		GUI::Label(Rect(r.xMax() - 320, r.y + 16, 60, 24), "Search", text);
		GUI::SetNextControlName("SettingsSearch"); // the keys are Settings' (SettingsKeys), Ctrl+F included
		cs::String typed = GUI::TextField(Rect(r.xMax() - 260, r.y + 14, 240, 26), search, 48, field);
		if (typed != search)
		{
			search = typed;
			settingsScroll = Vector2::zero();
			settingsPick = -1;
		}
		bool searching = !cs::Strings::IsNullOrWhiteSpace(search);

		const float navW = 220, step = 34;
		float top = r.y + 70, bottom = r.yMax() - 92;
		{
			auto navView = Rect(r.x + 20, top, navW, bottom - top);
			float navH = groups.Count() * step;
			float bw = navH > navView.height ? navW - 18 : navW;
			navScroll = GUI::BeginScrollView(navView, navScroll, Rect(0, 0, bw, Mathf::Max(navH, 1.0f)));
			float ny = 0;
			for (const auto& g : groups)
			{
				NavGroup(g, Rect(0, ny, bw, 30), searching);
				ny += step;
			}
			GUI::EndScrollView();
		}

		float px = r.x + 20 + navW + 20, pw = r.xMax() - 20 - px;
		cs::List<cs::Ref<GameSetting>> shown;
		if (searching)
		{
			for (const auto& s : registry->Search(search))
				if (groups.Contains(s->Group)) shown.Add(s);
		}
		else if (!group.empty()) shown = registry->InGroup(group);
		cs::String heading = searching ? cs::Cat(shown.Count(), " found for \"", cs::Strings::Trim(search), "\"") : !group.empty() ? group : cs::String("Nothing to set");
		GUI::Label(Rect(px, top, pw, 22), heading, text);
		float listTop = top + 28;

		auto view = Rect(px, listTop, pw, bottom - listTop);
		shownRowTops.Clear();
		for (int i = 0; i < shown.Count(); i++) shownRowTops.Add(i * SettingRowH);
		auto content = Rect(0, 0, pw - 18, Mathf::Max(shown.Count() * SettingRowH, 1.0f));
		shownSettings = shown;
		displayRowShown = false;
		settingsViewH = view.height;
		navGroups = groups;
		if (settingsPick >= shown.Count()) settingsPick = shown.Count() - 1;
		settingsScroll = GUI::BeginScrollView(view, settingsScroll, content);
		const float markW = 80, textW = content.width - 250 - 60 - 24; // DrawSetting's text column
		for (int i = 0; i < shown.Count(); i++)
		{
			float y = shownRowTops[i];
			if (i == settingsPick) GUI::DrawTexture(Rect(0, y, content.width, SettingRowH - 2), rowSelectedTexture);
			DrawSetting(shown[i], Rect(0, y, content.width, SettingRowH), searching);
			const char* mark = !shown[i]->IsDefault() ? "changed" : registry->IsLockedIn(shown[i]->Key) ? "locked in" : nullptr;
			if (mark != nullptr) GUI::Label(Rect(textW - markW, y + 4, markW, 20), mark, small);
			if (registry->IsLockedIn(shown[i]->Key) && GUI::Button(Rect(textW - markW - 76, y + 2, 70, 24), "Unlock", button)) // US-T518
				AskUnlock(shown[i]);
		}
		if (shown.Count() == 0)
			GUI::Label(Rect(4, 4, content.width - 8, 40), searching ? "No setting matches that." : "No system has declared a setting yet.", small);
		GUI::EndScrollView();

		float ay = r.yMax() - 82, by = r.yMax() - 46;
		cs::List<cs::Ref<GameSetting>> resettable;
		cs::List<cs::String> resettableKeys;
		for (const auto& s : shown)
			if (!s->IsDefault())
			{
				resettable.Add(s);
				resettableKeys.Add(s->Key);
			}
		bool anyChanged = false;
		for (const auto& s : registry->All()) anyChanged |= !s->IsDefault();
		auto lockShown = [&]
		{
			if (searching) AskLockIn(shown, resettableKeys, "found by the search");
			else AskLockIn(shown, { group }, cs::Cat("in ", group));
		};
		auto lockAll = [&] { AskLockIn(registry->All(), { cs::String() }, "on every tab, the player's included"); };

		bool enabled = GUI::enabled;
		GUI::enabled = enabled && resettable.Count() > 0;
		if (GUI::Button(Rect(px, ay, 220, 30), searching ? cs::String("Reset these") : cs::Cat("Reset ", group), button))
			for (const auto& s : resettable) s->Reset();
		if (GUI::Button(Rect(px + 230, ay, 220, 30), searching ? cs::String("Lock in these") : cs::String("Lock in this group"), button)) lockShown(); // US-T518
		GUI::enabled = enabled && anyChanged;
		if (GUI::Button(Rect(px + 460, ay, 220, 30), "Lock in all changes", button)) lockAll();
		GUI::enabled = enabled;

		float lineW = r.xMax() - 20 - 110 - 10 - px;
		if (asked.has_value())
		{
			GUI::Label(Rect(px, by - 2, lineW - 190, 36), asked->question, small);
			if (GUI::Button(Rect(px + lineW - 180, by, 85, 30), "Yes (Y)", button)) AnswerLock(true);
			if (GUI::Button(Rect(px + lineW - 90, by, 85, 30), "No (N)", button)) AnswerLock(false);
		}
		else
			GUI::Label(Rect(px, by + (lockNotice.empty() ? 6 : -2), lineW, lockNotice.empty() ? 18 : 36), !lockNotice.empty() ? lockNotice
				: cs::String("Up / Down pick  ·  Left / Right change  ·  Enter flips  ·  Del resets  ·  L locks in, Shift+L all  ·  U unlocks  ·  PgUp / PgDn group  ·  Ctrl+F search"), small);
		if (GUI::Button(Rect(r.xMax() - 20 - 110, by, 110, 30), "Back", button))
		{
			asked.reset();
			search = "";
			Back();
		}

		// US-T518: the lock-in keys, once a frame, never while a field is typed in (Settings' own keys are SettingsKeys).
		if (Event::current().type != EventType::Repaint || !UiInput::present() || textEditing != nullptr || GUI::GetNameOfFocusedControl() == "SettingsSearch") return;
		auto pressed = [](KeyCode k) { return UiInput::WasPressedThisFrame(k); };
		if (asked.has_value())
		{
			if (pressed(KeyCode::Y)) AnswerLock(true);
			else if (pressed(KeyCode::N)) AnswerLock(false);
			return;
		}
		if (pressed(KeyCode::L))
		{
			if (UiInput::IsPressed(KeyCode::LeftShift) || UiInput::IsPressed(KeyCode::RightShift)) lockAll();
			else lockShown();
		}
		else if (pressed(KeyCode::U) && settingsPick >= 0 && settingsPick < shown.Count() && registry->IsLockedIn(shown[settingsPick]->Key))
			AskUnlock(shown[settingsPick]);
	}
}
