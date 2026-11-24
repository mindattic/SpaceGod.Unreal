// US-T525: the dev menu's Developer category, one line per developer flag (Settings/DevFlags.h) with its ON / OFF. In co-op
// the flags are the host's (Coop/CoopSettings.cpp): a client's toggle is asked of the host.

#include "UI/CheatMenu.h"

#include "Settings/DevFlags.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		void RegisterDevFlagCheats()
		{
			CheatRegistry::DeclareCategory("Developer", 70, "test flags: invulnerable, one-hit kills, free FTL, infinite credits, a frozen AI (the host's in co-op)");
			for (const auto& flag : DevFlags::All())
			{
				CheatRegistry::Add("Developer", CheatEntry::Action(flag->Label, flag->Description, [flag](UnitySpaceApp&) { flag->Set(!flag->Value); })
					.WithState([flag](UnitySpaceApp&) { return cs::String(flag->Value ? "ON" : "OFF"); })
					.StaysOpen());
			}
		}

		CheatRegistration devFlagRegistration(&RegisterDevFlagCheats);
	}
}
