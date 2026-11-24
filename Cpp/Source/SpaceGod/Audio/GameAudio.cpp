#include "Audio/GameAudio.h"
#include "Audio/AudioMixerIncludes.h"
#include "Audio/StereoFold.h"
#include "EngineSettings.h"
#include "SessionAudio.h"
#include "UnrealSpace.h"
#include "UnityCompat/UnityTypes.h"
#include "Settings/GameSetting.h"
#include "PlayerOptions.h"
#include "Voice/TtsService.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MindAtticHeadless.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/Package.h"

#include <array>

using namespace SpaceGod::Core;

namespace SpaceGod::Audio
{
	namespace
	{
		struct Voice
		{
			TWeakObjectPtr<UAudioComponent> Component;
			AudioBus Bus = AudioBus::Effects;
			float Gain = 1.0f;
			/// FPlatformTime seconds the one-shot is over (its procedural wave never ends by itself); < 0 for a loop.
			double End = -1;
			LoopId Loop = 0;
		};

		struct State
		{
			bool built = false;
			std::array<USoundClass*, AudioBusCount> classes{};
			std::array<USoundSubmix*, AudioBusCount> submixes{};
			TMap<uint64, USoundAttenuation*> attenuations;
			TArray<Voice> voices;
			std::array<float, AudioBusCount> appliedVolumes{};
			FTSTicker::FDelegateHandle ticker;
			LoopId nextLoop = 1;
			/// The night compressor (US-T552), and the audio device it is on (0: on none).
			USubmixEffectDynamicsProcessorPreset* night = nullptr;
			::Audio::FDeviceId nightDevice = 0;
			/// The stereo fold-down (US-T672), and the audio device it is on (0: on none).
			USpaceGodStereoFoldPreset* fold = nullptr;
			::Audio::FDeviceId foldDevice = 0;
		};

		State& S()
		{
			static State state;
			return state;
		}

		/// Live volumes of the four buses (Master is the listener's, audio.masterVolume): set by the settings' Applying.
		std::array<float, AudioBusCount> busVolumes{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		/// US-T552: Sound while paused (0 Play, 1 Duck, 2 Mute) and Dynamic range (0 Full, 1 Night), set by their Applying.
		int whilePaused = 0;
		int dynamicRange = 0;

		/// US-T613: how 3D sounds are placed, set by AudioOutput's speaker mode.
		GameAudio::Placement placement = GameAudio::Placement::Panned;
		/// US-T673: 3D sounds use the spatialization plugin (set by AudioOutput's spatial sound once it is up).
		bool spatialPlugin = false;
		/// US-T672: whether the main mix is folded down to two channels, set by AudioOutput's speaker mode.
		bool stereoFold = false;

		/// How loud a ducked bus is while the pause menu is open.
		constexpr float PausedDuck = 0.3f;

		/// A run that is driven or photographed (the smoke, shots, audits, the race, co-op smoke, -demo, -capture),
		/// headless or offscreen plays nothing unless -GameAudio asks for sound.
		bool ScriptedSilence()
		{
			static const bool value = []
			{
				const TCHAR* cmd = FCommandLine::Get();
				if (FParse::Param(cmd, TEXT("GameAudio"))) return false;
				if (FMindAtticHeadless::IsActive() || IsRunningCommandlet() || GIsAutomationTesting || !FApp::CanEverRenderAudio()) return true;
				if (FParse::Param(cmd, TEXT("RenderOffscreen")) || FParse::Param(cmd, TEXT("nosound"))) return true;
				const TCHAR* cursor = cmd;
				FString token;
				while (FParse::Token(cursor, token, false))
					if ((token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) && !token.Equals(TEXT("-SpaceGodCoopLog"), ESearchCase::IgnoreCase))
						|| token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
						return true;
				return false;
			}();
			return value;
		}

		/// The world sounds play in: the game's (or PIE's).
		UWorld* PlayWorld()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& context : GEngine->GetWorldContexts())
				if ((context.WorldType == EWorldType::Game || context.WorldType == EWorldType::PIE) && context.World())
					return context.World();
			return nullptr;
		}

		/// The pause menu's gain on a bus (US-T552): the menu's own Interface bus is never ducked or muted.
		float Paused(AudioBus bus)
		{
			if (bus == AudioBus::Interface || UnityCompat::Time::timeScale > 0) return 1.0f;
			return whilePaused == 2 ? 0.0f : whilePaused == 1 ? PausedDuck : 1.0f;
		}

