// US-T458/US-T460: the Manalith (see ManalithView.h, docs/MANALITH_VISUAL_SPEC.md).
#include "Views/ManalithView.h"
#include "Rendering/Materials.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include <cmath>

namespace SpaceGod::Views
{
	using Rendering::Materials;

	namespace
	{
		constexpr const char* MeshPath = "/Game/SpaceGod/Manalith/manalith-satellite-00.manalith-satellite-00";
		constexpr const char* LitMaterial = "/Game/SpaceGod/Manalith/M_ManalithLit";
		constexpr const char* ShardMaterial = "/Game/SpaceGod/Manalith/M_ManalithShard";

		/// The spec's <MANA_BLUE> #7590C8 as linear colour, and the base map's average (0.409, 0.362, 0.519 sRGB) x 0.5.
		const Color ManaBlue(0.178f, 0.279f, 0.578f);
		const Color DarkRock(0.034f, 0.027f, 0.055f);

		/// The pool: 12 a second for 6 s, plus the bursts, with room to spare.
		constexpr int PoolSize = 100;
		constexpr int RockVariants = 4;

		cs::Ref<Transform> Child(const cs::Ref<Transform>& parent, const cs::String& name)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			return go->transform;
		}

		/// An irregular low-poly rock about 1 across: an icosahedron with each corner pushed in or out, flat-shaded.
		cs::Ref<Mesh> Rock(int variant)
		{
			static cs::Ref<Mesh> rocks[RockVariants];
			if (rocks[variant]) return rocks[variant];
			const float p = 0.5f * (1.0f + std::sqrt(5.0f));
			Vector3 c[12] = { Vector3(-1, p, 0), Vector3(1, p, 0), Vector3(-1, -p, 0), Vector3(1, -p, 0), Vector3(0, -1, p), Vector3(0, 1, p),
				Vector3(0, -1, -p), Vector3(0, 1, -p), Vector3(p, 0, -1), Vector3(p, 0, 1), Vector3(-p, 0, -1), Vector3(-p, 0, 1) };
			static const int f[60] = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
				3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
			uint32_t s = 7919u * static_cast<uint32_t>(variant + 1);
			auto next = [&s] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / 16777216.0f; };
			// The shared spec's rock (GG-T680): each corner's radius x U(0.75, 1.15), squashed (1, U(0.6, 1), U(0.7, 1)).
			Vector3 squash(1.0f, 0.6f + 0.4f * next(), 0.7f + 0.3f * next());
			for (auto& v : c)
			{
				v = v.normalized() * (0.5f * (0.75f + 0.4f * next()));
				v = Vector3(v.x * squash.x, v.y * squash.y, v.z * squash.z);
			}
			std::vector<Vector3> verts, normals;
			std::vector<int> tris;
			for (int i = 0; i < 60; i += 3)
			{
				Vector3 a = c[f[i]], b = c[f[i + 1]], d = c[f[i + 2]];
				Vector3 n = Vector3::Cross(b - a, d - a).normalized();
				int k = static_cast<int>(verts.size());
				verts.insert(verts.end(), { a, b, d });
				normals.insert(normals.end(), { n, n, n });
				// Unity winds clockwise seen from outside.
				tris.insert(tris.end(), { k, k + 2, k + 1 });
			}
			auto m = cs::New<Mesh>(cs::Cat("Manalith shard ", variant));
			m->SetVertices(verts);
			m->SetNormals(normals);
			m->SetTriangles(tris, 0);
			m->RecalculateBounds();
			rocks[variant] = m;
			return m;
		}
	}

	float ManalithView::Next()
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng & 0xffffff) / 16777216.0f;
	}

	Vector3 ManalithView::OnSphere()
	{
		float z = Range(-1, 1), a = Range(0, 2 * Mathf::PI);
		float r = std::sqrt(Mathf::Max(0.0f, 1 - z * z));
		return Vector3(r * std::cos(a), z, r * std::sin(a));
	}

	bool ManalithView::Build(const cs::Ref<Transform>& root_)
	{
		UStaticMesh* asset = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(MeshPath), nullptr, LOAD_NoWarn | LOAD_Quiet);
		bool materials = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SpaceGod/Manalith/M_ManalithLit.M_ManalithLit"), nullptr, LOAD_NoWarn | LOAD_Quiet)
			&& LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SpaceGod/Manalith/M_ManalithShard.M_ManalithShard"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!asset || !materials) return false;
		root = root_;

		// The model, centred by the import, scaled so its longest side is D (2 under the root, whose scale is the radius).
		FBox box = asset->GetBoundingBox();
		// Unreal (x, y, z) cm is Unity (x, z, -y) / 100.
		Vector3 mn(static_cast<float>(box.Min.X), static_cast<float>(box.Min.Z), static_cast<float>(-box.Max.Y));
		Vector3 mx(static_cast<float>(box.Max.X), static_cast<float>(box.Max.Z), static_cast<float>(-box.Min.Y));
		mn = mn / 100.0f;
		mx = mx / 100.0f;
		Vector3 extent = mx - mn;
		float k = 2.0f / Mathf::Max(Mathf::Max(extent.x, extent.y), Mathf::Max(extent.z, 1e-4f));
		auto mesh = cs::New<Mesh>("Manalith");
		mesh->ImportedAsset = MeshPath;
		mesh->ImportedScale = 0.01f;
		mesh->SetBounds(Bounds((mn + mx) * 0.5f, extent));

		tumble = Child(root, "Manalith tumble");
		auto model = Child(tumble, "Manalith model");
		model->localScale = Vector3::one() * k;
		model->localPosition = -(mn + mx) * 0.5f * k;
		AddComponent<MeshFilter>(model->gameObject())->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(model->gameObject());
		mr->SetSharedMaterial(Materials::MarkShared(cs::New<Material>(Shader::Find(LitMaterial), "Manalith")));
		mr->shadowCastingMode = ShadowCastingMode::On;
		mr->receiveShadows = true;

		dark = Materials::MarkShared(cs::New<Material>(Shader::Find(ShardMaterial), "Manalith shard"));
		dark->SetColor("BaseColor", DarkRock);
		dark->SetColor("Glow", ManaBlue);
		dark->SetFloat("GlowIntensity", 0.0f);
		glow = Materials::MarkShared(cs::New<Material>(Shader::Find(ShardMaterial), "Manalith shard (glowing)"));
		glow->SetColor("BaseColor", DarkRock);
		glow->SetColor("Glow", ManaBlue);
		glow->SetFloat("GlowIntensity", GlowIntensity);

		auto debris = Child(root, "Manalith debris");
		shards.resize(PoolSize);
		for (int i = 0; i < PoolSize; i++)
		{
			auto t = Child(debris, cs::Cat("Shard ", i));
			AddComponent<MeshFilter>(t->gameObject())->sharedMesh = Rock(i % RockVariants);
			auto r = AddComponent<MeshRenderer>(t->gameObject());
			r->SetSharedMaterial(dark);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->receiveShadows = false;
			r->enabled = false;
			shards[i].T = t;
			shards[i].R = r;
		}
		nextBurst = Range(BurstMinS, BurstMaxS);
		return true;
	}

	void ManalithView::Spawn(float sizeMin, float sizeMax)
	{
		Shard* s = nullptr;
		for (auto& c : shards)
			if (!c.Live) { s = &c; break; }
		if (!s)
		{
			// Every slot taken: the oldest goes (it was nearly gone).
			s = &shards[0];
			for (auto& c : shards)
				if (c.Age > s->Age) s = &c;
		}
		Vector3 out = OnSphere();
		// D is 2 under the root.
		s->Pos = out * (EmitRadiusD * 2);
		s->Vel = out * (SpeedD * 2 * Range(1 - SpeedSpread, 1 + SpeedSpread));
		s->Size = Range(sizeMin, sizeMax) * 2;
		s->Axis = OnSphere();
		s->Spin = Range(SpinMin, SpinMax);
		s->Angle = Range(0, 360);
		s->Age = 0;
		// The bigger chunks of a burst are always dark rock (the shared spec); 20% of the steady shards glow.
		s->Glow = sizeMin < ChunkMinD && Next() < GlowShare;
		s->Live = true;
		s->R->SetSharedMaterial(s->Glow ? glow : dark);
		s->R->enabled = true;
	}

	void ManalithView::Apply(Vector3 down)
	{
		if (!root) return;
		float now = Time::unscaledTime;
		if (started < 0) started = last = now;
		float t = now - started;
		// A hitch (a load, the editor pausing) is not a burst of shards.
		float dt = Mathf::Clamp(now - last, 0.0f, 0.25f);
		last = now;

		// The tumble: the starting pose, then a constant rate about the local axes (one rotation about the rate vector, so
		// it is truly constant), then the precession wobble on local X.
		Vector3 rate(RateX, RateY, RateZ);
		float wobble = WobbleDeg * std::sin(2 * Mathf::PI * t / WobblePeriodS);
		tumble->localRotation = Quaternion::Euler(StartPitch, StartYaw, StartRoll) * Quaternion::AngleAxis(rate.magnitude() * t, rate.normalized())
			* Quaternion::AngleAxis(wobble, Vector3::right());

		Vector3 fall = (down.sqrMagnitude() > 1e-8f ? down.normalized() : Vector3::down()) * (FallD * 2);
		emitDebt += EmitPerSecond * dt;
		while (emitDebt >= 1)
		{
			Spawn(ShardMinD, ShardMaxD);
			emitDebt -= 1;
		}
		nextBurst -= dt;
		if (nextBurst <= 0)
		{
			for (int i = 0; i < BurstCount; i++) Spawn(ChunkMinD, ChunkMaxD);
			nextBurst = Range(BurstMinS, BurstMaxS);
		}
		for (auto& s : shards)
		{
			if (!s.Live) continue;
			s.Age += dt;
			if (s.Age >= LifeS)
			{
				s.Live = false;
				s.R->enabled = false;
				continue;
			}
			s.Vel = s.Vel + fall * dt;
			s.Pos = s.Pos + s.Vel * dt;
			s.Angle += s.Spin * dt;
			s.T->localPosition = s.Pos;
			s.T->localRotation = Quaternion::AngleAxis(s.Angle, s.Axis);
			s.T->localScale = Vector3::one() * s.Size;
			block.SetFloat("Fade", Mathf::Clamp01((LifeS - s.Age) / FadeS));
			s.R->SetPropertyBlock(block);
		}
	}
}
