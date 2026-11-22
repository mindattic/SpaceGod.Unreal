// US-T458/US-T460: sg.Manalith, the Manalith's reference frame (docs/MANALITH_VISUAL_SPEC.md). Travels to Nightglass,
// pauses the simulation clock (the tumble and the shards run on real time, so they keep going while the orbit holds
// still) and holds the free camera 2.666 D from the Manalith (D spans 70% of the frame
// height) on its lit side, and at 20 s after the view first drew it takes the screenshot.
// US-T462: with `reference` the screenshot is instead GridGame's rig rebuilt here, to compare with its Unity frame: a
// 768 x 768 capture of the Manalith alone on black at a held 30 degrees, D at 70% of the height, the camera unrotated
// down world +Z, the spec's white key from view (-0.5, 0.7, 0.5) and mana-blue rim light from (0.4, 0.3, -1) in place
// of the star and the sky (no shadows, as the Unity rig has none), the pose and the shards at t = 20 s (Unity's
// GaidenBuild shot), no tone curve and a bloom over 1. key= fill= rim= exp= rimlight= ambient= bloom= t= override the
// tuned values for a tuning pass.
//   sg.Manalith [shot <file>] [reference] [quit] [key=<f>] [fill=<f>] [rim=<f>] [exp=<f>] [rimlight=<f>] [ambient=<f>] [bloom=<f>] [t=<s>]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnityCompat/UnityScene.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpace.h"
#include "Views/ManalithView.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Systems/Galaxy.h"
#include "Systems/ManalithSystem.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

#include <cmath>
#include <memory>
#include <optional>

using namespace SpaceGod::Core;
using namespace SpaceGod::UnityCompat;
using SpaceGod::UnitySpaceApp;
using SpaceGod::Views::ManalithView;

namespace
{
	UnitySpaceApp* App()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	constexpr float Fov = 30.0f;
	constexpr double ShotAtSeconds = 20.0;

	// ---- US-T462: the reference frame (GridGame's ManalithRig and GaidenBuild's shot, in Unity's view axes)
	constexpr int RefSize = 768;
	constexpr float RefFill = 0.7f;
	const Vector3 KeyFrom(-0.5f, 0.7f, 0.5f), RimFrom(0.4f, 0.3f, -1.0f);
	/// The Unity rig's view space (+z toward the viewer) to its world (+z away from the camera): ManalithRig.ViewToWorld.
	Vector3 ViewToWorld(Vector3 v) { return Vector3(v.x, v.y, -v.z).normalized(); }

	/// Unity's gamma colour space lights and adds in sRGB values: k times a colour there is k^2.2 times it in linear light
	/// (each channel raised alike, so the hue holds). The spec's numbers are Unity's, so the linear renderer here takes
	/// them through this to draw the same pixels.
	float Gamma(float unity) { return std::pow(unity, 2.2f); }

	/// What the reference frame is drawn with: the spec's values carried from Unity's gamma space, then tuned against
	/// the Unity frame (US-T462, six passes compared by colour statistics). The carry alone (key 1, rim 2.44^3, ambient
	/// 0.07) left it darker and more saturated than Unity's, whose default skybox lights and reflects onto the fairly
	/// metallic model, and whose bloom whitens the rim; the override light here has neither, so the key, the ambient
	/// floor and the rim stand in for them.
	struct Tune
	{
		float T = 20.0f;
		// The rim light (0.4) is a real light here, as the Unity rig's, so the material's stand-in for it is off.
		float Key = 1.3f, Fill = Gamma(0.4f);
		float Rim = 5.5f, Exponent = 2.5f, RimLight = 0.0f;
		float Ambient = 0.6f;
		float Glow = 8.0f;
		float Bloom = 1.0f;
	};

	struct Shot
	{
		FString file;
		bool quit = false, askedNewGame = false, travelled = false, shot = false;
		double started = FPlatformTime::Seconds(), arrived = -1, shotAt = -1;
		bool reference = false;
		Tune tune;
		int refFrames = -1;
		std::optional<SpaceGod::Rendering::StarLighting> lightBefore;
		int shadowsBefore = 1;
		TStrongObjectPtr<UTextureRenderTarget2D> target;
		TWeakObjectPtr<USceneCaptureComponent2D> capture;

		SpaceGod::Rendering::StarLighting ReferenceLight() const
		{
			// Far off in each direction (the key is a directional light, the fill a far point light).
			Vector3 key = ViewToWorld(KeyFrom) * 1e9f, rim = ViewToWorld(RimFrom) * 1e9f;
			SpaceGod::Rendering::StarLighting l;
			l.SunPosition = Vector4(key.x, key.y, key.z, 0);
			l.SunColor = Vector4(tune.Key, tune.Key, tune.Key, 1);
			l.Sun2Position = Vector4(rim.x, rim.y, rim.z, 0);
			// <MANA_BLUE> #7590C8 as linear colour, times the Unity rig's RimLightIntensity.
			l.Sun2Color = Vector4(0.178f * tune.Fill, 0.279f * tune.Fill, 0.578f * tune.Fill, 1);
			return l;
		}

