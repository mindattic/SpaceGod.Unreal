// US-T765: sg.Assemble <class> <seed> [family=<kit set>] [pack=<pack>] [imported], the seeded hull grammar
// (SpaceGodCore's ModularAssembler) shown in the model viewer: every part of the design at its pose and scale, drawn with
// its imported mesh where an import brings one in (Star Sparrow's modules, USC's bolted-on ones), else as a grey box of its
// bounds. The catalogue (Data/ModularParts.json) is read once.

#include "ModelViewer.h"
#include "SpaceGodLog.h"

#include "Assembly/ModularAssembler.h"
#include "Combat/VehicleData.h"
#include "Rendering/DesignMeshes.h"
#include "Rendering/EnginePorts.h"
#include "Rendering/HullLighting.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Ships.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

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

	void Draw(const cs::Ref<GameObject>& go, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& material)
	{
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = HullLighting::Enabled() ? ShadowCastingMode::On : ShadowCastingMode::Off;
		mr->receiveShadows = false;
	}

	cs::Ref<GameObject> BuildModel(const ModularDesign& design, const KitCatalogue& catalogue, int& imported, std::vector<cs::Ref<GameObject>>& bodies)
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
			bodies.push_back(body);
			Vector3 middle = ToVector((part.Min + part.Max) * 0.5);
			const DesignMeshes::Fitted* fit = part.Asset.empty() ? nullptr : &DesignMeshes::FitOf(part);
			if (fit && fit->Ok)
			{
				body->transform->localPosition = part.PivotOrigin ? Vector3::zero() : middle;
				body->transform->localRotation = fit->Turn;
				body->transform->localScale = Vector3::one() * fit->Scale;
				Draw(body, fit->Drawn, DesignMeshes::MaterialOf(part));
				imported++;
			}
			else
			{
				body->transform->localPosition = middle;
				body->transform->localScale = Vector3::Max(ToVector(part.Max - part.Min), Vector3::one() * 0.01f);
				Draw(body, cube, ShipModels::ProceduralMaterial());
			}

			// US-T766: the flames leave from the engine parts' own nozzles, riding with the part.
			if (p.Slot == "engine")
				if (const KitSocket* nozzle = part.Socket("exhaust"))
					EnginePorts::Mark(go->transform, ToVector(nozzle->Pos), ToVector(nozzle->Normal),
						0.5f * Mathf::Sqrt(static_cast<float>(std::max(nozzle->Size[0], 0.0) * std::max(nozzle->Size[1], 0.0))));
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

		VehicleData::Geometry geometry = VehicleData::FromDesign(design, catalogue);
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: ports from the parts: %d engine nozzles, %d gun mounts, %d hangar mouths, %d bridge"),
			static_cast<int>(geometry.Engines.size()), static_cast<int>(geometry.Guns.size()), static_cast<int>(geometry.Hangars.size()),
			static_cast<int>(geometry.Bridges.size()));

		int imported = 0;
		std::vector<cs::Ref<GameObject>> bodies;
		auto model = BuildModel(design, catalogue, imported, bodies);
		cs::String name = cs::Cat(ToString(cls), " #", design.Seed);
		if (!ModelViewer::ShowBuilt(model, name))
		{
			Destroy(model);
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: no model viewer (start the game first)"));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: shown in the model viewer, %d of %d parts drawn with their own mesh, the rest as boxes"), imported,
			static_cast<int>(design.Parts.size()));

		// US-T839: the parts swap to the design merged into one mesh once it is built; the nozzle marks stay on the parts.
		std::weak_ptr<GameObject> shown = model;
		std::vector<std::weak_ptr<GameObject>> parts(bodies.begin(), bodies.end());
		DesignMeshes::Request(design, catalogue, [shown, parts](const cs::Ref<Mesh>& merged, const std::vector<cs::Ref<Material>>& materials)
		{
			cs::Ref<GameObject> root = shown.lock();
			if (!root || root->destroyed) return;
			for (const auto& part : parts)
				if (cs::Ref<GameObject> body = part.lock()) Destroy(body);
			AddComponent<MeshFilter>(root)->sharedMesh = merged;
			auto mr = AddComponent<MeshRenderer>(root);
			mr->SetSharedMaterials(materials);
			mr->shadowCastingMode = HullLighting::Enabled() ? ShadowCastingMode::On : ShadowCastingMode::Off;
			mr->receiveShadows = false;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: swapped to the merged mesh, %d sections"), static_cast<int>(materials.size()));
		});
	}

	FAutoConsoleCommand GAssembleCommand(
		TEXT("sg.Assemble"),
		TEXT("US-T765: assembles a hull from the kit parts (Data/ModularParts.json) for a class and seed and shows it in the model viewer. "
			"sg.Assemble <class> <seed> [family=<kit set>] [pack=<usc|starsparrow|ssc>] [imported]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Assemble(args); }));
}
}