		/// A bus's own volume on a sound's component (Master's is the listener's, applied to everything already).
		float Mix(AudioBus bus) { return bus == AudioBus::Master ? 1.0f : busVolumes[static_cast<int>(bus)] * Paused(bus); }

		/// The device a main submix effect is wanted on: the one the game plays on while <paramref name="on"/>, else 0.
		/// Takes the effect of <paramref name="preset"/> off the device it is on (<paramref name="placed"/>) when that
		/// differs; returns the device to add it to, 0 when nothing is to be added.
		::Audio::FDeviceId MainEffectDevice(UWorld* world, bool on, USoundEffectSubmixPreset* preset, ::Audio::FDeviceId& placed)
		{
			FAudioDeviceHandle device = on && world ? world->GetAudioDevice() : FAudioDeviceHandle();
			const ::Audio::FDeviceId want = device.IsValid() ? device.GetDeviceID() : 0;
			if (want == placed) return 0;
			if (placed != 0)
				if (FAudioDeviceManager* manager = GEngine ? GEngine->GetAudioDeviceManager() : nullptr)
					if (FAudioDevice* old = manager->GetAudioDeviceRaw(placed))
						static_cast<::Audio::FMixerDevice*>(old)->RemoveMasterSubmixEffect(preset->GetUniqueID());
			placed = 0;
			return want;
		}

		/// Stereo speakers and Headphones on a surround device fold the main submix down to two channels; otherwise the
		/// effect is not in the chain.
		void ApplyStereoFold()
		{
			State& s = S();
			UWorld* world = PlayWorld();
			const ::Audio::FDeviceId want = MainEffectDevice(world, stereoFold, s.fold, s.foldDevice);
			if (want == 0) return;
			if (!s.fold)
			{
				s.fold = NewObject<USpaceGodStereoFoldPreset>(GetTransientPackage(), TEXT("SpaceGod_StereoFold"));
				s.fold->AddToRoot();
			}
			UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(world, s.fold);
			s.foldDevice = want;
		}

		/// Night puts a compressor on the main submix of the device the game plays on; Full takes it off.
		void ApplyDynamicRange()
		{
			State& s = S();
			UWorld* world = PlayWorld();
			const ::Audio::FDeviceId want = MainEffectDevice(world, dynamicRange == 1, s.night, s.nightDevice);
			if (want == 0) return;
			if (!s.night)
			{
				s.night = NewObject<USubmixEffectDynamicsProcessorPreset>(GetTransientPackage(), TEXT("SpaceGod_NightRange"));
				s.night->AddToRoot();
				FSubmixEffectDynamicsProcessorSettings settings;
				settings.DynamicsProcessorType = ESubmixEffectDynamicsProcessorType::Compressor;
				settings.ThresholdDb = -24.0f;
				settings.Ratio = 4.0f;
				settings.KneeBandwidthDb = 10.0f;
				settings.AttackTimeMsec = 5.0f;
				settings.ReleaseTimeMsec = 200.0f;
				settings.OutputGainDb = 6.0f;
				s.night->SetSettings(settings);
			}
			UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(world, s.night);
			s.nightDevice = want;
		}

		float Effective(const Voice& v) { return v.Gain * Mix(v.Bus); }

		bool Tick(float)
		{
			State& s = S();
			const double now = FPlatformTime::Seconds();
			std::array<float, AudioBusCount> mixes;
			for (int i = 0; i < AudioBusCount; i++) mixes[i] = Mix(static_cast<AudioBus>(i));
			const bool volumesChanged = s.appliedVolumes != mixes;
			s.appliedVolumes = mixes;
			ApplyDynamicRange();
			ApplyStereoFold();
			for (int i = s.voices.Num() - 1; i >= 0; i--)
			{
				Voice& v = s.voices[i];
				UAudioComponent* c = v.Component.Get();
				if (!c || (v.End >= 0 && !c->IsPlaying()))
				{
					s.voices.RemoveAtSwap(i);
					continue;
				}
				if (v.End >= 0 && now >= v.End)
				{
					c->Stop(); // auto-destroys
					s.voices.RemoveAtSwap(i);
					continue;
				}
				if (volumesChanged) c->SetVolumeMultiplier(Effective(v));
			}
			return true;
		}

