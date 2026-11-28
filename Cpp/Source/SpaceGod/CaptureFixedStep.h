#pragma once

#include "CoreMinimal.h"

// A fixed-step capture run (US-T944, docs/CAPTURE.md "The 1920x1080 tour video"): -capture-fixed or -capture-fixed=<fps>
// (60 when no rate is given). The game then advances exactly 1/fps seconds per rendered frame (FApp's fixed time step,
// which UnitySpaceAppActor::Tick and so the Unity clock, the sim and the tour read), and never runs faster than real
// time. The capture plugin reads the same flag and films every frame once, in order (MindAttic.Media.Capture's frame
// feed fixed step). Without the flag nothing changes.
namespace SpaceGod::CaptureFixedStep
{
	/// The fixed rate from the command line, or 0 without -capture-fixed.
	int32 Fps();

	/// Hooks the engine's frame once (from the module's startup): every frame runs at the fixed delta and waits out
	/// what is left of 1/fps of real time. Does nothing without -capture-fixed.
	void Install();
}