		static void SetShadows(int value)
		{
			if (IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.HullLighting.Shadows"))) v->Set(value, ECVF_SetByConsole);
		}

		static int Shadows()
		{
			IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.HullLighting.Shadows"));
			return v ? v->GetInt() : 1;
		}

		USceneCaptureComponent2D* EnsureCapture()
		{
			if (capture.IsValid()) return capture.Get();
			AActor* host = SpaceGod::UnityCompat::UnityScene::Host();
			if (!host) return nullptr;
			auto* c = NewObject<USceneCaptureComponent2D>(host, TEXT("ManalithReferenceCamera"), RF_Transient);
			c->bCaptureEveryFrame = false;
			c->bCaptureOnMovement = false;
			// The Manalith alone (the Unity rig's culling mask): no sky, starfield, orbits or tether, black behind.
			c->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			c->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			// Square, so this horizontal angle is the Unity camera's vertical 30 degrees too; held, never eased.
			c->FOVAngle = Fov;
			c->ShowFlags.SetAtmosphere(false);
			c->ShowFlags.SetFog(false);
			// As ShipThumbnails (US-T116): exposure exactly 1, which the materials' SG_Expose divides by, so the image is the
			// shaders' colour; no tone curve, gamut expansion or vignette (the Unity rig's built-in pipeline has none).
			c->ShowFlags.SetEyeAdaptation(false);
			c->ShowFlags.SetLocalExposure(false);
			c->ShowFlags.SetPostProcessing(true);
			c->ShowFlags.SetBloom(true);
			FPostProcessSettings& pp = c->PostProcessSettings;
			pp.bOverride_AutoExposureMethod = true;
			pp.AutoExposureMethod = AEM_Manual;
			pp.bOverride_AutoExposureBias = true;
			pp.AutoExposureBias = 0;
			pp.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			pp.AutoExposureApplyPhysicalCameraExposure = false;
			pp.bOverride_ToneCurveAmount = true;
			pp.ToneCurveAmount = 0;
			pp.bOverride_ExpandGamut = true;
			pp.ExpandGamut = 0;
			pp.bOverride_VignetteIntensity = true;
			pp.VignetteIntensity = 0;
			pp.bOverride_ColorGain = true;
			pp.ColorGain = FVector4(1, 1, 1, 1);
			pp.bOverride_SceneColorTint = true;
			pp.SceneColorTint = FLinearColor::White;
			pp.bOverride_ColorOffsetShadows = true;
			pp.ColorOffsetShadows = FVector4(0, 0, 0, 0);
			// The Unity rig has no ambient occlusion: Unreal's darkened every fold of the model.
			pp.bOverride_AmbientOcclusionIntensity = true;
			pp.AmbientOcclusionIntensity = 0;
			// The Unity rig's bloom: over 1, at the tuned strength.
			pp.bOverride_BloomThreshold = true;
			pp.BloomThreshold = 1.0f;
			pp.bOverride_BloomIntensity = true;
			pp.BloomIntensity = tune.Bloom;
			c->PostProcessBlendWeight = 1.0f;
			c->SetUsingAbsoluteLocation(true);
			c->SetUsingAbsoluteRotation(true);
			c->RegisterComponent();
			host->AddInstanceComponent(c);
			capture = c;
			return c;
		}

		UTextureRenderTarget2D* EnsureTarget()
		{
			if (target.IsValid()) return target.Get();
			auto* rt = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("ManalithReference"), RF_Transient);
			// sRGB, as ShipThumbnails' (US-T116): the capture writes linear colour.
			rt->RenderTargetFormat = RTF_RGBA8_SRGB;
			rt->ClearColor = FLinearColor::Black;
			rt->InitAutoFormat(RefSize, RefSize);
			rt->UpdateResourceImmediate(true);
			target.Reset(rt);
			return rt;
		}

		/// Steps the reference frame on by one frame; true when it is written (or failed), with `failure` set on a failure.
		bool StepReference(UnitySpaceApp& app, int i, const TCHAR*& failure)
		{
			namespace US = SpaceGod::UnrealSpace;
			ManalithView* view = ManalithView::Current();
			if (!view || !view->Root()) { failure = TEXT("the Manalith is not drawn from its model"); return true; }
			auto backend = SpaceGod::Rendering::RenderBackend::Active();
			if (refFrames < 0)
			{
				view->BeginReference(tune.T);
				view->SetRim(tune.Rim, tune.Exponent, tune.RimLight, tune.Ambient, tune.Glow);
				lightBefore = backend->LightingOverride();
				shadowsBefore = Shadows();
				SetShadows(0);
				refFrames = 0;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: reference frame at t = %.2f s (key %.3g, fill %.3g, rim %.3g^%.3g, rim light %.3g, ambient %.3g, glow %.3g, bloom %.3g)"),
					tune.T, tune.Key, tune.Fill, tune.Rim, tune.Exponent, tune.RimLight, tune.Ambient, tune.Glow, tune.Bloom);
			}
			// Held each frame (the game's own light would come back otherwise); it and the new pose reach the renderer a
			// frame or two late (ShipThumbnails, US-T116), so the capture waits.
			backend->OverrideLighting(ReferenceLight());
			if (++refFrames < 4) return false;

