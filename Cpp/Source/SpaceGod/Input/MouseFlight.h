#pragma once

// US-T736: mouse flight, as the player sets it under Controls. Aim: the string aim a launched craft has always flown with
// (free aim on a light craft, the nose a little ahead on a heavy one). Virtual stick (Freelancer, FreeSpace 2): the mouse
// moves a cursor about the screen's centre and its offset is the nose's turn-rate demand, through a dead zone and one
// response curve, read the same frame. Auto-level eases the roof back to the system's north when roll is let go.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod::Input
{
	class MouseFlight final
	{
	public:
		/// <summary>The modes, in the setting's order.</summary>
		enum class Mode { Aim = 0, VirtualStick = 1 };

		/// <summary>Controls: the settings, touched by GameSettingsHost.</summary>
		static void Declare();

		static const cs::Ref<Core::ChoiceSetting>& ModeSetting();
		static const cs::Ref<Core::RangeSetting>& DeadZone();
		static const cs::Ref<Core::RangeSetting>& FullAt();
		static const cs::Ref<Core::RangeSetting>& Exponent();
		static const cs::Ref<Core::ToggleSetting>& AutoLevel();
		static const cs::Ref<Core::RangeSetting>& AutoLevelSeconds();

		static bool VirtualStick();

		/// <summary>Auto-level's time constant, s, or 0 with it off.</summary>
		static double AutoLevelS();

		/// <summary>Moves the cursor by this frame's mouse (already shaped and sensitivity-scaled, deg of look) and returns
		/// the stick it makes: x to starboard, y to the roof, length 0..1.</summary>
		static Vector2 Step(Vector2 lookDeg);

		/// <summary>The stick for a cursor at <paramref name="c"/> (half-screens from the centre): nothing inside the dead zone,
		/// full rate at <see cref="FullAt"/>, the curve's exponent between.</summary>
		static Vector2 Shape(Vector2 c);

		/// <summary>Where the cursor sits, half-screens from the centre (x right, y up), for the HUD.</summary>
		static Vector2 Cursor() { return cursor; }

		/// <summary>Back to the centre (taking a seat, leaving one).</summary>
		static void Centre() { cursor = Vector2::zero(); }

		/// <summary>Degrees of mouse look that move the cursor a half-screen.</summary>
		static constexpr float LookDegPerHalfScreen = 60.0f;

	private:
		static inline Vector2 cursor = Vector2::zero();
	};
}
