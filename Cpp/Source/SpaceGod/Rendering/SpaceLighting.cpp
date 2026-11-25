// US-T790: the renderer's lighting features tuned for space (see SpaceLighting.h).
#include "Rendering/SpaceLighting.h"

#include "HAL/IConsoleManager.h"

#include <algorithm>
#include <cmath>

namespace SpaceGod::Rendering
{
	namespace
	{
		int spaceClipmapLast = 0;

		void ReapplySpaceLighting(IConsoleVariable*) { SpaceLighting::Apply(); }

		TAutoConsoleVariable<float> CVarSpaceLumenReachM(TEXT("sg.SpaceLighting.LumenReachM"), 4000.0f,
			TEXT("US-T790: how far (true metres round the seat) Lumen traces, so a 3 km hull's windows light the craft beside it."),
			FConsoleVariableDelegate::CreateStatic(&ReapplySpaceLighting));
		TAutoConsoleVariable<float> CVarSpaceShadowReachKm(TEXT("sg.SpaceLighting.ShadowReachKm"), 4.0f,
			TEXT("US-T790: how far (true km round the seat, 1 UU = 1 cm there) the virtual shadow maps' last clipmap reaches."),
			FConsoleVariableDelegate::CreateStatic(&ReapplySpaceLighting));
		TAutoConsoleVariable<float> CVarSpaceExposureEv(TEXT("sg.SpaceLighting.ExposureEv"), 0.0f,
			TEXT("US-T790: exposure bias (EV) over the fixed exposure the lit hulls are seen at against black space."));

		void SpaceCVarSet(const TCHAR* name, float value)
		{
			if (IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(name)) v->Set(value, ECVF_SetByCode);
		}

		int SpaceCVarInt(const TCHAR* name)
		{
			IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(name);
			return v != nullptr ? v->GetInt() : 0;
		}

		/// The virtual shadow maps' last clipmap level: level n covers 2^n UU round the camera.
		int ClipmapLastLevel()
		{
			double uu = std::max(0.01, static_cast<double>(CVarSpaceShadowReachKm.GetValueOnGameThread())) * 1e5;
			return std::clamp(static_cast<int>(std::ceil(std::log2(uu))), 12, 22);
		}
	}

	bool SpaceLighting::Deferred() { return SpaceCVarInt(TEXT("r.ForwardShading")) == 0; }

	float SpaceLighting::ExposureEv() { return CVarSpaceExposureEv.GetValueOnGameThread(); }

	void SpaceLighting::Apply()
	{
		// Lumen: the black sky leaks nothing in, and empty space has nothing for light to bounce round in more than once;
		// traced far enough that a 3 km hull's windows light the craft beside it.
		SpaceCVarSet(TEXT("r.Lumen.SkylightLeaking"), 0);
		SpaceCVarSet(TEXT("r.LumenScene.Radiosity"), 0);
		SpaceCVarSet(TEXT("r.Lumen.MaxTraceDistance"), std::max(100.0f, CVarSpaceLumenReachM.GetValueOnGameThread()) * 100.0f);

		// Virtual shadow maps' clipmaps out past a 3 km hull (set only on a change: it invalidates their cache).
		int last = ClipmapLastLevel();
		if (last != spaceClipmapLast) SpaceCVarSet(TEXT("r.Shadow.Virtual.Clipmap.LastLevel"), static_cast<float>(last));
		spaceClipmapLast = last;
	}

	cs::String SpaceLighting::Describe()
	{
		bool lumen = Deferred() && SpaceCVarInt(TEXT("r.DynamicGlobalIlluminationMethod")) == 1;
		bool virtualShadows = SpaceCVarInt(TEXT("r.Shadow.Virtual.Enable")) != 0;
		bool megaLights = Deferred() && SpaceCVarInt(TEXT("r.MegaLights.EnableForProject")) != 0;
		return cs::Cat(Deferred() ? "deferred" : "forward", ", Lumen ",
			lumen ? cs::Cat("on to ", cs::Fmt(static_cast<double>(CVarSpaceLumenReachM.GetValueOnGameThread()), "0"), " m") : cs::String("off"),
			", MegaLights ", megaLights ? "on" : "off", ", shadows ",
			virtualShadows ? cs::Cat("virtual to clipmap ", spaceClipmapLast) : cs::String("cascaded"),
			", exposure ", cs::Fmt(static_cast<double>(ExposureEv()), "0.##"), " EV");
	}
}
