#pragma once
// US-T251: Settings > Graphics beyond the frame-rate cap: a quality preset (Low/Medium/High/Epic/Custom) over Unreal's
// scalability groups and the game's own LOD budgets (RenderLod, ShipLod, BattleFx::EffectsBudget), the main
// scalability knobs, the anti-aliasing method (replacing render.msaa's MSAA toggle, US-T55), resolution scale with its
// upscaler, and the field of view of the flown (chase and cockpit) view. Every row goes into effect the moment it
// changes and whenever a profile is loaded, with no restart; a profile's first launch picks its preset from Unreal's
// hardware benchmark (never in a scripted run, so shots and digests keep the defaults).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ISettingStore;
}

namespace SpaceGod
{
	/// C#: [SettingsSource] -- registered with the settings registry by GameSettingsHost.
	class GraphicsSettings
	{
	public:
		/// The preset indices of Quality (the scalability levels Unreal numbers 0..3), then Custom.
		static constexpr int Low = 0, Medium = 1, High = 2, Epic = 3, Custom = 4;

		static const cs::Ref<Core::ChoiceSetting>& Quality();
		static const cs::Ref<Core::ChoiceSetting>& Shadows();
		static const cs::Ref<Core::ChoiceSetting>& Textures();
		static const cs::Ref<Core::ChoiceSetting>& Effects();
		static const cs::Ref<Core::ChoiceSetting>& PostProcessing();
		static const cs::Ref<Core::ChoiceSetting>& ViewDistance();
		static const cs::Ref<Core::ChoiceSetting>& GlobalIllumination();
		static const cs::Ref<Core::ChoiceSetting>& Reflections();
		static const cs::Ref<Core::ChoiceSetting>& Shading();
		// US-T799: the renderer features the preset turns off on weaker hardware. On leaves each where the project sets
		// it (DefaultEngine.ini; the light setup decides); Off holds it off: Lumen (r.DynamicGlobalIlluminationMethod,
		// and Lumen reflections fall back to screen-space ones), virtual shadow maps (r.Shadow.Virtual.Enable) and
		// MegaLights (r.MegaLights.EnableForProject). None of them draws under the forward renderer.
		static const cs::Ref<Core::ChoiceSetting>& Lumen();
		static const cs::Ref<Core::ChoiceSetting>& VirtualShadows();
		static const cs::Ref<Core::ChoiceSetting>& MegaLights();
		static const cs::Ref<Core::ChoiceSetting>& AntiAliasing();
		static const cs::Ref<Core::RangeSetting>& ResolutionScale();
		static const cs::Ref<Core::ChoiceSetting>& Upscaler();
		static const cs::Ref<Core::RangeSetting>& FieldOfView();
		// US-T836: the base field of view of each other seat, beside FieldOfView (the helm's chase view).
		static const cs::Ref<Core::RangeSetting>& CockpitFieldOfView();
		static const cs::Ref<Core::RangeSetting>& TurretFieldOfView();
		static const cs::Ref<Core::RangeSetting>& PersonFieldOfView();
		static const cs::Ref<Core::RangeSetting>& FreeFieldOfView();

		/// Builds every row in menu order and ties the LOD budgets to the preset (the settings source GameSettingsHost adds).
		static void Declare();

		/// The anti-aliasing method in effect, as Unreal numbers it (r.AntiAliasingMethod: 0 none, 1 FXAA, 2 TAA, 3 MSAA,
		/// 4 TSR): the chosen one, or TSR while it upscales a frame drawn below full resolution.
		static int AntiAliasingMethod();

		/// Puts AntiAliasingMethod into effect on every camera (a camera set up after the row changed calls it too).
		static void ApplyAntiAliasing();

		/// The base field of view of a flown view (the helm's chase camera, a cockpit), in degrees.
		static float FlownFov();

		/// US-T836: the seats a view takes its base field of view by: no seat (the god's free view), the helm's chase, a
		/// launched craft's cockpit, a gun mount, a person (an EVA worker or pilot, a mech, a marine).
		enum class SeatView { Free, Helm, Cockpit, Turret, Person };

		/// US-T836: that seat's base field of view (its row), in degrees.
		static float SeatFov(SeatView seat);

		/// While held, a knob or LOD budget loaded from a profile does not move the preset to Custom (the registry loads
		/// each setting in turn, so for a moment they disagree).
		struct LoadScope
		{
			LoadScope();
			~LoadScope();
		};

		/// After a profile is bound: carries an old render.msaa = off over to the anti-aliasing row, and on the profile's
		/// first launch picks the preset from Unreal's hardware benchmark (once per profile; not in a scripted run).
		static void AfterBind(const cs::Ref<Core::ISettingStore>& store);
	};
}
