#pragma once
// US-T524 (not a C# source): the Developer screen's test flags (US-T501).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ToggleSetting;

	/// <summary>
	/// Switches for testing a fight, a trip or the economy by hand. Every flag is off by default, so with none on the
	/// simulation is unchanged. A flag acts on what the player holds (their own ships and craft), never on the AI's
	/// ships unless it says so. Declared here; each system reads the flag where it applies.
	/// </summary>
	// [SettingsSource]: the settings registry finds these by touching each accessor (no reflection in C++).
	class SPACEGODCORE_API DevFlags
	{
	public:
		static const cs::Ref<ToggleSetting>& Invulnerable();
		static const cs::Ref<ToggleSetting>& OneHitKills();
		static const cs::Ref<ToggleSetting>& NoWeaponCooldown();
		static const cs::Ref<ToggleSetting>& NoHeat();
		static const cs::Ref<ToggleSetting>& UnlimitedFtlFuel();
		static const cs::Ref<ToggleSetting>& InstantFtlSpool();
		static const cs::Ref<ToggleSetting>& RevealEveryStar();
		static const cs::Ref<ToggleSetting>& InfiniteCredits();
		static const cs::Ref<ToggleSetting>& AiIgnoresPlayer();
		static const cs::Ref<ToggleSetting>& FreezeAi();

		/// <summary>Every flag, in the order the Developer screen lists them.</summary>
		static const cs::List<cs::Ref<ToggleSetting>>& All();

		/// <summary>Whether any flag is on (the HUD and co-op say so when one is).</summary>
		static bool AnyOn();
	};
}
