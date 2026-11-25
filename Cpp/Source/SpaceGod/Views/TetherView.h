#pragma once
// US-T459: the Ascension Tether, SpaceGod's space elevator, on Earth.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class BodyDefinition;
}

namespace SpaceGod::Views
{
	class LineStrip;

	/// <summary>
	/// The Ascension Tether (US-T459), "the Thread": the one space elevator, raised 2163-2191 and run by the Federation's
	/// Tether Authority (docs/HISTORY.md). One
	/// carbon-nanotube ribbon, 104,000 km long, rises from the Anchor Station on the northern tip of Isla Isabela in the
	/// Galapagos (0.383 N, 91.117 W) straight up past geostationary orbit to its counterweight, and turns with the Earth.
	/// The ribbon cannot be seen without augmentation, so it is drawn as a faint line one pixel wide at any range. What
	/// you see are the climber pods: lit points a few pixels across, widely spaced, going up and down at a car's pace
	/// (so slow they seem still until you watch one). The counterweight is swappable (<see cref="Counterweight"/>): a
	/// captured asteroid by default, or a station. Every height goes through the scale model's own mapping about the
	/// Earth (ScaleModel::LocalDistance), so it reads right at the realistic and the game scale. Purely visual.
	/// </summary>
	class TetherView final
	{
	public:
		enum class CounterweightKind { Asteroid, Station };

		/// <summary>Which counterweight is drawn: canon has a station, Ryan remembers an asteroid (asteroid until told).</summary>
		static inline CounterweightKind Counterweight = CounterweightKind::Asteroid;

		static constexpr double AnchorLatDeg = 0.383, AnchorLonDeg = -91.117;
		static constexpr double LengthKm = 104000;
		/// <summary>The Anchor Station's side (km): a 12 km^2 industrial complex.</summary>
		static constexpr double AnchorSideKm = 3.5;
		static constexpr double CounterweightKm = 2.0;
		static constexpr int Pods = 64;
		/// <summary>Climber speeds (km/h), each pod its own, on the simulation clock.</summary>
		static constexpr double PodMinKmH = 150, PodMaxKmH = 420;
		static constexpr float PodPx = 3.0f, LinePxAlpha = 0.28f;

		TetherView(UnitySpaceApp* app, const cs::Ref<Core::BodyDefinition>& earth, const cs::Ref<Transform>& parent);
		~TetherView();

		/// <summary>Places it with the Earth: <paramref name="relative"/> is the Earth's centre from the camera, and the
		/// frame its body-fixed axes (ecliptic), as BodyView has them.</summary>
		void Apply(Vector3 relative, float radius, Core::DVec3 mapNorth, Core::DVec3 primeMeridian);

		void SetVisible(bool visible);

	private:
		UnitySpaceApp* app;
		cs::Ref<Core::BodyDefinition> earth;
		cs::Ref<GameObject> root;
		cs::Ref<LineStrip> line;
		cs::Ref<Transform> anchor, weight;
		struct Pod
		{
			cs::Ref<Transform> T;
			double PhaseKm = 0, SpeedKmS = 0;
			bool Up = true;
			float Side = 0;
		};
		std::vector<Pod> pods;
		bool visible = true;

		static cs::Ref<Mesh> AsteroidMesh();
	};
}
