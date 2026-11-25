// US-T763: sg.IndexKitParts writes Data/ModularParts.json, every modular part of the third-party kits (Ultimate
// Spaceships Creator, Star Sparrow, Space Station Creator) by role with its bounds, attach sockets and style tags, for
// the seeded hull assembler. The parts are read from the packs' own FBX files under Cpp/SourceArt/ThirdParty (never
// committed) by tools/index_kit_parts.py in Blender, run here as a hidden, low-priority child process; the game thread
// only reads its output four times a second. Without the packs nothing is written and the game keeps its procedural models.
//   sg.IndexKitParts [blender=<path to blender.exe>]

#include "SpaceGodLog.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#include <memory>

namespace
{
	FString RepoRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."))); }

	/// The newest Blender under Program Files\Blender Foundation, as tools/import-usc.ps1 finds it.
	FString FindBlender()
	{
		const FString root = TEXT("C:/Program Files/Blender Foundation");
		TArray<FString> dirs;
		IFileManager::Get().FindFiles(dirs, *(root / TEXT("*")), /*Files*/ false, /*Directories*/ true);
		dirs.Sort([](const FString& a, const FString& b) { return a > b; });
		for (const FString& d : dirs)
		{
			const FString exe = root / d / TEXT("blender.exe");
			if (FPaths::FileExists(exe)) return exe;
		}
		return FString();
	}

	struct IndexRun
	{
		FProcHandle Proc;
		void* Read = nullptr;
		void* Write = nullptr;
		FString Pending;
		double Started = FPlatformTime::Seconds();

		void Drain()
		{
			Pending += FPlatformProcess::ReadPipe(Read);
			int32 nl;
			while (Pending.FindChar(TEXT('\n'), nl))
			{
				FString line = Pending.Left(nl).TrimEnd();
				Pending.RightChopInline(nl + 1);
				if (line.StartsWith(TEXT("KitParts:")) || line.Contains(TEXT("Error")))
					UE_LOG(LogSpaceGod, Display, TEXT("[IndexKitParts] %s"), *line);
			}
		}

		bool Tick()
		{
			Drain();
			if (FPlatformProcess::IsProcRunning(Proc)) return true;
			Drain();
			int32 code = -1;
			FPlatformProcess::GetProcReturnCode(Proc, &code);
			FPlatformProcess::CloseProc(Proc);
			FPlatformProcess::ClosePipe(Read, Write);
			UE_LOG(LogSpaceGod, Display, TEXT("[IndexKitParts] %s in %.0f s"),
				code == 0 ? TEXT("wrote Data/ModularParts.json") : TEXT("failed; nothing written"), FPlatformTime::Seconds() - Started);
			return false;
		}
	};

	bool GRunning = false;

	void Start(const TArray<FString>& args)
	{
		if (GRunning)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("[IndexKitParts] already running"));
			return;
		}
		FString blender;
		for (const FString& a : args)
			if (a.StartsWith(TEXT("blender="))) blender = a.Mid(8).TrimQuotes();
		if (blender.IsEmpty()) blender = FindBlender();
		if (blender.IsEmpty() || !FPaths::FileExists(blender))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("[IndexKitParts] Blender (3.3 or later) not found; pass blender=<path to blender.exe>"));
			return;
		}
		const FString repo = RepoRoot();
		const FString source = repo / TEXT("Cpp/SourceArt/ThirdParty");
		if (!FPaths::DirectoryExists(source))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("[IndexKitParts] no kit is imported here (%s); see docs/ThirdPartyArt.md"), *source);
			return;
		}
		const FString args2 = FString::Printf(TEXT("-b --factory-startup --python \"%s\" -- \"%s\" \"%s\""),
			*(repo / TEXT("tools/index_kit_parts.py")), *source, *(repo / TEXT("Data/ModularParts.json")));
		auto run = std::make_shared<IndexRun>();
		if (!FPlatformProcess::CreatePipe(run->Read, run->Write)) return;
		run->Proc = FPlatformProcess::CreateProc(*blender, *args2, /*bLaunchDetached*/ false, /*bLaunchHidden*/ true,
			/*bLaunchReallyHidden*/ true, nullptr, /*PriorityModifier*/ -1, *repo, run->Write);
		if (!run->Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(run->Read, run->Write);
			UE_LOG(LogSpaceGod, Warning, TEXT("[IndexKitParts] could not start %s"), *blender);
			return;
		}
		GRunning = true;
		UE_LOG(LogSpaceGod, Display, TEXT("[IndexKitParts] indexing the kits with %s"), *blender);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			const bool more = run->Tick();
			if (!more) GRunning = false;
			return more;
		}), 0.25f);
	}

	FAutoConsoleCommand GIndexKitPartsCommand(
		TEXT("sg.IndexKitParts"),
		TEXT("US-T763: index every modular part of the imported third-party kits (role, bounds, sockets, style tags) into "
			"Data/ModularParts.json with tools/index_kit_parts.py in Blender. sg.IndexKitParts [blender=<path to blender.exe>]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