		/// The buses' sound classes (the four under Master) and submixes (each into Unreal's main submix), built on the
		/// first sound played: they carry each bus's identity for routing, mixes and effects (later audio tickets);
		/// the volume itself is applied on each sound's component, so it reaches a sound the moment it changes.
		void Build()
		{
			State& s = S();
			if (s.built) return;
			s.built = true;
			FAudioDeviceManager* manager = GEngine ? GEngine->GetAudioDeviceManager() : nullptr;
			for (int i = 0; i < AudioBusCount; i++)
			{
				const AudioBus bus = static_cast<AudioBus>(i);
				USoundClass* cls = NewObject<USoundClass>(GetTransientPackage(), FName(*FString::Printf(TEXT("SpaceGod_%hs"), BusName(bus))));
				cls->AddToRoot();
				if (i > 0)
				{
					cls->ParentClass = s.classes[0];
					s.classes[0]->ChildClasses.Add(cls);
				}
				s.classes[i] = cls;
				if (i > 0)
				{
					USoundSubmix* mix = NewObject<USoundSubmix>(GetTransientPackage(), FName(*FString::Printf(TEXT("SpaceGod_%hsSubmix"), BusName(bus))));
					mix->AddToRoot();
					s.submixes[i] = mix;
				}
			}
			if (manager)
			{
				for (USoundClass* cls : s.classes) manager->RegisterSoundClass(cls);
				for (USoundSubmix* mix : s.submixes) if (mix) manager->RegisterSoundSubmix(mix);
			}
			s.ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
			Debug::Log("[Unity.SpaceGod] Audio: buses Master, Music, Effects, Interface, Ambience, Voice ready.");
		}

		/// The speaker mode's placement on an attenuation: Mono plays it unpanned; Headphones keeps sounds inside their
		/// min distance (<paramref name="minCm"/>) in the middle, panning as they leave it; Panned is the engine's own.
		void Place(FSoundAttenuationSettings& at, float minCm)
		{
			at.bSpatialize = placement != GameAudio::Placement::Mono;
			at.NonSpatializedRadiusStart = placement == GameAudio::Placement::Headphones ? minCm : 0.0f;
			at.NonSpatializedRadiusEnd = 0.0f;
			at.NonSpatializedRadiusMode = ENonSpatializedRadiusSpeakerMapMode::OmniDirectional;
			at.SpatializationAlgorithm = spatialPlugin && at.bSpatialize ? ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF
				: ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
		}

		USoundAttenuation* Attenuation(float minDistance, float maxDistance)
		{
			State& s = S();
			const float minCm = FMath::Max(0.0f, minDistance) * static_cast<float>(UnrealSpace::CmPerUnit);
			const float maxCm = FMath::Max(minDistance + 1.0f, maxDistance) * static_cast<float>(UnrealSpace::CmPerUnit);
			const uint64 key = (static_cast<uint64>(FMath::RoundToInt(minCm)) << 32) | static_cast<uint32>(FMath::RoundToInt(maxCm));
			if (USoundAttenuation** found = s.attenuations.Find(key)) return *found;
			USoundAttenuation* a = NewObject<USoundAttenuation>(GetTransientPackage());
			a->AddToRoot();
			FSoundAttenuationSettings& at = a->Attenuation;
			at.bAttenuate = true;
			at.bSpatialize = true;
			at.AttenuationShape = EAttenuationShape::Sphere;
			at.AttenuationShapeExtents = FVector(minCm, 0, 0);
			at.FalloffDistance = maxCm - minCm;
			at.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
			Place(at, minCm);
			s.attenuations.Add(key, a);
			return a;
		}

