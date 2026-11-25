// US-T765: sg.Assemble <class> <seed> [family=<kit set>] [pack=<pack>] [imported], the seeded hull grammar
// (SpaceGodCore's ModularAssembler) shown in the model viewer: every part of the design at its pose and scale, drawn with
// its imported mesh where an import brings one in (Star Sparrow's modules, USC's bolted-on ones), else as a grey box of its
// bounds. The catalogue (Data/ModularParts.json) is read once.

#include "ModelViewer.h"
#include "SpaceGodLog.h"

#include "Assembly/ModularAssembler.h"
#include "Rendering/HullLighting.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"
#include "Ships.h"

#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#include <cmath>
#include <unordered_map>

using namespace SpaceGod::Core;
using namespace SpaceGod::Rendering;

namespace SpaceGod
{
namespace
{
	const KitCatalogue& Catalogue()
	{
		static const KitCatalogue catalogue = []
		{
			FString file = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Data/ModularParts.json")));
			return KitCatalogue::Load(cs::String(TCHAR_TO_UTF8(*file)));
		}();
		return catalogue;
	}

	bool ParseClass(const FString& name, ShipClass& cls)
	{
		for (int c = 0; c <= static_cast<int>(ShipClass::CaptureTug); c++)
		{
			if (c == 7) continue; // the retired Breacher hull's number (see ShipClass)
			if (name.Equals(UTF8_TO_TCHAR(ToString(static_cast<ShipClass>(c))), ESearchCase::IgnoreCase))
			{
				cls = static_cast<ShipClass>(c);
				return true;
			}
		}
		return false;
	}

	Vector3 ToVector(const DVec3& v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }

	/// A part's imported mesh, fitted to the catalogue's box in the model frame: turned from the import's frame (USC's nose
	/// -Z, Star Sparrow's +X; whichever turn makes the mesh's box the catalogue's) and scaled to its size.
	struct Fitted
	{
		cs::Ref<Mesh> Drawn;
		Quaternion Turn;
		float Scale = 1;
		bool Ok = false;
	};

	const Fitted& FitOf(const KitPart& part)
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
			Vector3 want = ToVector(part.Max - part.Min);
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

	cs::Ref<Material> MaterialOf(const KitPart& part)
	{
		static std::unordered_map<cs::String, cs::Ref<Material>> materials;
		if (part.Pack() == "starsparrow") return StarSparrowKit::Livery(nullptr);
		auto it = materials.find(part.Atlas);
		if (it != materials.end()) return it->second;
		cs::String leaf = cs::Cat("MI_USC_", part.Atlas, "_Grey");
		auto m = Materials::MarkShared(cs::New<Material>(Shader::Find(HullLighting::LitVariant(cs::Cat(UscKit::Root, "/Materials/", leaf))), cs::Cat("Assembled_", part.Atlas)));
		return materials.emplace(part.Atlas, m).first->second;
	}

	void Draw(const cs::Ref<GameObject>& go, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& material)
	{
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = HullLighting::Enabled() ? ShadowCastingMode::On : ShadowCastingMode::Off;
		mr->receiveShadows = false;
	}

	cs::Ref<GameObject> BuildModel(const ModularDesign& design, const KitCatalogue& catalogue, int& imported)
	{
		static cs::Ref<Mesh> cube;
		if (!cube) cube = MeshFactory::BuiltinCube();
		auto root = NewGameObject(cs::Cat("Assembled ", ToString(design.Class), " ", design.Seed));
		for (const auto& p : design.Parts)
		{
			const KitPart& part = catalogue.Parts()[p.Part];
			auto go = NewGameObject(cs::Cat(p.Slot, " ", part.Id));
			go->transform->SetParent(root->transform, false);
			go->transform->localPosition = ToVector(p.Pos);
			go->transform->localRotation = Quaternion::LookRotation(ToVector(p.Forward), ToVector(p.Up));
			float s = static_cast<float>(p.Scale);
			go->transform->localScale = Vector3(p.Mirrored ? -s : s, s, s);

			auto body = NewGameObject("Mesh");
			body->transform->SetParent(go->transform, false);
			Vector3 middle = ToVector((part.Min + part.Max) * 0.5);
			const Fitted* fit = part.Asset.empty() ? nullptr : &FitOf(part);
			if (fit && fit->Ok)
			{
				body->transform->localPosition = part.PivotOrigin ? Vector3::zero() : middle;
				body->transform->localRotation = fit->Turn;
				body->transform->localScale = Vector3::one() * fit->Scale;
				Draw(body, fit->Drawn, MaterialOf(part));
				imported++;
			}
			else
			{
				body->transform->localPosition = middle;
				body->transform->localScale = Vector3::Max(ToVector(part.Max - part.Min), Vector3::one() * 0.01f);
				Draw(body, cube, ShipModels::ProceduralMaterial());
			}
		}
		return root;
	}

