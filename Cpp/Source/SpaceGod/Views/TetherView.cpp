// US-T459: the Ascension Tether on Earth (see TetherView.h).
#include "Views/TetherView.h"
#include "Views/LineStrip.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/SscKit.h"
#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "SimClock.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipModels;
	using Rendering::SscKit;

	namespace
	{
		Vector3 ToUnity(DVec3 v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Z), static_cast<float>(v.Y)); }

		cs::Ref<Transform> Child(const cs::Ref<Transform>& parent, const cs::String& name)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			return go->transform;
		}

		uint32_t Mix(uint32_t s)
		{
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			return s;
		}

		cs::Ref<Mesh>& Quad()
		{
			static cs::Ref<Mesh> q;
			if (!q) q = MeshFactory::Quad();
			return q;
		}

		cs::Ref<Material>& PodMaterial()
		{
			static cs::Ref<Material> m;
			if (!Alive(m))
			{
				// Pale water-droplet light (the Thread's pods are "a gossamer string of water droplets").
				m = Materials::MarkShared(Materials::Create(Materials::Glow(), "Tether pod"));
				m->SetColor("_Color", Color(0.82f, 0.92f, 1.0f));
				m->SetFloat("_Intensity", 1.6f);
				m->SetFloat("_Falloff", 2.2f);
			}
			return m;
		}
	}

	cs::Ref<Mesh> TetherView::AsteroidMesh()
	{
		// A captured asteroid: a sphere lumped by a few low-frequency bumps (fixed, so it is always the same rock).
		static cs::Ref<Mesh> rock;
		if (rock) return rock;
		auto m = MeshFactory::UvSphere(48, 24);
		std::vector<Vector3> v = m->Vertices;
		Vector3 bumps[7];
		uint32_t s = 104000u;
		for (auto& b : bumps)
		{
			s = Mix(s);
			float z = (s & 0xffff) / 32768.0f - 1;
			s = Mix(s);
			float a = (s & 0xffff) / 65536.0f * 2 * Mathf::PI;
			float r = std::sqrt(Mathf::Max(0.0f, 1 - z * z));
			b = Vector3(r * std::cos(a), z, r * std::sin(a));
		}
		for (auto& p : v)
		{
			Vector3 d = p.normalized();
			float k = 1;
			for (int i = 0; i < 7; i++)
			{
				float dot = Vector3::Dot(d, bumps[i]);
				k += (i % 2 == 0 ? 0.18f : -0.14f) * Mathf::Max(0.0f, dot * dot * dot);
			}
			p = Vector3(d.x * k * 1.15f, d.y * k * 0.8f, d.z * k);
		}
		rock = cs::New<Mesh>("Tether counterweight");
		rock->SetVertices(v);
		rock->SetTriangles(m->SubMeshes[0].Indices, 0);
		rock->SetUVs(0, std::vector<Vector2>(v.size(), Vector2(0, 0)));
		rock->RecalculateNormals();
		rock->RecalculateBounds();
		return rock;
	}

	TetherView::TetherView(UnitySpaceApp* app_, const cs::Ref<BodyDefinition>& earth_, const cs::Ref<Transform>& parent)
		: app(app_), earth(earth_)
	{
		root = NewGameObject("Ascension Tether");
		root->transform->SetParent(parent, false);
		line = cs::New<LineStrip>("Ascension Tether ribbon", root->transform);

		// The Anchor Station: a low industrial block on blasted rock.
		anchor = Child(root->transform, "Anchor Station");
		AddComponent<MeshFilter>(anchor->gameObject())->sharedMesh = MeshFactory::BuiltinCube();
		auto ar = AddComponent<MeshRenderer>(anchor->gameObject());
		ar->SetSharedMaterial(ShipModels::ProceduralMaterial());

		weight = Child(root->transform, "Counterweight");
		cs::List<cs::Ref<Renderer>> parts;
		bool station = Counterweight == CounterweightKind::Station
			&& SscKit::Station(weight, SscKit::ForStationBody("Counterweight Station"), 2.0f, nullptr, parts, SscKit::Condition::Working, true);
		if (!station)
		{
			AddComponent<MeshFilter>(weight->gameObject())->sharedMesh = AsteroidMesh();
			auto wr = AddComponent<MeshRenderer>(weight->gameObject());
			auto rockMaterial = Materials::Create(Materials::Planet(), "Tether counterweight");
			rockMaterial->SetFloat("_UseTexture", 0);
			rockMaterial->SetColor("_Color", Color(0.42f, 0.39f, 0.36f));
			rockMaterial->SetFloat("_NoiseStrength", 0.35f);
			rockMaterial->SetFloat("_NoiseScale", 3.5f);
			wr->SetSharedMaterial(rockMaterial);
		}

		uint32_t s = 459u;
		for (int i = 0; i < Pods; i++)
		{
			Pod p;
			p.T = Child(root->transform, cs::Cat("Climber pod ", i));
			AddComponent<MeshFilter>(p.T->gameObject())->sharedMesh = Quad();
			auto r = AddComponent<MeshRenderer>(p.T->gameObject());
			r->SetSharedMaterial(PodMaterial());
			r->shadowCastingMode = ShadowCastingMode::Off;
			s = Mix(s);
			p.PhaseKm = (s % 100000u) / 100000.0 * LengthKm;
			s = Mix(s);
			double kmh = PodMinKmH + (PodMaxKmH - PodMinKmH) * ((s % 1000u) / 1000.0);
			p.SpeedKmS = kmh / 3600;
			p.Up = i % 2 == 0;
			// Up and down run on the two faces of the ribbon: a hair apart, so the two streams can be told apart close in.
			p.Side = p.Up ? 1.0f : -1.0f;
			pods.push_back(p);
		}
	}

	TetherView::~TetherView()
	{
		if (line) line->Dispose();
		if (Alive(root)) Destroy(root);
	}

	void TetherView::SetVisible(bool v)
	{
		visible = v;
		root->SetActive(v);
	}

	void TetherView::Apply(Vector3 relative, float radius, DVec3 mapNorth, DVec3 primeMeridian)
	{
		if (!visible) return;
		auto scale = app->Scale();
		double lat = AnchorLatDeg * Astro::Deg2Rad, lon = AnchorLonDeg * Astro::Deg2Rad;
		DVec3 east = DVec3::Cross(mapNorth, primeMeridian);
		DVec3 upEcl = (primeMeridian * (std::cos(lat) * std::cos(lon)) + east * (std::cos(lat) * std::sin(lon)) + mapNorth * std::sin(lat)).Normalized();
		Vector3 up = ToUnity(upEcl).normalized();
		Vector3 northU = ToUnity(mapNorth).normalized();
		double R = earth->RadiusKm;
		auto at = [&](double heightKm)
		{
			double d = heightKm <= 0 ? radius : Mathf::Max(radius, static_cast<float>(scale->LocalDistance(earth, R + heightKm)));
			return relative + up * static_cast<float>(d);
		};
		Vector3 base = at(0), top = at(LengthKm);

		cs::List<Vector3> pts;
		pts.Add(base);
		pts.Add(top);
		line->SetPoints(pts, 2, Color(0.75f, 0.85f, 1.0f, 1.0f), LinePxAlpha, LinePxAlpha * 0.6f);

		// Both ends are drawn at the ship scale (as stations are), turning with the Earth; the anchor is held to a sliver of the
		// planet, though, or the game scale's inflated hulls would make a 3.5 km complex a slab hundreds of km across.
		Quaternion frame = Quaternion::LookRotation(northU, up);
		float side = Mathf::Min(static_cast<float>(scale->ShipLength(AnchorSideKm)), radius * 0.02f);
		anchor->localPosition = base;
		anchor->localRotation = frame;
		anchor->localScale = Vector3(side, side * 0.12f, side);
		float w = static_cast<float>(scale->ShipLength(CounterweightKm));
		weight->localPosition = top + up * (w * 0.5f);
		weight->localRotation = frame;
		weight->localScale = Vector3::one() * w;

		// The pods: on the simulation clock, a car's pace, up on one face and down on the other, wrapping at the ends.
		double t = app->Clock->Jd * 86400.0;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		auto cam = app->UnityCamera->transform;
		Vector3 across = Vector3::Cross(up, northU).normalized();
		for (auto& p : pods)
		{
			double s = std::fmod(p.PhaseKm + p.SpeedKmS * t, LengthKm);
			if (s < 0) s += LengthKm;
			double h = p.Up ? s : LengthKm - s;
			Vector3 pos = at(h);
			float px = PodPx * Mathf::Max(pos.magnitude(), 1e-6f) / static_cast<float>(ppu);
			pos = pos + across * (p.Side * px * 0.6f);
			p.T->localPosition = pos;
			p.T->SetRotation(Quaternion::LookRotation(p.T->position() - cam->position(), cam->up()));
			p.T->localScale = Vector3::one() * px;
		}
	}
}
