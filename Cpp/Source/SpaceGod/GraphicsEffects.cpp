#include "GraphicsEffects.h"

#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#include "RHI.h"
#include "RenderUtils.h"

#include "PlayerOptions.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "UnityCompat/UnityScene.h"

#include <optional>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// A console variable a row holds while it is off its default: the engine's own value is kept from the moment the
		/// row first takes it and put back when the row returns to its default (or a scripted run starts).
		struct Held
		{
			const TCHAR* Name;
			bool Holding = false;
			float Saved = 0;

			void Set(std::optional<float> value)
			{
				IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(Name);
				if (v == nullptr) return;
				if (value.has_value() && !PlayerOptions::ScriptedRun())
				{
					if (!Holding) { Saved = v->GetFloat(); Holding = true; }
					if (v->GetFloat() != *value) v->Set(*value, ECVF_SetByCode);
				}
				else if (Holding)
				{
					Holding = false;
					v->Set(Saved, ECVF_SetByCode);
				}
			}
		};

		Held motionBlurAmount{ TEXT("r.MotionBlur.Amount") };
		Held motionBlurQuality{ TEXT("r.MotionBlurQuality") };
		Held aoLevels{ TEXT("r.AmbientOcclusionLevels") };
		Held anisotropy{ TEXT("r.MaxAnisotropy") };
		Held sharpen{ TEXT("r.Tonemapper.Sharpen") };

		// Motion blur's options past Off: how much of a frame's movement smears (r.MotionBlur.Amount), and its quality.
		constexpr float BlurAmounts[] = { 0.2f, 0.4f, 0.6f };
		constexpr float BlurQualities[] = { 2, 3, 4 };
		// Anisotropic filtering's options past Auto (the Textures row's level decides): the most samples a texel takes.
		constexpr float AnisotropyLevels[] = { 1, 2, 4, 8, 16 };
		/// Sharpening at 100% (r.Tonemapper.Sharpen; past ~1.5 edges ring).
		constexpr double SharpenAtFull = 1.5;

		// US-T700: the post-process rows, as last put into effect (written by their Applying callbacks only).
		int dofLevel = 0;   // 0 off, then the aperture's index (+1) in DofFstops
		double grain = 0;   // percent
		double fringe = 0;  // percent
		int flareLevel = 0; // 0 off, then the index (+1) in FlareIntensities
		// Depth of field's options past Off: the aperture (lower = shallower focus, more blur either side).
		constexpr float DofFstops[] = { 5.6f, 2.8f, 1.4f };
		/// Film grain at 100% (FilmGrainIntensity; HyperspaceToll's own grain is 0.55).
		constexpr double GrainAtFull = 1.0;
		/// Chromatic aberration at 100% (SceneFringeIntensity, which Unreal caps at 5).
		constexpr double FringeAtFull = 4.0;
		// Lens flares' options past Off (LensFlareIntensity; Unreal's own default when enabled is 1).
		constexpr float FlareIntensities[] = { 0.4f, 1.0f };
		/// How far ahead the autofocus trace looks, in centimetres (100 km).
		constexpr double FocusReach = 1.0e7;
		/// Under the render backend's volume (100) and HyperspaceToll's (1000), which blend over it.
		constexpr float VolumePriority = 50;

		TWeakObjectPtr<APostProcessVolume> volume;
		FTSTicker::FDelegateHandle ticker;

		bool PostActive()
		{
			return !PlayerOptions::ScriptedRun() && (dofLevel > 0 || grain > 0 || fringe > 0 || flareLevel > 0);
		}

		/// Drops every override the volume holds, so it adds nothing to the look.
		void ClearOverrides(FPostProcessSettings& s)
		{
			s.bOverride_DepthOfFieldFocalDistance = false;
			s.bOverride_DepthOfFieldFstop = false;
			s.bOverride_FilmGrainIntensity = false;
			s.bOverride_SceneFringeIntensity = false;
			s.bOverride_LensFlareIntensity = false;
		}

		/// The distance to whatever the player's camera looks straight at, or 0 when it meets nothing.
		float FocusDistance(UWorld* world)
		{
			APlayerController* pc = world->GetFirstPlayerController();
			APlayerCameraManager* cam = pc != nullptr ? pc->PlayerCameraManager.Get() : nullptr;
			if (cam == nullptr) return 0;
			FVector from = cam->GetCameraLocation();
			FVector to = from + cam->GetCameraRotation().Vector() * FocusReach;
			FCollisionQueryParams query(SCENE_QUERY_STAT(GraphicsEffectsFocus), true);
			FHitResult hit;
			if (!world->LineTraceSingleByChannel(hit, from, to, ECC_Visibility, query) || !hit.bBlockingHit) return 0;
			return FMath::Max(static_cast<float>(hit.Distance), 1.0f);
		}

		/// Puts the post-process rows into the volume (spawning it in the current world when one is needed); false once
		/// every row is back at its default (or a scripted run starts), which also ends the ticker.
		bool ApplyPost(float)
		{
			if (!PostActive())
			{
				if (volume.IsValid()) ClearOverrides(volume->Settings);
				ticker.Reset();
				return false;
			}
			UWorld* world = UnityCompat::UnityScene::World();
			if (world == nullptr) return true; // no scene yet: keep waiting for one
			APostProcessVolume* v = volume.IsValid() && volume->GetWorld() == world ? volume.Get() : nullptr;
			if (v == nullptr)
			{
				FActorSpawnParameters spawn;
				spawn.ObjectFlags |= RF_Transient;
				v = world->SpawnActor<APostProcessVolume>(spawn);
				if (v == nullptr) return true;
				v->bUnbound = true;
				v->Priority = VolumePriority;
				v->BlendWeight = 1;
				volume = v;
			}
			FPostProcessSettings& s = v->Settings;
			float focus = dofLevel > 0 ? FocusDistance(world) : 0.0f;
			s.bOverride_DepthOfFieldFocalDistance = focus > 0;
			s.DepthOfFieldFocalDistance = focus;
			s.bOverride_DepthOfFieldFstop = focus > 0;
			if (dofLevel > 0) s.DepthOfFieldFstop = DofFstops[dofLevel - 1];
			s.bOverride_FilmGrainIntensity = grain > 0;
			s.FilmGrainIntensity = static_cast<float>(grain / 100.0 * GrainAtFull);
			s.bOverride_SceneFringeIntensity = fringe > 0;
			s.SceneFringeIntensity = static_cast<float>(fringe / 100.0 * FringeAtFull);
			s.bOverride_LensFlareIntensity = flareLevel > 0;
			s.LensFlareIntensity = flareLevel > 0 ? FlareIntensities[flareLevel - 1] : 0.0f;
			return true;
		}

		/// Each post-process row calls this as it changes: applies at once and keeps the ticker for as long as it is needed.
		void PostChanged()
		{
			if (ApplyPost(0) && !ticker.IsValid())
				ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ApplyPost), 0.0f);
		}
	}

	const cs::Ref<ChoiceSetting>& GraphicsEffects::MotionBlur()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.motionBlur", SettingGroups::Graphics,
			"Motion blur", cs::List<cs::String>{ "Off", "Low", "Medium", "High" }, 0,
			"Smears what moves fast across the screen, as a camera's shutter would. Off keeps every star and hull sharp.",
			"motion blur smear speed camera shutter post processing")->Applying([](int i)
			{
				bool on = i > 0 && i <= 3;
				motionBlurAmount.Set(on ? std::optional<float>(BlurAmounts[i - 1]) : std::nullopt);
				motionBlurQuality.Set(on ? std::optional<float>(BlurQualities[i - 1]) : std::nullopt);
			});
		return value;
	}

	const cs::Ref<ToggleSetting>& GraphicsEffects::AmbientOcclusion()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("graphics.ambientOcclusion", SettingGroups::Graphics,
			"Ambient occlusion", true,
			"Darkens creases, corners and the gaps between hull plates where light reaches less. Off is a little faster.",
			"ambient occlusion ao ssao shadows creases contact post processing")->Applying([](bool on)
			{
				aoLevels.Set(on ? std::nullopt : std::optional<float>(0.0f));
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsEffects::AnisotropicFiltering()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.anisotropicFiltering", SettingGroups::Graphics,
			"Anisotropic filtering", cs::List<cs::String>{ "Auto", "Off", "2x", "4x", "8x", "16x" }, 0,
			"Keeps textures seen at a glancing angle, such as a station's long flank, sharp. Auto follows the Textures row.",
			"anisotropic filtering af texture sharpness angle quality")->Applying([](int i)
			{
				anisotropy.Set(i > 0 && i <= 5 ? std::optional<float>(AnisotropyLevels[i - 1]) : std::nullopt);
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsEffects::Sharpening()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.sharpening", SettingGroups::Graphics,
			"Sharpening", 0, 0, 100, 5,
			"Crisps up edges softened by TSR, TAA or a lower resolution scale. Too much makes edges ring.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"sharpening sharpen crisp blur soft tsr taa upscale post processing")->Applying([](double v)
			{
				sharpen.Set(v > 0 ? std::optional<float>(static_cast<float>(v / 100.0 * SharpenAtFull)) : std::nullopt);
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsEffects::DepthOfField()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.depthOfField", SettingGroups::Graphics,
			"Depth of field", cs::List<cs::String>{ "Off", "Low", "Medium", "High" }, 0,
			"Focuses on whatever is straight ahead and softens what lies nearer or farther, as a camera lens would.",
			"depth of field dof focus bokeh blur lens camera post processing")->Applying([](int i)
			{
				dofLevel = i > 0 && i <= 3 ? i : 0;
				PostChanged();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsEffects::FilmGrain()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.filmGrain", SettingGroups::Graphics,
			"Film grain", 0, 0, 100, 5,
			"A fine, shifting noise over the picture, as on film stock.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"film grain noise cinematic post processing")->Applying([](double v)
			{
				grain = v > 0 ? v : 0;
				PostChanged();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsEffects::ChromaticAberration()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.chromaticAberration", SettingGroups::Graphics,
			"Chromatic aberration", 0, 0, 100, 5,
			"Splits the colours apart toward the edges of the screen, as a cheap lens does.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"chromatic aberration fringe colour color split lens edges post processing")->Applying([](double v)
			{
				fringe = v > 0 ? v : 0;
				PostChanged();
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsEffects::LensFlares()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.lensFlares", SettingGroups::Graphics,
			"Lens flares", cs::List<cs::String>{ "Off", "Low", "High" }, 0,
			"Ghosts and halos thrown across the screen by suns and other bright lights, as off a camera's lens.",
			"lens flares flare ghosts halo sun glare bright lights post processing")->Applying([](int i)
			{
				flareLevel = i > 0 && i <= 2 ? i : 0;
				PostChanged();
			});
		return value;
	}

	namespace
	{
		// US-T596: as last put into effect by their Applying callbacks.
		int bloomLevel = 2;    // 0 off, 1 low, 2 high (the game's own bloom)
		double vignette = 0;   // percent
		// Bloom's options: Off, Low (half the game's bloom), High (as drawn).
		constexpr float BloomScales[] = { 0.0f, 0.5f, 1.0f };
		/// Vignette at 100% (VignetteIntensity; Unreal's own default is 0.4, HyperspaceToll's 1).
		constexpr double VignetteAtFull = 1.0;
		Held hardwareRayTracing{ TEXT("r.Lumen.HardwareRayTracing") };

		void ReapplyBackend()
		{
			if (Rendering::UnrealRenderBackend* b = Rendering::UnrealRenderBackend::Existing()) b->Reapply();
		}

		bool RayTracingAvailable()
		{
			return GIsRHIInitialized && IsRayTracingEnabled()
				&& IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing")) != nullptr;
		}
	}

	float GraphicsEffects::BloomScale()
	{
		return PlayerOptions::ScriptedRun() ? 1.0f : BloomScales[FMath::Clamp(bloomLevel, 0, 2)];
	}

	float GraphicsEffects::VignetteIntensity()
	{
		return PlayerOptions::ScriptedRun() ? 0.0f : static_cast<float>(vignette / 100.0 * VignetteAtFull);
	}

	const cs::Ref<ChoiceSetting>& GraphicsEffects::Bloom()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.bloom", SettingGroups::Graphics,
			"Bloom", cs::List<cs::String>{ "Off", "Low", "High" }, 2,
			"The glow that spills around suns, engines and other bright lights. High is the game's own; Off keeps them crisp.",
			"bloom glow bright lights sun engines halo post processing")->Applying([](int i)
			{
				bloomLevel = i >= 0 && i <= 2 ? i : 2;
				ReapplyBackend();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsEffects::Vignette()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.vignette", SettingGroups::Graphics,
			"Vignette", 0, 0, 100, 5,
			"Darkens the corners and edges of the screen, as a camera lens does.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"vignette dark edges corners lens cinematic post processing")->Applying([](double v)
			{
				vignette = v > 0 ? v : 0;
				ReapplyBackend();
			});
		return value;
	}

	const cs::Ref<ToggleSetting>& GraphicsEffects::HardwareRayTracing()
	{
		static const bool engineDefault = []
		{
			IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
			return v != nullptr && v->GetInt() != 0;
		}();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("graphics.hardwareRayTracing", SettingGroups::Graphics,
			"Hardware ray tracing", engineDefault,
			"Lumen traces light on the GPU's ray tracing cores: truer reflections and bounced light. Costs GPU time.",
			"hardware ray tracing rtx dxr lumen reflections global illumination")->Applying([](bool on)
			{
				hardwareRayTracing.Set(on != engineDefault ? std::optional<float>(on ? 1.0f : 0.0f) : std::nullopt);
			});
		return value;
	}

	namespace
	{
		double dustPercent = 50; // US-T729: as last put into effect
	}

	double GraphicsEffects::DustDensity()
	{
		return PlayerOptions::ScriptedRun() ? 0.0 : dustPercent / 100.0;
	}

	const cs::Ref<RangeSetting>& GraphicsEffects::SpaceDust()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.spaceDust", SettingGroups::Graphics,
			"Space dust", 50, 0, 100, 5,
			"Specks of dust drifting round the cockpit or the held seat, streaming past as you fly, so speed and a big hull's size read.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::Fmt(v, "0"), "%"); }),
			"space dust motes particles speed parallax scale cockpit")->Applying([](double v)
			{
				dustPercent = v > 0 ? v : 0;
			});
		return value;
	}

	void GraphicsEffects::Declare()
	{
		MotionBlur(); AmbientOcclusion(); AnisotropicFiltering(); Sharpening();
		DepthOfField(); FilmGrain(); ChromaticAberration(); LensFlares(); // US-T700
		Bloom(); Vignette(); // US-T596
		SpaceDust(); // US-T729
		if (RayTracingAvailable()) HardwareRayTracing(); // hidden where the GPU or project has no ray tracing
	}
}
