// US-T729: space dust round the camera (Views/DustField.h).
#include "Views/DustField.h"

#include "GraphicsEffects.h"
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
		/// <summary>The lattice's cell edge in true metres; each cell holds up to DustPerCell motes at spots hashed from it.</summary>
		constexpr double DustCellM = 25.0;
		/// <summary>Cells drawn each way from the camera's own (a cube 2 x DustCells + 1 across), and the most motes a cell holds.</summary>
		constexpr int DustCells = 3, DustPerCell = 2;
		/// <summary>How wide a mote's glow is drawn (px), before each mote's own spread of DustSizeSpread either way.</summary>
		constexpr float DustMotePx = 4.0f, DustSizeSpread = 0.4f;
		/// <summary>From this share of the reach out to the reach a mote shrinks away, so the field's edge never pops.</summary>
		constexpr double DustFadeFrom = 0.55;
		/// <summary>The camera moving more than this share of the reach in one frame (cruise, a jump, time warp) hides the field.</summary>
		constexpr double DustJumpShare = 0.5;
		/// <summary>US-T744: a mote streaks the way it slides across the view by as far as it slides in this long (s), a
		/// shutter's worth of motion, the same at any frame rate.</summary>
		constexpr double StreakSeconds = 0.04;
		/// <summary>The longest a streak is drawn, as a share of its distance (so a lurch never draws a line across the screen).</summary>
		constexpr double StreakMaxShare = 0.35;
		/// <summary>A streak shorter than this share of its mote's own width is drawn as the plain mote.</summary>
		constexpr double StreakFrom = 0.25;

		uint64 DustMix(uint64 x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		/// <summary>The next of a cell's hashed numbers, in [0, 1).</summary>
		double DustNext(uint64& h)
		{
			h = DustMix(h);
			return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
		}

		cs::Ref<Material>& DustMaterial()
		{
			static cs::Ref<Material> value;
			return value;
		}
	}

	DustField::DustField(UnitySpaceApp* app_, const cs::Ref<Transform>& root_)
		: app(app_), root(root_)
	{
		if (!Alive(DustMaterial()))
		{
			DustMaterial() = Materials::MarkShared(Materials::Create(Materials::Glow(), "SpaceDust"));
			DustMaterial()->SetColor("_Color", Color(0.75f, 0.8f, 0.9f));
			DustMaterial()->SetFloat("_Intensity", 0.6f);
			DustMaterial()->SetFloat("_Falloff", 2.5f);
		}
		motes = cs::New<UnityCompat::InstancedBatch>(root, MeshFactory::Quad(), DustMaterial());
	}

	int DustField::Count() const { return motes->Count(); }

	void DustField::Update()
	{
		double density = GraphicsEffects::DustDensity();
		double cell = DustCellM * app->Scale()->ShipLength(0.001);
		double reach = DustCells * cell;
		DVec3 eye = app->SpaceCamera->ScenePosition;
		// US-T744: the camera's own motion this frame, in the motes' frame (both ends through this frame's mapping).
		Vector3 moved = hasEye ? app->Relative(eye) - app->Relative(lastEye) : Vector3::zero();
		float dt = Time::deltaTime;
		bool jumped = hasEye && DVec3::Distance(eye, lastEye) > reach * DustJumpShare;
		lastEye = eye;
		hasEye = true;
		motes->Clear();
		if (density <= 0 || app->InhabitedSizeKm() <= 0 || jumped || !(cell > 0) || app->UnityCamera == nullptr)
		{
			if (shown) motes->Flush();
			shown = false;
			return;
		}

		auto cam = app->UnityCamera->transform;
		Quaternion facing = Quaternion::Inverse(root->rotation()) * cam->rotation();
		double ppu = app->PixelsPerUnitAtUnitDistance();
		int64 cx = static_cast<int64>(std::floor(eye.X / cell));
		int64 cy = static_cast<int64>(std::floor(eye.Y / cell));
		int64 cz = static_cast<int64>(std::floor(eye.Z / cell));
		for (int64 x = cx - DustCells; x <= cx + DustCells; x++)
			for (int64 y = cy - DustCells; y <= cy + DustCells; y++)
				for (int64 z = cz - DustCells; z <= cz + DustCells; z++)
				{
					uint64 h = DustMix(static_cast<uint64>(x) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64>(y) * 0xC2B2AE3D27D4EB4Full
						^ static_cast<uint64>(z) * 0x165667B19E3779F9ull);
					for (int k = 0; k < DustPerCell; k++)
					{
						if (DustNext(h) >= density) { DustNext(h); DustNext(h); DustNext(h); DustNext(h); continue; }
						DVec3 at((x + DustNext(h)) * cell, (y + DustNext(h)) * cell, (z + DustNext(h)) * cell);
						Vector3 rel = app->Relative(at);
						double d = rel.magnitude();
						if (d >= reach) { DustNext(h); continue; }
						double fade = 1.0 - Mathf::Clamp01(static_cast<float>((d / reach - DustFadeFrom) / (1.0 - DustFadeFrom)));
						double size = DustMotePx * (1.0 + DustSizeSpread * (2.0 * DustNext(h) - 1.0)) * d / ppu * fade;
						if (!(size > 0)) continue;
						// US-T744: speed streaks. The mote slides across the view against the camera's motion: drawn
						// stretched along that slide, by the part of the motion across its line of sight.
						Vector3 across = d > 0 ? moved - rel * (Vector3::Dot(moved, rel) / static_cast<float>(d * d)) : Vector3::zero();
						double streak = dt > 0 ? across.magnitude() / dt * StreakSeconds : 0;
						streak = std::fmin(streak, d * StreakMaxShare);
						if (streak > size * StreakFrom)
						{
							Quaternion along = Quaternion::LookRotation(rel.normalized(), across.normalized());
							motes->Add(rel + across.normalized() * static_cast<float>(streak * 0.5), along,
								Vector3(static_cast<float>(size), static_cast<float>(size + streak), static_cast<float>(size)));
						}
						else motes->Add(rel, facing, static_cast<float>(size));
					}
				}
		motes->Flush();
		shown = true;
	}
}
