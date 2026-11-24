#pragma once
// US-T549: Settings > Graphics rows after GraphicsSettings' own for the screen effects Unreal drives from console
// variables: motion blur, ambient occlusion, anisotropic filtering and sharpening. Each goes into effect the moment it
// changes and whenever a profile is loaded. Every default leaves its console variable where the engine had it, so the
// look (and every bench digest) stays as it was; moving a row back to its default puts the engine's value back. A view
// setting: each machine's own, never shared in co-op; a scripted run (PlayerOptions::ScriptedRun) keeps the defaults.
// US-T700: depth of field, film grain, chromatic aberration and lens flares are post-process settings, not console
// variables: they live on GraphicsEffects' own unbound post-process volume (priority 50, under the render backend's 100
// and HyperspaceToll's 1000, so the toll's fringe and grain still blend over it), spawned in UnityScene::World() and
// kept on a ticker only while one of them is off its default. Depth of field focuses on whatever the player camera
// manager's view meets straight ahead (a line trace each frame); no hit, no depth of field that frame.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod
{
	/// C#: [SettingsSource] -- registered with the settings registry by GameSettingsHost.
	class GraphicsEffects
	{
	public:
		static const cs::Ref<Core::ChoiceSetting>& MotionBlur();
		static const cs::Ref<Core::ToggleSetting>& AmbientOcclusion();
		static const cs::Ref<Core::ChoiceSetting>& AnisotropicFiltering();
		static const cs::Ref<Core::RangeSetting>& Sharpening();
		static const cs::Ref<Core::ChoiceSetting>& DepthOfField(); // US-T700
		static const cs::Ref<Core::RangeSetting>& FilmGrain();
		static const cs::Ref<Core::RangeSetting>& ChromaticAberration();
		static const cs::Ref<Core::ChoiceSetting>& LensFlares();

		/// Builds every row in menu order (the settings source GameSettingsHost adds).
		static void Declare();
	};
}
