// US-T765: the seeded hull grammar (see ModularAssembler.h).

#include "Assembly/ModularAssembler.h"

#include "Combat/Outfitting.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Shared/Json/JsonObject.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

namespace SpaceGod::Core
{
	using Shared::Json::JsonObject;
	using Shared::Json::JsonValue;

	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

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

		double Num(const JsonValue& v)
		{
			auto n = v.AsNumber();
			double d = n ? n->Value() : 0;
			return std::isfinite(d) ? d : 0;
		}

		DVec3 Vec(const JsonValue& v)
		{
			auto a = v.AsArray();
			if (!a || a->Count() != 3) return DVec3();
			return DVec3(Num((*a)[0]), Num((*a)[1]), Num((*a)[2]));
		}

		cs::String Str(const JsonValue& v)
		{
			auto s = v.AsString();
			return s ? *s : cs::String();
		}

		/// SplitMix64: the design's own generator, so a seed's hull never depends on the library's.
		class Rng
		{
		public:
			explicit Rng(uint64_t seed) : state(seed) {}

			uint64_t Next()
			{
				uint64_t z = state += 0x9E3779B97F4A7C15ull;
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
			int Below(int n) { return n <= 1 ? 0 : static_cast<int>(Next() % static_cast<uint64_t>(n)); }
			bool Chance(double p) { return Unit() < p; }

		private:
			uint64_t state;
		};

		uint64_t Fnv(const cs::String& s)
		{
			uint64_t h = 0xCBF29CE484222325ull;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 0x100000001B3ull;
			}
			return h;
		}

		/// A turn: its columns are where the part's own x, y and z go.
		struct Mat3
		{
			DVec3 X = DVec3(1, 0, 0), Y = DVec3(0, 1, 0), Z = DVec3(0, 0, 1);
			DVec3 operator*(const DVec3& v) const { return X * v.X + Y * v.Y + Z * v.Z; }
		};

		/// The turn by <paramref name="angle"/> about unit axis <paramref name="k"/> (Rodrigues).
		Mat3 AxisAngle(const DVec3& k, double angle)
		{
			double c = std::cos(angle), s = std::sin(angle);
			auto turn = [&](const DVec3& v) { return v * c + DVec3::Cross(k, v) * s + k * (DVec3::Dot(k, v) * (1 - c)); };
			Mat3 m;
			m.X = turn(DVec3(1, 0, 0));
			m.Y = turn(DVec3(0, 1, 0));
			m.Z = turn(DVec3(0, 0, 1));
			return m;
		}

		/// The least turn taking unit <paramref name="a"/> onto unit <paramref name="b"/>. Exactly opposite, it turns half
		/// round the part's own nose axis when that is square to <paramref name="a"/> (a wing stays level, flipped over),
		/// else round its up axis (a section turns end for end, staying upright).
		Mat3 Between(const DVec3& a, const DVec3& b)
		{
			double d = DVec3::Dot(a, b);
			if (d > 1 - 1e-9) return Mat3();
			if (d < -1 + 1e-9)
			{
				for (const DVec3& k : { DVec3(0, 0, 1), DVec3(0, 1, 0), DVec3(1, 0, 0) })
					if (std::abs(DVec3::Dot(a, k)) < 0.5) return AxisAngle(k, Pi);
			}
			return AxisAngle(DVec3::Cross(a, b).Normalized(), std::acos(std::max(-1.0, std::min(1.0, d))));
		}

		double Diag(const KitPart& p) { return std::max((p.Max - p.Min).Length(), 1e-3); }

		double Extent(const KitSocket& s) { return std::sqrt(std::max(s.Size[0], 1e-3) * std::max(s.Size[1], 1e-3)); }

		bool Contains(const std::vector<cs::String>& list, const cs::String& s) { return std::find(list.begin(), list.end(), s) != list.end(); }

		/// One design being built: the catalogue, the generator and the parts placed so far.
		class Build
		{
		public:
			const KitCatalogue& Cat;
			const FactionStyle& Style;
			Rng R;
			cs::String ScaleTag;
			std::vector<cs::String> Packs;
			cs::String Family;
			ModularDesign& D;
			/// Placements to copy to port once the starboard side is done.
			std::vector<bool> Pair;

			Build(const KitCatalogue& cat, const FactionStyle& style, uint64_t seed, ModularDesign& d) : Cat(cat), Style(style), R(seed), D(d) {}

			const KitPart& PartOf(int placement) const { return Cat.Parts()[D.Parts[placement].Part]; }

