// US-T880: hull-hit sparks on the player craft (Views/HullSparks.h).
#include "Views/HullSparks.h"

#include "PlayerOptions.h"
#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityScene.h"

#include "ScaleModel.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	namespace
	{
		/// <summary>How far off the camera a burst starts (true m), and how far either side of the hit's line it scatters.</summary>
		constexpr float BurstDistanceM = 3.0f, BurstScatterM = 0.35f;
		/// <summary>Sparks in a burst at the lightest hit, and the most a heavy one adds; with Reduce flashes on, this share of them.</summary>
		constexpr int BurstSparks = 6, BurstSparksHeavy = 8;
		constexpr float CalmShare = 0.4f;
		/// <summary>How fast a spark flies out (true m/s), before its own spread of SpeedSpread either way.</summary>
		constexpr float SparkSpeedMps = 9.0f, SpeedSpread = 0.5f;
		/// <summary>How long a spark lasts (s), before its own spread of LifeSpread either way.</summary>
		constexpr float SparkLifeS = 0.3f, LifeSpread = 0.35f;
		/// <summary>A spark's width (true m), and its streak's length as the distance it flies in this long (s).</summary>
		constexpr float SparkWidthM = 0.025f, StreakSeconds = 0.03f;
		/// <summary>The glow's brightness, and with Reduce flashes on.</summary>
		constexpr float Intensity = 1.6f, CalmIntensity = 0.6f;

		cs::Ref<Material>& SparkMaterial()
		{
			static cs::Ref<Material> value;
			return value;
		}
	}

	HullSparks::HullSparks(UnitySpaceApp* app_, const cs::Ref<Transform>& root_)
		: app(app_), root(root_)
	{
		if (!Alive(SparkMaterial()))
		{
			SparkMaterial() = Materials::MarkShared(Materials::Create(Materials::Glow(), "HullSparks"));
			SparkMaterial()->SetColor("_Color", Color(1.0f, 0.72f, 0.38f));
			SparkMaterial()->SetFloat("_Intensity", Intensity);
			SparkMaterial()->SetFloat("_Falloff", 2.0f);
		}
		sparks = cs::New<UnityCompat::InstancedBatch>(root, MeshFactory::Quad(), SparkMaterial());
	}

	int HullSparks::Count() const { return sparks->Count(); }

	float HullSparks::Next()
	{
		seed += 0x9E3779B97F4A7C15ull;
		uint64 x = seed;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		x ^= x >> 31;
		return static_cast<float>(static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0));
	}

	void HullSparks::Hit(DVec3 from, float weight)
	{
		auto d = from.SwapYZ();
		Vector3 dir(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
		if (dir.sqrMagnitude() < 1e-12f) return;
		dir = dir.normalized();
		bool calm = PlayerOptions::Calm();
		float w = Mathf::Clamp01(weight);
		int count = static_cast<int>(std::lround((BurstSparks + BurstSparksHeavy * w) * (calm ? CalmShare : 1.0f)));
		Vector3 side = Vector3::Cross(dir, std::fabs(dir.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized();
		Vector3 lift = Vector3::Cross(dir, side);
		Vector3 centre = dir * BurstDistanceM;
		float now = Time::time;
		for (int i = 0; i < count; i++)
		{
			float turn = Next() * Mathf::Pi * 2.0f, out = Next();
			Vector3 across = side * Mathf::Cos(turn) + lift * Mathf::Sin(turn);
			Spark& s = pool[next];
			next = (next + 1) % Pool;
			s.At = centre + across * (BurstScatterM * Next());
			// Mostly across the line of sight, some back toward the camera, so they fan out round where the hit landed.
			float speed = SparkSpeedMps * (1.0f + SpeedSpread * (2.0f * Next() - 1.0f));
			s.Velocity = (across * (0.6f + 0.4f * out) - dir * (0.5f * Next())).normalized() * speed;
			s.Born = now;
			s.Life = SparkLifeS * (1.0f + LifeSpread * (2.0f * Next() - 1.0f));
			s.Width = SparkWidthM * (0.7f + 0.6f * w);
		}
	}

	void HullSparks::Update()
	{
		sparks->Clear();
		float now = Time::time, dt = Time::deltaTime;
		double metre = app->Scale()->ShipLength(0.001);
		bool any = false;
		for (auto& s : pool)
		{
			if (s.Born < 0) continue;
			float age = now - s.Born;
			if (age >= s.Life || age < 0) { s.Born = -1; continue; }
			s.At += s.Velocity * dt;
			any = true;
		}
		if (!any || app->UnityCamera == nullptr || !(metre > 0))
		{
			if (shown) sparks->Flush();
			shown = false;
			return;
		}

		SparkMaterial()->SetFloat("_Intensity", PlayerOptions::Calm() ? CalmIntensity : Intensity);
		Vector3 eye = app->Relative(app->SpaceCamera->ScenePosition);
		float m = static_cast<float>(metre);
		for (const auto& s : pool)
		{
			if (s.Born < 0) continue;
			float left = 1.0f - (now - s.Born) / s.Life;
			float width = s.Width * left;
			float streak = s.Velocity.magnitude() * StreakSeconds * left;
			if (!(width > 0) || s.At.sqrMagnitude() < 1e-8f) continue;
			Quaternion along = Quaternion::LookRotation(s.At.normalized(), s.Velocity.normalized());
			sparks->Add(eye + s.At * m, along, Vector3(width * m, (width + streak) * m, width * m));
		}
		sparks->Flush();
		shown = true;
	}
}