		USoundWaveProcedural* MakeWave(const SoundClip& clip, AudioBus bus, bool loop)
		{
			State& s = S();
			USoundWaveProcedural* wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
			wave->SetSampleRate(static_cast<uint32>(clip.SampleRate));
			wave->NumChannels = 1;
			wave->Duration = loop ? INDEFINITELY_LOOPING_DURATION : clip.Seconds();
			wave->SoundGroup = SOUNDGROUP_Default;
			wave->bLooping = loop;
			wave->SoundClassObject = s.classes[static_cast<int>(bus)];
			if (USoundSubmix* mix = s.submixes[static_cast<int>(bus)]) wave->SoundSubmixObject = mix;
			const auto data = clip.Samples;
			const int32 bytes = static_cast<int32>(data->size() * sizeof(std::int16_t));
			wave->QueueAudio(reinterpret_cast<const uint8*>(data->data()), bytes);
			if (loop)
			{
				wave->QueueAudio(reinterpret_cast<const uint8*>(data->data()), bytes);
				// On the audio render thread: the clip again, whole, whenever the queue runs short (a seamless loop).
				wave->OnSoundWaveProceduralUnderflow.BindLambda([data, bytes](USoundWaveProcedural* w, int32)
				{
					w->QueueAudio(reinterpret_cast<const uint8*>(data->data()), bytes);
				});
			}
			return wave;
		}

		UAudioComponent* Spawn(const SoundClip& clip, AudioBus bus, float gain, float pitch, bool loop, bool positional, UnityCompat::Vector3 position,
			float minDistance, float maxDistance)
		{
			if (!clip.IsValid() || !GameAudio::Enabled()) return nullptr;
			UWorld* world = PlayWorld();
			if (!world || !world->GetAudioDevice()) return nullptr;
			const float volume = gain * Mix(bus);
			if (!loop && volume <= 0.0f) return nullptr;
			Build();
			USoundWaveProcedural* wave = MakeWave(clip, bus, loop);
			UAudioComponent* c = positional
				? UGameplayStatics::SpawnSoundAtLocation(world, wave, UnrealSpace::ToUnreal(position), FRotator::ZeroRotator, volume, pitch, 0.0f,
					Attenuation(minDistance, maxDistance), nullptr, true)
				: UGameplayStatics::SpawnSound2D(world, wave, volume, pitch, 0.0f, nullptr, false, true);
			if (!c) return nullptr;
			Voice v;
			v.Component = c;
			v.Bus = bus;
			v.Gain = gain;
			v.End = loop ? -1.0 : FPlatformTime::Seconds() + clip.Seconds() / FMath::Max(0.05f, pitch) + 0.1;
			S().voices.Add(v);
			return c;
		}

		Voice* FindLoop(LoopId loop)
		{
			if (loop == 0) return nullptr;
			for (Voice& v : S().voices)
				if (v.Loop == loop) return &v;
			return nullptr;
		}

