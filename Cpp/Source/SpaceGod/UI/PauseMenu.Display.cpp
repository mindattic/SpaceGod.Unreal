// US-T545: the Display page's own settings (DisplayExtras' group) under its resolution, window mode and VSync. Unlike those,
// they take effect the moment they change, like any setting; the Settings keys (SettingsKeys) reach them.

#include "UI/PauseMenu.h"

#include "DisplayExtras.h"
#include "GameSettingsHost.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void PauseMenu::DrawDisplayRows(Rect view)
	{
		auto shown = GameSettingsHost::Registry()->InGroup(DisplayExtras::Group);
		shownRowTops.Clear();
		for (int i = 0; i < shown.Count(); i++) shownRowTops.Add(i * SettingRowH);
		shownSettings = shown;
		displayRowShown = false;
		settingsViewH = view.height;
		navGroups.Clear(); // PgUp / PgDn turn no group here
		if (settingsPick >= shown.Count()) settingsPick = shown.Count() - 1;

		auto content = Rect(0, 0, view.width - 18, Mathf::Max(shown.Count() * SettingRowH, 1.0f));
		settingsScroll = GUI::BeginScrollView(view, settingsScroll, content);
		for (int i = 0; i < shown.Count(); i++)
		{
			float y = shownRowTops[i];
			if (i == settingsPick) GUI::DrawTexture(Rect(0, y, content.width, SettingRowH - 2), rowSelectedTexture);
			DrawSetting(shown[i], Rect(0, y, content.width, SettingRowH), false);
		}
		GUI::EndScrollView();
	}

	void PauseMenu::DrawDisplayReset(Rect r)
	{
		cs::List<cs::Ref<GameSetting>> resettable;
		for (const auto& s : shownSettings)
			if (!s->IsDefault()) resettable.Add(s);
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && resettable.Count() > 0;
		if (GUI::Button(r, "Reset Display", button))
			for (const auto& s : resettable) s->Reset();
		GUI::enabled = enabled;
	}
}
