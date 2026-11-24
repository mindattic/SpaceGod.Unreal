// The engine's audio mixer headers use Unreal's PI macro, which Cs.h takes away: included before it.
#include "AudioMixerDevice.h"
#include "Audio/AudioOutput.h"
#include "Audio/GameAudio.h"
#include "PlayerOptions.h"
#include "Settings/GameSetting.h"
#include "UnityCompat/UnityTypes.h"

#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Audio
{
	namespace
	{
		struct Listed
		{
			FString Id;
			FString Name;
		};

		struct State
		{
			/// The output devices the audio engine lists, as last asked (on the game thread).
			TArray<Listed> devices;
			bool listed = false;
			bool querying = false;
			double lastQuery = -1e9;
			/// The device the game was last switched to (empty: the system default, as Unreal opens it).
			FString applied;
			bool swapping = false;
			/// audio.outputDevice's value, set by its Applying.
			FString wanted;
			FTSTicker::FDelegateHandle ticker;
		};

		State& S()
		{
			static State state;
			return state;
		}

		/// How often the device list is asked again while a device is chosen or the row is on screen.
		constexpr double RecheckSeconds = 2.0;
		constexpr int MaxIdLength = 512;

		bool Active() { return !PlayerOptions::ScriptedRun() && GameAudio::Enabled(); }

		/// The audio device the game plays on: the game's (or PIE's) world's, else the main one.
		::Audio::FDeviceId PlayDevice()
		{
			if (!GEngine) return 0;
			for (const FWorldContext& context : GEngine->GetWorldContexts())
				if ((context.WorldType == EWorldType::Game || context.WorldType == EWorldType::PIE) && context.World())
				{
					FAudioDeviceHandle handle = context.World()->GetAudioDevice();
					if (handle.IsValid()) return handle.GetDeviceID();
				}
			return GEngine->GetMainAudioDeviceID();
		}

		/// On the audio thread: the mixer platform of a device.
		::Audio::IAudioMixerPlatformInterface* Platform(::Audio::FDeviceId id)
		{
			FAudioDeviceManager* manager = FAudioDeviceManager::Get();
			FAudioDevice* device = manager && id != 0 ? manager->GetAudioDeviceRaw(id) : nullptr;
			return device ? static_cast<::Audio::FMixerDevice*>(device)->GetAudioMixerPlatform() : nullptr;
		}

		/// Switches to the chosen device while it is listed, else to the system default (the choice stays saved).
		void Reconcile()
		{
			State& s = S();
			if (!s.listed || s.swapping || !Active()) return;
			FString target = s.wanted;
			if (!target.IsEmpty() && !s.devices.ContainsByPredicate([&](const Listed& d) { return d.Id == target; })) target.Empty();
			if (target == s.applied) return;
			const ::Audio::FDeviceId id = PlayDevice();
			if (id == 0) return;
			s.swapping = true;
			FAudioThread::RunCommandOnAudioThread([id, target]
			{
				::Audio::IAudioMixerPlatformInterface* platform = Platform(id);
				const bool swapped = platform && platform->RequestDeviceSwap(target, false, TEXT("Settings > Audio > Output device"));
				FAudioThread::RunCommandOnGameThread([target, swapped]
				{
					State& s = S();
					s.swapping = false;
					if (!swapped) return; // refused (a swap already under way): the next check asks again
					s.applied = target;
					Debug::Log(cs::Cat("[Unity.SpaceGod] Audio: output on ", target.IsEmpty() ? cs::String("the system default") : cs::String(TCHAR_TO_UTF8(*target)), "."));
					Reconcile();
				});
			});
		}

		/// Asks the audio engine for its output devices (as AudioMixerBlueprintLibrary does), then reconciles.
		void Query()
		{
			State& s = S();
			if (s.querying || !Active()) return;
			const ::Audio::FDeviceId id = PlayDevice();
			if (id == 0) return;
			s.querying = true;
			s.lastQuery = FPlatformTime::Seconds();
			FAudioThread::RunCommandOnAudioThread([id]
			{
				TArray<Listed> list;
				bool ok = false;
				if (::Audio::IAudioMixerPlatformInterface* platform = Platform(id))
				{
					ok = true;
					auto add = [&list](const ::Audio::FAudioPlatformDeviceInfo& info)
					{
						if (!info.DeviceId.IsEmpty() && info.DeviceId.Len() <= MaxIdLength) list.Add({ info.DeviceId, info.Name });
					};
					if (::Audio::IAudioPlatformDeviceInfoCache* cache = platform->GetDeviceInfoCache())
					{
						for (const ::Audio::FAudioPlatformDeviceInfo& info : cache->GetAllActiveOutputDevices()) add(info);
					}
					else
					{
						uint32 count = 0;
						platform->GetNumOutputDevices(count);
						for (uint32 i = 0; i < count; i++)
						{
							::Audio::FAudioPlatformDeviceInfo info;
							if (platform->GetOutputDeviceInfo(i, info)) add(info);
						}
					}
				}
				FAudioThread::RunCommandOnGameThread([list = MoveTemp(list), ok]() mutable
				{
					State& s = S();
					s.querying = false;
					if (!ok) return;
					s.devices = MoveTemp(list);
					s.listed = true;
					Reconcile();
				});
			});
		}

		bool Tick(float)
		{
			State& s = S();
			if ((!s.wanted.IsEmpty() || !s.applied.IsEmpty()) && FPlatformTime::Seconds() - s.lastQuery >= RecheckSeconds) Query();
			return true;
		}

		void Watch()
		{
			State& s = S();
			if (!s.ticker.IsValid()) s.ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.25f);
		}

		void Apply(const cs::String& value)
		{
			State& s = S();
			s.wanted = FString(UTF8_TO_TCHAR(value.c_str()));
			if (!Active()) return;
			if (s.wanted.IsEmpty() && s.applied.IsEmpty()) return; // the system default, as Unreal opens it
			Watch();
			if (s.listed) Reconcile();
			Query();
		}
	}

	void AudioOutput::Declare() { Device(); }

	const cs::Ref<TextSetting>& AudioOutput::Device()
	{
		static const cs::Ref<TextSetting> value = cs::New<TextSetting>("audio.outputDevice", SettingGroups::Audio, "Output device", "", MaxIdLength,
			"Where the game's sound plays: the system default, or a listed device (switched at once). A device that goes away plays "
			"on the default until it comes back.", nullptr, "sound output device speakers headphones headset audio interface")
			->Applying([](const cs::String& v) { Apply(v); });
		return value;
	}

	cs::String AudioOutput::Shown()
	{
		State& s = S();
		if (FPlatformTime::Seconds() - s.lastQuery >= RecheckSeconds) Query();
		if (s.wanted.IsEmpty()) return "System default";
		for (const Listed& d : s.devices)
			if (d.Id == s.wanted) return cs::String(TCHAR_TO_UTF8(*d.Name));
		return s.listed ? "Not connected (on the default)" : "Not listed here";
	}

	void AudioOutput::Cycle(int step)
	{
		State& s = S();
		if (!s.listed)
		{
			Query();
			return;
		}
		TArray<FString> ids{ FString() };
		for (const Listed& d : s.devices) ids.Add(d.Id);
		const int at = FMath::Max(0, ids.IndexOfByKey(s.wanted));
		const int next = ((at + step) % ids.Num() + ids.Num()) % ids.Num();
		Device()->Set(cs::String(TCHAR_TO_UTF8(*ids[next])));
	}
}
