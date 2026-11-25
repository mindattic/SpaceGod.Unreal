#pragma once
// US-T765: the seeded hull grammar. A seed, a ship class and a faction style become a tree of the kits' modular parts
// (Data/ModularParts.json, US-T763; docs/ThirdPartyArt.md "The kit part catalogue"), scaled to the class's true length.

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	enum class ShipClass : int;

	/// <summary>A face of a part where a neighbour mates, in the part's own model frame (nose +Z, up +Y, +X starboard,
	/// pack metres, about its pivot).</summary>
	struct KitSocket
	{
		cs::String Name;
		DVec3 Pos;
		/// Outward: the neighbour's side.
		DVec3 Normal;
		/// The patch's extent along the face's two other axes, in x, y, z order.
		double Size[2] = { 0, 0 };
	};

	/// <summary>One modular part of the catalogue.</summary>
	struct KitPart
	{
		/// "pack/set/name", e.g. "usc/GalacticLeopard/Turret".
		cs::String Id;
		/// The kit set it belongs to (GalacticLeopard, StarSparrow, Hangars...).
		cs::String Family;
		/// core, bow, stern, engine, wing, turret, hangar, antenna, armour, connector or detail.
		cs::String Role;
		/// The texture atlas that paints it.
		cs::String Atlas;
		/// The imported mesh that draws it, empty when no import brings it in.
		cs::String Asset;
		/// The asset is about the pivot (Star Sparrow); otherwise about the middle of Min..Max (USC).
		bool PivotOrigin = false;
		/// The pack first, then fighter / capital / station, then the role's own.
		std::vector<cs::String> Tags;
		DVec3 Min, Max;
		std::vector<KitSocket> Sockets;

		const KitSocket* Socket(const cs::String& name) const
		{
			for (const auto& s : Sockets)
				if (s.Name == name) return &s;
			return nullptr;
		}

		bool HasTag(const cs::String& tag) const
		{
			for (const auto& t : Tags)
				if (t == tag) return true;
			return false;
		}

		cs::String Pack() const { return Tags.empty() ? cs::String() : Tags[0]; }
	};

	/// <summary>Data/ModularParts.json read back. Pure data, no engine types: load it once and keep it.</summary>
	class SPACEGODCORE_API KitCatalogue final
	{
	public:
		/// <summary>Reads the catalogue at <paramref name="file"/>; a missing or broken file gives an empty catalogue
		/// whose <see cref="Problem"/> says why.</summary>
		static KitCatalogue Load(const cs::String& file);

		const std::vector<KitPart>& Parts() const { return parts; }
		const cs::String& Path() const { return path; }
		const cs::String& Problem() const { return problem; }
		bool Loaded() const { return !parts.empty(); }

	private:
		std::vector<KitPart> parts;
		cs::String path;
		cs::String problem;
	};

	/// <summary>
	/// How a faction likes its hulls built: the assembler's style input. Faction style profiles (US-T770) fill it; the
	/// default is no preference at all. Every weight is 0..1, 0.5 being the class's usual.
	/// </summary>
	struct FactionStyle
	{
		/// Names the style in the design (and seeds it: two styles give the same seed different hulls).
		cs::String Id = "neutral";
		/// Packs the parts come from, by preference ("usc", "starsparrow", "ssc"); empty: the class's own (fighter kits
		/// for small craft, capital kits for the rest).
		std::vector<cs::String> Packs;
		/// Kit sets to build from (GalacticLeopard, VoidWhale, HyperFalcon...); empty: the seed picks one per hull.
		std::vector<cs::String> Families;
		/// Only parts with an imported mesh, so every part draws as itself (falls back to all when none fits a slot).
		bool ImportedOnly = false;
		/// Longer spines: more core sections.
		double Length = 0.5;
		/// More and likelier wing pairs and fins.
		double Wings = 0.5;
		/// More turrets.
		double Armament = 0.5;
		/// How likely each spine section is a different core part from the first (0: one part repeated).
		double Variety = 0.35;
	};

	/// <summary>One part placed in a design: its pose in the design frame (nose +Z, up +Y, +X starboard, metres, about
	/// the hull's middle) and where it hangs on its parent.</summary>
	struct SPACEGODCORE_API ModularPlacement
	{
		/// Index into <see cref="KitCatalogue::Parts"/>.
		int Part = -1;
		cs::String PartId;
		/// spine, hangar, bow, stern, engine, wing, fin, turret or bridge.
		cs::String Slot;
		/// Index into <see cref="ModularDesign::Parts"/> (-1: the root section).
		int Parent = -1;
		/// Its socket that mates, and the parent's it mates to.
		cs::String Socket, ParentSocket;
		/// Where its pivot goes.
		DVec3 Pos;
		/// Its own +Z and +Y in the design frame (its +X is Up x Forward, then negated when mirrored).
		DVec3 Forward = DVec3(0, 0, 1);
		DVec3 Up = DVec3(0, 1, 0);
		/// Uniform scale from pack metres.
		double Scale = 1;
		/// The port copy of a starboard part: its own x is negated before it is turned (a reflection, not a turn).
		bool Mirrored = false;
		/// The placement this one mirrors (-1: none).
		int MirrorOf = -1;

		DVec3 Right() const;
		/// A direction in the part's own frame turned into the design frame (scale aside).
		DVec3 Turn(const DVec3& local) const;
		/// A point in the part's own frame (pack metres, about its pivot) in the design frame.
		DVec3 ToDesign(const DVec3& local) const;
	};

	/// <summary>A hull assembled from kit parts: what <see cref="ModularAssembler::Assemble"/> returns. The same inputs
	/// always give the same design.</summary>
	struct SPACEGODCORE_API ModularDesign
	{
		ShipClass Class{};
		uint64_t Seed = 0;
		cs::String Style;
		/// The kit set the hull was built from.
		cs::String Family;
		/// Its length along Z, the class's true length (docs/TrueScale.md).
		double LengthM = 0;
		/// Parents always come before their children.
		std::vector<ModularPlacement> Parts;
		/// The box round every part's box, in the design frame (centred on the origin).
		DVec3 Min, Max;
		/// Why the design is empty, or empty.
		cs::String Problem;

		int Count(const cs::String& slot) const;
	};

	/// <summary>
	/// The seeded hull grammar: a spine of core sections (hangar sections in it on carriers) with a bow before it and a
	/// stern behind, engines on the stern, wings and turrets in mirrored pairs, a bridge on a capital, every part mated
	/// socket to socket (one socket's normal turned onto the reverse of the other's, their positions brought together)
	/// and the whole scaled to the class's true length. Pure and cheap (a few hundred operations), no engine types, safe
	/// off the game thread.
	/// </summary>
	class SPACEGODCORE_API ModularAssembler final
	{
	public:
		static ModularDesign Assemble(const KitCatalogue& catalogue, ShipClass cls, uint64_t seed, const FactionStyle& style = FactionStyle());
	};
}
