// US-T452: the vehicle data file for SpaceGod.Studio (docs/Vehicles.md).
//   sg.ExportVehicles [dir] [quit]   writes <dir>/Vehicles.json (default <repo>/Data/Vehicles) and a mesh per vehicle under
//                                    <dir>/Meshes (third-party kit geometry under Meshes/Kit, which git ignores), built by
//                                    the very code that builds the models, then quits with `quit`.
//   sg.VehicleData                   where the game read the file from, what it overrode and anything it left out.
#include "VehicleExport.h"

#include "Rendering/AssetStreamer.h"
#include "Rendering/EnginePorts.h"
#include "Rendering/ImportedGeometry.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/SscKit.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"
#include "UnityCompat/UnityScene.h"
#include "Views/HullThrusters.h"
#include "Views/MechView.h"
#include "SpaceGodLog.h"

#include "Assembly/FactionStyles.h"
#include "Assembly/ShipDesigns.h"
#include "Combat/BulkFreighters.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/Outfitting.h"
#include "Combat/Radiators.h"
#include "Combat/VehicleData.h"
#include "Economy/Industry.h"
#include "Economy/LocalTraffic.h"
#include "Infrastructure/Infrastructure.h"
#include "Ships.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SpaceGod::Core;
using namespace SpaceGod::Rendering;

namespace SpaceGod::VehicleExport
{
	FString RepoDataDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Data"), TEXT("Vehicles")));
	}

	void FindData()
	{
		// US-T869: the faction style book every ShipDesign is styled from, registered once (else every design is neutral).
		{
			const FString styles = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Data"), TEXT("FactionStyles.json")));
			auto book = std::make_shared<const FactionStyleBook>(FactionStyleBook::Load(cs::String(TCHAR_TO_UTF8(*styles))));
			if (!book->Loaded()) UE_LOG(LogSpaceGod, Display, TEXT("FactionStyles: %s, designs are neutral"), UTF8_TO_TCHAR(book->Problem().c_str()));
			ShipDesigns::UseStyles(book);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("NoVehicleData")))
		{
			VehicleData::SetPath(cs::String());
			UE_LOG(LogSpaceGod, Display, TEXT("VehicleData: -NoVehicleData, the built-in tables"));
			return;
		}
		FString file = FPlatformMisc::GetEnvironmentVariable(TEXT("SPACEGOD_VEHICLES"));
		if (!file.IsEmpty()) file = FPaths::ConvertRelativePathToFull(file);
		else
		{
			const FString packaged = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::RootDir(), TEXT("Vehicles"), TEXT("Vehicles.json")));
			file = FPaths::FileExists(packaged) ? packaged : FPaths::Combine(RepoDataDir(), TEXT("Vehicles.json"));
		}
		VehicleData::SetPath(cs::String(TCHAR_TO_UTF8(*file)));
		UE_LOG(LogSpaceGod, Display, TEXT("VehicleData: reading %s"), *file);
	}
}

namespace SpaceGod {
namespace
{

	FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	// ---------------------------------------------------------------- JSON out (stable, readable)

	/// A JSON value to write: objects keep the order fields were added, arrays of scalars go on one line.
	struct Node
	{
		enum class T { Null, Bool, Num, Str, Arr, Obj } Type = T::Null;
		bool B = false;
		std::string S;
		std::vector<Node> A;
		std::vector<std::pair<std::string, Node>> O;

		static Node Null() { return Node(); }
		static Node Bool(bool b) { Node n; n.Type = T::Bool; n.B = b; return n; }
		static Node Str(const std::string& s) { Node n; n.Type = T::Str; n.S = s; return n; }
		static Node Arr() { Node n; n.Type = T::Arr; return n; }
		static Node Obj() { Node n; n.Type = T::Obj; return n; }

		/// The shortest text that reads back to exactly this double.
		static Node Num(double v)
		{
			Node n;
			n.Type = T::Num;
			if (!std::isfinite(v)) v = 0;
			char buf[64];
			auto r = std::to_chars(buf, buf + sizeof(buf), v);
			n.S.assign(buf, r.ptr);
			return n;
		}

		/// The shortest text that reads back (through a double, then to float) to exactly this float.
		static Node Num(float v)
		{
			Node n;
			n.Type = T::Num;
			if (!std::isfinite(v)) v = 0;
			char buf[64];
			auto r = std::to_chars(buf, buf + sizeof(buf), v);
			n.S.assign(buf, r.ptr);
			return n;
		}

		static Node Int(int v) { return Num(static_cast<double>(v)); }

		static Node Vec(double x, double y, double z)
		{
			Node n = Arr();
			n.A = { Num(x), Num(y), Num(z) };
			return n;
		}

		static Node VecF(Vector3 v)
		{
			Node n = Arr();
			n.A = { Num(v.x), Num(v.y), Num(v.z) };
			return n;
		}

		static Node Strings(const std::vector<std::string>& list)
		{
			Node n = Arr();
			for (const auto& s : list) n.A.push_back(Str(s));
			return n;
		}

		Node& Add(const std::string& key, Node v)
		{
			O.emplace_back(key, std::move(v));
			return O.back().second;
		}

		Node& Push(Node v)
		{
			A.push_back(std::move(v));
			return A.back();
		}

		bool Scalar() const { return Type != T::Arr && Type != T::Obj; }
	};

	void Escape(std::string& out, const std::string& s)
	{
		out += '"';
		for (unsigned char c : s)
		{
			switch (c)
			{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20)
					{
						char buf[8];
						snprintf(buf, sizeof(buf), "\\u%04x", c);
						out += buf;
					}
					else out += static_cast<char>(c);
			}
		}
		out += '"';
	}