			USceneCaptureComponent2D* c = EnsureCapture();
			UTextureRenderTarget2D* rt = EnsureTarget();
			if (!c || !rt) { failure = TEXT("no capture"); return true; }
			SpaceGod::UnityCompat::UnityScene::SyncNow();
			if (UWorld* world = SpaceGod::UnityCompat::UnityScene::World())
			{
				world->UpdateParameterCollectionInstances(true, true);
				world->SendAllEndOfFrameUpdates();
			}
			// D at 70% of the height at 30 degrees; the camera on -Z of the satellite, unrotated, as the Unity rig's.
			Vector3 at = view->Root()->position();
			float d = static_cast<float>(2 * app.BodyRadii()[i]);
			float dist = d / RefFill / (2 * std::tan(Fov * 0.5f * Mathf::Deg2Rad));
			Vector3 eye = at + Vector3(0, 0, -dist);
			// A Unity camera looks down its +Z, which UnrealSpace maps to Unreal's -Y; a capture looks down its +X.
			static const FQuat LookAlongUnityForward(FVector::UpVector, FMath::DegreesToRadians(-90.0));
			c->SetWorldLocationAndRotation(US::ToUnreal(eye), US::ToUnreal(Quaternion::identity()) * LookAlongUnityForward);
			c->bOverride_CustomNearClippingPlane = true;
			c->CustomNearClippingPlane = US::ClipToUnreal(Mathf::Max(dist - d, dist * 0.05f));
			c->MaxViewDistanceOverride = US::ClipToUnreal(dist + d * 5);
			c->ShowOnlyComponents.Reset();
			for (auto* p : SpaceGod::UnityCompat::UnityScene::ComponentsUnder(view->Root())) c->ShowOnlyComponents.Add(p);
			c->TextureTarget = rt;
			c->CaptureScene();
			c->TextureTarget = nullptr;
			c->ShowOnlyComponents.Reset();

			TArray<FColor> pixels;
			FTextureRenderTargetResource* res = rt->GameThread_GetRenderTargetResource();
			if (!res || !res->ReadPixels(pixels) || pixels.Num() != RefSize * RefSize) failure = TEXT("could not read the reference frame back");
			else
			{
				FImage image(RefSize, RefSize, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				TArrayView64<FColor> out = image.AsBGRA8();
				for (int p = 0; p < pixels.Num(); p++)
				{
					FColor px = pixels[p];
					px.A = 255;
					out[p] = px;
				}
				if (!FImageUtils::SaveImageByExtension(*file, image)) failure = TEXT("could not write the reference frame");
				else UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: reference frame %s"), *file);
			}
			EndReference();
			return true;
		}

		void EndReference()
		{
			if (refFrames < 0) return;
			if (ManalithView* view = ManalithView::Current())
			{
				view->EndReference();
				view->SetRim(1.5f, 3.0f, 0.4f, 0.012f, ManalithView::GlowIntensity);
			}
			SpaceGod::Rendering::RenderBackend::Active()->OverrideLighting(lightBefore);
			SetShadows(shadowsBefore);
			refFrames = -1;
		}

		int Index(UnitySpaceApp& app) const
		{
			cs::Ref<BodyDefinition> b;
			return app.Catalog()->TryGet(ManalithSystem::SatelliteName, b) ? b->Index : -1;
		}

		void Pose(UnitySpaceApp& app, int i)
		{
			DVec3 at = app.BodyScenes()[i];
			double d = 2 * app.BodyRadii()[i];
			// The game eases its own field of view every frame (pace, cruise, helm), so frame D at 70% of the height at
			// the field of view it really draws with, rather than the spec rig's 30 degrees.
			float fov = app.UnityCamera ? app.UnityCamera->fieldOfView : Fov;
			double dist = d / 0.7 / (2 * std::tan(fov * 0.5 * 3.14159265358979 / 180));
			// From the star's side, raised a little and off to one side, so the key light falls upper left.
			DVec3 toStar = (app.BodyScenes()[0] - at).Normalized();
			DVec3 side = DVec3::Cross(toStar, DVec3(0, 1, 0)).Normalized();
			DVec3 eye = at + (toStar * 0.75 + side * 0.55 + DVec3(0, 0.35, 0)).Normalized() * dist;
			DVec3 look = (at - eye).Normalized();
			auto rot = Quaternion::LookRotation(Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), Vector3::up());
			app.SpaceCamera->SetPose(eye, rot);
		}

