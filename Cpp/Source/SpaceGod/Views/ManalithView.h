#pragma once
// US-T458/US-T460: the Manalith (Nightglass), drawn to docs/MANALITH_VISUAL_SPEC.md.

#include "UnityCompat/UnityRenderingTypes.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Views
{
	/// <summary>
	/// The Manalith (US-T458): the owner's Meshy model (/Game/SpaceGod/Manalith) tumbling slowly on top of its orbit, a
	/// mana-blue Fresnel rim, and shards raining off it toward its planet, all to the shared Manalith spec
	/// (docs/MANALITH_VISUAL_SPEC.md, US-T460) that GridGame's Unity Manalith follows too. Everything is laid out in the
	/// satellite's diameter D under the body's root (whose scale is the drawn radius), so it reads the same at either
	/// scale and nothing pops when the scale toggles. Purely visual, on real (unscaled) time: the simulation never sees it.
	/// </summary>
	class ManalithView final
	{
	public:
		~ManalithView();

		/// <summary>Builds the model and the shard pool under <paramref name="root"/>; false when the model is not imported
		/// (tools/import-manalith.ps1), leaving the caller to draw its plain station.</summary>
		bool Build(const cs::Ref<Transform>& root);

		/// <summary>Turns the model and steps the shards. <paramref name="down"/> is the direction to the planet (Unity axes).</summary>
		void Apply(Vector3 down);

		/// <summary>US-T462: the reference frame's pose, GridGame's rig at time <paramref name="t"/>: the tumble set in world
		/// axes (the reference camera looks down world +Z unrotated, as the Unity rig's does) and the shards re-run from
		/// t = 0 in fixed 1/30 s steps falling world-down (screen-down), all held still until <see cref="EndReference"/>.</summary>
		void BeginReference(float t);
		void EndReference();

		/// <summary>The rim's material values (the shared spec's 1.5 and exponent 3, the in-material rim light 0.4, the
		/// ambient floor) and the glowing shards' strength (2), for the reference frame's tuning against the Unity one.</summary>
		void SetRim(float intensity, float exponent, float rimLight, float ambientFloor, float glowIntensity);

		const cs::Ref<Transform>& Root() const { return root; }

		/// <summary>The view built last and still alive (null when none): Nightglass has one Manalith.</summary>
		static ManalithView* Current();

		/// <summary>The closed-form tumble at time <paramref name="t"/> seconds, the same function as GridGame's
		/// ManalithRig.PoseAt.</summary>
		static Quaternion PoseAt(float t);

		// ---- the spec, in one place (degrees, seconds, D)
		static constexpr float StartPitch = 15, StartYaw = 0, StartRoll = 25;
		static constexpr float RateX = 3.0f, RateY = 7.0f, RateZ = 1.7f;
		static constexpr float WobbleDeg = 4.0f, WobblePeriodS = 40.0f;
		static constexpr float EmitPerSecond = 12, EmitRadiusD = 0.45f;
		static constexpr float ShardMinD = 0.006f, ShardMaxD = 0.025f;
		static constexpr float SpeedD = 0.02f, SpeedSpread = 0.5f, FallD = 0.05f;
		static constexpr float SpinMin = 30, SpinMax = 180, LifeS = 6, FadeS = 1.5f;
		static constexpr float GlowShare = 0.2f, GlowIntensity = 2.0f;
		static constexpr float BurstMinS = 8, BurstMaxS = 12, ChunkMinD = 0.03f, ChunkMaxD = 0.05f;
		static constexpr int BurstCount = 5;
		static constexpr uint32_t Seed = 1337;

	private:
		struct Shard
		{
			cs::Ref<Transform> T;
			cs::Ref<MeshRenderer> R;
			Vector3 Pos, Vel, Axis;
			float Size = 0, Spin = 0, Angle = 0, Age = 0;
			bool Live = false, Glow = false;
		};

		cs::Ref<Transform> root, tumble;
		std::vector<Shard> shards;
		cs::Ref<Material> lit, dark, glow;
		MaterialPropertyBlock block;
		uint32_t rng = Seed;
		float started = -1, last = -1, emitDebt = 0, nextBurst = 0;
		float referenceT = -1;
		bool referenceRun = false;

		float Next();
		float Range(float a, float b) { return a + (b - a) * Next(); }
		Vector3 OnSphere();
		void Spawn(float sizeMin, float sizeMax);
		void Reset();
		void Step(float dt, Vector3 down);
	};
}