	void Write(std::string& out, const Node& n, int indent)
	{
		auto pad = [&](int k) { out.append(static_cast<size_t>(k) * 2, ' '); };
		switch (n.Type)
		{
			case Node::T::Null: out += "null"; return;
			case Node::T::Bool: out += n.B ? "true" : "false"; return;
			case Node::T::Num: out += n.S; return;
			case Node::T::Str: Escape(out, n.S); return;
			case Node::T::Arr:
			{
				if (n.A.empty()) { out += "[]"; return; }
				bool inline_ = std::all_of(n.A.begin(), n.A.end(), [](const Node& x) { return x.Scalar(); });
				if (inline_)
				{
					out += '[';
					for (size_t i = 0; i < n.A.size(); i++)
					{
						if (i) out += ", ";
						Write(out, n.A[i], indent);
					}
					out += ']';
					return;
				}
				out += "[\n";
				for (size_t i = 0; i < n.A.size(); i++)
				{
					pad(indent + 1);
					Write(out, n.A[i], indent + 1);
					out += i + 1 < n.A.size() ? ",\n" : "\n";
				}
				pad(indent);
				out += ']';
				return;
			}
			case Node::T::Obj:
			{
				if (n.O.empty()) { out += "{}"; return; }
				out += "{\n";
				for (size_t i = 0; i < n.O.size(); i++)
				{
					pad(indent + 1);
					Escape(out, n.O[i].first);
					out += ": ";
					Write(out, n.O[i].second, indent + 1);
					out += i + 1 < n.O.size() ? ",\n" : "\n";
				}
				pad(indent);
				out += '}';
				return;
			}
		}
	}

