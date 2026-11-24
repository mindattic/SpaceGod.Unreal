// US-T524: the Developer screen's test flags (DevFlags.h).

#include "Settings/DevFlags.h"

#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	namespace
	{
		cs::Ref<ToggleSetting> Flag(const char* key, const char* label, const char* description, const char* keywords)
		{
			return cs::New<ToggleSetting>(key, SettingGroups::Developer, label, false, description, cs::Cat("developer cheat test ", keywords));
		}
	}

	const cs::Ref<ToggleSetting>& DevFlags::Invulnerable()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.invulnerable", "Invulnerable",
			"Your ships and craft take no damage: hits land, but hull, shields and modules stay as they were.",
			"god mode invincible immortal damage");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::OneHitKills()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.oneHitKills", "One-hit kills",
			"Any hit from your ships or craft destroys what it strikes.", "instakill instant kill damage");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::NoWeaponCooldown()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.noWeaponCooldown", "No weapon cooldown",
			"Your weapons are ready again the moment they fire: no reload, recharge or cooldown.", "reload recharge rate of fire");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::NoHeat()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.noHeat", "No heat",
			"Your ships build up no heat from firing or drives, so they never overheat or slow down from it.",
			"heat radiators overheat temperature");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::UnlimitedFtlFuel()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.unlimitedFtlFuel", "Unlimited FTL fuel",
			"Your FTL jumps burn no fuel: the tank stays full however far you go.", "ftl fuel jump range tank");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::InstantFtlSpool()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.instantFtlSpool", "Instant FTL spool",
			"Your FTL drive spools up and cools down at once, so a jump starts the moment you order it.",
			"ftl spool charge cooldown jump drive");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::RevealEveryStar()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.revealEveryStar", "Reveal every star",
			"Every star and void station is discovered and on the charts, however far from where you have been.",
			"discover chart map reveal fog undiscovered");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::InfiniteCredits()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.infiniteCredits", "Infinite credits",
			"You can buy, build and pay for anything: your purse never runs short and never goes down.",
			"money credits cash purse economy trade");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::AiIgnoresPlayer()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.aiIgnoresPlayer", "AI ignores the player",
			"The AI's ships never target or attack you; they still fight one another.", "ai hostile aggro target peaceful");
		return setting;
	}

	const cs::Ref<ToggleSetting>& DevFlags::FreezeAi()
	{
		static const cs::Ref<ToggleSetting> setting = Flag("dev.freezeAi", "Freeze AI",
			"The AI's ships stop where they are and do nothing: no moving, firing or decisions.", "ai freeze stop pause idle");
		return setting;
	}

	const cs::List<cs::Ref<ToggleSetting>>& DevFlags::All()
	{
		static const cs::List<cs::Ref<ToggleSetting>> value
		{
			Invulnerable(), OneHitKills(), NoWeaponCooldown(), NoHeat(), UnlimitedFtlFuel(), InstantFtlSpool(), RevealEveryStar(),
			InfiniteCredits(), AiIgnoresPlayer(), FreezeAi(),
		};
		return value;
	}

	bool DevFlags::AnyOn()
	{
		for (const auto& flag : All())
			if (flag->Value) return true;
		return false;
	}
}