		bool Finish(const TCHAR* failure)
		{
			EndReference();
			if (auto* app = App())
			{
				app->Clock->SetPaused(false);
			}
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Manalith: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}

		bool Tick()
		{
			auto* app = App();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - started > 180.0 ? Finish(TEXT("no game came up")) : true;
			}
			if (now - started > 300.0) return Finish(TEXT("timed out"));
			if (!travelled)
			{
				auto system = app->Galaxy->Realize(ManalithSystem::SystemName, app->Clock->Jd);
				if (system == nullptr) return Finish(TEXT("Nightglass would not build (is it charted?)"));
				if (!app->SpaceCamera->FocusTarget.IsNone()) app->SpaceCamera->ExitFocus();
				app->CheatTravelTo(system);
				travelled = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: travelling to %s, %.2f ly from Sol"), UTF8_TO_TCHAR(system->Name.c_str()),
					system->PositionLy.Length());
				return true;
			}
			bool travelling = app->Travel != nullptr && app->Travel->Active;
			if (travelling || app->ActiveSystem == nullptr || !ManalithSystem::ObservationOnly(app->ActiveSystem->Name)) return true;
			int i = Index(*app);
			if (i < 0) return Finish(TEXT("Nightglass has no Manalith"));
			if (arrived < 0)
			{
				arrived = now;
				app->Clock->SetPaused(true);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: arrived; Manalith drawn radius %.6g scene units"), app->BodyRadii()[i]);
			}
			Pose(*app, i);
			if (reference)
			{
				// A few seconds for the model and its textures to stream in at this range, then the reference frame.
				if (now - arrived < 4.0) return true;
				const TCHAR* failure = nullptr;
				return !StepReference(*app, i, failure) || Finish(failure);
			}
			if (!shot && now - arrived >= ShotAtSeconds)
			{
				shot = true;
				shotAt = now;
				if (!file.IsEmpty())
				{
					FScreenshotRequest::RequestScreenshot(file, false, false);
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Manalith: screenshot %s"), *file);
				}
			}
			return !(shot && now - shotAt > 1.5) || Finish(nullptr);
		}
	};

	void Start(const TArray<FString>& args)
	{
		auto run = std::make_shared<Shot>();
		for (int i = 0; i < args.Num(); i++)
		{
			FString key, value;
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->file = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->quit = true;
			else if (args[i].Equals(TEXT("reference"), ESearchCase::IgnoreCase)) run->reference = true;
			else if (args[i].Split(TEXT("="), &key, &value))
			{
				float v = FCString::Atof(*value);
				Tune& t = run->tune;
				if (key.Equals(TEXT("key"), ESearchCase::IgnoreCase)) t.Key = v;
				else if (key.Equals(TEXT("fill"), ESearchCase::IgnoreCase)) t.Fill = v;
				else if (key.Equals(TEXT("rim"), ESearchCase::IgnoreCase)) t.Rim = v;
				else if (key.Equals(TEXT("exp"), ESearchCase::IgnoreCase)) t.Exponent = v;
				else if (key.Equals(TEXT("rimlight"), ESearchCase::IgnoreCase)) t.RimLight = v;
				else if (key.Equals(TEXT("ambient"), ESearchCase::IgnoreCase)) t.Ambient = v;
				else if (key.Equals(TEXT("bloom"), ESearchCase::IgnoreCase)) t.Bloom = v;
				else if (key.Equals(TEXT("glow"), ESearchCase::IgnoreCase)) t.Glow = v;
				else if (key.Equals(TEXT("t"), ESearchCase::IgnoreCase)) t.T = v;
			}
		}
		if (run->reference && run->file.IsEmpty()) run->file = FPaths::ProjectSavedDir() / TEXT("manalith-reference.png");
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float) { return run->Tick(); }));
	}

	FAutoConsoleCommand GManalithCommand(
		TEXT("sg.Manalith"),
		TEXT("US-T458/US-T460: sg.Manalith [shot <file>] [reference] [quit] travels to Nightglass and holds the camera on the Manalith as the "
			"shared spec frames it (D at 70% of the height at the game's own field of view, the simulation paused), screenshotting it 20 s in. "
			"US-T462: with reference, the shot is GridGame's Unity rig rebuilt (768 px on black, 30 degrees held, the spec's white key and "
			"rim light, t = 20 s, no tone curve), to compare with Unity.GridGame/docs/images/manalith-unity-00.png; key= fill= rim= exp= "
			"rimlight= ambient= bloom= t= tune it."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
