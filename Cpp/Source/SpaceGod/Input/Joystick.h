#pragma once

// US-T825: a joystick or HOTAS. One more device under InputState beside the keyboard, mouse and pad: up to eight raw axes
// (Unreal's Windows RawInput plugin, its GenericUSBController_Axis1..8 keys), filled once a frame by
// UnrealInputDevice::Feed and polled like the pad, no Enhanced Input assets (Input/UnrealInputDevice.h says why).
//
// Which axis flies what is a Controls setting per logical axis (pitch, yaw, roll, strafe x, strafe y, throttle): the
// source (None or Axis 1-8), inverted or not, an inner dead zone, an outer dead zone (0-10%, the last of the travel reads
// full) and a response exponent (1 straight, up to 3), all through one AxisCurve. So a HOTAS maps to the helm without code
// changes. A scripted run reads every axis straight: no dead zones, no curve, no invert.
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod::Input
{
	/// <summary>The helm's axes a joystick axis can fly.</summary>
	enum class JoyAxis { Pitch, Yaw, Roll, StrafeX, StrafeY, Throttle, Count };

	/// <summary>One frame of the joystick: each raw axis, -1..1 as the device reports it.</summary>
	class JoystickDevice final
	{
	public:
		static constexpr int AxisCount = 8;

		/// <summary>An axis has moved off 0 since the game started (a stick is connected): until then nothing reads, so a
		/// throttle source with no stick behind it never sets half throttle.</summary>
		bool present = false;
		float axes[AxisCount] = {};

		/// <summary>Every axis 0; <see cref="present"/> left as it is.</summary>
		void Clear();

		/// <summary>The one joystick (the axes of every connected one read into the same).</summary>
		static JoystickDevice& current();
	};

	/// <summary>The one response curve every joystick axis goes through.</summary>
	struct AxisCurve final
	{
		/// <summary><paramref name="raw"/> (-1..1; flipped with <paramref name="invert"/>) to -1..1: inside
		/// <paramref name="inner"/> reads 0, past 1 - <paramref name="outer"/> reads full, between is rescaled from 0 and
		/// raised to <paramref name="exponent"/>, the sign kept.</summary>
		static float Apply(float raw, float inner, float outer, float exponent, bool invert);
	};

	class Joystick final
	{
	public:
		/// <summary>Controls: which raw axis flies <paramref name="axis"/>: None, or Axis 1..8 (index 0 is None).</summary>
		static const cs::Ref<Core::ChoiceSetting>& Source(JoyAxis axis);
		/// <summary>Controls: the axis reads the other way.</summary>
		static const cs::Ref<Core::ToggleSetting>& Invert(JoyAxis axis);
		/// <summary>Controls: how far the axis moves off centre (off the stop, for the throttle) before it counts.</summary>
		static const cs::Ref<Core::RangeSetting>& InnerDeadZone(JoyAxis axis);
		/// <summary>Controls: the last of the travel, 0-10%, that reads full.</summary>
		static const cs::Ref<Core::RangeSetting>& OuterDeadZone(JoyAxis axis);
		/// <summary>Controls: the response exponent, 1 (straight) to 3 (fine near the centre).</summary>
		static const cs::Ref<Core::RangeSetting>& Exponent(JoyAxis axis);

		/// <summary>Builds every joystick setting (the declaration source GameSettingsHost registers).</summary>
		static void Declare();

		/// <summary>A stick is connected and <paramref name="axis"/> has a source.</summary>
		static bool Bound(JoyAxis axis);

		/// <summary><paramref name="axis"/> this frame through its curve: -1..1 (0 when not <see cref="Bound"/>); the
		/// throttle is a lever, 0..1, and -1 when not bound.</summary>
		static float Read(JoyAxis axis);
	};
}