	void Assemble(const TArray<FString>& args)
	{
		ShipClass cls{};
		if (args.Num() < 2 || !ParseClass(args[0], cls))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble <class> <seed> [family=<kit set>] [pack=<pack>] [imported], e.g. sg.Assemble Carrier 42"));
			return;
		}
		uint64 seed = FCString::Strtoui64(*args[1], nullptr, 10);
		FactionStyle style;
		for (int i = 2; i < args.Num(); i++)
		{
			FString key, value;
			if (!args[i].Split(TEXT("="), &key, &value)) key = args[i];
			if (key.Equals(TEXT("family"), ESearchCase::IgnoreCase)) style.Families.push_back(TCHAR_TO_UTF8(*value));
			else if (key.Equals(TEXT("pack"), ESearchCase::IgnoreCase)) style.Packs.push_back(TCHAR_TO_UTF8(*value.ToLower()));
			else if (key.Equals(TEXT("imported"), ESearchCase::IgnoreCase)) style.ImportedOnly = true;
		}

		const KitCatalogue& catalogue = Catalogue();
		ModularDesign design = ModularAssembler::Assemble(catalogue, cls, seed, style);
		if (!design.Problem.empty())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: %s"), UTF8_TO_TCHAR(design.Problem.c_str()));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: %s seed %llu, %s set, %d parts (%d spine, %d hangar, %d engine, %d wing, %d turret), %.0f m"),
			UTF8_TO_TCHAR(ToString(cls)), static_cast<unsigned long long>(seed), UTF8_TO_TCHAR(design.Family.c_str()), static_cast<int>(design.Parts.size()),
			design.Count("spine"), design.Count("hangar"), design.Count("engine"), design.Count("wing"), design.Count("turret"), design.LengthM);
		for (const auto& p : design.Parts)
			UE_LOG(LogSpaceGod, Verbose, TEXT("sg.Assemble:   %-7s %-40s on %3d %-9s scale %.2f at (%.1f, %.1f, %.1f)%s"), UTF8_TO_TCHAR(p.Slot.c_str()),
				UTF8_TO_TCHAR(p.PartId.c_str()), p.Parent, UTF8_TO_TCHAR(p.ParentSocket.c_str()), p.Scale, p.Pos.X, p.Pos.Y, p.Pos.Z, p.Mirrored ? TEXT(" mirrored") : TEXT(""));

		int imported = 0;
		auto model = BuildModel(design, catalogue, imported);
		cs::String name = cs::Cat(ToString(cls), " #", design.Seed);
		if (!ModelViewer::ShowBuilt(model, name))
		{
			Destroy(model);
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: no model viewer (start the game first)"));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: shown in the model viewer, %d of %d parts drawn with their own mesh, the rest as boxes"), imported,
			static_cast<int>(design.Parts.size()));
	}

	FAutoConsoleCommand GAssembleCommand(
		TEXT("sg.Assemble"),
		TEXT("US-T765: assembles a hull from the kit parts (Data/ModularParts.json) for a class and seed and shows it in the model viewer. "
			"sg.Assemble <class> <seed> [family=<kit set>] [pack=<usc|starsparrow|ssc>] [imported]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Assemble(args); }));
}
}
