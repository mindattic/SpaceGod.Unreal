#pragma once
// US-T790: the renderer's lighting features tuned for space. See SpaceLighting below.

#include "Cs/Cs.h"

namespace SpaceGod::Rendering
{
	/// <summary>
	/// US-T790: Unreal's lighting features tuned for space, on top of US-T172's lights (HullLighting): one star as the key
	/// light, the black sky giving nothing to bounce, and the hulls' own emissive windows and running lights the only
	/// other light round a ship. The features themselves are the project's (Config/DefaultEngine.ini: the deferred
	/// renderer, Lumen, MegaLights, virtual shadow maps), held off per preset by Settings > Graphics (US-T799), so Low
	/// falls back to the cheap path. This tunes them whenever a graphics setting changes:
	/// <list type="bullet">
	/// <item>Lumen with no sky leaking and no multi-bounce radiosity, traced out to sg.SpaceLighting.LumenReachM;</item>
	/// <item>the virtual shadow maps' clipmaps reaching sg.SpaceLighting.ShadowReachKm round the camera: round the seat
	/// 1 UU is 1 true cm (US-T716, UnrealSpace::CmPerUnit), so a 3 km hull is 300,000 UU;</item>
	/// <item>a fixed exposure (sg.SpaceLighting.ExposureEv over RenderBackend's manual one), so a frame of black space
	/// never opens the exposure up and blows out the lit side of a hull.</item>
	/// </list>
	/// Render only, and nothing per frame.
	/// </summary>
	class SpaceLighting
	{
	public:
		/// <summary>Puts the space tuning into effect.</summary>
		static void Apply();

		/// <summary>Whether the renderer is the deferred one (Lumen and MegaLights need it).</summary>
		static bool Deferred();

		/// <summary>The exposure bias (EV) over the fixed exposure.</summary>
		static float ExposureEv();

		/// <summary>What is in effect (sg.Lighting): Lumen, MegaLights, the shadow method and its reach, the exposure.</summary>
		static cs::String Describe();
	};
}
