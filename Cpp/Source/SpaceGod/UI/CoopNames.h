#pragma once
// US-T564: in co-op, each other player's name drawn over the ship they fly, while Settings > Multiplayer's
// "Show player names over ships" is on.

#include "Cs/Cs.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::UI::CoopNames
{
	/// <summary>Draws <paramref name="name"/> just above the hull the player in <paramref name="seat"/> holds (the helm or
	/// a held seat), in their seat colour; nothing when the setting is off, the hull is this machine's own or doesn't show,
	/// it is off screen, or the tag would sit under the HUD. True when the name was drawn.</summary>
	bool DrawOverShip(UnitySpaceApp& app, int seat, const cs::String& name);
}