		cs::Ref<RangeSetting> BusSetting(const char* key, const char* label, const char* description, const char* keywords, AudioBus bus)
		{
			return cs::New<RangeSetting>(key, SettingGroups::Audio, label, 100, 0, 100, 5, description,
				[](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }, keywords)
				->Applying([bus](double v) { busVolumes[static_cast<int>(bus)] = Mathf::Clamp01(static_cast<float>(v / 100)); });
		}
	}

	bool GameAudio::Enabled()
	{
		if (ScriptedSilence() || SessionAudio::Muted() || !GEngine) return false;
		return static_cast<bool>(GEngine->GetMainAudioDevice());
	}

	float GameAudio::BusVolume(AudioBus bus)
	{
		if (bus == AudioBus::Master) return SessionAudio::ListenerVolume(EngineSettings::MasterVolume()->Value);
		return busVolumes[static_cast<int>(bus)];
	}

	void GameAudio::Play2D(const SoundClip& clip, AudioBus bus, float volume, float pitch)
	{
		Spawn(clip, bus, volume, pitch, false, false, UnityCompat::Vector3(), 0, 0);
	}

	LoopId GameAudio::Play2DStoppable(const SoundClip& clip, AudioBus bus, float volume, float pitch)
	{
		if (!Spawn(clip, bus, volume, pitch, false, false, UnityCompat::Vector3(), 0, 0)) return 0;
		Voice& v = S().voices.Last();
		v.Loop = S().nextLoop++;
		return v.Loop;
	}

	void GameAudio::Play3D(const SoundClip& clip, AudioBus bus, UnityCompat::Vector3 position, float volume, float pitch, float minDistance, float maxDistance)
	{
		Spawn(clip, bus, volume, pitch, false, true, position, minDistance, maxDistance);
	}

	LoopId GameAudio::StartLoop(const SoundClip& clip, AudioBus bus, float volume, bool positional, UnityCompat::Vector3 position, float minDistance,
		float maxDistance)
	{
		if (!Spawn(clip, bus, volume, 1.0f, true, positional, position, minDistance, maxDistance)) return 0;
		Voice& v = S().voices.Last();
		v.Loop = S().nextLoop++;
		return v.Loop;
	}

	void GameAudio::SetLoopVolume(LoopId loop, float volume)
	{
		if (Voice* v = FindLoop(loop))
		{
			v->Gain = volume;
			if (UAudioComponent* c = v->Component.Get()) c->SetVolumeMultiplier(Effective(*v));
		}
	}

	void GameAudio::SetLoopPitch(LoopId loop, float pitch)
	{
		if (Voice* v = FindLoop(loop))
			if (UAudioComponent* c = v->Component.Get()) c->SetPitchMultiplier(FMath::Clamp(pitch, 0.2f, 4.0f));
	}

	void GameAudio::SetLoopPosition(LoopId loop, UnityCompat::Vector3 position)
	{
		if (Voice* v = FindLoop(loop))
			if (UAudioComponent* c = v->Component.Get()) c->SetWorldLocation(UnrealSpace::ToUnreal(position));
	}

	void GameAudio::StopLoop(LoopId loop, float fadeSeconds)
	{
		TArray<Voice>& voices = S().voices;
		for (int i = 0; i < voices.Num(); i++)
		{
			if (voices[i].Loop != loop || loop == 0) continue;
			if (UAudioComponent* c = voices[i].Component.Get())
			{
				if (fadeSeconds > 0) c->FadeOut(fadeSeconds, 0.0f);
				else c->Stop();
			}
			voices.RemoveAtSwap(i);
			return;
		}
	}

	bool GameAudio::LoopPlaying(LoopId loop)
	{
		const Voice* v = FindLoop(loop);
		return v != nullptr && v->Component.IsValid();
	}

	void GameAudio::StopAll()
	{
		for (Voice& v : S().voices)
			if (UAudioComponent* c = v.Component.Get()) c->Stop();
		S().voices.Reset();
	}

	int GameAudio::Playing() { return S().voices.Num(); }

	void GameAudio::SetPlacement(Placement value)
	{
		if (value == placement) return;
		placement = value;
		State& s = S();
		for (const TPair<uint64, USoundAttenuation*>& entry : s.attenuations) Place(entry.Value->Attenuation, static_cast<float>(entry.Key >> 32));
		for (const Voice& v : s.voices)
			if (UAudioComponent* c = v.Component.Get())
				if (c->AttenuationSettings) c->AdjustAttenuation(c->AttenuationSettings->Attenuation);
	}

	void GameAudio::SetSpatialPlugin(bool on)
	{
		if (on == spatialPlugin) return;
		spatialPlugin = on;
		State& s = S();
		for (const TPair<uint64, USoundAttenuation*>& entry : s.attenuations) Place(entry.Value->Attenuation, static_cast<float>(entry.Key >> 32));
		for (const Voice& v : s.voices)
			if (UAudioComponent* c = v.Component.Get())
				if (c->AttenuationSettings) c->AdjustAttenuation(c->AttenuationSettings->Attenuation);
	}

	void GameAudio::SetStereoFold(bool on)
	{
		stereoFold = on;
		if (S().built) ApplyStereoFold();
	}

	void GameAudio::Declare()
	{
		MusicVolume(); EffectsVolume(); InterfaceVolume(); AmbienceVolume(); VoiceVolume();
		SpokenCommsSetting(); VoiceEngineSetting(); RadioEffectSetting();
		MuteInBackground(); SoundWhilePaused(); DynamicRange();
	}

	const cs::Ref<RangeSetting>& GameAudio::MusicVolume()
	{
		static const cs::Ref<RangeSetting> value = BusSetting("audio.musicVolume", "Music volume", "How loud the music is, under the master volume.",
			"sound soundtrack score loudness", AudioBus::Music);
		return value;
	}

	const cs::Ref<RangeSetting>& GameAudio::EffectsVolume()
	{
		static const cs::Ref<RangeSetting> value = BusSetting("audio.effectsVolume", "Effects volume",
			"How loud weapons, impacts, engines and alerts are, under the master volume.", "sound sfx weapons loudness", AudioBus::Effects);
		return value;
	}

	const cs::Ref<RangeSetting>& GameAudio::InterfaceVolume()
	{
		static const cs::Ref<RangeSetting> value = BusSetting("audio.interfaceVolume", "Interface volume",
			"How loud menu clicks, panels, toasts and hails are, under the master volume.", "sound ui menu clicks loudness", AudioBus::Interface);
		return value;
	}

	const cs::Ref<RangeSetting>& GameAudio::AmbienceVolume()
	{
		static const cs::Ref<RangeSetting> value = BusSetting("audio.ambienceVolume", "Ambience volume",
			"How loud the background hum of space, stations and ships is, under the master volume.", "sound ambient background loudness",
			AudioBus::Ambience);
		return value;
	}

	const cs::Ref<RangeSetting>& GameAudio::VoiceVolume()
	{
		static const cs::Ref<RangeSetting> value = BusSetting("audio.voiceVolume", "Voice volume",
			"How loud the voices on comms are, under the master volume.", "sound speech dialogue comms loudness", AudioBus::Voice);
		return value;
	}

	// US-T649: comms and radio rows in the Sound group, right after Voice volume. View settings: this machine's own, never
	// shared in co-op; a scripted run reads the factory defaults whatever the profile says.
	const cs::Ref<ToggleSetting>& GameAudio::SpokenCommsSetting()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("comms.spoken", SettingGroups::Audio, "Spoken comms", true,
			"Comms and radio lines read aloud; off, they are silent and their text still shows.",
			"voice speech spoken comms radio tts read aloud talk mute");
		return value;
	}

	const cs::Ref<ChoiceSetting>& GameAudio::VoiceEngineSetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("comms.voiceEngine", SettingGroups::Audio, "Voice engine",
			cs::List<cs::String>{ "Auto", "Piper", "Windows" }, 0,
			"What speaks the comms: Auto uses Piper when it is installed and the Windows voice otherwise; Piper or Windows uses only that one.",
			"voice engine tts speech piper windows sapi synthesizer")
			->Applying([](int i) { SpaceGod::Voice::TtsService::SetEngine(PlayerOptions::ScriptedRun() ? 0 : i); });
		return value;
	}

	const cs::Ref<ToggleSetting>& GameAudio::RadioEffectSetting()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("radio.effect", SettingGroups::Audio, "Radio effect", true,
			"The radio sound on spoken lines: a narrow band, a little static and a squelch click before and after.",
			"radio effect static squelch filter band voice comms crackle");
		return value;
	}

	bool GameAudio::SpokenComms() { return PlayerOptions::ScriptedRun() ? SpokenCommsSetting()->FactoryDefault : SpokenCommsSetting()->Value; }

	bool GameAudio::RadioEffectOn() { return PlayerOptions::ScriptedRun() ? RadioEffectSetting()->FactoryDefault : RadioEffectSetting()->Value; }

	const cs::Ref<ToggleSetting>& GameAudio::MuteInBackground()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("audio.muteInBackground", SettingGroups::Audio,
			"Mute when in the background", true, "Silences the game while its window is not the one in front (alt-tabbed or minimized).",
			"sound mute background unfocused focus alt tab minimized")
			->Applying([](bool v) { FApp::SetUnfocusedVolumeMultiplier(v ? 0.0f : 1.0f); });
		return value;
	}

	const cs::Ref<ChoiceSetting>& GameAudio::SoundWhilePaused()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("audio.soundWhilePaused", SettingGroups::Audio, "Sound while paused",
			cs::List<cs::String>{ "Play", "Duck", "Mute" }, 0,
			"What the game's sound does while the pause menu is open: plays on, drops quieter, or goes silent (menu sounds still play).",
			"sound pause paused menu duck mute quiet")
			->Applying([](int i) { whilePaused = i; });
		return value;
	}

	const cs::Ref<ChoiceSetting>& GameAudio::DynamicRange()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("audio.dynamicRange", SettingGroups::Audio, "Dynamic range",
			cs::List<cs::String>{ "Full", "Night" }, 0,
			"Full keeps the gap between quiet and loud; Night compresses it, so quiet sounds come up and loud ones come down (for late play at low volume).",
			"sound dynamic range night mode compression compressor loud quiet")
			->Applying([](int i) { dynamicRange = i; });
		return value;
	}
}
