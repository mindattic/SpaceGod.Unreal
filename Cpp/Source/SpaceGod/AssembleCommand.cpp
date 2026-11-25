// US-T765: sg.Assemble <class> <seed> [family=<kit set>] [pack=<pack>] [imported], the seeded hull grammar
// (SpaceGodCore's ModularAssembler) shown in the model viewer: every part of the design at its pose and scale, drawn with
// its imported mesh where an import brings one in (Star Sparrow's modules, USC's bolted-on ones), else as a grey box of its
// bounds. The catalogue (Data/ModularParts.json) is read once.

#include "ModelViewer.h"
#include "SpaceGodLog.h"

#include "Assembly/FactionStyles.h"
#include "Assembly/ModularAssembler.h"
#include "Combat/VehicleData.h"
#include "Rendering/DesignMeshes.h"
#include "Rendering/EnginePorts.h"
#include "Rendering/HullLighting.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/UscKit.h"
#include "Ships.h"
#include "Systems/Kardashev.h"

#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
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

	// US-T845: faction style profiles (Data/FactionStyles.json), each faction's palette over the USC unified colour.
	const FactionStyleBook& AssembleStyleBook()
	{
		static const FactionStyleBook book = []
		{
			FString file = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Data/FactionStyles.json")));
			return FactionStyleBook::Load(cs::String(TCHAR_TO_UTF8(*file)));
		}();
		return book;
	}

	bool AssembleStyledInstance(const cs::String& leaf)
	{
		static std::unordered_map<cs::String, bool> known;
		auto it = known.find(leaf);
		if (it != known.end()) return it->second;
		bool ok = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/Materials/%s.%s"), UTF8_TO_TCHAR(UscKit::Root), UTF8_TO_TCHAR(leaf.c_str()),
			UTF8_TO_TCHAR(leaf.c_str())), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
		known.emplace(leaf, ok);
		return ok;
	}

	/// A USC atlas in a style's palette: its unified colour's instance with a light wash of the primary, as UscKit's
	/// faction livery does. One shared material per atlas and style.
	cs::Ref<Material> AssembleStyledMaterial(const cs::String& atlas, const FactionStyleProfile& profile)
	{
		static std::unordered_map<cs::String, cs::Ref<Material>> cache;
		cs::String key = cs::Cat(atlas, "|", profile.Id);
		auto it = cache.find(key);
		if (it != cache.end() && Alive(it->second)) return it->second;
		bool pirate = profile.Palette.Colour == "Black";
		cs::String colour = profile.Palette.Colour;
		if (!AssembleStyledInstance(cs::Cat("MI_USC_", atlas, "_", colour)))
			colour = pirate && AssembleStyledInstance(cs::Cat("MI_USC_", atlas, "_Red")) ? cs::String("Red") : cs::String("Grey");
		cs::String leaf = cs::Cat("MI_USC_", atlas, "_", colour);
		auto m = Materials::MarkShared(cs::New<Material>(Shader::Find(HullLighting::LitVariant(cs::Cat(UscKit::Root, "/Materials/", leaf))),
			cs::Cat("Assembled_", atlas, "_", profile.Id)));
		Color tint = Color::white();
		if (profile.Palette.Primary.Set)
		{
			Color f(static_cast<float>(profile.Palette.Primary.R), static_cast<float>(profile.Palette.Primary.G), static_cast<float>(profile.Palette.Primary.B));
			float peak = Mathf::Max(f.r, Mathf::Max(f.g, f.b));
			if (peak > 1e-3f) f = f * (1.0f / peak);
			tint = Color::Lerp(Color::white(), f, pirate ? 0.25f : 0.18f);
			if (pirate) tint = tint * 0.85f;
		}
		m->SetColor("Tint", tint);
		m->SetColor("_Tint", Color::white());
		cache[key] = m;
		return m;
	}

	/// Repaints the USC parts BuildModel drew with their own mesh (its bodies, one per placement) in the style's palette.
	void AssemblePaint(const ModularDesign& design, const KitCatalogue& catalogue, const std::vector<cs::Ref<GameObject>>& bodies, const FactionStyleProfile& profile)
	{
		for (size_t i = 0; i < design.Parts.size() && i < bodies.size(); i++)
		{
			const KitPart& part = catalogue.Parts()[design.Parts[i].Part];
			if (part.Pack() != "usc" || part.Asset.empty() || !DesignMeshes::FitOf(part).Ok) continue;
			if (auto mr = GetComponent<MeshRenderer>(bodies[i])) mr->SetSharedMaterial(AssembleStyledMaterial(part.Atlas, profile));
		}
	}

	/// sg.Assemble <class> <seed> lineup: every faction's take on the class, side by side along +X under one root.
	void AssembleLineup(ShipClass cls, uint64 seed)
	{
		const KitCatalogue& catalogue = Catalogue();
		const FactionStyleBook& book = AssembleStyleBook();
		if (!book.Loaded())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: %s"), UTF8_TO_TCHAR(book.Problem().c_str()));
			return;
		}
		auto root = NewGameObject(cs::Cat("Lineup ", ToString(cls), " ", seed));
		double x = 0, lastHalf = 0;
		int shown = 0;
		for (const auto& profile : book.Profiles())
		{
			if (profile.Faction.empty()) continue;
			ModularDesign design = ModularAssembler::Assemble(catalogue, cls, seed, profile.For(cls));
			if (!design.Problem.empty())
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: %s: %s"), UTF8_TO_TCHAR(profile.Id.c_str()), UTF8_TO_TCHAR(design.Problem.c_str()));
				continue;
			}
			double half = std::max(0.5 * (design.Max.X - design.Min.X), 1.0);
			if (shown > 0) x += lastHalf + half + 0.25 * std::max(design.LengthM, 1.0);
			lastHalf = half;
			int imported = 0;
			std::vector<cs::Ref<GameObject>> bodies;
			auto model = BuildModel(design, catalogue, imported, bodies);
			AssemblePaint(design, catalogue, bodies, profile);
			cs::String hull = profile.HullNumber(seed);
			model->name = cs::Cat(profile.Faction, " ", hull);
			model->transform->SetParent(root->transform, false);
			model->transform->localPosition = Vector3(static_cast<float>(x), 0, 0);
			shown++;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: %-24s %-10s %s set, %s, %d parts, %d drawn with their own mesh"), UTF8_TO_TCHAR(profile.Faction.c_str()),
				UTF8_TO_TCHAR(hull.c_str()), UTF8_TO_TCHAR(design.Family.c_str()), UTF8_TO_TCHAR(profile.Palette.Colour.c_str()), static_cast<int>(design.Parts.size()), imported);
		}
		if (shown == 0 || !ModelViewer::ShowBuilt(root, cs::Cat(ToString(cls), " #", seed, " lineup")))
		{
			Destroy(root);
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: no lineup shown (%s)"), shown == 0 ? TEXT("no style built") : TEXT("no model viewer, start the game first"));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: lineup of %d faction styles shown in the model viewer"), shown);
	}

	/// US-T815: sg.Assemble ... kardashev=<index> builds as a civilisation at that index would: a class whose milestone it
	/// has not reached is refused, and a capital class past its own is built heavier (Kardashev::Tiered).
	bool AssembleAtKardashev(const TArray<FString>& args, ShipClass cls, FactionStyle& style)
	{
		for (int i = 2; i < args.Num(); i++)
		{
			FString key, value;
			if (!args[i].Split(TEXT("="), &key, &value) || !key.Equals(TEXT("kardashev"), ESearchCase::IgnoreCase)) continue;
			double index = FCString::Atod(*value);
			if (!Core::Kardashev::Unlocked(cls, index))
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: %s opens at Kardashev %.2f, not %.2f"), UTF8_TO_TCHAR(ToString(cls)),
					Core::Kardashev::UnlockIndex(cls), index);
				return false;
			}
			style = Core::Kardashev::Tiered(style, cls, index);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: at Kardashev %.2f, length %.2f, armament %.2f"), index, style.Length, style.Armament);
		}
		return true;
	}

	void Assemble(const TArray<FString>& args)
	{
		ShipClass cls{};
		if (args.Num() < 2 || !ParseClass(args[0], cls))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble <class> <seed> [family=<kit set>] [pack=<pack>] [imported] [faction=<style id>] [tubes=<per hangar>] [lineup], e.g. sg.Assemble Carrier 42"));
			return;
		}
		uint64 seed = FCString::Strtoui64(*args[1], nullptr, 10);
		FactionStyle style;
		FString factionId;
		bool lineup = false;
		for (int i = 2; i < args.Num(); i++)
		{
			FString key, value;
			if (!args[i].Split(TEXT("="), &key, &value)) key = args[i];
			if (key.Equals(TEXT("family"), ESearchCase::IgnoreCase)) style.Families.push_back(TCHAR_TO_UTF8(*value));
			else if (key.Equals(TEXT("pack"), ESearchCase::IgnoreCase)) style.Packs.push_back(TCHAR_TO_UTF8(*value.ToLower()));
			else if (key.Equals(TEXT("imported"), ESearchCase::IgnoreCase)) style.ImportedOnly = true;
			else if (key.Equals(TEXT("tubes"), ESearchCase::IgnoreCase)) style.LaunchTubes = FMath::Clamp(FCString::Atoi(*value), 0, 64);
			else if (key.Equals(TEXT("faction"), ESearchCase::IgnoreCase)) factionId = value;
			else if (key.Equals(TEXT("lineup"), ESearchCase::IgnoreCase)) lineup = true;
		}
		if (lineup)
		{
			AssembleLineup(cls, seed);
			return;
		}
		const FactionStyleProfile* profile = nullptr;
		if (!factionId.IsEmpty())
		{
			profile = AssembleStyleBook().Find(TCHAR_TO_UTF8(*factionId));
			if (!profile)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: no faction style '%s' in Data/FactionStyles.json %s"), *factionId,
					UTF8_TO_TCHAR(AssembleStyleBook().Problem().c_str()));
				return;
			}
			FactionStyle given = style;
			style = profile->For(cls);
			if (!given.Families.empty()) style.Families = given.Families;
			if (!given.Packs.empty()) style.Packs = given.Packs;
			style.ImportedOnly = style.ImportedOnly || given.ImportedOnly;
			if (given.LaunchTubes > 0) style.LaunchTubes = given.LaunchTubes;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: %s style, hull number %s, %s"), UTF8_TO_TCHAR(profile->Id.c_str()),
				UTF8_TO_TCHAR(profile->HullNumber(seed).c_str()), UTF8_TO_TCHAR(profile->Palette.Colour.c_str()));
		}

		if (!AssembleAtKardashev(args, cls, style)) return;
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
		int tubes = 0;
		for (const auto& h : geometry.Hangars) tubes += h.Tubes;
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: ports from the parts: %d engine nozzles, %d gun mounts, %d hangar mouths (%d launch tubes), %d bridge"),
			static_cast<int>(geometry.Engines.size()), static_cast<int>(geometry.Guns.size()), static_cast<int>(geometry.Hangars.size()), tubes,
			static_cast<int>(geometry.Bridges.size()));

		int imported = 0;
		std::vector<cs::Ref<GameObject>> bodies;
		auto model = BuildModel(design, catalogue, imported, bodies);
		if (profile) AssemblePaint(design, catalogue, bodies, *profile);
		cs::String name = cs::Cat(ToString(cls), " #", design.Seed);
		if (!ModelViewer::ShowBuilt(model, name))
		{
			Destroy(model);
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Assemble: no model viewer (start the game first)"));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Assemble: shown in the model viewer, %d of %d parts drawn with their own mesh, the rest as boxes"), imported,
			static_cast<int>(design.Parts.size()));
		// The merged mesh draws in the neutral grey, so a styled hull keeps its repainted parts.
		if (profile) return;

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
			"sg.Assemble <class> <seed> [family=<kit set>] [pack=<usc|starsparrow|ssc>] [imported] [faction=<style id>] [tubes=<per hangar>] [lineup] (US-T845: Data/FactionStyles.json)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Assemble(args); }));
}
}
