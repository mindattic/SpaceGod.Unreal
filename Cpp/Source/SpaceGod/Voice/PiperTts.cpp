#include "Voice/PiperTts.h"

#include "SpaceGodLog.h"
#include "Voice/VoiceCache.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace SpaceGod::Voice
{
	namespace
	{
		/// A second failure (the one restart used up) turns Piper off for the run.
		constexpr int MaxFailures = 2;
	}

	PiperTts::~PiperTts()
	{
		Stop();
	}

	cs::String PiperTts::Version() const
	{
		return TCHAR_TO_UTF8(*VersionText);
	}

	void PiperTts::Locate()
	{
		Located = true;
		TArray<FString> roots;
		const FString env = FPlatformMisc::GetEnvironmentVariable(TEXT("SPACEGOD_PIPER_DIR"));
		if (!env.IsEmpty()) roots.Add(env);
		// tools/package.ps1 stages it beside the package's Engine/ folder.
		roots.Add(FPaths::Combine(FPaths::RootDir(), TEXT("Piper")));
		roots.Add(FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("Piper")));
#if WITH_EDITOR
		roots.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Vendor"), TEXT("Piper")));
#endif
		for (const FString& root : roots)
		{
			const FString full = FPaths::ConvertRelativePathToFull(root);
			const FString exe = FPaths::Combine(full, TEXT("piper"), TEXT("piper.exe"));
			const FString voices = FPaths::Combine(full, TEXT("voices"));
			if (!FPaths::FileExists(exe) || !FPaths::DirectoryExists(voices)) continue;
			Exe = exe;
			VoicesDir = voices;
			if (FFileHelper::LoadFileToString(VersionText, *FPaths::Combine(full, TEXT("piper"), TEXT(".version"))))
				VersionText.TrimStartAndEndInline();
			if (VersionText.IsEmpty()) VersionText = TEXT("unknown");
			UE_LOG(LogSpaceGod, Display, TEXT("[Voice] Piper %s at %s"), *VersionText, *full);
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("[Voice] No Piper found (SPACEGOD_PIPER_DIR, the package's Piper/ or Vendor/Piper): it stays silent."));
	}

	bool PiperTts::Available()
	{
		if (!Located) Locate();
		return !Exe.IsEmpty() && Failures < MaxFailures && !Aborted;
	}

	FString PiperTts::ModelFor(const cs::String& engineVoice) const
	{
		TArray<FString> models;
		IFileManager::Get().FindFiles(models, *FPaths::Combine(VoicesDir, TEXT("*.onnx")), true, false);
		models.Sort();
		const FString want = UTF8_TO_TCHAR(engineVoice.c_str());
		for (const FString& m : models)
			if (want.IsEmpty() || m.Contains(want)) return FPaths::Combine(VoicesDir, m);
		return FString();
	}

	bool PiperTts::Start(const FString& model)
	{
		void* stdoutWrite = nullptr;
		void* stdinRead = nullptr;
		if (!FPlatformProcess::CreatePipe(StdoutRead, stdoutWrite)) return false;
		if (!FPlatformProcess::CreatePipe(stdinRead, StdinWrite, /*bWritePipeLocal*/ true))
		{
			FPlatformProcess::ClosePipe(StdoutRead, stdoutWrite);
			StdoutRead = nullptr;
			return false;
		}
		const FString args = FString::Printf(TEXT("--model \"%s\" --json-input"), *model);
		uint32 pid = 0;
		// Below normal, like the worker: speech never takes a core from the game.
		Proc = FPlatformProcess::CreateProc(*Exe, *args, /*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
			&pid, /*PriorityModifier*/ -1, *FPaths::GetPath(Exe), stdoutWrite, stdinRead);
		// The child's ends are its own now (inherited); ours stay open until Stop.
		FPlatformProcess::ClosePipe(stdinRead, stdoutWrite);
		if (!Proc.IsValid())
		{
			Stop();
			return false;
		}
		Model = model;
		return true;
	}

	void PiperTts::Stop()
	{
		// Its stdin closed, Piper finishes the line in hand and exits; a hung one is ended.
		if (StdinWrite) FPlatformProcess::ClosePipe(nullptr, StdinWrite);
		StdinWrite = nullptr;
		if (Proc.IsValid())
		{
			if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, /*KillTree*/ true);
			FPlatformProcess::CloseProc(Proc);
		}
		Proc.Reset();
		if (StdoutRead) FPlatformProcess::ClosePipe(StdoutRead, nullptr);
		StdoutRead = nullptr;
		Pending.Reset();
		Model.Reset();
	}

	void PiperTts::Failed(const TCHAR* why)
	{
		Stop();
		++Failures;
		if (Failures < MaxFailures) { UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] Piper %s; it is restarted for the next line."), why); }
		else { UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] Piper %s again; it is off for this run."), why); }
	}

	bool PiperTts::AwaitEcho(const FString& fileName)
	{
		const double deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		TArray<uint8> chunk;
		while (!Aborted)
		{
			bool read = false;
			while (FPlatformProcess::ReadPipeToArray(StdoutRead, chunk) && chunk.Num() > 0)
			{
				Pending.Append(chunk);
				chunk.Reset();
				read = true;
			}
			bool echoed = false;
			int32 start = 0;
			for (int32 i = 0; i < Pending.Num(); ++i)
			{
				if (Pending[i] != '\n') continue;
				FUTF8ToTCHAR conv(reinterpret_cast<const ANSICHAR*>(Pending.GetData() + start), i - start);
				FString line(conv.Length(), conv.Get());
				line.TrimStartAndEndInline();
				if (line.EndsWith(fileName)) echoed = true;
				start = i + 1;
			}
			if (start > 0) Pending.RemoveAt(0, start);
			if (echoed) return true;
			if (!FPlatformProcess::IsProcRunning(Proc) || FPlatformTime::Seconds() > deadline) return false;
			if (!read) FPlatformProcess::Sleep(0.005f);
		}
		return false;
	}

	Audio::SoundClip PiperTts::Synthesize(const TtsRequest& request)
	{
		if (!Available() || request.Text.empty()) return {};
		const FString model = ModelFor(request.Voice.EngineVoice);
		if (model.IsEmpty())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] No Piper model for the voice '%s' in %s."), UTF8_TO_TCHAR(request.Voice.EngineVoice.c_str()), *VoicesDir);
			return {};
		}
		if (Proc.IsValid() && model != Model) Stop();
		if (!Proc.IsValid() && !Start(model))
		{
			Failed(TEXT("could not be started"));
			return {};
		}

		const FString dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Voice"), TEXT("Piper")));
		IFileManager::Get().MakeDirectory(*dir, true);
		const FString fileName = FString::Printf(TEXT("line-%u-%d.wav"), FPlatformProcess::GetCurrentProcessId(), ++Serial);
		const FString path = FPaths::Combine(dir, fileName);

		TSharedRef<FJsonObject> json = MakeShared<FJsonObject>();
		json->SetStringField(TEXT("text"), UTF8_TO_TCHAR(request.Text.c_str()));
		json->SetNumberField(TEXT("speaker_id"), request.Voice.SpeakerId);
		json->SetNumberField(TEXT("length_scale"), request.Voice.LengthScale);
		json->SetStringField(TEXT("output_file"), path);
		FString line;
		FJsonSerializer::Serialize(json, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&line));

		Audio::SoundClip clip;
		if (!FPlatformProcess::WritePipe(StdinWrite, line)) Failed(TEXT("stopped reading its input"));
		else if (!AwaitEcho(fileName))
		{
			if (Aborted) Stop();
			else Failed(TEXT("gave no answer in time or exited"));
		}
		else
		{
			// The echo can come a moment before the file is all flushed.
			TArray<uint8> bytes;
			for (int attempt = 0; attempt < 50 && !Aborted; ++attempt)
			{
				if (FFileHelper::LoadFileToArray(bytes, *path, FILEREAD_Silent) && VoiceCache::ReadWav(bytes, clip))
				{
					clip.Name = "piper";
					break;
				}
				FPlatformProcess::Sleep(0.01f);
			}
			if (!clip.IsValid()) UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] Piper wrote no readable PCM16 mono WAV at %s."), *path);
		}
		IFileManager::Get().Delete(*path, false, false, true);
		return clip;
	}
}
