#pragma once

#include "Voice/TtsEngine.h"

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

#include <atomic>

namespace SpaceGod::Voice
{
	/// Piper (US-T587; Vendor/README.md): one long-lived `piper.exe --model <voice>.onnx --json-input` child process,
	/// one JSON line per request written to its stdin, the WAV it writes read back. Found in SPACEGOD_PIPER_DIR, else the
	/// package's Piper/ folder, else (an editor build) the repo's Vendor/Piper. If the process dies or hangs it is
	/// started once more; after a second failure Piper reports itself unavailable for the rest of the run.
	class PiperTts final : public ITtsEngine
	{
	public:
		~PiperTts() override;

		const char* Name() const override { return "piper"; }
		cs::String Version() const override;
		bool Available() override;
		Audio::SoundClip Synthesize(const TtsRequest& request) override;
		void Abort() override { Aborted = true; }

		/// How long one line may take before the process is taken for hung.
		static constexpr double TimeoutSeconds = 10.0;

	private:
		bool Located = false;
		FString Exe;
		FString VoicesDir;
		FString VersionText;
		FString Model;
		FProcHandle Proc;
		void* StdinWrite = nullptr;
		void* StdoutRead = nullptr;
		TArray<uint8> Pending;
		int Failures = 0;
		int Serial = 0;
		std::atomic<bool> Aborted { false };

		void Locate();
		FString ModelFor(const cs::String& engineVoice) const;
		bool Start(const FString& model);
		void Stop();
		void Failed(const TCHAR* why);
		/// Waits for the line naming <paramref name="fileName"/>: false on timeout, abort or the process dying.
		bool AwaitEcho(const FString& fileName);
	};
}
