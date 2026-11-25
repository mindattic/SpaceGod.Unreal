#pragma once
// US-T461: the Biomass on SpaceGod's Earth, the Exclusion.

#include "UnityCompat/UnityRenderingTypes.h"

#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The Biomass (US-T461), the Exclusion: the globule that got past Earth's guns in 2751, the Carolina Fall, walled,
	/// watched and burned ever since (docs/HISTORY.md). It covers the old south-east of the United States from central
	/// Alabama up through Tennessee into southern Kentucky, held by the Appalachians to the east, the Tombigbee and
	/// Mississippi to the west, and slowed short of the Ohio. Drawn as a patch of flesh on the Earth's surface (a person
	/// turned inside out: muscle, fat, purple-black, cartilage ridges, grey-pink folds), lit like the planet, breathing
	/// about six times a minute on the simulation clock, with a haze over it in some daylight hours. It hangs under the
	/// Earth's own transform, so it turns and scales with the planet at both scales. Purely visual: the gameplay Bio Mass
	/// (globules, blooms, tumours) is BioMassView's.
	/// </summary>
	class EarthBiomassView final
	{
	public:
		/// <summary>Its outline, (latitude, longitude) in degrees, east positive.</summary>
		static const std::vector<Vector2>& Outline();

		/// <summary>The patch's height over the sphere (Earth radii), and the breath's swing about it.</summary>
		static constexpr float Lift = 0.0015f, BreathLift = 0.0003f;
		/// <summary>Breaths a minute (sim clock).</summary>
		static constexpr double BreathsPerMinute = 6;

		EarthBiomassView(UnitySpaceApp* app, const cs::Ref<Transform>& earthRoot);
		~EarthBiomassView();

		/// <summary>Breath and haze, after the Earth's transform is placed for the frame.</summary>
		void Apply();

		void SetVisible(bool visible);

	private:
		UnitySpaceApp* app;
		cs::Ref<GameObject> root;
		cs::Ref<Transform> earth;
		cs::Ref<Material> flesh;
		cs::Ref<Mesh> hazeMesh;
		std::vector<Vector3> hazeCentres;
		std::vector<Vector3> hazeVerts;
		std::vector<Vector3> hazeUvs;
		std::vector<Color> hazeColors;
		double breathPhase = 0, lastJd = 0;
		bool visible = true;
	};
}