			/// A part of <paramref name="role"/> that <paramref name="fits"/>: from the hull's kit set, else the style's packs at
			/// the class's scale, else the style's packs, else anywhere; imported meshes first when the style asks.
			int Pick(const cs::String& role, const std::function<bool(const KitPart&)>& fits)
			{
				const auto& parts = Cat.Parts();
				for (int tier = 0; tier < 4; tier++)
					for (int imported = Style.ImportedOnly ? 1 : 0; imported >= 0; imported--)
					{
						std::vector<int> found;
						for (int i = 0; i < static_cast<int>(parts.size()); i++)
						{
							const KitPart& p = parts[i];
							if (p.Role != role || !fits(p) || (imported && p.Asset.empty())) continue;
							bool ok = tier == 0 ? p.Family == Family
								: tier == 1 ? Contains(Packs, p.Pack()) && p.HasTag(ScaleTag)
								: tier == 2 ? Contains(Packs, p.Pack())
								: true;
							if (ok) found.push_back(i);
						}
						if (!found.empty()) return found[R.Below(static_cast<int>(found.size()))];
					}
				return -1;
			}

			/// Places part <paramref name="part"/> with its socket <paramref name="socket"/> on placement
			/// <paramref name="parent"/>'s socket <paramref name="parentSocket"/>, moved by <paramref name="offset"/> (the
			/// parent's own frame) along that face. Returns its index, or -1 when either socket is missing.
			int Mate(int parent, const cs::String& parentSocket, int part, const cs::String& socket, double scale, const cs::String& slot,
				const DVec3& offset = DVec3(), bool pair = false)
			{
				const KitPart& pp = PartOf(parent);
				const KitPart& cp = Cat.Parts()[part];
				const KitSocket* ps = pp.Socket(parentSocket);
				const KitSocket* cs_ = cp.Socket(socket);
				if (!ps || !cs_) return -1;
				const ModularPlacement& p = D.Parts[parent];
				DVec3 at = p.ToDesign(ps->Pos + offset);
				DVec3 n = p.Turn(ps->Normal).Normalized();
				Mat3 turn = Between(cs_->Normal.Normalized(), -n);
				ModularPlacement c;
				c.Part = part;
				c.PartId = cp.Id;
				c.Slot = slot;
				c.Parent = parent;
				c.Socket = socket;
				c.ParentSocket = parentSocket;
				c.Forward = turn.Z;
				c.Up = turn.Y;
				c.Scale = scale;
				c.Pos = at - turn * (cs_->Pos * scale);
				D.Parts.push_back(c);
				Pair.push_back(pair);
				return static_cast<int>(D.Parts.size()) - 1;
			}

			/// The scale that matches part <paramref name="part"/>'s socket to the parent's, within a band of its scale.
			double Matched(int parent, const cs::String& parentSocket, int part, const cs::String& socket, double lo, double hi) const
			{
				const KitSocket* ps = PartOf(parent).Socket(parentSocket);
				const KitSocket* cs_ = Cat.Parts()[part].Socket(socket);
				double s = D.Parts[parent].Scale;
				if (!ps || !cs_) return s;
				return std::max(s * lo, std::min(s * hi, s * Extent(*ps) / Extent(*cs_)));
			}

			/// The scale that makes part <paramref name="part"/> <paramref name="share"/> of the parent's size.
			double Sized(int parent, int part, double share) const { return D.Parts[parent].Scale * Diag(PartOf(parent)) * share / Diag(Cat.Parts()[part]); }

			/// The port copies of every starboard part, their parents' copies first.
			void Mirror()
			{
				int n = static_cast<int>(D.Parts.size());
				std::vector<int> copyOf(n, -1);
				for (int i = 0; i < n; i++)
				{
					if (!Pair[i] && !(D.Parts[i].Parent >= 0 && copyOf[D.Parts[i].Parent] >= 0)) continue;
					ModularPlacement m = D.Parts[i];
					auto flip = [](const DVec3& v) { return DVec3(-v.X, v.Y, v.Z); };
					m.Pos = flip(m.Pos);
					m.Forward = flip(m.Forward);
					m.Up = flip(m.Up);
					m.Mirrored = true;
					m.MirrorOf = i;
					if (m.Parent >= 0 && copyOf[m.Parent] >= 0) m.Parent = copyOf[m.Parent];
					// The copy reads as the port part: a starboard socket on its centreline parent is now the port one.
					if (m.ParentSocket == "starboard") m.ParentSocket = "port";
					D.Parts.push_back(m);
					copyOf[i] = static_cast<int>(D.Parts.size()) - 1;
				}
			}
		};

