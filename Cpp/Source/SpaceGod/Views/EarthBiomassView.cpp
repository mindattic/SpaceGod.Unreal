// US-T461: the Biomass on SpaceGod's Earth (see EarthBiomassView.h).
#include "Views/EarthBiomassView.h"
#include "Rendering/Materials.h"
#include "UnitySpaceApp.h"

#include "Astro.h"
#include "SimClock.h"

#include <algorithm>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	namespace EarthBiomassDetail
	{
		// The outline's bounding box (degrees), which the flesh texture spans.
		constexpr float LatMin = 32.8f, LatMax = 37.1f, LonMin = -88.7f, LonMax = -85.2f;
		constexpr float CellDeg = 0.05f;
		// The edge is ragged, not surveyed: each cell is tested a little off its centre, by up to this much.
		constexpr float EdgeJitterDeg = 0.14f;
		constexpr int TexSize = 256;
		// The flesh is lit like the planet; this brings its albedo up to the land's brightness around it.
		constexpr float FleshGain = 1.5f;
		constexpr float HazeHalfKm = 40.0f, EarthKm = 6371.0f;

		uint32_t Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return h ^ (h >> 16);
		}

		float Lattice(int x, int y, uint32_t seed) { return (Hash(x, y, seed) & 0xffffff) / 16777215.0f; }

		float Smooth(float t) { return t * t * (3 - 2 * t); }

		float SmoothStep(float a, float b, float x)
		{
			float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return Smooth(t);
		}

		float ValueNoise(float x, float y, uint32_t seed)
		{
			int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float fx = Smooth(x - ix), fy = Smooth(y - iy);
			float a = Lattice(ix, iy, seed), b = Lattice(ix + 1, iy, seed), c = Lattice(ix, iy + 1, seed), d = Lattice(ix + 1, iy + 1, seed);
			return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
		}

		float Fbm(float x, float y, uint32_t seed)
		{
			float sum = 0, amp = 0.5f, norm = 0;
			for (int o = 0; o < 5; o++)
			{
				sum += ValueNoise(x, y, seed + o * 17u) * amp;
				norm += amp;
				x *= 2.03f;
				y *= 2.03f;
				amp *= 0.5f;
			}
			return sum / norm;
		}

		bool Inside(float lat, float lon)
		{
			const auto& poly = EarthBiomassView::Outline();
			bool in = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				float yi = poly[i].x, xi = poly[i].y, yj = poly[j].x, xj = poly[j].y;
				if ((yi > lat) != (yj > lat) && lon < (xj - xi) * (lat - yi) / (yj - yi) + xi) in = !in;
			}
			return in;
		}

		bool InsideRagged(float lat, float lon)
		{
			float dLat = (Fbm(lat * 3.1f, lon * 3.1f, 461u) - 0.5f) * 2 * EdgeJitterDeg;
			float dLon = (Fbm(lat * 3.1f + 40, lon * 3.1f + 40, 462u) - 0.5f) * 2 * EdgeJitterDeg;
			return Inside(lat + dLat, lon + dLon);
		}

		/// The sphere mesh's frame: north on +Y, longitude 0 on +X, east on +Z (BodyView).
		Vector3 OnSphere(float latDeg, float lonDeg)
		{
			double lat = latDeg * Astro::Deg2Rad, lon = lonDeg * Astro::Deg2Rad;
			return Vector3(static_cast<float>(std::cos(lat) * std::cos(lon)), static_cast<float>(std::sin(lat)), static_cast<float>(std::cos(lat) * std::sin(lon)));
		}

		Color Lerp(Color a, Color b, float t) { return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1); }

		cs::Ref<Texture2D>& FleshTexture()
		{
			// A person turned inside out (GLMZ): crimson muscle, sheets of yellow fat, purple-black liver and clot, grey-pink
			// folds like brain matter, pale veins, and white cartilage ridges breaking the surface. Shared by every Earth view.
			static cs::Ref<Texture2D> tex;
			if (tex) return tex;
			tex = cs::New<Texture2D>(TexSize, TexSize, TextureFormat::RGBA32, false);
			tex->wrapMode = TextureWrapMode::Clamp;
			tex->filterMode = FilterMode::Bilinear;
			tex->hideFlags = HideFlags::DontSave;
			const Color muscle(0.80f, 0.16f, 0.15f), deep(0.56f, 0.07f, 0.09f), fat(0.95f, 0.82f, 0.50f), liver(0.32f, 0.09f, 0.20f),
				brain(0.88f, 0.66f, 0.66f), vein(0.55f, 0.64f, 0.84f), cartilage(0.97f, 0.95f, 0.90f);
			for (int y = 0; y < TexSize; y++)
				for (int x = 0; x < TexSize; x++)
				{
					float u = x / static_cast<float>(TexSize) * 9, v = y / static_cast<float>(TexSize) * 9;
					float a = Fbm(u, v, 1u), b = Fbm(u * 0.7f + 11, v * 0.7f + 3, 2u), c = Fbm(u * 1.3f + 5, v * 1.3f + 9, 3u);
					float ridge = 1 - std::fabs(Fbm(u * 1.6f + 21, v * 1.6f + 7, 4u) * 2 - 1);
					float folds = 0.5f + 0.5f * std::sin((u + c * 6) * 7.0f);
					Color col = Lerp(muscle, deep, SmoothStep(0.35f, 0.7f, a));
					col = Lerp(col, fat, SmoothStep(0.58f, 0.7f, b) * 0.85f);
					col = Lerp(col, liver, SmoothStep(0.33f, 0.24f, c) * 0.75f);
					col = Lerp(col, brain, SmoothStep(0.52f, 0.6f, c) * SmoothStep(0.62f, 0.72f, a + folds * 0.2f) * 0.8f);
					col = Lerp(col, vein, SmoothStep(0.985f, 0.998f, 1 - std::fabs(Fbm(u * 2.2f + 3, v * 2.2f + 1, 5u) * 2 - 1)) * 0.6f);
					col = Lerp(col, cartilage, SmoothStep(0.955f, 0.985f, ridge));
					tex->SetPixel(x, y, col);
				}
			tex->Apply(false);
			return tex;
		}

		cs::Ref<Mesh> PatchMesh()
		{
			// The flesh: every grid cell whose (ragged) centre lies in the outline, on the unit sphere at the lift.
			std::vector<Vector3> verts;
			std::vector<Vector2> uvs;
			std::vector<Vector3> normals;
			std::vector<int> tris;
			int nLat = static_cast<int>(std::ceil((LatMax - LatMin) / CellDeg)), nLon = static_cast<int>(std::ceil((LonMax - LonMin) / CellDeg));
			std::vector<int> index((nLat + 1) * (nLon + 1), -1);
			auto vertex = [&](int i, int j)
			{
				int& k = index[i * (nLon + 1) + j];
				if (k >= 0) return k;
				float lat = LatMin + i * CellDeg, lon = LonMin + j * CellDeg;
				k = static_cast<int>(verts.size());
				verts.push_back(OnSphere(lat, lon) * (1 + EarthBiomassView::Lift));
				normals.push_back(OnSphere(lat, lon));
				uvs.push_back(Vector2((lon - LonMin) / (LonMax - LonMin), (lat - LatMin) / (LatMax - LatMin)));
				return k;
			};
			for (int i = 0; i < nLat; i++)
				for (int j = 0; j < nLon; j++)
				{
					if (!InsideRagged(LatMin + (i + 0.5f) * CellDeg, LonMin + (j + 0.5f) * CellDeg)) continue;
					int a = vertex(i, j), b = vertex(i, j + 1), c = vertex(i + 1, j + 1), d = vertex(i + 1, j);
					// Outward-facing in the left-handed mesh frame (north +Y, east +Z): north-east-south order.
					tris.insert(tris.end(), { a, d, c, a, c, b });
				}
			auto m = cs::New<Mesh>("Earth Biomass");
			m->SetVertices(verts);
			m->SetUVs(0, uvs);
			m->SetTriangles(tris, 0);
			m->SetNormals(normals); // straight out of the sphere, as the planet's own
			m->RecalculateBounds();
			return m;
		}
	}

	using namespace EarthBiomassDetail;

	const std::vector<Vector2>& EarthBiomassView::Outline()
	{
		// From central Alabama north through north-east Mississippi and middle Tennessee into southern Kentucky (about
		// 150 km short of the Ohio), the Appalachian front to the east, the Tombigbee to the west. About 78,000 km^2.
		static const std::vector<Vector2> outline = {
			{ 36.85f, -86.90f }, { 36.78f, -86.05f }, { 36.35f, -85.62f }, { 35.65f, -85.55f }, { 35.00f, -85.50f },
			{ 34.40f, -85.66f }, { 33.80f, -86.00f }, { 33.30f, -86.50f }, { 33.05f, -87.20f }, { 33.30f, -87.90f },
			{ 33.90f, -88.35f }, { 34.60f, -88.40f }, { 35.20f, -88.20f }, { 35.80f, -87.90f }, { 36.30f, -87.70f },
			{ 36.70f, -87.40f },
		};
		return outline;
	}

	EarthBiomassView::EarthBiomassView(UnitySpaceApp* app_, const cs::Ref<Transform>& earthRoot)
		: app(app_), earth(earthRoot)
	{
		root = NewGameObject("Earth Biomass");
		root->transform->SetParent(earthRoot, false);

		auto patch = NewGameObject("Earth Biomass flesh");
		patch->transform->SetParent(root->transform, false);
		AddComponent<MeshFilter>(patch)->sharedMesh = Materials::Own(PatchMesh());
		auto pr = AddComponent<MeshRenderer>(patch);
		flesh = Materials::Create(Materials::Planet(), "Earth Biomass");
		flesh->SetTexture("_MainTex", FleshTexture());
		flesh->SetFloat("_UseTexture", 1);
		flesh->SetFloat("_PoleMargin", 0);
		flesh->SetColor("_Color", Color(1, 1, 1) * FleshGain);
		flesh->SetFloat("_NoiseStrength", 0);
		flesh->SetFloat("_Haze", 0);
		flesh->SetFloat("_AtmoStrength", 0);
		flesh->SetFloat("_Highlight", 0);
		pr->SetSharedMaterial(flesh);
		pr->shadowCastingMode = ShadowCastingMode::Off;

		// The haze: soft additive blobs lying flat on the flesh, a little above it, lit only where it is day.
		uint32_t s = 2147u;
		while (hazeCentres.size() < 20)
		{
			s = Hash(static_cast<int>(s), 7, 461u);
			float lat = LatMin + (s & 0xffff) / 65535.0f * (LatMax - LatMin);
			s = Hash(static_cast<int>(s), 11, 461u);
			float lon = LonMin + (s & 0xffff) / 65535.0f * (LonMax - LonMin);
			if (Inside(lat, lon)) hazeCentres.push_back(OnSphere(lat, lon));
		}
		std::vector<int> tris;
		float h = HazeHalfKm / EarthKm;
		for (const auto& c : hazeCentres)
		{
			Vector3 east = Vector3::Cross(Vector3::up(), c).normalized();
			Vector3 north = Vector3::Cross(c, east).normalized();
			Vector3 at = c * (1 + Lift * 2.5f);
			int k = static_cast<int>(hazeVerts.size());
			hazeVerts.push_back(at - east * h - north * h);
			hazeVerts.push_back(at + east * h - north * h);
			hazeVerts.push_back(at + east * h + north * h);
			hazeVerts.push_back(at - east * h + north * h);
			hazeUvs.insert(hazeUvs.end(), { Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(0, 1, 0) });
			tris.insert(tris.end(), { k, k + 1, k + 2, k, k + 2, k + 3, k, k + 2, k + 1, k, k + 3, k + 2 });
		}
		hazeColors.assign(hazeVerts.size(), Color(0, 0, 0, 0));
		auto haze = NewGameObject("Earth Biomass haze");
		haze->transform->SetParent(root->transform, false);
		auto m = cs::New<Mesh>("Earth Biomass haze");
		hazeMesh = Materials::Own(m);
		hazeMesh->MarkDynamic();
		hazeMesh->SetVertices(hazeVerts);
		hazeMesh->SetColors(hazeColors);
		hazeMesh->SetUVs(0, hazeUvs);
		hazeMesh->SetTriangles(tris, 0);
		hazeMesh->RecalculateBounds();
		AddComponent<MeshFilter>(haze)->sharedMesh = hazeMesh;
		auto hr = AddComponent<MeshRenderer>(haze);
		hr->SetSharedMaterial(Materials::Create(Materials::Fx(), "Earth Biomass haze"));
		hr->shadowCastingMode = ShadowCastingMode::Off;
		hr->receiveShadows = false;

		lastJd = app->Clock->Jd;
	}

	EarthBiomassView::~EarthBiomassView()
	{
		if (Alive(root)) Destroy(root);
	}

	void EarthBiomassView::SetVisible(bool v)
	{
		visible = v;
		root->SetActive(v);
	}

	void EarthBiomassView::Apply()
	{
		if (!visible) return;
		// The breath runs on the simulation clock (pause holds it, rewind runs it back), but a frame's step is capped, so
		// a fast-forwarded day reads as quick breathing rather than a strobe.
		double jd = app->Clock->Jd;
		double step = std::clamp((jd - lastJd) * 86400.0, -0.25, 0.25);
		lastJd = jd;
		breathPhase += step * BreathsPerMinute / 60.0 * 2 * Mathf::PI;
		float breath = static_cast<float>(0.5 + 0.5 * std::sin(breathPhase));
		root->transform->localScale = Vector3::one() * (1 + BreathLift * (breath * 2 - 1));
		flesh->SetColor("_Color", Color(1, 1, 1) * (FleshGain * (0.92f + 0.1f * breath)));

		// "At certain times of day, when atmospheric conditions are right": a slow swell over hours, on the day side only.
		double hours = jd * 24;
		float conditions = SmoothStep(0.55f, 0.9f, static_cast<float>(0.5 + 0.5 * std::sin(hours * 2 * Mathf::PI / 7.3 + 1.3 * std::sin(hours * 2 * Mathf::PI / 31))));
		Vector4 sun4 = Shader::GetGlobalVector("_SunPositionWS");
		Vector3 sun(sun4.x, sun4.y, sun4.z);
		Quaternion rot = earth->rotation();
		Vector3 centre = earth->position();
		for (size_t i = 0; i < hazeCentres.size(); i++)
		{
			Vector3 n = rot * hazeCentres[i];
			Vector3 toSun = sun - centre;
			float day = SmoothStep(-0.05f, 0.3f, Vector3::Dot(n, toSun.normalized()));
			float a = 0.07f * conditions * day * (0.85f + 0.15f * breath);
			Color c(0.80f, 0.62f, 0.60f, a);
			for (int k = 0; k < 4; k++) hazeColors[i * 4 + k] = c;
		}
		hazeMesh->SetColors(hazeColors);
	}
}
