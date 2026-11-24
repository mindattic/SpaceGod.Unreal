#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SettingsRegistry;
	class GameSetting;
}

namespace SpaceGod
{
	/// Puts the settings registry (U.SPC-T431) to work in the game: finds every setting the game's systems declare
	/// (classes marked SettingsSourceAttribute, in every assembly that uses Core) once, and binds them
	/// to the active profile, so each takes the value that profile remembers and each change is written there.
	///
	/// C++: no reflection, so the Runtime's settings sources are registered by hand with
	/// SettingsRegistry::AddDeclarationSource (RegisterRuntimeSources, in C#'s discovery order: the Runtime assembly's
	/// [SettingsSource] classes after Core's) before the first discovery.
	class GameSettingsHost
	{
	public:
		static const cs::Ref<Core::SettingsRegistry>& Registry();

		/// Discovers the declarations (the first time) and loads the active profile's values into them.
		static void Bind();

		/// US-T512: Content/Settings/shipped-defaults.json, the locked-in values that ship with the game as the defaults
		/// (laid over the declared ones at Bind, under the profile's values). No file: the declared defaults stand.
		static cs::String ShippedDefaultsPath();

		/// The file's key/text pairs; empty when there is no file. Throws cs::FormatException on a damaged one.
		static cs::Dictionary<cs::String, cs::String> ReadShippedDefaults();

		/// Locks in every setting under <paramref name="groupOrKey"/> (a key, a group, or every setting when empty) that is
		/// not at its default: its value is merged into the file and becomes its default, and the profile forgets it.
		/// Returns the keys locked in.
		static cs::List<cs::String> LockIn(const cs::String& groupOrKey = cs::String());

		/// Takes the settings under <paramref name="groupOrKey"/> (a key, a group, or every one when empty) out of the file,
		/// back to their factory defaults. Returns the keys unlocked.
		static cs::List<cs::String> Unlock(const cs::String& groupOrKey);

	private:
		static void RegisterRuntimeSources();
		static cs::List<cs::Ref<Core::GameSetting>> Select(const cs::String& groupOrKey);
		static void WriteShippedDefaults(const cs::Dictionary<cs::String, cs::String>& defaults);
		static inline bool discovered = false;
	};
}