	bool SaveBytes(const FString& path, const std::string& bytes)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(path), true);
		TUniquePtr<FArchive> ar(IFileManager::Get().CreateFileWriter(*path));
		if (!ar) return false;
		ar->Serialize(const_cast<char*>(bytes.data()), static_cast<int64>(bytes.size()));
		return ar->Close();
	}

	// ---------------------------------------------------------------- the vehicles

	const ShipClass AllClasses[] = {
		ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Freighter, ShipClass::Miner,
		ShipClass::Courier, ShipClass::Fighter, ShipClass::Explorer, ShipClass::Gunship, ShipClass::OreTug, ShipClass::Interceptor,
		ShipClass::Bomber, ShipClass::LaneTender, ShipClass::GatePicket, ShipClass::CommandCruiser, ShipClass::Frigate, ShipClass::Corvette,
		ShipClass::Recon, ShipClass::AssaultShuttle, ShipClass::GateBreaker, ShipClass::CaptureTug,
	};
	const CivilCraft AllCivil[] = { CivilCraft::Bus, CivilCraft::Taxi, CivilCraft::Skiff, CivilCraft::Runabout, CivilCraft::Tourer };
	const IndustryKind AllIndustries[] = { IndustryKind::Mine, IndustryKind::Farm, IndustryKind::PharmaLab, IndustryKind::Smelter,
		IndustryKind::ElectronicsFab, IndustryKind::MachineWorks, IndustryKind::MunitionsPlant, IndustryKind::Shipyard };
	const char* const StationBodies[] = { "Rigil Gate", "Toliman Forge", "Blackhollow" };

	std::string ClassLayoutId(ShipClass c, const cs::Ref<CombatSpec>& design) { return cs::Cat("ship/", ToString(c), "/", design->DesignName); }
	std::string CivilLayoutId(CivilCraft c) { return cs::Cat("civil/", ToString(c)); }

	cs::Ref<Faction> MakeFaction(const char* name, const char* prefix, Rgb color, bool military, bool pirate)
	{
		auto f = cs::New<Faction>(name, prefix, color);
		f->IsMilitary = military;
		f->IsPirate = pirate;
		return f;
	}

	cs::Ref<Ship> MakeShip(ShipClass c, CivilCraft civil, const cs::Ref<Faction>& faction, int id)
	{
		auto ship = cs::New<Ship>();
		ship->Id = id;
		ship->Name = cs::Cat(ToString(c), id);
		ship->Spec = ShipClassSpec::Of(c);
		ship->Faction = faction;
		ship->Civil = civil;
		ship->Combat = cs::New<ShipCombat>(CombatSpec::Of(c), cs::Nullable<ShipClass>(c));
		return ship;
	}

	using Builder = std::function<bool(const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& renderers)>;

	struct PortOut
	{
		Vector3 Pos, Exhaust;
		float Radius = 0;
	};

	struct JetOut
	{
		int Kind = 2;
		Vector3 Pos, Exhaust;
		float Width = 0;
	};

	struct Vehicle
	{
		std::string Id, Name, Kind, Source, Note;
		std::set<std::string> Classes, Civil, Structures;
		bool Navy = false, Pirate = false;
		std::vector<std::string> Layouts;
		bool Committed = true, PortsEditable = false, Thrusters = false;
		std::vector<PortOut> FixedPorts; // the editable ports (usc), as the game uses them
		const cs::List<cs::Ref<SubsystemDef>>* EngineLayout = nullptr;
		Builder Build;

		// Results.
		std::string Mesh;
		std::vector<std::string> Sources, Sections;
		bool HasBounds = false;
		Vector3 Min, Max;
		std::vector<PortOut> Ports;
		std::vector<JetOut> Jets;
		double LengthMeters = -1;

		void AddLayout(const std::string& id)
		{
			if (std::find(Layouts.begin(), Layouts.end(), id) == Layouts.end()) Layouts.push_back(id);
		}

		void AddClass(ShipClass c)
		{
			Classes.insert(ToString(c));
			for (const auto& d : CombatSpec::DesignsOf(c)) AddLayout(ClassLayoutId(c, d));
		}

		void AddCivil(CivilCraft c)
		{
			Civil.insert(ToString(c));
			AddLayout(CivilLayoutId(c));
		}
	};

	std::string FileStem(const std::string& id)
	{
		std::string s = id;
		for (char& c : s)
			if (c == ':' || c == '/' || c == ' ' || c == '\\') c = '_';
		return s;
	}

	/// A point of a node's frame in <paramref name="root"/>'s.
	Vector3 ToRoot(const Transform* t, const Transform* root, Vector3 p)
	{
		while (t != nullptr && t != root)
		{
			p = t->localPosition + t->localRotation * Vector3::Scale(t->localScale, p);
			auto up = t->parent.lock();
			t = up.get();
		}
		return p;
	}

	bool Under(const Transform* t, const Transform* root, const char* prefix, std::string* topName)
	{
		bool hit = false;
		while (t != nullptr && t != root)
		{
			auto go = t->gameObject();
			if (go && go->name.rfind(prefix, 0) == 0) hit = true;
			auto up = t->parent.lock();
			if (up.get() == root && topName && go) *topName = go->name;
			t = up.get();
		}
		return hit;
	}

	/// Every source file in Cpp/SourceArt, by lower-case name without extension.
	const std::unordered_map<std::string, std::string>& SourceArt()
	{
		static const std::unordered_map<std::string, std::string> index = []
		{
			std::unordered_map<std::string, std::string> m;
			FString repo = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
			FString art = FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceArt"));
			TArray<FString> files;
			IFileManager::Get().FindFilesRecursive(files, *art, TEXT("*.fbx"), true, false);
			files.Sort();
			for (FString f : files)
			{
				f = FPaths::ConvertRelativePathToFull(f);
				FString rel = f;
				FPaths::MakePathRelativeTo(rel, *(repo + TEXT("/")));
				rel.ReplaceInline(TEXT("\\"), TEXT("/"));
				std::string key = TCHAR_TO_UTF8(*FPaths::GetBaseFilename(f).ToLower());
				m.emplace(key, std::string(TCHAR_TO_UTF8(*rel)));
			}
			return m;
		}();
		return index;
	}

	/// The repo file an imported part comes from: its FBX in Cpp/SourceArt when there is one, else its .uasset.
	std::string SourceOf(const cs::String& asset)
	{
		std::string path = asset;
		auto dot = path.rfind('.');
		std::string package = dot == std::string::npos ? path : path.substr(0, dot);
		std::string leaf = package.substr(package.rfind('/') + 1);
		std::string lower = leaf;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const auto& art = SourceArt();
		auto it = art.find(lower);
		if (it != art.end()) return it->second;
		if (package.rfind("/Game/", 0) == 0) return "Cpp/Content/" + package.substr(6) + ".uasset";
		if (package.rfind("/SpaceGodRender/", 0) == 0) return "Shared/Plugins/SpaceGodRender/Content/" + package.substr(16) + ".uasset";
		return package;
	}

	/// Writes the hull drawn under <paramref name="root"/> as an OBJ in its frame: every mesh but the parts bolted on hardpoints
	/// (USC's Module:Engine bells), one object per section (the root's children), shared corners welded.
	bool BakeObj(Vehicle& v, const cs::Ref<Transform>& root, const FString& file)
	{
		std::string out = cs::Cat("# ", v.Id, ": ", v.Name, "\n# Written by sg.ExportVehicles (SpaceGod, US-T452; docs/Vehicles.md). The game's model frame: hull lengths,\n",
			"# +X starboard, +Y up, +Z nose, left-handed; triangles in the game's order. Do not edit: re-export.\n");
		std::unordered_map<std::string, int> welded;
		std::string faces;
		int count = 0, tris = 0;
		std::string current;
		std::set<std::string> sources, sections;
		bool first = true;
		Vector3 mn, mx;
		auto vertex = [&](Vector3 p) -> int
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "%.6g %.6g %.6g", p.x, p.y, p.z);
			auto it = welded.find(buf);
			if (it != welded.end()) return it->second;
			out += "v ";
			out += buf;
			out += '\n';
			welded.emplace(buf, ++count);
			mn = first ? p : Vector3::Min(mn, p);
			mx = first ? p : Vector3::Max(mx, p);
			first = false;
			return count;
		};
		for (const auto& filter : GetComponentsInChildren<MeshFilter>(root, true))
		{
			if (!filter || !filter->sharedMesh) continue;
			const Transform* t = filter->transform().get();
			std::string section;
			if (Under(t, root.get(), "Module:Engine", &section)) continue;
			if (section.empty()) section = "Hull";
			const auto& mesh = filter->sharedMesh;
			std::vector<Vector3> verts;
			std::vector<int> idx;
			if (!mesh->ImportedAsset.empty())
			{
				const auto* g = ImportedGeometry::Of(mesh);
				if (!g) continue;
				verts = g->Vertices;
				idx = g->Triangles;
				sources.insert(SourceOf(mesh->ImportedAsset));
			}
			else
			{
				verts = mesh->Vertices;
				for (const auto& sub : mesh->SubMeshes)
				{
					if (sub.Topology == MeshTopology::Triangles) idx.insert(idx.end(), sub.Indices.begin(), sub.Indices.end());
					else if (sub.Topology == MeshTopology::Quads)
						for (size_t q = 0; q + 3 < sub.Indices.size(); q += 4)
							for (int k : { 0, 1, 2, 0, 2, 3 }) idx.push_back(sub.Indices[q + k]);
				}
			}
			if (verts.empty() || idx.size() < 3) continue;
			if (section != current)
			{
				faces += "o " + section + "\n";
				current = section;
				sections.insert(section);
			}
			std::vector<int> at(verts.size(), 0);
			for (size_t i = 0; i < verts.size(); i++) at[i] = vertex(ToRoot(t, root.get(), verts[i]));
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				int a = idx[i], b = idx[i + 1], c = idx[i + 2];
				if (a < 0 || b < 0 || c < 0 || a >= static_cast<int>(at.size()) || b >= static_cast<int>(at.size()) || c >= static_cast<int>(at.size())) continue;
				if (at[a] == at[b] || at[b] == at[c] || at[a] == at[c]) continue;
				faces += cs::Cat("f ", at[a], " ", at[b], " ", at[c], "\n");
				tris++;
			}
		}
		if (tris == 0) return false;
		out += faces;
		if (!SaveBytes(file, out)) return false;
		v.Sources.assign(sources.begin(), sources.end());
		v.Sections.assign(sections.begin(), sections.end());
		v.HasBounds = true;
		v.Min = mn;
		v.Max = mx;
		return true;
	}

	/// A procedural section (a mesh of ShipMeshFactory's) under the root, its nozzles marked as ShipModels marks them.
	cs::Ref<Transform> Section(const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& renderers, const cs::Ref<Mesh>& mesh, const char* name)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(root, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		renderers.Add(mr);
		for (const auto& n : ShipMeshFactory::NozzlesOf(mesh)) EnginePorts::Mark(go->transform, n.Position, n.Exhaust, n.Radius);
		return go->transform;
	}

	/// Every vehicle the game can draw, from its own model tables.
	std::vector<Vehicle> Vehicles()
	{
		std::vector<Vehicle> all;
		auto grey = MakeFaction("Export", "EX", Rgb(0.6f, 0.6f, 0.62f), true, false);
		auto pirate = MakeFaction("Export Pirates", "XP", Rgb(0.85f, 0.2f, 0.25f), false, true);
		auto find = [&all](const std::string& id) -> Vehicle*
		{
			for (auto& v : all)
				if (v.Id == id) return &v;
			return nullptr;
		};

		// Ultimate Spaceships Creator: the model each class (navy, pirate) and each local-traffic craft flies.
		{
			struct Use { ShipClass Cls; CivilCraft Civil; int Id; bool Pirate; };
			std::map<std::string, std::vector<Use>> uses;
			for (ShipClass c : AllClasses)
				for (int id = 0; id < 2; id++)
					for (bool p : { false, true })
						uses[UscKit::ModelOf(c, CivilCraft::None, id, p ? pirate : grey)].push_back({ c, CivilCraft::None, id, p });
			for (CivilCraft c : AllCivil) uses[UscKit::ModelOf(ShipClass::Courier, c, 0, grey)].push_back({ ShipClass::Courier, c, 0, false });
			for (const auto& model : UscKit::ModelNames())
			{
				Vehicle v;
				v.Id = UscKit::VehicleId(model);
				v.Name = model;
				v.Source = "usc";
				v.Committed = false;
				v.PortsEditable = true;
				v.Thrusters = true;
				for (const auto& p : UscKit::PortsOf(model)) v.FixedPorts.push_back({ p.Position, p.Exhaust, p.Radius });
				auto it = uses.find(model);
				bool onlyCivil = true;
				if (it != uses.end())
					for (const auto& u : it->second)
					{
						if (u.Civil != CivilCraft::None) v.AddCivil(u.Civil);
						else
						{
							v.AddClass(u.Cls);
							onlyCivil = false;
						}
						(u.Pirate ? v.Pirate : v.Navy) = true;
					}
				v.Kind = onlyCivil && !v.Civil.empty() ? "craft" : "ship";
				if (it == uses.end()) v.Note = "listed by the kit but flown by no class";
				else
				{
					Use u = it->second.front();
					auto faction = u.Pirate ? pirate : grey;
					v.EngineLayout = u.Civil == CivilCraft::None ? StarSparrowKit::LayoutOf(u.Cls, nullptr) : nullptr;
					auto layout = v.EngineLayout;
					v.Build = [u, faction, layout](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
					{
						cs::Ref<Transform> stern, bow;
						return UscKit::Build(u.Cls, u.Civil, u.Id, faction, layout, root, r, stern, bow, 8, 2);
					};
					if (!UscKit::Available()) v.Note = "the Ultimate Spaceships Creator pack is not imported where this was exported: no mesh";
				}
				all.push_back(std::move(v));
			}
		}

		// The Star Sparrow kit: each class's recipe, drawn when the USC pack is not there.
		for (ShipClass c : AllClasses)
		{
			Vehicle v;
			v.Id = cs::Cat("starsparrow:", ToString(c));
			v.Name = cs::Cat("Star Sparrow ", ToString(c));
			v.Kind = "ship";
			v.Source = "starsparrow";
			v.Committed = false;
			v.Thrusters = true;
			v.Navy = v.Pirate = true;
			v.AddClass(c);
			v.Note = "drawn when the Ultimate Spaceships Creator pack is not available";
			v.EngineLayout = StarSparrowKit::LayoutOf(c, nullptr);
			auto layout = v.EngineLayout;
			v.Build = [c, grey, layout](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
			{
				cs::Ref<Transform> stern, bow;
				return StarSparrowKit::Build(c, CivilCraft::None, 0, grey, layout, root, r, stern, bow, 8, 2);
			};
			all.push_back(std::move(v));
		}
		for (CivilCraft c : AllCivil)
		{
			Vehicle v;
			v.Id = cs::Cat("starsparrow:civil/", ToString(c));
			v.Name = cs::Cat("Star Sparrow ", ToString(c));
			v.Kind = "craft";
			v.Source = "starsparrow";
			v.Committed = false;
			v.Thrusters = true;
			v.Navy = v.Pirate = true;
			v.AddCivil(c);
			v.Note = "drawn when the Ultimate Spaceships Creator pack is not available";
			v.Build = [c, grey](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
			{
				cs::Ref<Transform> stern, bow;
				return StarSparrowKit::Build(ShipClass::Courier, c, 0, grey, nullptr, root, r, stern, bow);
			};
			all.push_back(std::move(v));
		}

		// The Kenney craft (CC0), drawn without either kit.
		{
			std::map<std::string, Vehicle> kenney;
			auto use = [&kenney](const std::string& model) -> Vehicle&
			{
				auto& v = kenney[model];
				if (v.Id.empty())
				{
					v.Id = "kenney:" + model;
					v.Name = model;
					v.Kind = "craft";
					v.Source = "kenney";
					v.Thrusters = true;
					v.Navy = v.Pirate = true;
					v.Note = "drawn when neither ship kit is available";
					v.Build = [model](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
					{
						auto source = AssetStreamer::Loaded<GameObject>(AssetStreamer::ShipPath(model));
						if (!Alive(source)) return false;
						auto instance = Instantiate(source, root, false);
						instance->name = model;
						instance->SetActive(true);
						for (auto& renderer : GetComponentsInChildren<Renderer>(instance, true)) r.Add(renderer);
						return r.Count() > 0;
					};
				}
				return v;
			};
			for (ShipClass c : AllClasses)
				for (int id = 0; id < 2; id++)
				{
					std::string m = ShipModels::KenneyModelOf(c, CivilCraft::None, id);
					if (m.empty()) continue;
					auto& v = use(m);
					v.AddClass(c);
					v.Kind = "ship";
				}
			for (CivilCraft c : AllCivil)
			{
				std::string m = ShipModels::KenneyModelOf(ShipClass::Courier, c, 0);
				if (!m.empty()) use(m).AddCivil(c);
			}
			for (auto& [name, v] : kenney) all.push_back(std::move(v));
		}

		// The procedural hulls (ShipMeshFactory), drawn without the kits and Kenney's craft.
		for (ShipClass c : AllClasses)
		{
			Vehicle v;
			v.Id = cs::Cat("procedural:", ToString(c));
			v.Name = cs::Cat("Procedural ", ToString(c));
			v.Kind = "ship";
			v.Source = "procedural";
			v.Thrusters = true;
			v.Navy = v.Pirate = true;
			v.AddClass(c);
			v.Note = "drawn when no kit (nor a Kenney craft) draws the class";
			v.EngineLayout = &CombatSpec::Of(c)->Layout;
			v.Build = [c, grey](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
			{
				if (BulkFreighters::IsTruss(c))
				{
					Section(root, r, ShipModels::TrussMesh(MakeShip(c, CivilCraft::None, grey, 0)), ShipModels::TrussSection);
					return true;
				}
				if (ShipMeshFactory::IsWarship(c))
				{
					Section(root, r, ShipMeshFactory::GetSection(c, grey, ShipMeshFactory::Stern), "Stern");
					Section(root, r, ShipMeshFactory::GetSection(c, grey, ShipMeshFactory::Bow), "Bow");
					return true;
				}
				Section(root, r, ShipMeshFactory::GetSection(c, grey, ShipMeshFactory::WholeHull), "Hull");
				return true;
			};
			all.push_back(std::move(v));
		}

		// The orbital structures and stations, procedural (drawn without Space Station Creator, or for what it does not draw).
		auto structure = [&all, grey](const std::string& id, const std::string& name, const std::string& kind, std::vector<std::string> roles,
			std::function<cs::Ref<Mesh>()> mesh, const std::string& note)
		{
			Vehicle v;
			v.Id = "procedural:" + id;
			v.Name = name;
			v.Kind = kind;
			v.Source = "procedural";
			v.Navy = v.Pirate = true;
			for (auto& r : roles) v.Structures.insert(r);
			v.Note = note;
			v.Build = [mesh](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
			{
				Section(root, r, mesh(), "Hull");
				return true;
			};
			all.push_back(std::move(v));
			return &all.back();
		};
		structure("structure/Shipyard", "Shipyard", "structure", { "Shipyard" }, [grey] { return ShipMeshFactory::GetShipyard(grey); },
			"one unit long; the hull being built sits in its cradle");
		for (IndustryKind k : AllIndustries)
		{
			std::vector<std::string> roles{ cs::Cat("Factory (", ToString(k), ")") };
			if (k == IndustryKind::Farm) roles.push_back("Depot");
			structure(cs::Cat("structure/Factory/", ToString(k)), cs::Cat("Factory: ", ToString(k)), "structure", roles,
				[grey, k] { return ShipMeshFactory::GetFactory(grey, k); }, "drawn without Space Station Creator");
		}
		structure("structure/Habitat", "Habitat ring", "structure", { "Habitat", "Gateway" }, [grey] { return ShipMeshFactory::GetHabitat(grey); },
			"drawn without Space Station Creator");
		structure("structure/ISS", "ISS", "structure", { "CrewedStation (ISS)" }, [] { return ShipMeshFactory::GetCrewedStation(false); }, "one unit across");
		structure("structure/Tiangong", "Tiangong", "structure", { "CrewedStation (Tiangong)" }, [] { return ShipMeshFactory::GetCrewedStation(true); },
			"one unit across");
		structure("structure/Hubble", "Hubble", "structure", { "Telescope (Hubble)" }, [] { return ShipMeshFactory::GetSpacecraft("Hubble"); }, "");
		structure("structure/JWST", "JWST", "structure", { "Telescope (JWST)" }, [] { return ShipMeshFactory::GetSpacecraft("JWST"); }, "");
		structure("structure/Probe", "Probe", "structure", { "Probe", "Relay" }, [] { return ShipMeshFactory::GetSpacecraft("Probe"); }, "");
		structure("structure/DockingRing", "Docking ring", "structure", { "Docking ring" }, [grey] { return ShipMeshFactory::GetDockingRing(grey); },
			"one unit across, axis +Z");
		for (const char* name : StationBodies)
		{
			std::string n = name;
			structure(cs::Cat("station/", n), n, "station", { cs::Cat("Station body (", n, ")") }, [n] { return ShipMeshFactory::GetStationBody(n); },
				"radius one; drawn without Space Station Creator");
		}
		if (auto* platform = structure("platform", "Defence platform", "platform", { "Defence platform" }, [grey] { return ShipMeshFactory::GetPlatform(grey); },
				"its guns are parts on its layout's hardpoints"))
			platform->AddLayout("station/defence-platform");

		// Space Station Creator: each station model, with every role it plays.
		{
			std::map<std::string, std::set<std::string>> roles;
			for (IndustryKind k : AllIndustries) roles[SscKit::ForStructure(StructureKind::Factory, k, "")].insert(cs::Cat("Factory (", ToString(k), ")"));
			for (StructureKind k : { StructureKind::Shipyard, StructureKind::Depot, StructureKind::Habitat, StructureKind::Gateway })
				roles[SscKit::ForStructure(k, IndustryKind::Mine, "")].insert(ToString(k));
			for (const char* name : StationBodies) roles[SscKit::ForStationBody(name)].insert(cs::Cat("Station body (", name, ")"));
			roles["Combined20"].insert("Void Station");
			roles[SscKit::ForPlatform()].insert("Defence platform");
			roles["Combined28"].insert("Megastructure");
			for (const char* wreck : { "Combined16", "Combined24", "Combined10" }) roles[wreck].insert("Derelict");
			for (const auto& model : SscKit::ModelNames())
			{
				Vehicle v;
				v.Id = cs::Cat("ssc:", model);
				v.Name = model;
				v.Source = "ssc";
				v.Committed = false;
				v.Navy = v.Pirate = true;
				v.Structures = roles[model];
				bool structureRole = false;
				for (const auto& r : v.Structures)
					if (r.rfind("Factory", 0) == 0 || r == "Depot" || r == "Habitat" || r == "Gateway") structureRole = true;
				v.Kind = structureRole ? "structure" : "station";
				v.Note = "longest side one unit (the game scales it to the role)";
				if (!SscKit::Available()) v.Note = "the Space Station Creator pack is not imported where this was exported: no mesh";
				v.Build = [model](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
				{
					return SscKit::Station(root, model, 1.0f, nullptr, r);
				};
				all.push_back(std::move(v));
			}
		}

		// The walking mech (Quaternius, CC0).
		{
			Vehicle v;
			v.Id = "mech:Mech";
			v.Name = "Mech";
			v.Kind = "mech";
			v.Source = "mech";
			v.Navy = v.Pirate = true;
			v.Structures.insert("Mech");
			v.Note = "standing at rest; one unit is the mech's height, the origin on the ground under its hips";
			v.Build = [](const cs::Ref<Transform>& root, cs::List<cs::Ref<Renderer>>& r)
			{
				Views::MechView::RestPose(root, r);
				return true;
			};
			all.push_back(std::move(v));
		}
		(void)find;
		std::sort(all.begin(), all.end(), [](const Vehicle& a, const Vehicle& b) { return a.Id < b.Id; });
		return all;
	}

	double LayoutLengthMeters(const std::string& layoutId)
	{
		if (layoutId.rfind("civil/", 0) == 0)
		{
			for (CivilCraft c : AllCivil)
				if (CivilLayoutId(c) == layoutId) return LocalTraffic::DesignOf(c)->LengthKm * 1000;
			return -1;
		}
		if (auto f = HullFrames::Of(layoutId)) return f->Design->LengthKm * 1000;
		return -1;
	}

	/// One design's hardpoints as the file has them.
	Node LayoutNode(const std::string& id, const cs::Nullable<ShipClass>& cls, const cs::Ref<CombatSpec>& design, const cs::Ref<HullFrame>& frame,
		const std::vector<std::string>& vehicles, int& hardpoints)
	{
		Node l = Node::Obj();
		l.Add("class", cls.has_value() ? Node::Str(ToString(*cls)) : Node::Null());
		l.Add("design", Node::Str(design->DesignName));
		l.Add("lengthMeters", Node::Num(design->LengthKm * 1000));
		l.Add("vehicles", Node::Strings(vehicles));
		std::unordered_map<int, cs::Ref<Hardpoint>> slots;
		if (frame)
			for (const auto& h : frame->Hardpoints()) slots.emplace(h->LayoutIndex, h);
		const cs::List<cs::Ref<SubsystemDef>>* panels = cls.has_value() ? &Radiators::PanelsFor(*cls) : nullptr;
		Node& list = l.Add("hardpoints", Node::Arr());
		for (int i = 0; i < design->Layout.Count(); i++)
		{
			const auto& d = design->Layout[i];
			if (!d) continue;
			Node h = Node::Obj();
			h.Add("id", Node::Str(cs::Cat("hp", i)));
			h.Add("index", Node::Int(i));
			h.Add("kind", Node::Str(ToString(d->Kind)));
			h.Add("name", Node::Str(SubsystemDef::Name(d->Kind)));
			h.Add("pos", Node::Vec(d->X, d->Y, d->Z));
			h.Add("facesAft", Node::Bool(d->FacesAft));
			h.Add("coneAxis", d->ConeAxis.has_value() ? Node::Vec(d->ConeAxis->X, d->ConeAxis->Y, d->ConeAxis->Z) : Node::Null());
			h.Add("coneHalfDeg", d->ConeHalfDeg.has_value() ? Node::Num(*d->ConeHalfDeg) : Node::Null());
			h.Add("buried", Node::Bool(d->Buried));
			h.Add("spinal", Node::Bool(d->Spinal));
			h.Add("derived", Node::Bool(panels && panels->Contains(d)));
			auto slot = slots.find(i);
			if (slot != slots.end())
			{
				const auto& hp = slot->second;
				h.Add("socket", Node::Str(hp->Socket));
				h.Add("mount", Node::Str(ToString(hp->Type)));
				h.Add("size", Node::Str(ToString(hp->Size)));
				h.Add("artKey", Node::Str(Hardpoint::ArtKey(hp, hp->Stock)));
			}
			else
			{
				h.Add("socket", Node::Null());
				h.Add("mount", Node::Null());
				h.Add("size", Node::Null());
				h.Add("artKey", Node::Null());
			}
			list.Push(std::move(h));
			hardpoints++;
		}
		return l;
	}

	struct Counts
	{
		int Vehicles = 0, Meshes = 0, KitMeshes = 0, NoMesh = 0, Ports = 0, Jets = 0, Layouts = 0, Hardpoints = 0;
	};

	/// Builds, bakes and writes everything; false (with the reason logged) when the file cannot be written.
	bool Export(const FString& dir, Counts& n)
	{
		auto vehicles = Vehicles();
		FString meshDir = FPaths::Combine(dir, TEXT("Meshes"));
		FString repo = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
		auto relative = [&repo](const FString& full)
		{
			FString rel = FPaths::ConvertRelativePathToFull(full);
			if (!FPaths::MakePathRelativeTo(rel, *(repo + TEXT("/")))) rel = full;
			rel.ReplaceInline(TEXT("\\"), TEXT("/"));
			return std::string(TCHAR_TO_UTF8(*rel));
		};

		auto holder = NewGameObject("VehicleExport");
		holder->SetActive(false);
		for (auto& v : vehicles)
		{
			n.Vehicles++;
			auto root = NewGameObject(v.Id);
			root->transform->SetParent(holder->transform, false);
			cs::List<cs::Ref<Renderer>> renderers;
			bool built = false;
			try
			{
				built = v.Build && v.Build(root->transform, renderers);
			}
			catch (const std::exception& e)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.ExportVehicles: %s would not build: %s"), *U(v.Id), UTF8_TO_TCHAR(e.what()));
			}
			if (built)
			{
				FString file = v.Committed ? FPaths::Combine(meshDir, U(FileStem(v.Id)) + TEXT(".obj"))
				                           : FPaths::Combine(meshDir, TEXT("Kit"), U(FileStem(v.Id)) + TEXT(".obj"));
				if (BakeObj(v, root->transform, file))
				{
					v.Mesh = relative(file);
					(v.Committed ? n.Meshes : n.KitMeshes)++;
				}
				// The nozzles the model marks (a kit's or a procedural hull's own; not the bells on Engine hardpoints).
				for (const auto& p : EnginePorts::Of(root->transform))
					if (p.Engine < 0) v.Ports.push_back({ p.Position, p.Exhaust, p.Radius });
				if (v.Thrusters)
				{
					cs::List<Vector3> engines;
					if (v.EngineLayout)
						for (const auto& d : *v.EngineLayout)
							if (d && d->Kind == SubsystemKind::Engine) engines.Add(Vector3(static_cast<float>(d->X), static_cast<float>(d->Y), static_cast<float>(d->Z)));
					Views::HullThrusters thrusters(root->transform, renderers, engines, EnginePorts::Of(root->transform));
					cs::List<Views::HullThrusters::JetView> mains, others;
					thrusters.Jets(mains, others);
					for (const auto& j : mains) v.Jets.push_back({ 0, j.At, j.Exhaust, j.Width });
					for (const auto& j : others) v.Jets.push_back({ j.Kind, j.At, j.Exhaust, j.Width });
				}
			}
			if (v.Mesh.empty()) n.NoMesh++;
			if (v.PortsEditable) v.Ports = v.FixedPorts;
			n.Ports += static_cast<int>(v.Ports.size());
			n.Jets += static_cast<int>(v.Jets.size());
			Destroy(root);
		}
		Destroy(holder);

		// The layouts, and which vehicles draw each.
		std::map<std::string, std::vector<std::string>> drawnOn;
		for (const auto& v : vehicles)
			for (const auto& l : v.Layouts) drawnOn[l].push_back(v.Id);
		std::map<std::string, Node> layouts;
		for (const auto& f : HullFrames::Catalogue())
			layouts.emplace(f->Id, LayoutNode(f->Id, f->Class, f->Design, f, drawnOn[f->Id], n.Hardpoints));
		for (CivilCraft c : AllCivil)
		{
			std::string id = CivilLayoutId(c);
			layouts.emplace(id, LayoutNode(id, std::nullopt, LocalTraffic::DesignOf(c), nullptr, drawnOn[id], n.Hardpoints));
		}
		n.Layouts = static_cast<int>(layouts.size());

		Node root = Node::Obj();
		root.Add("version", Node::Int(1));
		root.Add("generator", Node::Str("sg.ExportVehicles"));
		Node& frame = root.Add("frame", Node::Obj());
		frame.Add("units", Node::Str("hullLengths"));
		frame.Add("right", Node::Vec(1, 0, 0));
		frame.Add("up", Node::Vec(0, 1, 0));
		frame.Add("forward", Node::Vec(0, 0, 1));
		frame.Add("handedness", Node::Str("left"));
		Node& list = root.Add("vehicles", Node::Arr());
		for (auto& v : vehicles)
		{
			Node o = Node::Obj();
			o.Add("id", Node::Str(v.Id));
			o.Add("name", Node::Str(v.Name));
			o.Add("kind", Node::Str(v.Kind));
			o.Add("source", Node::Str(v.Source));
			o.Add("classes", Node::Strings(std::vector<std::string>(v.Classes.begin(), v.Classes.end())));
			o.Add("civil", Node::Strings(std::vector<std::string>(v.Civil.begin(), v.Civil.end())));
			o.Add("structures", Node::Strings(std::vector<std::string>(v.Structures.begin(), v.Structures.end())));
			o.Add("factions", Node::Str(v.Navy && v.Pirate ? "any" : v.Pirate ? "pirate" : v.Navy ? "navy" : "any"));
			o.Add("layouts", Node::Strings(v.Layouts));
			double length = v.Layouts.empty() ? -1 : LayoutLengthMeters(v.Layouts.front());
			o.Add("lengthMeters", length > 0 ? Node::Num(length) : Node::Null());
			o.Add("mesh", v.Mesh.empty() ? Node::Null() : Node::Str(v.Mesh));
			o.Add("meshCommitted", Node::Bool(v.Committed));
			o.Add("sourceMeshes", Node::Strings(v.Sources));
			Node& transform = o.Add("meshTransform", Node::Obj());
			transform.Add("space", Node::Str("model"));
			Node& m = transform.Add("matrix", Node::Arr());
			for (int i = 0; i < 16; i++) m.Push(Node::Int(i % 5 == 0 ? 1 : 0));
			if (v.HasBounds)
			{
				Node& b = o.Add("bounds", Node::Obj());
				b.Add("min", Node::VecF(v.Min));
				b.Add("max", Node::VecF(v.Max));
			}
			else o.Add("bounds", Node::Null());
			o.Add("sections", Node::Strings(v.Sections));
			o.Add("enginePortsEditable", Node::Bool(v.PortsEditable));
			Node& ports = o.Add("enginePorts", Node::Arr());
			for (size_t i = 0; i < v.Ports.size(); i++)
			{
				Node p = Node::Obj();
				p.Add("id", Node::Str(cs::Cat("port", static_cast<int>(i))));
				p.Add("pos", Node::VecF(v.Ports[i].Pos));
				p.Add("exhaust", Node::VecF(v.Ports[i].Exhaust));
				p.Add("radius", Node::Num(v.Ports[i].Radius));
				p.Add("engine", Node::Int(-1));
				ports.Push(std::move(p));
			}
			Node& jets = o.Add("rcs", Node::Arr());
			for (size_t i = 0; i < v.Jets.size(); i++)
			{
				static const char* const kinds[] = { "Main", "Retro", "Rcs" };
				Node j = Node::Obj();
				j.Add("id", Node::Str(cs::Cat("rcs", static_cast<int>(i))));
				j.Add("kind", Node::Str(kinds[std::clamp(v.Jets[i].Kind, 0, 2)]));
				j.Add("pos", Node::VecF(v.Jets[i].Pos));
				j.Add("exhaust", Node::VecF(v.Jets[i].Exhaust));
				j.Add("width", Node::Num(v.Jets[i].Width));
				jets.Push(std::move(j));
			}
			o.Add("note", Node::Str(v.Note));
			list.Push(std::move(o));
		}
		Node& layoutsNode = root.Add("classLayouts", Node::Obj());
		for (auto& [id, node] : layouts) layoutsNode.Add(id, std::move(node));

		std::string text;
		Write(text, root, 0);
		text += '\n';
		FString file = FPaths::Combine(dir, TEXT("Vehicles.json"));
		if (!SaveBytes(file, text))
		{
			UE_LOG(LogSpaceGod, Error, TEXT("sg.ExportVehicles: could not write %s"), *file);
			return false;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.ExportVehicles: wrote %s (%d bytes)"), *file, static_cast<int>(text.size()));
		return true;
	}

	/// Waits for the scene (and the Kenney craft, streamed in the background) before exporting.
	struct ExportRun
	{
		FString Dir;
		bool Quit = false;
		double Started = 0;
		bool Requested = false;

		bool Tick()
		{
			double now = FPlatformTime::Seconds();
			if (!UnityCompat::UnityScene::Host())
			{
				if (now - Started < 120) return true;
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.ExportVehicles: no scene came up; exporting anyway"));
			}
			cs::List<cs::String> kenney;
			for (ShipClass c : AllClasses)
				for (int id = 0; id < 2; id++)
					if (auto m = ShipModels::KenneyModelOf(c, CivilCraft::None, id); !m.empty() && !kenney.Contains(m)) kenney.Add(m);
			for (CivilCraft c : AllCivil)
				if (auto m = ShipModels::KenneyModelOf(ShipClass::Courier, c, 0); !m.empty() && !kenney.Contains(m)) kenney.Add(m);
			if (!Requested)
			{
				Requested = true;
				for (const auto& m : kenney) AssetStreamer::Warm(AssetStreamer::ShipPath(m));
				return true;
			}
			bool ready = true;
			for (const auto& m : kenney) ready = ready && AssetStreamer::Ready(AssetStreamer::ShipPath(m));
			if (!ready && now - Started < 60) return true;
			Counts n;
			bool ok = false;
			try
			{
				ok = Export(Dir, n);
			}
			catch (const std::exception& e)
			{
				UE_LOG(LogSpaceGod, Error, TEXT("sg.ExportVehicles: failed: %s"), UTF8_TO_TCHAR(e.what()));
			}
			const auto& data = VehicleData::Get();
			UE_LOG(LogSpaceGod, Display,
				TEXT("sg.ExportVehicles: %s: %d vehicles (%d meshes committed, %d kit meshes, %d without a mesh), %d engine ports, %d thruster jets, %d layouts with %d hardpoints; the game is using %s"),
				ok ? TEXT("done") : TEXT("FAILED"), n.Vehicles, n.Meshes, n.KitMeshes, n.NoMesh, n.Ports, n.Jets, n.Layouts, n.Hardpoints,
				data.Loaded() ? *U(data.Path()) : *U(data.Problem()));
			if (Quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void ExportCommand(const TArray<FString>& args)
	{
		auto run = std::make_shared<ExportRun>();
		run->Dir = SpaceGod::VehicleExport::RepoDataDir();
		for (const auto& a : args)
		{
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->Quit = true;
			else run->Dir = FPaths::ConvertRelativePathToFull(a);
		}
		run->Started = FPlatformTime::Seconds();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.ExportVehicles: exporting to %s"), *run->Dir);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float) { return run->Tick(); }));
	}

	FAutoConsoleCommand GExportVehicles(
		TEXT("sg.ExportVehicles"),
		TEXT("US-T452: sg.ExportVehicles [dir] [quit] writes <dir>/Vehicles.json (default <repo>/Data/Vehicles) and every vehicle's mesh under "
			"<dir>/Meshes, for SpaceGod.Studio (docs/Vehicles.md)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { ExportCommand(args); }));

	void StatusCommand(const TArray<FString>&)
	{
		const auto& d = VehicleData::Get();
		if (!d.Loaded())
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData: %s"), *U(d.Problem()));
			return;
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData: %s: %d vehicles with engine ports, %d layouts, %d hardpoints placed so far, %d entries left out"),
			*U(d.Path()), d.VehicleCount(), d.LayoutCount(), d.Applied(), d.Ignored().Count());
		for (const auto& i : d.Ignored()) UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData:   left out %s"), *U(i));
		if (const auto* p = d.PortsOf("usc:GalacticLeopard31"); p && !p->empty())
			UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData: usc:GalacticLeopard31 port0 at (%.6g, %.6g, %.6g) radius %.6g"), (*p)[0].Pos[0], (*p)[0].Pos[1],
				(*p)[0].Pos[2], (*p)[0].Radius);
		// What the model marks (UscKit::PortsOf is what UscKit::Build puts the flames on).
		auto used = UscKit::PortsOf("GalacticLeopard31");
		if (!used.empty())
			UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData: GalacticLeopard31 draws port0 at (%.6g, %.6g, %.6g) exhaust (%.4g, %.4g, %.4g) radius %.6g"),
				used[0].Position.x, used[0].Position.y, used[0].Position.z, used[0].Exhaust.x, used[0].Exhaust.y, used[0].Exhaust.z, used[0].Radius);
		const auto& carrier = CombatSpec::Of(ShipClass::Carrier)->Layout;
		if (carrier.Count() > 0)
			UE_LOG(LogSpaceGod, Display, TEXT("sg.VehicleData: ship/Carrier/Carrier hp0 (%s) at (%.6g, %.6g, %.6g)"), UTF8_TO_TCHAR(ToString(carrier[0]->Kind)),
				carrier[0]->X, carrier[0]->Y, carrier[0]->Z);
	}

	FAutoConsoleCommand GVehicleData(
		TEXT("sg.VehicleData"),
		TEXT("US-T452: where the vehicle data file was read from, what it overrode and anything it left out (docs/Vehicles.md)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { StatusCommand(args); }));
}
}
