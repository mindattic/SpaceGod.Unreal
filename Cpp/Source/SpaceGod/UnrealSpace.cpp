// US-T716: the render scale round the seat (docs/TrueScale.md section 5.2) and the far field (section 5.3).

#include "UnrealSpace.h"

#include "HAL/IConsoleManager.h"

namespace SpaceGod::UnrealSpace
{
	namespace
	{
		TAutoConsoleVariable<float> CVarCmPerTrueKm(TEXT("sg.Origin.CmPerTrueKm"), 100000.0f,
			TEXT("US-T716: Unreal units per true km of hardware round the camera (100000: 1 UU = 1 true cm). 0 keeps the old fixed 100 cm per scene unit."));
		TAutoConsoleVariable<float> CVarCockpitNearCm(TEXT("sg.Origin.CockpitNearCm"), 5.0f,
			TEXT("US-T716: the near plane (true cm) while a seat is held."));
		TAutoConsoleVariable<float> CVarFarFieldCm(TEXT("sg.Origin.FarFieldCm"), 1e11f,
			TEXT("US-T716: past this distance from the camera (cm) drawn positions are log-compressed and their sizes shrunk to match, inside Unreal's large-world limit."));
	}

	void SetRenderScale(double shipUnitsPerKm)
	{
		double perKm = CVarCmPerTrueKm.GetValueOnGameThread();
		double next = perKm > 0 && shipUnitsPerKm > 0 ? perKm / shipUnitsPerKm : 100.0;
		if (!FMath::IsFinite(next) || next == Detail::CmPerUnit) return;
		Detail::CmPerUnit = next;
		Detail::ScaleRevision++;
	}

	double CockpitNearCm() { return FMath::Max(0.0f, CVarCockpitNearCm.GetValueOnGameThread()); }

	double FarFieldCm() { return FMath::Max(1.0f, CVarFarFieldCm.GetValueOnGameThread()); }

	FVector FarField(const FVector& cm, double& shrink)
	{
		shrink = 1.0;
		double far = FarFieldCm();
		double d = cm.Size();
		if (d <= far) return cm;
		shrink = far * (1.0 + FMath::Loge(d / far)) / d;
		return cm * shrink;
	}
}
