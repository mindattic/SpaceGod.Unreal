// US-T620: see GameConsole.h.

#include "Console/GameConsole.h"

#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	const cs::Ref<ToggleSetting>& GameConsole::Enabled()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("dev.console", SettingGroups::Developer, "Console", true,
			"The ` key opens the developer console: type sg.Help for its commands.", "developer console command backtick tilde");
		return value;
	}

	void GameConsole::Declare()
	{
		Enabled();
	}
}
