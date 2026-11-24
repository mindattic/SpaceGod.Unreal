#pragma once
// US-T562: Settings > Multiplayer, each machine's own (not among CoopSettings' shared groups): the name this player goes
// by, the port a hosted game listens on, how many may play, whether the hosts joined are kept, and whether the other
// players' names are drawn over their ships. Declared as every setting is (GameSettingsHost registers this source) and
// kept per profile. Defaults are what the main menu's co-op pages did before: an empty name (the account's user name),
// the engine's port 7777, CoopPeer::MaxPlayers seats and the recent hosts kept.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class TextSetting;
	class ToggleSetting;
}

namespace SpaceGod::Coop
{
	class CoopOptions
	{
	public:
		/// The most characters a player name holds (the main menu's own limit).
		static constexpr int MaxNameLength = 32;

		/// The name this player goes by in co-op; empty: -CoopName= or the account's user name. One trimmed line with no
		/// control characters, as the main menu keeps it.
		static const cs::Ref<Core::TextSetting>& PlayerName();

		static const cs::Ref<Core::RangeSetting>& HostPort();
		static const cs::Ref<Core::RangeSetting>& MaxPlayers();
		static const cs::Ref<Core::ToggleSetting>& RememberRecentHosts();
		static const cs::Ref<Core::ToggleSetting>& ShowPlayerNames();

		static void Declare();

		/// After the profile's values are bound: a profile with no player name yet takes the one the main menu kept under
		/// GameLaunch::CoopNameKey, so a name picked before this setting existed carries over.
		static void AfterBind();
	};
}
