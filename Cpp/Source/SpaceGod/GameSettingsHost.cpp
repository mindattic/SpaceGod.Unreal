#include "GameSettingsHost.h"
#include "AccessibilityOptions.h" // US-T634
#include "ActiveProfile.h"
#include "Audio/AudioOutput.h" // US-T612
#include "Audio/GameAudio.h" // US-T240
#include "DisplayExtras.h" // US-T544
#include "EngineSettings.h"
#include "GraphicsEffects.h" // US-T549
#include "GraphicsSettings.h"
#include "InterfaceOptions.h" // US-T559
#include "PlayerOptions.h" // US-T252
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Console/GameConsole.h" // US-T620
#include "Coop/CoopOptions.h" // US-T562
#include "Input/DriftKey.h"
#include "Input/Gamepad.h" // US-T250
#include "Input/Joystick.h" // US-T838
#include "Input/MouseFlight.h" // US-T736
#include "Input/KeyBindings.h" // US-T249
#include "Rendering/Pipeline/RenderBackend.h"
#include "Views/BattleFx.h"
#include "Views/WorldFx.h" // US-T796
#include "UnityCompat/UnityTypes.h"
#include "Settings/SettingsRegistry.h"
#include "Settings/GameSetting.h"
#include "Settings/ShippedDefaults.h"
#include "Persistence/Profiles.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	const cs::Ref<SettingsRegistry>& GameSettingsHost::Registry() { return SettingsRegistry::Default(); }

	void GameSettingsHost::RegisterRuntimeSources()
	{
		// US-T70: URP's post-processing settings first. In Unity they live in an assembly of their own
		// (Unity.SpaceGod.Rendering.Urp) that declares them before the game's Runtime does, so the original's Render pipeline
		// page reads URP's rows, then render.msaa (graphics.antiAliasing since US-T251), then the pipeline and HDRP's (SpaceGod.Unity.CSharp's audit; the Unreal C#
		// copy's Bootstrap.MatchSettingsOrder, 57c111a). Nothing touches these settings before this discovery (GameSettingsHost::Bind).
		SettingsRegistry::AddDeclarationSource([]
		{
			using Rendering::UrpPostProcessSettings;
			UrpPostProcessSettings::Enabled(); UrpPostProcessSettings::Metering(); UrpPostProcessSettings::Compensation();
			UrpPostProcessSettings::Iso(); UrpPostProcessSettings::Shutter(); UrpPostProcessSettings::Aperture();
			UrpPostProcessSettings::Bloom(); UrpPostProcessSettings::BloomThreshold(); UrpPostProcessSettings::SunBrightness();
			UrpPostProcessSettings::Tonemapping(); UrpPostProcessSettings::Contrast(); UrpPostProcessSettings::Saturation();
			UrpPostProcessSettings::Temperature();
		});
		// The Runtime assembly's [SettingsSource] classes, each accessor touched in C# field order.
		SettingsRegistry::AddDeclarationSource([] { EngineSettings::FrameRateCap(); EngineSettings::MasterVolume(); });
		SettingsRegistry::AddDeclarationSource([] { Audio::GameAudio::Declare(); }); // US-T240: the bus volumes, under the master volume
		SettingsRegistry::AddDeclarationSource([] { Audio::AudioOutput::Declare(); }); // US-T612: the output device
		// US-T251: the rest of Settings > Graphics (the preset, scalability knobs, anti-aliasing (render.msaa before),
		// resolution scale, upscaler and field of view), right under the frame-rate cap.
		SettingsRegistry::AddDeclarationSource([] { GraphicsSettings::Declare(); });
		SettingsRegistry::AddDeclarationSource([] { GraphicsEffects::Declare(); }); // US-T549, US-T700: motion blur, AO, anisotropy, sharpening, depth of field, grain, aberration, flares
		SettingsRegistry::AddDeclarationSource([] { DisplayExtras::Declare(); }); // US-T544: the Display page's brightness, HDR and background cap
		SettingsRegistry::AddDeclarationSource([] { PlayerOptions::Declare(); }); // US-T252: Settings > Gameplay and Accessibility
		SettingsRegistry::AddDeclarationSource([] { InterfaceOptions::Declare(); }); // US-T559: Settings > Interface
		SettingsRegistry::AddDeclarationSource([] { Coop::CoopOptions::Declare(); }); // US-T562: Settings > Multiplayer
		SettingsRegistry::AddDeclarationSource([] { AccessibilityOptions::Declare(); }); // US-T634: the colour-blind filter
		SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::AutoReorientSetting(); });
		SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::TutorialHintsSetting(); }); // US-T254: the first-time hints
		SettingsRegistry::AddDeclarationSource([]
		{
			Cameras::SpaceCamera::LookSensitivitySetting(); Cameras::SpaceCamera::InvertLook();
			Cameras::SpaceCamera::LookHorizontal(); Cameras::SpaceCamera::LookVertical(); Cameras::SpaceCamera::MouseSmoothing(); // US-T557
			Cameras::SpaceCamera::DeclareTuning(); // US-T537
		});
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::DriftKey::ModeSetting(); }); // US-T165: drift on R, hold or toggle
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::MouseFlight::Declare(); }); // US-T736: mouse flight and auto-level
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::Gamepad::Declare(); }); // US-T250: the pad's dead zone, aim and buttons
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::Joystick::Declare(); }); // US-T838: Controls > the joystick's axes
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::KeyBindings::Declare(); }); // US-T249: Controls > Key bindings
		SettingsRegistry::AddDeclarationSource([]
		{
			using Rendering::RenderPipelineSettings;
			RenderPipelineSettings::Pipeline(); RenderPipelineSettings::PhysicalSky(); RenderPipelineSettings::Volumetrics();
			RenderPipelineSettings::Exposure(); RenderPipelineSettings::ExposureCompensation(); RenderPipelineSettings::SunBrightness();
			RenderPipelineSettings::RayTracing();
		});
		SettingsRegistry::AddDeclarationSource([] { Views::BattleFx::EffectsBudget(); Views::WorldFx::EffectsBudget(); });
		SettingsRegistry::AddDeclarationSource([] { GameConsole::Declare(); }); // US-T620: Developer > Console
	}

	void GameSettingsHost::Bind()
	{
		if (!discovered)
		{
			discovered = true;
			RegisterRuntimeSources();
			SettingsRegistry::DiscoverLoadedDeclarations();
			Registry()->StoreFailed += [](cs::Ref<GameSetting> s, const cs::Exception& e)
			{
				Debug::LogError(cs::Cat("[Unity.SpaceGod] Saving the setting '", s->Key, "' failed: ", e.what()));
			};
		}
		cs::Dictionary<cs::String, cs::String> shipped;
		try { shipped = ReadShippedDefaults(); }
		catch (const cs::FormatException& e)
		{
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] The shipped defaults '", ShippedDefaultsPath(), "' could not be read, so the declared defaults stand: ", e.what()));
		}
		Registry()->ApplyShippedDefaults(shipped); // US-T512: before the profile, whose values still stand over these
		auto store = cs::New<ProfileSettingStore>(ActiveProfile::Current());
		{
			GraphicsSettings::LoadScope loading; // the LOD budgets load before the preset: not the player making it Custom
			Registry()->Bind(store);
		}
		GraphicsSettings::AfterBind(store); // US-T251: a first launch benchmarks for its preset
		Coop::CoopOptions::AfterBind(); // US-T562: the old co-op name carries over
	}

	cs::String GameSettingsHost::ShippedDefaultsPath()
	{
		FString path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Settings"), TEXT("shipped-defaults.json")));
		return cs::String(TCHAR_TO_UTF8(*path));
	}

	cs::Dictionary<cs::String, cs::String> GameSettingsHost::ReadShippedDefaults()
	{
		FString path(UTF8_TO_TCHAR(ShippedDefaultsPath().c_str()));
		FString text;
		if (!FPaths::FileExists(path) || !FFileHelper::LoadFileToString(text, *path)) return {};
		return ShippedDefaults::Parse(cs::String(TCHAR_TO_UTF8(*text)));
	}

	void GameSettingsHost::WriteShippedDefaults(const cs::Dictionary<cs::String, cs::String>& defaults)
	{
		FString path(UTF8_TO_TCHAR(ShippedDefaultsPath().c_str()));
		if (defaults.Count() == 0)
		{
			if (FPaths::FileExists(path) && !IFileManager::Get().Delete(*path)) throw cs::IOException(cs::Cat("Could not delete '", ShippedDefaultsPath(), "'."));
			return;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(path), true);
		FString text(UTF8_TO_TCHAR(ShippedDefaults::Write(defaults).c_str()));
		if (!FFileHelper::SaveStringToFile(text, *path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			throw cs::IOException(cs::Cat("Could not write '", ShippedDefaultsPath(), "'."));
	}

	cs::List<cs::Ref<GameSetting>> GameSettingsHost::Select(const cs::String& groupOrKey)
	{
		if (groupOrKey.empty()) return Registry()->All();
		if (auto s = Registry()->Find(groupOrKey)) return { s };
		return Registry()->InGroup(groupOrKey);
	}

	cs::List<cs::String> GameSettingsHost::LockIn(const cs::String& groupOrKey)
	{
		auto file = ReadShippedDefaults();
		const auto before = file;
		cs::List<cs::Ref<GameSetting>> chosen;
		for (const auto& s : Select(groupOrKey))
		{
			if (s->IsDefault()) continue;
			file[s->Key] = s->Format();
			chosen.Add(s);
		}
		cs::List<cs::String> locked;
		if (chosen.Count() == 0) return locked;
		Registry()->ApplyShippedDefaults(file);
		bool refused = false;
		for (const auto& s : chosen)
		{
			if (Registry()->IsLockedIn(s->Key))
			{
				Registry()->Remember(*s); // at its default now: the profile forgets it
				locked.Add(s->Key);
				continue;
			}
			refused = true;
			cs::String was;
			if (before.TryGetValue(s->Key, was)) file[s->Key] = was;
			else file.Remove(s->Key);
		}
		if (refused) Registry()->ApplyShippedDefaults(file);
		WriteShippedDefaults(file);
		return locked;
	}

	cs::List<cs::String> GameSettingsHost::Unlock(const cs::String& groupOrKey)
	{
		auto file = ReadShippedDefaults();
		cs::List<cs::String> unlocked;
		cs::List<cs::Ref<GameSetting>> settings;
		for (const auto& s : Select(groupOrKey))
		{
			if (!file.Remove(s->Key)) continue;
			unlocked.Add(s->Key);
			settings.Add(s);
		}
		if (!groupOrKey.empty() && file.Remove(groupOrKey)) unlocked.Add(groupOrKey); // a key no setting declares any more
		if (groupOrKey.empty())
		{
			for (const auto& key : file.Keys()) unlocked.Add(key);
			file.Clear();
		}
		if (unlocked.Count() == 0) return unlocked;
		Registry()->ApplyShippedDefaults(file);
		for (const auto& s : settings) Registry()->Remember(*s);
		WriteShippedDefaults(file);
		return unlocked;
	}

	bool GameSettingsHost::MovesDigests(const cs::String& key)
	{
		auto s = Registry()->Find(key);
		if (s == nullptr) return true;
		for (const char* g : { SettingGroups::Graphics, SettingGroups::RenderPipeline, SettingGroups::RenderBudget, SettingGroups::Controls,
			SettingGroups::Audio, SettingGroups::Interface, SettingGroups::Multiplayer, SettingGroups::Developer })
			if (s->Group == g) return false;
		return true;
	}
}
