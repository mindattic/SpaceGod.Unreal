// US-T839: an assembled design merged into one mesh (see DesignMeshes.h).
#include "Rendering/DesignMeshes.h"

#include "Rendering/HullLighting.h"
#include "Rendering/ImportedGeometry.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"

#include "Async/Async.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace SpaceGod::Rendering
{
	namespace
	{
		Vector3 DesignVector(const Core::DVec3& v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }
		Core::DVec3 DesignDVec(const Vector3& v) { return Core::DVec3(v.x, v.y, v.z); }

		/// The grey box a part without its own mesh draws as: Unity's unit cube, its triangles Unity-wound.
		struct DesignBox
		{
			std::vector<Vector3> Vertices, Normals;
			std::vector<Vector2> Uv;
			std::vector<int> Triangles;
		};

		const DesignBox& DesignBoxSurface()
		{
			static const DesignBox box = []
			{
				DesignBox b;
				cs::Ref<Mesh> cube = MeshFactory::BuiltinCube();
				b.Vertices = cube->Vertices;
				b.Normals = cube->Normals;
				b.Normals.resize(b.Vertices.size(), Vector3::up());
				for (size_t i = 0; i < b.Vertices.size(); i++)
					b.Uv.push_back(i < cube->Uv[0].size() ? Vector2(cube->Uv[0][i].x, 1 - cube->Uv[0][i].y) : Vector2(0, 0));
				for (const auto& sub : cube->SubMeshes)
					if (sub.Topology == MeshTopology::Triangles) b.Triangles.insert(b.Triangles.end(), sub.Indices.begin(), sub.Indices.end());
				return b;
			}();
			return box;
		}

		/// One part of a merge: its placement, and either its imported surface (fitted) or the box of its bounds.
		struct DesignPartJob
		{
			Core::ModularPlacement Place;
			const ImportedGeometry* Geometry = nullptr;
			Vector3 Offset;
			Quaternion Turn = Quaternion::identity();
			float Scale = 1;
			Vector3 BoxSize;
			int Group = 0;
		};

		struct DesignBuilt
		{
			FMeshDescription Description;
			Vector3 Min = Vector3::one() * 1e30f, Max = Vector3::one() * -1e30f;
			int Triangles = 0;
		};

		struct DesignEntry
		{
			cs::Ref<Mesh> Drawn;
			std::vector<cs::Ref<Material>> Materials;
			std::vector<DesignMeshes::Done> Waiting;
			bool Building = false;
		};

		std::unordered_map<uint64_t, DesignEntry>& DesignCache()
		{
			static std::unordered_map<uint64_t, DesignEntry> cache;
			return cache;
		}

		/// Unity (x, y, z) m is Unreal (x, -z, y) cm (UnrealSpace); the merged mesh is drawn at ImportedScale 0.01.
		FVector3f DesignToUnreal(const Core::DVec3& v, float k) { return FVector3f(static_cast<float>(v.X), static_cast<float>(-v.Z), static_cast<float>(v.Y)) * k; }

		/// Thread pool: every part's triangles in the design frame, one polygon group per material.
		void DesignBuild(const std::vector<DesignPartJob>& parts, int groups, DesignBuilt& out)
		{
			FStaticMeshAttributes attributes(out.Description);
			attributes.Register();
			TVertexAttributesRef<FVector3f> positions = attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector3f> normals = attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector2f> uvs = attributes.GetVertexInstanceUVs();
			TPolygonGroupAttributesRef<FName> slots = attributes.GetPolygonGroupMaterialSlotNames();
			TArray<FPolygonGroupID> polygonGroups;
			for (int g = 0; g < groups; g++)
			{
				FPolygonGroupID id = out.Description.CreatePolygonGroup();
				slots[id] = FName(*FString::Printf(TEXT("Section%d"), g));
				polygonGroups.Add(id);
			}
			const DesignBox& box = DesignBoxSurface();
			TArray<FVertexInstanceID> instances;
			for (const DesignPartJob& job : parts)
			{
				const Core::ModularPlacement& p = job.Place;
				bool imported = job.Geometry != nullptr;
				const std::vector<Vector3>& vertices = imported ? job.Geometry->Vertices : box.Vertices;
				const std::vector<int>& triangles = imported ? job.Geometry->Triangles : box.Triangles;
				instances.Reset(static_cast<int32>(vertices.size()));
				for (size_t i = 0; i < vertices.size(); i++)
				{
					Vector3 local, normal;
					FVector2f uv(0, 0);
					if (imported)
					{
						local = job.Offset + job.Turn * (vertices[i] * job.Scale);
						normal = i < job.Geometry->Normals.size() ? job.Turn * job.Geometry->Normals[i] : Vector3::up();
						if (i < job.Geometry->Uv0.size()) uv = FVector2f(job.Geometry->Uv0[i].x, job.Geometry->Uv0[i].y);
					}
					else
					{
						const Vector3& n = box.Normals[i];
						local = job.Offset + Vector3::Scale(vertices[i], job.BoxSize);
						normal = Vector3(n.x / job.BoxSize.x, n.y / job.BoxSize.y, n.z / job.BoxSize.z);
						uv = FVector2f(box.Uv[i].x, box.Uv[i].y);
					}
					Core::DVec3 at = p.ToDesign(DesignDVec(local));
					Vector3 a = DesignVector(at);
					out.Min = Vector3::Min(out.Min, a);
					out.Max = Vector3::Max(out.Max, a);
					FVertexID v = out.Description.CreateVertex();
					positions[v] = DesignToUnreal(at, 100.0f);
					FVertexInstanceID vi = out.Description.CreateVertexInstance(v);
					FVector3f nn = DesignToUnreal(p.Turn(DesignDVec(normal)), 1.0f);
					normals[vi] = nn.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
					uvs.Set(vi, 0, uv);
					instances.Add(vi);
				}
				// An import's triangles keep Unreal's winding through the two axis changes (both rotations); the box's are
				// Unity's (the renderer re-winds them on upload, so here too); a mirrored part turns each face over once more.
				bool swap = p.Mirrored != !imported;
				int n = instances.Num();
				for (size_t t = 0; t + 2 < triangles.size(); t += 3)
				{
					int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
					if (a < 0 || b < 0 || c < 0 || a >= n || b >= n || c >= n || a == b || b == c || a == c) continue;
					if (swap) std::swap(b, c);
					FVertexInstanceID tri[3] = { instances[a], instances[b], instances[c] };
					out.Description.CreateTriangle(polygonGroups[job.Group], MakeArrayView(tri, 3));
					out.Triangles++;
				}
			}
		}
	}

	const DesignMeshes::Fitted& DesignMeshes::FitOf(const Core::KitPart& part)
	{
		static std::unordered_map<cs::String, Fitted> fits;
		auto it = fits.find(part.Asset);
		if (it != fits.end()) return it->second;
		Fitted f;
		cs::String leaf = part.Asset.substr(part.Asset.find_last_of('/') + 1);
		cs::String path = cs::Cat(part.Asset, ".", leaf);
		if (UStaticMesh* asset = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(path.c_str()), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			FBox box = asset->GetBoundingBox();
			// Unreal (x, y, z) cm is Unity (x, z, -y) / 100 (UnrealSpace).
			Vector3 mn = Vector3(static_cast<float>(box.Min.X), static_cast<float>(box.Min.Z), static_cast<float>(-box.Max.Y)) / 100.0f;
			Vector3 mx = Vector3(static_cast<float>(box.Max.X), static_cast<float>(box.Max.Z), static_cast<float>(-box.Min.Y)) / 100.0f;
			Vector3 size = mx - mn;
			Vector3 want = DesignVector(part.Max - part.Min);
			f.Scale = want.magnitude() / Mathf::Max(size.magnitude(), 1e-4f);
			float guess = part.Pack() == "starsparrow" ? -90.0f : 180.0f;
			float best = 1e30f;
			for (float yaw : { guess, guess + 90.0f })
			{
				Quaternion q = Quaternion::Euler(0, yaw, 0);
				Vector3 s = q * size;
				float miss = Mathf::Abs(Mathf::Abs(s.x) * f.Scale - want.x) + Mathf::Abs(Mathf::Abs(s.z) * f.Scale - want.z);
				if (miss < best - 1e-3f)
				{
					best = miss;
					f.Turn = q;
				}
			}
			auto mesh = cs::New<Mesh>(leaf);
			mesh->ImportedAsset = path;
			mesh->ImportedScale = 0.01f;
			mesh->SetBounds(Bounds((mn + mx) * 0.5f, size));
			f.Drawn = mesh;
			f.Ok = true;
		}
		return fits.emplace(part.Asset, f).first->second;
	}

	cs::Ref<Material> DesignMeshes::MaterialOf(const Core::KitPart& part)
	{
		static std::unordered_map<cs::String, cs::Ref<Material>> materials;
		if (part.Pack() == "starsparrow") return StarSparrowKit::Livery(nullptr);
		auto it = materials.find(part.Atlas);
		if (it != materials.end()) return it->second;
		cs::String leaf = cs::Cat("MI_USC_", part.Atlas, "_Grey");
		auto m = Materials::MarkShared(cs::New<Material>(Shader::Find(HullLighting::LitVariant(cs::Cat(UscKit::Root, "/Materials/", leaf))), cs::Cat("Assembled_", part.Atlas)));
		return materials.emplace(part.Atlas, m).first->second;
	}

	uint64_t DesignMeshes::Hash(const Core::ModularDesign& design)
	{
		uint64_t h = 14695981039346656037ull;
		auto bytes = [&h](const void* data, size_t n)
		{
			const auto* b = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < n; i++)
			{
				h ^= b[i];
				h *= 1099511628211ull;
			}
		};
		auto vec = [&bytes](const Core::DVec3& v)
		{
			double d[3] = { v.X, v.Y, v.Z };
			bytes(d, sizeof(d));
		};
		for (const auto& p : design.Parts)
		{
			bytes(p.PartId.data(), p.PartId.size() + 1);
			vec(p.Pos);
			vec(p.Forward);
			vec(p.Up);
			bytes(&p.Scale, sizeof(p.Scale));
			unsigned char mirrored = p.Mirrored ? 1 : 0;
			bytes(&mirrored, 1);
		}
		return h;
	}

	void DesignMeshes::Request(const Core::ModularDesign& design, const Core::KitCatalogue& catalogue, Done done)
	{
		uint64_t hash = Hash(design);
		DesignEntry& entry = DesignCache()[hash];
		if (entry.Drawn)
		{
			done(entry.Drawn, entry.Materials);
			return;
		}
		entry.Waiting.push_back(std::move(done));
		if (entry.Building) return;
		entry.Building = true;

		// Game thread: each part's surface (loaded and read once per asset) and its material's section.
		DesignBoxSurface();
		auto parts = std::make_shared<std::vector<DesignPartJob>>();
		parts->reserve(design.Parts.size());
		std::vector<cs::String> keys;
		std::vector<cs::Ref<Material>> materials;
		for (const auto& p : design.Parts)
		{
			const Core::KitPart& part = catalogue.Parts()[p.Part];
			DesignPartJob job;
			job.Place = p;
			Vector3 middle = DesignVector((part.Min + part.Max) * 0.5);
			const Fitted* fit = part.Asset.empty() ? nullptr : &FitOf(part);
			const ImportedGeometry* surface = fit && fit->Ok ? ImportedGeometry::Of(fit->Drawn) : nullptr;
			job.Geometry = surface && surface->Ok ? surface : nullptr;
			cs::String key;
			if (job.Geometry)
			{
				job.Offset = part.PivotOrigin ? Vector3::zero() : middle;
				job.Turn = fit->Turn;
				job.Scale = fit->Scale;
				key = part.Pack() == "starsparrow" ? cs::String("starsparrow") : part.Atlas;
			}
			else
			{
				job.Offset = middle;
				job.BoxSize = Vector3::Max(DesignVector(part.Max - part.Min), Vector3::one() * 0.01f);
				key = "box";
			}
			auto k = std::find(keys.begin(), keys.end(), key);
			job.Group = static_cast<int>(k - keys.begin());
			if (k == keys.end())
			{
				keys.push_back(key);
				materials.push_back(job.Geometry ? MaterialOf(part) : ShipModels::ProceduralMaterial());
			}
			parts->push_back(std::move(job));
		}
		entry.Materials = std::move(materials);
		int groups = static_cast<int>(keys.size());

		Async(EAsyncExecution::ThreadPool, [hash, parts, groups]
		{
			auto built = std::make_shared<DesignBuilt>();
			DesignBuild(*parts, groups, *built);
			AsyncTask(ENamedThreads::GameThread, [hash, built, groups]
			{
				if (IsEngineExitRequested()) return;
				DesignEntry& entry = DesignCache()[hash];
				entry.Building = false;
				std::vector<Done> waiting = std::move(entry.Waiting);
				entry.Waiting.clear();
				if (built->Triangles == 0) return;

				// The fast non-Nanite build: no game-thread stall worth a frame (Nanite designs are an offline bake).
				FName name(*FString::Printf(TEXT("SG_Design_%016llx"), static_cast<unsigned long long>(hash)));
				UStaticMesh* sm = NewObject<UStaticMesh>(GetTransientPackage(), name, RF_Transient);
				sm->AddToRoot();
				for (int g = 0; g < groups; g++)
				{
					FName slot(*FString::Printf(TEXT("Section%d"), g));
					sm->GetStaticMaterials().Add(FStaticMaterial(nullptr, slot, slot));
				}
				UStaticMesh::FBuildMeshDescriptionsParams params;
				params.bMarkPackageDirty = false;
				params.bCommitMeshDescription = false;
				params.bFastBuild = true;
				sm->BuildFromMeshDescriptions(TArray<const FMeshDescription*>{ &built->Description }, params);

				auto mesh = cs::New<Mesh>(cs::Cat("Design ", static_cast<long long>(hash)));
				mesh->ImportedAsset = TCHAR_TO_UTF8(*sm->GetPathName());
				mesh->ImportedScale = 0.01f;
				mesh->SetBounds(Bounds((built->Min + built->Max) * 0.5f, built->Max - built->Min));
				entry.Drawn = mesh;
				for (auto& done : waiting) done(entry.Drawn, entry.Materials);
			});
		});
	}
}
