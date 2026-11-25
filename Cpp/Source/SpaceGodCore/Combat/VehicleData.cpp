// US-T452: the vehicle data file read back (see VehicleData.h, docs/Vehicles.md).

#include "Combat/VehicleData.h"

#include "Assembly/ModularAssembler.h"
#include "Combat/CombatSpecs.h"
#include "Shared/Json/JsonObject.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace SpaceGod::Core
{
	using Shared::Json::JsonArray;
	using Shared::Json::JsonObject;
	using Shared::Json::JsonValue;

	namespace
	{
		cs::String& PathSetting()
		{
			static cs::String p;
			return p;
		}

		bool& PathGiven()
		{
			static bool given = false;
			return given;
		}

		bool ReadText(const cs::String& file, cs::String& text)
		{
			std::ifstream f(std::filesystem::path(std::u8string(file.begin(), file.end())), std::ios::binary);
			if (!f) return false;
			text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
				static_cast<unsigned char>(text[2]) == 0xBF)
				text.erase(0, 3);
			return true;
		}

		bool Number(const JsonValue& v, double& out)
		{
			auto n = v.AsNumber();
			if (!n) return false;
			out = n->Value();
			return std::isfinite(out);
		}

		/// A [x, y, z] array of finite numbers.
		bool Vec(const JsonValue& v, double out[3])
		{
			auto a = v.AsArray();
			if (!a || a->Count() != 3) return false;
			for (int i = 0; i < 3; i++)
				if (!Number((*a)[i], out[i])) return false;
			return true;
		}

		bool NonZero(const double v[3]) { return v[0] != 0 || v[1] != 0 || v[2] != 0; }
	}

	void VehicleData::SetPath(const cs::String& p)
	{
		PathSetting() = p;
		PathGiven() = true;
	}

	const VehicleData& VehicleData::Get()
	{
		static const VehicleData data = []
		{
			VehicleData d;
			cs::String file = PathSetting();
			if (!PathGiven())
			{
#if defined(_MSC_VER)
				char* env = nullptr;
				size_t size = 0;
				if (_dupenv_s(&env, &size, "SPACEGOD_VEHICLES") == 0 && env)
				{
					file = env;
					free(env);
				}
#else
				if (const char* env = std::getenv("SPACEGOD_VEHICLES")) file = env;
#endif
			}
			if (file.empty()) d.problem = "no vehicle data file given (built-in tables)";
			else d.Load(file);
			return d;
		}();
		return data;
	}

	void VehicleData::Load(const cs::String& file)
	{
		path = file;
		cs::String text;
		if (!ReadText(file, text))
		{
			problem = cs::Cat("no file at ", file, " (built-in tables)");
			return;
		}
		cs::Ref<JsonObject> root;
		try
		{
			root = JsonObject::Parse(text);
		}
		catch (const std::exception& e)
		{
			problem = cs::Cat(file, " does not parse: ", e.what(), " (built-in tables)");
			return;
		}
		if (!root || root->GetInt("version", 0) != 1)
		{
			problem = cs::Cat(file, " is not version 1 (built-in tables)");
			return;
		}

		if (auto vehicles = root->GetArray("vehicles"))
			for (int i = 0; i < vehicles->Count(); i++)
			{
				auto v = (*vehicles)[i].AsObject();
				if (!v) continue;
				cs::String id = v->GetString("id");
				if (id.empty() || !v->GetBool("enginePortsEditable", false)) continue;
				auto list = v->GetArray("enginePorts");
				if (!list) continue;
				std::vector<Port> row;
				for (int j = 0; j < list->Count(); j++)
				{
					auto p = (*list)[j].AsObject();
					if (!p) continue;
					// The bells on Engine hardpoints sit on the hardpoints, not here.
					if (p->GetInt("engine", -1) >= 0) continue;
					Port port;
					double r = 0;
					if (!Vec((*p)["pos"], port.Pos) || !Vec((*p)["exhaust"], port.Exhaust) || !NonZero(port.Exhaust) || !Number((*p)["radius"], r) || !(r > 0))
					{
						ignored.Add(cs::Cat(id, " port", j, ": needs a finite pos, a non-zero exhaust and a radius above 0"));
						continue;
					}
					port.Radius = r;
					row.push_back(port);
				}
				ports.emplace_back(id, std::move(row));
			}

		if (auto layoutObj = root->GetObject("classLayouts"))
			for (auto [id, value] : *layoutObj)
			{
				auto l = value.AsObject();
				if (!l) continue;
				auto list = l->GetArray("hardpoints");
				if (!list) continue;
				std::vector<Placement> row;
				for (int j = 0; j < list->Count(); j++)
				{
					auto h = (*list)[j].AsObject();
					if (!h || h->GetBool("derived", false)) continue;
					Placement p;
					p.Index = h->GetInt("index", -1);
					p.Kind = h->GetString("kind");
					double pos[3];
					if (p.Index < 0 || p.Kind.empty() || !Vec((*h)["pos"], pos))
					{
						ignored.Add(cs::Cat(id, " hardpoint ", j, ": needs an index, a kind and a finite pos"));
						continue;
					}
					p.Pos = DVec3(pos[0], pos[1], pos[2]);
					p.FacesAft = h->GetBool("facesAft", false);
					JsonValue axis = (*h)["coneAxis"];
					if (!axis.IsNull())
					{
						double a[3];
						if (!Vec(axis, a) || !NonZero(a))
						{
							ignored.Add(cs::Cat(id, " hp", p.Index, ": coneAxis must be null or a non-zero [x, y, z]"));
							continue;
						}
						p.HasAxis = true;
						p.Axis = DVec3(a[0], a[1], a[2]);
					}
					JsonValue half = (*h)["coneHalfDeg"];
					if (!half.IsNull())
					{
						if (!Number(half, p.Half) || !(p.Half > 0))
						{
							ignored.Add(cs::Cat(id, " hp", p.Index, ": coneHalfDeg must be null or a number above 0"));
							continue;
						}
						p.HasHalf = true;
					}
					row.push_back(p);
				}
				layouts.emplace_back(id, std::move(row));
			}
		loaded = true;
		unsigned long long h = 14695981039346656037ull;
		for (unsigned char c : text)
		{
			h ^= c;
			h *= 1099511628211ull;
		}
		char buf[17];
		std::snprintf(buf, sizeof(buf), "%016llx", h);
		hash = buf;
	}

	const std::vector<VehicleData::Port>* VehicleData::PortsOf(const cs::String& id) const
	{
		for (const auto& [key, row] : ports)
			if (key == id) return &row;
		return nullptr;
	}

	int VehicleData::Apply(const cs::String& layoutId, const cs::List<cs::Ref<SubsystemDef>>& layout) const
	{
		int set = 0;
		for (const auto& [key, row] : layouts)
		{
			if (key != layoutId) continue;
			for (const auto& p : row)
			{
				if (p.Index >= layout.Count() || !layout[p.Index])
				{
					ignored.Add(cs::Cat(layoutId, " hp", p.Index, ": the design has no such hardpoint"));
					continue;
				}
				const auto& d = layout[p.Index];
				if (p.Kind != ToString(d->Kind))
				{
					ignored.Add(cs::Cat(layoutId, " hp", p.Index, ": is ", ToString(d->Kind), " in the code, not ", p.Kind));
					continue;
				}
				d->X = p.Pos.X;
				d->Y = p.Pos.Y;
				d->Z = p.Pos.Z;
				d->FacesAft = p.FacesAft;
				d->ConeAxis = p.HasAxis ? cs::Nullable<DVec3>(p.Axis) : std::nullopt;
				d->ConeHalfDeg = p.HasHalf ? cs::Nullable<double>(p.Half) : std::nullopt;
				set++;
			}
			break;
		}
		applied += set;
		return set;
	}

	namespace
	{
		DVec3 GeometryPoint(const ModularPlacement& p, const DVec3& local, double length) { return p.ToDesign(local) * (1 / length); }

		DVec3 GeometryAxis(const ModularPlacement& p, const DVec3& local)
		{
			DVec3 a = p.Turn(local);
			return a.LengthSquared() > 1e-12 ? a.Normalized() : DVec3(0, 1, 0);
		}

		VehicleData::Mount GeometryMount(const ModularPlacement& p, const KitSocket& s, double length, int part)
		{
			VehicleData::Mount m;
			m.Pos = GeometryPoint(p, s.Pos, length);
			m.Axis = GeometryAxis(p, s.Normal);
			m.Width = s.Size[0] * p.Scale / length;
			m.Height = s.Size[1] * p.Scale / length;
			m.Part = part;
			return m;
		}

		/// Weapons laid along the keel: the hull aims them, so a mount gives them a place and never a cone axis.
		bool GeometryFixed(SubsystemKind kind)
		{
			return kind == SubsystemKind::FighterGuns || kind == SubsystemKind::Railgun || kind == SubsystemKind::Torpedo ||
				kind == SubsystemKind::BoardingPods || kind == SubsystemKind::Drill;
		}

		/// The least-used spot, the nearest to <paramref name="at"/> among those; -1 with none.
		int GeometryPick(const DVec3& at, const std::vector<DVec3>& spots, std::vector<int>& use)
		{
			int best = -1;
			for (int k = 0; k < static_cast<int>(spots.size()); k++)
				if (best < 0 || use[k] < use[best] || (use[k] == use[best] && (spots[k] - at).LengthSquared() < (spots[best] - at).LengthSquared()))
					best = k;
			if (best >= 0) use[best]++;
			return best;
		}
	}

	VehicleData::Geometry VehicleData::FromDesign(const ModularDesign& design, const KitCatalogue& catalogue)
	{
		Geometry g;
		double length = design.LengthM;
		if (!(length > 0)) return g;
		const auto& parts = catalogue.Parts();
		int count = static_cast<int>(design.Parts.size());
		for (int i = 0; i < count; i++)
		{
			const ModularPlacement& p = design.Parts[i];
			if (p.Part < 0 || p.Part >= static_cast<int>(parts.size())) continue;
			const KitPart& part = parts[p.Part];
			if (p.Slot == "engine")
			{
				const KitSocket* s = part.Socket("exhaust");
				if (!s) continue;
				Port port;
				DVec3 at = GeometryPoint(p, s->Pos, length), out = GeometryAxis(p, s->Normal);
				port.Pos[0] = at.X;
				port.Pos[1] = at.Y;
				port.Pos[2] = at.Z;
				port.Exhaust[0] = out.X;
				port.Exhaust[1] = out.Y;
				port.Exhaust[2] = out.Z;
				port.Radius = 0.5 * std::sqrt(std::max(s->Size[0], 0.0) * std::max(s->Size[1], 0.0)) * p.Scale / length;
				if (port.Radius > 0) g.Engines.push_back(port);
			}
			else if (p.Slot == "turret")
			{
				const KitSocket* s = part.Socket("muzzle");
				if (!s) s = part.Socket("top");
				if (s) g.Guns.push_back(GeometryMount(p, *s, length, i));
			}
			else if (p.Slot == "hangar")
			{
				// The mouth: the largest face nothing mates to.
				const KitSocket* mouth = nullptr;
				for (const auto& s : part.Sockets)
				{
					if (s.Name == p.Socket) continue;
					bool used = false;
					for (const auto& c : design.Parts)
						if (&c != &p && c.Parent == i && c.ParentSocket == s.Name) used = true;
					if (!used && (!mouth || s.Size[0] * s.Size[1] > mouth->Size[0] * mouth->Size[1])) mouth = &s;
				}
				if (mouth) g.Hangars.push_back(GeometryMount(p, *mouth, length, i));
			}
			else if (p.Slot == "bridge")
			{
				Mount m;
				m.Pos = GeometryPoint(p, (part.Min + part.Max) * 0.5, length);
				m.Axis = GeometryAxis(p, DVec3(0, 1, 0));
				m.Width = (part.Max.X - part.Min.X) * p.Scale / length;
				m.Height = (part.Max.Y - part.Min.Y) * p.Scale / length;
				m.Part = i;
				g.Bridges.push_back(m);
			}
		}
		return g;
	}

	int VehicleData::ApplyGeometry(const Geometry& geometry, const cs::List<cs::Ref<SubsystemDef>>& layout)
	{
		auto spotsOf = [](const std::vector<Mount>& mounts)
		{
			std::vector<DVec3> spots;
			spots.reserve(mounts.size());
			for (const auto& m : mounts) spots.push_back(m.Pos);
			return spots;
		};
		std::vector<DVec3> nozzles;
		nozzles.reserve(geometry.Engines.size());
		for (const auto& e : geometry.Engines) nozzles.push_back(DVec3(e.Pos[0], e.Pos[1], e.Pos[2]));
		std::vector<DVec3> guns = spotsOf(geometry.Guns), hangars = spotsOf(geometry.Hangars), bridges = spotsOf(geometry.Bridges);
		std::vector<int> nozzleUse(nozzles.size()), gunUse(guns.size()), hangarUse(hangars.size()), bridgeUse(bridges.size());

		int set = 0;
		for (int i = 0; i < layout.Count(); i++)
		{
			const auto& d = layout[i];
			if (!d) continue;
			DVec3 at(d->X, d->Y, d->Z), to;
			int k = -1;
			if (d->Kind == SubsystemKind::Engine)
			{
				if ((k = GeometryPick(at, nozzles, nozzleUse)) >= 0) to = nozzles[k];
			}
			else if (d->Kind == SubsystemKind::Hangar)
			{
				if ((k = GeometryPick(at, hangars, hangarUse)) >= 0)
				{
					const auto& mouth = geometry.Hangars[k];
					to = hangars[k];
					d->MouthAxis = cs::Nullable<DVec3>(mouth.Axis);
					d->MouthWidth = mouth.Width;
					d->MouthHeight = mouth.Height;
				}
			}
			else if (d->Kind == SubsystemKind::Bridge)
			{
				if (!d->Buried && (k = GeometryPick(at, bridges, bridgeUse)) >= 0) to = bridges[k];
			}
			else if (d->IsWeapon() && !d->Spinal)
			{
				if ((k = GeometryPick(at, guns, gunUse)) >= 0)
				{
					to = guns[k];
					if (!GeometryFixed(d->Kind))
					{
						const DVec3& axis = geometry.Guns[k].Axis;
						d->ConeAxis = cs::Nullable<DVec3>(axis);
						d->FacesAft = axis.Z < -0.7;
					}
				}
			}
			if (k < 0) continue;
			d->X = to.X;
			d->Y = to.Y;
			d->Z = to.Z;
			set++;
		}
		return set;
	}
}
