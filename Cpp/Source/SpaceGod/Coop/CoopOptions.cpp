// US-T562: Settings > Multiplayer (CoopOptions.h).

#include "Coop/CoopOptions.h"

#include "Coop/CoopPeer.h"

#include "ActiveProfile.h"
#include "GameLaunch.h"
#include "Persistence/Profiles.h"
#include "Settings/GameSetting.h"
#include "UnityCompat/UnityRuntime.h"

#include <exception>

using namespace SpaceGod::Core;

namespace SpaceGod::Coop
{
	namespace
	{
		/// The main menu's name rules: no control characters, nothing to trim at either end (empty is the account's name).
		bool IsPlayerName(const cs::String& name)
		{
			for (char c : name)
				if (static_cast<unsigned char>(c) < ' ') return false;
			return name == cs::Strings::Trim(name);
		}
	}

	const cs::Ref<TextSetting>& CoopOptions::PlayerName()
	{
		static const cs::Ref<TextSetting> value = cs::New<TextSetting>("multiplayer.playerName", SettingGroups::Multiplayer, "Player name", "", MaxNameLength,
			"The name the other players see you by; left empty, your account's user name.",
			cs::Func<bool, cs::String>([](const cs::String& name) { return IsPlayerName(name); }),
			"name nickname callsign co-op coop multiplayer player");
		return value;
	}

	const cs::Ref<RangeSetting>& CoopOptions::HostPort()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("multiplayer.hostPort", SettingGroups::Multiplayer, "Host port", 7777, 1024, 65535, 1,
			"The UDP port a game you host listens on (a -Port= on the command line still wins); the players joining add it as :port "
			"when it is not 7777.", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); }),
			"port udp host server listen network firewall");
		return value;
	}

	const cs::Ref<RangeSetting>& CoopOptions::MaxPlayers()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("multiplayer.maxPlayers", SettingGroups::Multiplayer, "Max players",
			CoopPeer::MaxPlayers, 2, CoopPeer::MaxPlayers, 1, "How many may play in a game you host, you included.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " players"); }),
			"players seats slots limit host co-op coop");
		return value;
	}

	const cs::Ref<ToggleSetting>& CoopOptions::RememberRecentHosts()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("multiplayer.rememberRecentHosts", SettingGroups::Multiplayer,
			"Remember recent hosts", true, "The Join page keeps the hosts you join, newest first, to pick again.",
			"recent hosts history join addresses remember");
		return value;
	}

	const cs::Ref<ToggleSetting>& CoopOptions::ShowPlayerNames()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("multiplayer.showPlayerNames", SettingGroups::Multiplayer,
			"Show player names over ships", true, "Each other player's name drawn over the ship they fly.",
			"names labels tags overhead ships players co-op coop");
		return value;
	}

	void CoopOptions::Declare()
	{
		PlayerName(); HostPort(); MaxPlayers(); RememberRecentHosts(); ShowPlayerNames();
	}

	void CoopOptions::AfterBind()
	{
		if (!PlayerName()->Value.empty()) return;
		cs::String kept;
		try
		{
			kept = cs::Strings::Trim(GameSettings::Setting(ActiveProfile::Current(), GameLaunch::CoopNameKey));
		}
		catch (const std::exception& e)
		{
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] The co-op name could not be read from the profile: ", e.what()));
			return;
		}
		if (!kept.empty() && !PlayerName()->Set(kept))
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] The co-op name '", kept, "' does not fit the Player name setting, so it is left empty."));
	}
}
