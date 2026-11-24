#include "GameSettingsHost.h"
#include "AccessibilityOptions.h" // US-T634
#include "ActiveProfile.h"
#include "Audio/AudioOutput.h" // US-T612
#include "Audio/GameAudio.h" // US-T240
#include "DisplayExtras.h" // US-T544
#include "EngineSettings.h"
#include "GraphicsSettings.h"
#include "InterfaceOptions.h" // US-T559
#include "PlayerOptions.h" // US-T252
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Console/GameConsole.h" // US-T620
#include "Input/DriftKey.h"
#include "Input/Gamepad.h" // US-T250
#include "Input/KeyBindings.h" // US-T249
#include "Rendering/Pipeline/RenderBackend.h"
#include "Views/BattleFx.h"
#include "UnityCompat/UnityTypes.h"
#include "Settings/SettingsRegistry.h"
#include "Settings/GameSetting.h"
#include "Persistence/Profiles.h"

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
		SettingsRegistry::AddDeclarationSource([] { DisplayExtras::Declare(); }); // US-T544: the Display page's brightness, HDR and background cap
		SettingsRegistry::AddDeclarationSource([] { PlayerOptions::Declare(); }); // US-T252: Settings > Gameplay and Accessibility
		SettingsRegistry::AddDeclarationSource([] { InterfaceOptions::Declare(); }); // US-T559: Settings > Interface
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
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::Gamepad::Declare(); }); // US-T250: the pad's dead zone, aim and buttons
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::KeyBindings::Declare(); }); // US-T249: Controls > Key bindings
		SettingsRegistry::AddDeclarationSource([]
		{
			using Rendering::RenderPipelineSettings;
			RenderPipelineSettings::Pipeline(); RenderPipelineSettings::PhysicalSky(); RenderPipelineSettings::Volumetrics();
			RenderPipelineSettings::Exposure(); RenderPipelineSettings::ExposureCompensation(); RenderPipelineSettings::SunBrightness();
			RenderPipelineSettings::RayTracing();
		});
		SettingsRegistry::AddDeclarationSource([] { Views::BattleFx::EffectsBudget(); });
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
		auto store = cs::New<ProfileSettingStore>(ActiveProfile::Current());
		{
			GraphicsSettings::LoadScope loading; // the LOD budgets load before the preset: not the player making it Custom
			Registry()->Bind(store);
		}
		GraphicsSettings::AfterBind(store); // US-T251: a first launch benchmarks for its preset
	}
}