		bool HullLike(const KitPart& p) { return p.Socket("fore") && p.Socket("aft"); }
	}

	DVec3 ModularPlacement::Right() const { return DVec3::Cross(Up, Forward); }

	DVec3 ModularPlacement::Turn(const DVec3& local) const
	{
		return Right() * (Mirrored ? -local.X : local.X) + Up * local.Y + Forward * local.Z;
	}

	DVec3 ModularPlacement::ToDesign(const DVec3& local) const { return Pos + Turn(local) * Scale; }

	int ModularDesign::Count(const cs::String& slot) const
	{
		int n = 0;
		for (const auto& p : Parts)
			if (p.Slot == slot) n++;
		return n;
	}

	KitCatalogue KitCatalogue::Load(const cs::String& file)
	{
		KitCatalogue c;
		c.path = file;
		cs::String text;
		if (!ReadText(file, text))
		{
			c.problem = cs::Cat("no file at ", file);
			return c;
		}
		cs::Ref<JsonObject> root;
		try
		{
			root = JsonObject::Parse(text);
		}
		catch (const std::exception& e)
		{
			c.problem = cs::Cat(file, " does not parse: ", e.what());
			return c;
		}
		auto parts = root ? root->GetArray("parts") : nullptr;
		if (!parts)
		{
			c.problem = cs::Cat(file, " has no parts");
			return c;
		}
		for (int i = 0; i < parts->Count(); i++)
		{
			auto o = (*parts)[i].AsObject();
			if (!o) continue;
			KitPart p;
			p.Id = o->GetString("id");
			p.Family = o->GetString("family");
			p.Role = o->GetString("role");
			p.Atlas = o->GetString("atlas");
			p.Asset = o->GetString("asset");
			p.PivotOrigin = o->GetString("assetOrigin") == "pivot";
			if (auto tags = o->GetArray("tags"))
				for (int t = 0; t < tags->Count(); t++) p.Tags.push_back(Str((*tags)[t]));
			p.Min = Vec((*o)["min"]);
			p.Max = Vec((*o)["max"]);
			if (auto sockets = o->GetArray("sockets"))
				for (int s = 0; s < sockets->Count(); s++)
				{
					auto so = (*sockets)[s].AsObject();
					if (!so) continue;
					KitSocket k;
					k.Name = so->GetString("name");
					k.Pos = Vec((*so)["pos"]);
					k.Normal = Vec((*so)["normal"]);
					if (auto size = so->GetArray("size"); size && size->Count() == 2)
					{
						k.Size[0] = Num((*size)[0]);
						k.Size[1] = Num((*size)[1]);
					}
					if (k.Normal.LengthSquared() > 0) p.Sockets.push_back(k);
				}
			if (!p.Id.empty() && !p.Role.empty()) c.parts.push_back(std::move(p));
		}
		if (c.parts.empty()) c.problem = cs::Cat(file, " has no parts");
		return c;
	}

	ModularDesign ModularAssembler::Assemble(const KitCatalogue& catalogue, ShipClass cls, uint64_t seed, const FactionStyle& style)
	{
		ModularDesign d;
		d.Class = cls;
		d.Seed = seed;
		d.Style = style.Id;
		d.LengthM = ScaleModel::TrueLengthM(cls);
		if (!catalogue.Loaded())
		{
			d.Problem = catalogue.Problem().empty() ? cs::String("no kit part catalogue") : catalogue.Problem();
			return d;
		}

		bool small = Loadout::IsSmallCraft(cls);
		bool carrier = cls == ShipClass::Carrier;
		Build b(catalogue, style, seed ^ (Fnv(style.Id) * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(static_cast<int>(cls)) + 1) * 0xD6E8FEB86659FD93ull, d);
		b.ScaleTag = small ? "fighter" : "capital";
		b.Packs = !style.Packs.empty() ? style.Packs : small ? std::vector<cs::String>{ "usc", "starsparrow" } : std::vector<cs::String>{ "usc" };

		// The kit set: one whose hull sections suit the class, from the style's own sets when it names any.
		std::vector<cs::String> families, styled;
		for (const auto& p : catalogue.Parts())
		{
			if (p.Role != "core" || !HullLike(p) || Contains(families, p.Family)) continue;
			if (Contains(b.Packs, p.Pack()) && p.HasTag(b.ScaleTag)) families.push_back(p.Family);
			if (Contains(style.Families, p.Family) && !Contains(styled, p.Family)) styled.push_back(p.Family);
		}
		if (!styled.empty()) families = styled;
		b.Family = families.empty() ? cs::String() : families[b.R.Below(static_cast<int>(families.size()))];
		d.Family = b.Family;

		// The spine, bow to stern: section 0 at the origin, each next one's fore socket on the aft socket before it.
		int sections = 1;
		if (small) sections += b.R.Chance(0.2 + 0.4 * style.Length) ? 1 : 0;
		else
		{
			int most = d.LengthM < 150 ? 3 : d.LengthM < 400 ? 4 : d.LengthM < 900 ? 5 : 6;
			sections = std::min(most, 2 + static_cast<int>(b.R.Unit() * (most - 1) * (0.5 + style.Length)));
		}
		int hangars = carrier ? 1 + b.R.Below(2) : 0;
		int hangarAt = carrier ? 1 + b.R.Below(std::max(1, sections - 1)) : -1;

		int firstCore = b.Pick("core", [](const KitPart& p) { return HullLike(p) && !p.HasTag("bridge"); });
		if (firstCore < 0)
		{
			d.Problem = "the catalogue has no hull sections";
			return d;
		}
		ModularPlacement root;
		root.Part = firstCore;
		root.PartId = catalogue.Parts()[firstCore].Id;
		root.Slot = "spine";
		d.Parts.push_back(root);
		b.Pair.push_back(false);

		std::vector<int> spine{ 0 };
		std::vector<int> cores{ 0 };
		for (int i = 1; i < sections + hangars; i++)
		{
			bool hangar = i >= hangarAt && i < hangarAt + hangars;
			int part = hangar ? b.Pick("hangar", [](const KitPart& p) { return HullLike(p) && !p.HasTag("door") && !p.HasTag("deck"); })
				: b.R.Chance(style.Variety) ? b.Pick("core", [](const KitPart& p) { return HullLike(p) && !p.HasTag("bridge"); })
				: firstCore;
			if (part < 0) continue;
			int prev = spine.back();
			double scale = b.Matched(prev, "aft", part, "fore", hangar ? 0.5 : 0.6, hangar ? 2.0 : 1.6);
			int at = b.Mate(prev, "aft", part, "fore", scale, hangar ? "hangar" : "spine");
			if (at < 0) continue;
			spine.push_back(at);
			if (!hangar) cores.push_back(at);
		}

		// The bow before the first section, the stern behind the last, the engines on whichever is aft-most.
		int bow = b.Pick("bow", [](const KitPart& p) { return p.Socket("aft") != nullptr; });
		if (bow >= 0) b.Mate(0, "fore", bow, "aft", b.Matched(0, "fore", bow, "aft", 0.4, 2.0), "bow");

		int tail = spine.back();
		if (b.R.Chance(small ? 0.6 : 0.8))
		{
			int stern = b.Pick("stern", [](const KitPart& p) { return p.Socket("fore") && p.Socket("aft"); });
			if (stern >= 0)
			{
				int at = b.Mate(tail, "aft", stern, "fore", b.Matched(tail, "aft", stern, "fore", 0.4, 2.0), "stern");
				if (at >= 0) tail = at;
			}
		}

		int engines = small ? 1 + b.R.Below(2) : 2 + b.R.Below(3);
		int engine = b.Pick("engine", [](const KitPart& p) { return p.Socket("mount") != nullptr; });
		if (engine >= 0)
		{
			const KitSocket* aft = b.PartOf(tail).Socket("aft");
			double w = aft ? aft->Size[0] : 0, h = aft ? aft->Size[1] : 0;
			double scale = b.Sized(tail, engine, (small ? 0.5 : 0.4) / std::sqrt(static_cast<double>(engines)));
			auto place = [&](double x, double y) { b.Mate(tail, "aft", engine, "mount", scale, "engine", DVec3(x, y, 0), x > 1e-9); };
			if (engines % 2 == 1) place(0, 0);
			if (engines == 2) place(0.3 * w, 0);
			if (engines == 3) place(0.35 * w, 0);
			if (engines == 4)
			{
				place(0.3 * w, 0.25 * h);
				place(0.3 * w, -0.25 * h);
			}
		}

		// Wings in mirrored pairs on the sections' starboard faces (the aft-most first on small craft), a fin on top.
		int pairs = small ? 1 + (b.R.Chance(style.Wings * 0.6) ? 1 : 0) : b.R.Chance(style.Wings * 0.7) ? 1 : 0;
		for (int i = 0; i < pairs; i++)
		{
			int wing = b.Pick("wing", [](const KitPart& p) { return p.Socket("root") && !p.HasTag("paired"); });
			if (wing < 0) break;
			int on = small ? cores[std::max(0, static_cast<int>(cores.size()) - 1 - i)] : cores[b.R.Below(static_cast<int>(cores.size()))];
			const KitSocket* side = b.PartOf(on).Socket("starboard");
			double dz = side && i > 0 && cores.size() == 1 ? 0.3 * side->Size[1] : 0;
			b.Mate(on, "starboard", wing, "root", b.Sized(on, wing, small ? 0.8 : 0.45), "wing", DVec3(0, 0, dz), true);
		}
		if (small && b.R.Chance(style.Wings))
		{
			int fin = b.Pick("wing", [](const KitPart& p) { return p.Socket("root") && (p.HasTag("fin") || p.HasTag("vertical")) && !p.HasTag("paired"); });
			if (fin >= 0) b.Mate(cores.back(), "top", fin, "root", b.Sized(cores.back(), fin, 0.4), "fin");
		}

		// Turrets: a gun pair under a small craft's nose; on a capital, a mount on the centreline and pairs along each section's top.
		auto mountOf = [](const KitPart& p) { return p.Socket("base") ? cs::String("base") : p.Socket("breech") ? cs::String("breech") : cs::String(); };
		if (small)
		{
			if (b.R.Chance(0.4 + style.Armament))
			{
				int gun = b.Pick("turret", [&](const KitPart& p) { return p.HasTag("barrel") && !mountOf(p).empty(); });
				const KitSocket* under = b.PartOf(0).Socket("bottom");
				if (gun >= 0 && under)
					b.Mate(0, "bottom", gun, mountOf(catalogue.Parts()[gun]), b.Sized(0, gun, 0.3), "turret", DVec3(0.3 * under->Size[0], 0, 0), true);
			}
		}
		else
		{
			int turret = b.Pick("turret", [&](const KitPart& p) { return p.HasTag("mount") && p.Socket("base"); });
			if (turret < 0) turret = b.Pick("turret", [&](const KitPart& p) { return !mountOf(p).empty(); });
			for (int at : cores)
			{
				const KitSocket* top = b.PartOf(at).Socket("top");
				if (turret < 0 || !top) break;
				cs::String mount = mountOf(catalogue.Parts()[turret]);
				double scale = b.Sized(at, turret, 0.16);
				if (b.R.Chance(0.3 + 0.6 * style.Armament)) b.Mate(at, "top", turret, mount, scale, "turret", DVec3(0, 0, 0.25 * top->Size[1]));
				int rows = b.R.Below(1 + static_cast<int>(std::lround(2 * style.Armament)) + 1);
				for (int r = 0; r < rows; r++)
					b.Mate(at, "top", turret, mount, scale, "turret", DVec3(0.3 * top->Size[0], 0, (r == 0 ? -0.2 : 0.1) * top->Size[1]), true);
			}

			// A bridge tower on a capital, over its first or second section.
			int bridge = b.Pick("core", [](const KitPart& p) { return p.HasTag("bridge") && p.Socket("bottom"); });
			if (bridge >= 0)
			{
				int on = cores[std::min(static_cast<int>(cores.size()) - 1, b.R.Below(2))];
				const KitSocket* top = b.PartOf(on).Socket("top");
				b.Mate(on, "top", bridge, "bottom", b.Sized(on, bridge, 0.35), "bridge", DVec3(0, 0, top ? -0.3 * top->Size[1] : 0));
			}
		}

		b.Mirror();

		// Scaled to the class's true length and centred on its box.
		DVec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
		for (const auto& p : d.Parts)
		{
			const KitPart& part = catalogue.Parts()[p.Part];
			for (int c = 0; c < 8; c++)
			{
				DVec3 corner((c & 1) ? part.Max.X : part.Min.X, (c & 2) ? part.Max.Y : part.Min.Y, (c & 4) ? part.Max.Z : part.Min.Z);
				DVec3 w = p.ToDesign(corner);
				lo = DVec3(std::min(lo.X, w.X), std::min(lo.Y, w.Y), std::min(lo.Z, w.Z));
				hi = DVec3(std::max(hi.X, w.X), std::max(hi.Y, w.Y), std::max(hi.Z, w.Z));
			}
		}
		double k = d.LengthM / std::max(hi.Z - lo.Z, 1e-6);
		DVec3 mid = (lo + hi) * (0.5 * k);
		for (auto& p : d.Parts)
		{
			p.Pos = p.Pos * k - mid;
			p.Scale *= k;
		}
		d.Min = lo * k - mid;
		d.Max = hi * k - mid;
		return d;
	}
}
