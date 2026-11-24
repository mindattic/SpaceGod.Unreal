// US-T695: sg.Shot [caption], a screenshot of the game (UI in it) into the run's current MindAttic.Media.Capture preview
// set, made on first use ("SpaceGod shots" and the date; the tour's set when -demo -capture -preview made one), so a bug
// can be filed with a set. With the capture host running (-capture) the host takes the shot and copies it into the set;
// without it the shot is an Unreal screenshot under Saved/Shots, added to the set once written.

#include "CaptureRunner.h"
#include "MindAtticCapture.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	namespace Capture = MindAttic::Media::Capture;

	void Shot(const TArray<FString>& args, FOutputDevice& out)
	{
		const FDateTime now = FDateTime::Now();
		FString caption = FString::Join(args, TEXT(" ")).TrimStartAndEnd();
		if (caption.IsEmpty()) caption = FString::Printf(TEXT("Shot %s"), *now.ToString(TEXT("%H:%M:%S")));

		const FString set = SpaceGod::PreviewSet::Ensure(SpaceGod::PreviewSet::DatedTitle(TEXT("SpaceGod shots")));
		if (set.IsEmpty())
		{
			out.Log(TEXT("sg.Shot: no preview set (MindAttic.Media.Capture's agent is missing or failed); nothing taken."));
			return;
		}

		if (Capture::Ready() && Capture::Enabled())
		{
			const int32 id = Capture::Screenshot(TEXT("shot"), Capture::Data().Str(TEXT("caption"), caption), set, caption);
			out.Logf(TEXT("sg.Shot: '%s' requested into %s"), *caption, *set);
			const double deadline = FPlatformTime::Seconds() + 15.0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([id, caption, deadline](float)
			{
				bool ok = false;
				FString detail;
				if (!Capture::ScreenshotResult(id, ok, detail)) return FPlatformTime::Seconds() < deadline;
				if (ok) { UE_LOG(LogTemp, Log, TEXT("sg.Shot: '%s' saved as %s"), *caption, *detail); }
				else { UE_LOG(LogTemp, Error, TEXT("sg.Shot: '%s' failed: %s"), *caption, *detail); }
				return false;
			}), 0.1f);
			return;
		}

		// No capture host: an Unreal screenshot, written at the end of this frame, then copied into the set.
		const FString file = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Shots"),
			now.ToString(TEXT("%Y%m%d-%H%M%S-%s")) + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(file), true);
		FScreenshotRequest::RequestScreenshot(file, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		SpaceGod::PreviewSet::AddWhenWritten(set, file, TEXT("shot"), caption);
		out.Logf(TEXT("sg.Shot: '%s' requested into %s"), *caption, *set);
	}

	FAutoConsoleCommand GShotCommand(TEXT("sg.Shot"),
		TEXT("US-T695: sg.Shot [caption] takes a screenshot into the current preview set (made on first use) so a bug can be filed with a set; the log names the set's folder."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&Shot));
}
