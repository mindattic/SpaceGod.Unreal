#pragma once

// US-T250: the gamepad. One more device under InputState beside the keyboard and mouse (Input/InputDevice.h): the sticks,
// the triggers and the buttons of an Xbox-style pad, filled once a frame by UnrealInputDevice::Feed (Unreal's Gamepad_*
// keys) and read by InputState::Poll into the same fields the keys feed, so nothing downstream knows which device
// flew. The left stick thrusts and strafes (swims, in command), the right stick aims (the string aim's look, US-T168,
// as the mouse turns it) or looks about in command, the triggers fire, the bumpers roll, the face buttons boost, drift,
// flip and lock; the d-pad shares the reactor at the controls and steps time and ships in command.
//
// Menus (MenuMode, set by the app while a menu, panel or screen is up, and on the main menu): the pad is the GUI's
// instead (UnityCompat/GuiPadNav): d-pad or left stick moves a visible focus between the controls, A presses, B (or Menu)
// is Esc, the right stick scrolls. The game then reads nothing else of the pad.
//
// US-T437: the pad's buttons, triggers and stick directions are bindings of the actions (Input/KeyBindings.h), beside
// the keys and the mouse: each pad default of before (fire RT, second trigger LT, roll LB/RB, boost A, drift B, flip X,
// lock/focus Y, flight assist RS, embody LS, pause Menu, help View) is that action's default binding now, rebound under
// Settings > Controls > Key bindings like any other, and so are the d-pad's reactor/time/ship steps (US-T629). B's
// back-out in menus stays fixed. The dead zone, the aim sensitivity, an inverted aim, the vibration strength (US-T557,
// the player controller's force feedback scale), swapped sticks and an inverted move (US-T629) are Controls settings. The
// Help screen shows the pad's buttons when the pad was used last (LastUsed).
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Input/InputDevice.h"
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod::Input
{
	/// <summary>One frame of the pad: the sticks (raw, -1..1, y up: pushed away from the player is +), the triggers (0..1)
	/// and every button as a <see cref="ButtonControl"/> (a trigger is also a button, pulled past half way).</summary>
	class GamepadDevice final
	{
	public:
		/// <summary>False when there is nothing to read: everything then reads idle.</summary>
		bool present = false;
		Vector2 leftStick, rightStick;
		float leftTrigger = 0, rightTrigger = 0;

		const ButtonControl& operator[](PadButton button) const;
		ButtonControl& operator[](PadButton button);

		/// <summary>Everything idle and released; <see cref="present"/> left as it is.</summary>
		void Clear();

		/// <summary>Any button went down this frame, or a stick or trigger is pushed well past its dead zone.</summary>
		bool Touched() const;

		/// <summary>The one pad (the first connected; a second pad reads into the same).</summary>
		static GamepadDevice& current();

	private:
		ButtonControl buttons[static_cast<int>(PadButton::Count)];
		ButtonControl none;
	};

	class Gamepad final
	{
	public:
		/// <summary>Controls: the sticks' dead zone (radial; what is inside reads 0, the rest is rescaled to start from 0).</summary>
		static const cs::Ref<Core::RangeSetting>& DeadZone();
		/// <summary>Controls: how fast the right stick turns the aim and the look at full deflection (180 degrees a second at 1x).</summary>
		static const cs::Ref<Core::RangeSetting>& AimSensitivity();
		/// <summary>Controls: pushing the right stick forward looks (and pitches) down instead of up.</summary>
		static const cs::Ref<Core::ToggleSetting>& InvertAim();
		/// <summary>Controls (US-T629): the right stick moves and the left aims.</summary>
		static const cs::Ref<Core::ToggleSetting>& SwapSticks();
		/// <summary>Controls (US-T629): pushing the move stick forward moves back (and back forward).</summary>
		static const cs::Ref<Core::ToggleSetting>& InvertMove();
		/// <summary>Controls (US-T557): the scale on any force feedback the pad plays, 0 (off) to 1.</summary>
		static const cs::Ref<Core::RangeSetting>& VibrationStrength();
		/// <summary>The force feedback scale in effect: the setting, or 1 in a scripted run.</summary>
		static float Vibration();
		/// <summary>Controls (US-T699): whose button names the pad's prompts show: Auto (by the pad connected), Xbox or
		/// PlayStation.</summary>
		static const cs::Ref<Core::ChoiceSetting>& Prompts();
		/// <summary>Controls (US-T633): the sticks' response curve after the dead zone: Linear (as before), Smooth (the
		/// deflection squared) or Precise (cubed), the direction kept. A view setting, each machine's own; a scripted run
		/// keeps Linear.</summary>
		static const cs::Ref<Core::ChoiceSetting>& ResponseCurve();
		/// <summary>Controls (US-T827): the sticks' outer dead zone, 0 to 10%: the last of the travel already reads full.
		/// A scripted run keeps 0.</summary>
		static const cs::Ref<Core::RangeSetting>& OuterDeadZone();
		/// <summary>Controls (US-T827): the sticks' response exponent, 1 (straight) to 3, flown with the Custom
		/// <see cref="ResponseCurve"/>.</summary>
		static const cs::Ref<Core::RangeSetting>& Exponent();
		/// <summary>US-T827: the exponent the sticks fly: Linear 1, Smooth 2, Precise 3, Custom <see cref="Exponent"/>; 1 in
		/// a scripted run.</summary>
		static float CurveExponent();
		/// <summary>Controls (US-T633): the triggers' dead zone, 0 (as before) to 30%: a pull inside it reads 0, the rest is
		/// rescaled to start from 0. A scripted run keeps 0.</summary>
		static const cs::Ref<Core::RangeSetting>& TriggerDeadZone();
		/// <summary>US-T633: a raw trigger (0..1) through <see cref="TriggerDeadZone"/>, rescaled, 0..1.</summary>
		static float TriggerValue(float raw);
		/// <summary>US-T699: sets <see cref="ShownPadPrompts"/> from <see cref="Prompts"/> and <see cref="ConnectedPadName"/>
		/// (Auto: PlayStation when the name says DualSense, DualShock, PlayStation, PS4 or PS5); a scripted run keeps Xbox.
		/// Called each frame after the pad is fed.</summary>
		static void ResolvePrompts();
		/// <summary>US-T699: the last pad used, as the platform names its hardware (empty until one is); filled by
		/// UnrealInputDevice.</summary>
		static inline std::string ConnectedPadName;
		/// <summary>Builds every pad setting (the declaration source GameSettingsHost registers).</summary>
		static void Declare();

		/// <summary>A raw stick through the dead zone (radial, rescaled), the <see cref="OuterDeadZone"/> and the
		/// <see cref="CurveExponent"/> (one AxisCurve on its length), the result's length 0..1.</summary>
		static Vector2 Stick(Vector2 raw);
		/// <summary>The move stick through the dead zone: the left (the right with <see cref="SwapSticks"/>), its y flipped
		/// with <see cref="InvertMove"/>; a scripted run keeps the left, unflipped.</summary>
		static Vector2 MoveStick();
		/// <summary>The aim stick (the right, or the left with <see cref="SwapSticks"/>) as a mouse delta this frame (the counts the mouse look is tuned for): a rate, squared for
		/// fine aim near the centre, so holding the stick over swings the look on as a mouse moved on would.</summary>
		static Vector2 AimDelta(float dt);

		/// <summary>Degrees a second the look turns at full deflection, at 1x <see cref="AimSensitivity"/>.</summary>
		static constexpr float FullDeflectionDegS = 180.0f;

		/// <summary>The pad is the GUI's this frame (a menu, panel or screen is up; the main menu): set by the app's actor
		/// after each Update, read by InputState::Poll and UnityCompat/GuiPadNav the frame after.</summary>
		static inline bool MenuMode = false;

		/// <summary>The pad was the last device touched (the keyboard or mouse used since clears it): the focus highlight and
		/// the Help screen's pad buttons show only then.</summary>
		static inline bool LastUsed = false;
	};
}
