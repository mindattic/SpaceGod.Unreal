#pragma once

// US-T620: the in-game developer console (US-T603), opened by the Console key action (the backtick by default) while
// Options > Developer > Console is on. The setting is declared here and registered by GameSettingsHost; it is not one of
// Core's DevFlags (it changes nothing in the simulation, so it neither badges the HUD nor is shared in co-op).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ToggleSetting;
	class ChoiceSetting;
}

namespace SpaceGod
{
	class GameConsole final
	{
	public:
		/// <summary>Developer: the ` key opens the console. On by default.</summary>
		static const cs::Ref<Core::ToggleSetting>& Enabled();

		/// <summary>Developer: the engine stat overlay shown in the viewport (Off, FPS, Unit, Unit graph).</summary>
		static const cs::Ref<Core::ChoiceSetting>& StatsOverlay();

		/// <summary>Developer: how much LogSpaceGod writes (Normal is the category's boot default).</summary>
		static const cs::Ref<Core::ChoiceSetting>& LogVerbosity();

		/// <summary>The engine's console (USpaceGodConsole) is showing, its input line or its scrollback.</summary>
		static bool IsOpen();

		/// <summary>Builds the console's settings (the declaration source GameSettingsHost registers).</summary>
		static void Declare();
	};
}
