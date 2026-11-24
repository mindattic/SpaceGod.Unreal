#include "Voice/SapiTts.h"

#include "SpaceGodLog.h"

#include "HAL/PlatformTime.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <objbase.h>
#include <sapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace SpaceGod::Voice
{
#if PLATFORM_WINDOWS
	namespace
	{
		ISpVoice* AsVoice(void* p) { return static_cast<ISpVoice*>(p); }

		/// The line as SAPI XML text: its own markup characters escaped.
		FString Escape(const FString& text)
		{
			FString out;
			out.Reserve(text.Len());
			for (TCHAR c : text)
			{
				if (c == TEXT('&')) out += TEXT("&amp;");
				else if (c == TEXT('<')) out += TEXT("&lt;");
				else if (c == TEXT('>')) out += TEXT("&gt;");
				else out.AppendChar(c);
			}
			return out;
		}
	}

	SapiTts::~SapiTts()
	{
		for (void* token : Voices) static_cast<ISpObjectToken*>(token)->Release();
		if (Voice) AsVoice(Voice)->Release();
	}

	void SapiTts::Start()
	{
		Started = true;
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(com) && com != RPC_E_CHANGED_MODE)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] SAPI: COM would not start (0x%08x)."), static_cast<uint32>(com));
			return;
		}

		ISpObjectTokenCategory* category = nullptr;
		IEnumSpObjectTokens* tokens = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&category)))
			&& SUCCEEDED(category->SetId(SPCAT_VOICES, false))
			&& SUCCEEDED(category->EnumTokens(nullptr, nullptr, &tokens)))
		{
			ISpObjectToken* token = nullptr;
			while (tokens->Next(1, &token, nullptr) == S_OK) Voices.push_back(token);
		}
		if (tokens) tokens->Release();
		if (category) category->Release();

		ISpVoice* voice = nullptr;
		if (Voices.empty() || FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice))))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] SAPI: no voice installed or no SpVoice; there is no speech fallback."));
			return;
		}
		Voice = voice;
		Usable = true;
		UE_LOG(LogSpaceGod, Log, TEXT("[Voice] SAPI fallback ready with %d installed voice(s)."), static_cast<int32>(Voices.size()));
	}

	bool SapiTts::Available()
	{
		if (!Started && !Aborted) Start();
		return Usable && !Aborted;
	}

	Audio::SoundClip SapiTts::Synthesize(const TtsRequest& request)
	{
		Audio::SoundClip clip;
		if (!Available() || request.Text.empty()) return clip;
		ISpVoice* voice = AsVoice(Voice);

		const int count = static_cast<int>(Voices.size());
		const int index = ((request.Voice.SapiIndex % count) + count) % count;
		voice->SetVoice(static_cast<ISpObjectToken*>(Voices[index]));
		const double scale = request.Voice.LengthScale > 0 ? request.Voice.LengthScale : 1.0;
		// SAPI's rate 10 is three times as fast, -10 a third.
		voice->SetRate(std::clamp(static_cast<int>(std::lround(-10.0 * std::log(scale) / std::log(3.0))), -10, 10));

		IStream* memory = nullptr;
		ISpStream* stream = nullptr;
		if (FAILED(CreateStreamOnHGlobal(nullptr, true, &memory))
			|| FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&stream))))
		{
			if (memory) memory->Release();
			UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] SAPI: no memory stream."));
			return clip;
		}
		WAVEFORMATEX format {};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = SampleRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 2;
		format.nAvgBytesPerSec = SampleRate * 2;

		bool spoken = false;
		if (SUCCEEDED(stream->SetBaseStream(memory, SPDFID_WaveFormatEx, &format)) && SUCCEEDED(voice->SetOutput(stream, true)))
		{
			const int pitch = std::clamp(static_cast<int>(std::lround(request.Voice.Pitch * 10.0)), -10, 10);
			const FString xml = FString::Printf(TEXT("<pitch absmiddle=\"%d\">%s</pitch>"), pitch, *Escape(UTF8_TO_TCHAR(request.Text.c_str())));
			if (SUCCEEDED(voice->Speak(*xml, SPF_ASYNC | SPF_IS_XML, nullptr)))
			{
				const double deadline = FPlatformTime::Seconds() + TimeoutSeconds;
				while (!Aborted && voice->WaitUntilDone(100) == S_FALSE && FPlatformTime::Seconds() < deadline) {}
				spoken = voice->WaitUntilDone(0) == S_OK;
				if (!spoken)
				{
					voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
					if (!Aborted) UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] SAPI took more than %.0f s over a line; dropped."), TimeoutSeconds);
				}
			}
			voice->SetOutput(nullptr, false);
		}
		stream->Close();
		stream->Release();

		ULARGE_INTEGER end {};
		HGLOBAL global = nullptr;
		if (spoken && SUCCEEDED(memory->Seek(LARGE_INTEGER {}, STREAM_SEEK_CUR, &end)) && SUCCEEDED(GetHGlobalFromStream(memory, &global)))
		{
			const SIZE_T bytes = std::min<SIZE_T>(static_cast<SIZE_T>(end.QuadPart), GlobalSize(global));
			if (const void* data = GlobalLock(global))
			{
				auto samples = std::make_shared<std::vector<std::int16_t>>(bytes / 2);
				if (!samples->empty()) std::memcpy(samples->data(), data, samples->size() * 2);
				GlobalUnlock(global);
				clip.Samples = samples;
				clip.SampleRate = SampleRate;
				clip.Name = "sapi";
			}
		}
		memory->Release();
		return clip;
	}
#else
	SapiTts::~SapiTts() = default;
	void SapiTts::Start() { Started = true; }
	bool SapiTts::Available() { return false; }
	Audio::SoundClip SapiTts::Synthesize(const TtsRequest&) { return {}; }
#endif
}
