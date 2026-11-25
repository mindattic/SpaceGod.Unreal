// US-T716: sg.Origin, the floating origin and the render scale round the seat (docs/TrueScale.md section 5.2): where the
// origin is (the camera's scene position: everything is drawn at scene - camera), Unreal units per scene unit, the near
// plane and the far field.

#include "UnitySpaceApp.h"

#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpace.h"

#include "Camera/SpaceCamera.h"
#include "ScaleModel.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

using SpaceGod::UnitySpaceApp;
namespace OriginUS = SpaceGod::UnrealSpace;

namespace
{
	UnitySpaceApp* OriginApp()
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

	void PrintOrigin()
	{
		auto* app = OriginApp();
		double cm = OriginUS::CmPerUnit();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Origin: %.6g cm per scene unit (%.6g cm per true km of hardware), near plane %.4g cm, far field %.4g cm"),
			cm, cm * (app != nullptr ? app->Scale()->ShipLength(1.0) : 0.0), GNearClippingPlane, OriginUS::FarFieldCm());
		if (app == nullptr || app->SpaceCamera == nullptr)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Origin: no game running"));
			return;
		}
		auto o = app->SpaceCamera->ScenePosition;
		double seatKm = app->InhabitedSizeKm();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Origin: origin (camera) at scene (%.9g, %.9g, %.9g), %.6g scene units from the Sun"),
			o.X, o.Y, o.Z, o.Length());
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Origin: near plane %.6g scene units, seat %s (%.4g m), seat zoom %.3f, blend %.2f"),
			app->UnityCamera != nullptr ? app->UnityCamera->nearClipPlane : 0.0f, seatKm > 0 ? TEXT("held") : TEXT("none"), seatKm * 1000.0,
			app->Scale()->SeatZoom(), app->Scale()->Blend());
	}

	FAutoConsoleCommand GOriginCommand(
		TEXT("sg.Origin"),
		TEXT("US-T716: the floating origin (the camera's scene position), Unreal units per scene unit round the seat, the near plane and the far field."),
		FConsoleCommandDelegate::CreateLambda([] { PrintOrigin(); }));
}
