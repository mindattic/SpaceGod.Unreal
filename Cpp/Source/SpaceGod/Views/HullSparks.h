#pragma once
// US-T880: hull-hit sparks on the player craft: a short burst of hot streaks a few metres off the camera the way a hull
// hit came from, the sight beside the thud (US-T867). View only, per peer.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::UnityCompat
{
	class InstancedBatch;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// A fixed pool of sparks (the oldest taken when it runs out), each flying out from a point a few true metres off the
	/// camera toward the hit, drawn as one instanced draw of glow quads stretched along their flight and shrinking away
	/// over about 0.3 s. They ride with the camera, so a burst stays where the hit was felt. Fewer and fainter with
	/// Reduce flashes on (PlayerOptions::Calm). Nothing allocated past the first frame.
	/// </summary>
	class HullSparks final
	{
	public:
		HullSparks(UnitySpaceApp* app, const cs::Ref<Transform>& root);

		/// <summary>A hull hit on the held craft from this way (the battle's frame, unit length); weight 0 to 1.</summary>
		void Hit(Core::DVec3 from, float weight);

		/// <summary>Moves the live sparks and draws them.</summary>
		void Update();

		/// <summary>The sparks drawn this frame.</summary>
		int Count() const;

	private:
		struct Spark
		{
			/// <summary>Where it is and how fast it flies, in true metres off the camera (the scene's axes).</summary>
			Vector3 At, Velocity;
			float Born = -1, Life = 0, Width = 0;
		};
		static constexpr int Pool = 48;

		UnitySpaceApp* app;
		cs::Ref<Transform> root;
		cs::Ref<UnityCompat::InstancedBatch> sparks;
		Spark pool[Pool];
		int next = 0;
		uint64 seed = 0x5EED5A9C1E5ull;
		bool shown = false;

		float Next();
	};
}
