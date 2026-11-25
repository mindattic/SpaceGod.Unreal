// US-T715: sg.Scale, the true-scale table (docs/TrueScale.md) from the code: per ship class its catalog length (what the
// simulation reads), its real length (what is drawn while true-scale hulls are on) and its drawn length in scene units at
// the running game's current blend and seat zoom (the free view's gameplay scale with no game running).

#include "UnitySpaceApp.h"

#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Combat/CombatSpecs.h"
#include "Embodiment/Embodiment.h"
#include "ScaleModel.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

using namespace SpaceGod::Core;
using SpaceGod::UnitySpaceApp;

namespace
{
	UnitySpaceApp* ScaleApp()
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

	void PrintScale()
	{
		auto* app = ScaleApp();
		auto scale = app != nullptr ? app->Scale() : cs::New<ScaleModel>();
		bool on = ScaleModel::TrueScale()->Value;
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Scale: true-scale hulls %s, blend %.2f, seat zoom %.3f%s"), on ? TEXT("on") : TEXT("off"),
			scale->Blend(), scale->SeatZoom(), app != nullptr ? TEXT("") : TEXT(" (no game running)"));
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Scale: %-16s %10s %8s %12s"), TEXT("class"), TEXT("catalog km"), TEXT("real m"), TEXT("drawn units"));
		for (int c = 0; c <= static_cast<int>(ShipClass::CaptureTug); c++)
		{
			if (c == 7) continue; // the retired Breacher hull's number (see ShipClass)
			auto cls = static_cast<ShipClass>(c);
			double catalogKm = CombatSpec::Of(cls)->LengthKm;
			double realM = ScaleModel::TrueLengthM(cls);
			double drawnKm = on ? realM / 1000.0 : catalogKm;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Scale: %-16s %10.3f %8.0f %12.4f"), UTF8_TO_TCHAR(ToString(cls)), catalogKm, realM, scale->ShipLength(drawnKm));
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Scale: %-16s %10.4f %8.1f %12.4f"), TEXT("Person"), InhabitedSizes::PersonKm, InhabitedSizes::PersonKm * 1000.0,
			scale->ShipLength(InhabitedSizes::PersonKm));
	}

	FAutoConsoleCommand GScaleCommand(
		TEXT("sg.Scale"),
		TEXT("US-T715: the true-scale table: per ship class its catalog length (the simulation's), its real length (drawn while true-scale hulls are on) and its drawn length in scene units at the current blend and seat zoom."),
		FConsoleCommandDelegate::CreateLambda([] { PrintScale(); }));
}
