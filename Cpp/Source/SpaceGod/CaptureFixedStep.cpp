#include "CaptureFixedStep.h"
#include "SpaceGodLog.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"

namespace SpaceGod::CaptureFixedStep
{
	int32 Fps()
	{
		static const int32 fps = []
		{
			const TCHAR* line = FCommandLine::Get();
			int32 rate = 60;
			if (!FParse::Value(line, TEXT("-capture-fixed="), rate) && !FParse::Param(line, TEXT("capture-fixed"))) return 0;
			// The capture host's own range (CaptureOptions.Fps).
			return FMath::Clamp(rate, 1, 240);
		}();
		return fps;
	}

	void Install()
	{
		static bool installed = false;
		const int32 fps = Fps();
		if (installed || fps <= 0) return;
		installed = true;
		const double step = 1.0 / fps;
		UE_LOG(LogSpaceGod, Display, TEXT("[capture] fixed step: %d frames/s, %.4f s of game time per frame, never faster than real time"), fps, step);
		// Before the engine works out the frame's delta (UEngine::UpdateTimeAndHandleMaxTickRate), every frame, so nothing
		// that resets FApp's time step undoes it.
		FCoreDelegates::OnBeginFrame.AddLambda([step]
		{
			FApp::SetUseFixedTimeStep(true);
			FApp::SetFixedDeltaTime(step);
		});
		// A frame done sooner than 1/fps of real time waits the rest, so the frame feed, which reads back on the wall
		// clock, sees each frame once.
		FCoreDelegates::OnEndFrame.AddLambda([step]
		{
			static double due = 0;
			const double now = FPlatformTime::Seconds();
			if (due > now) FPlatformProcess::SleepNoStats(static_cast<float>(due - now));
			due = FMath::Max(due, now) + step;
		});
	}
}
