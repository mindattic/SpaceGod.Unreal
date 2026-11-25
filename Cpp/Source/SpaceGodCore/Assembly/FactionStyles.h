#pragma once
// US-T845: faction style profiles (Data/FactionStyles.json), the ModularAssembler's FactionStyle per faction and scale,
// with the palette and markings a faction's hulls carry.

#include "Assembly/ModularAssembler.h"
#include "Cs/Cs.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	/// <summary>A colour from the file's "#RRGGBB" (0..1 each); Set false when the file gives none.</summary>
	struct FactionTint
	{
		double R = 1, G = 1, B = 1;
		bool Set = false;
	};

	/// <summary>The colours a faction's hulls are painted.</summary>
	struct FactionPalette
	{
		/// The USC unified colour: Grey, Red, Blue, Green, Purple, White or Black.
		cs::String Colour = "Grey";
		FactionTint Primary, Trim;
	};

	/// <summary>How a faction numbers and marks its hulls.</summary>
	struct FactionMarkings
	{
		cs::String Prefix;
		/// "{prefix}" and "{number}" are filled in.
		cs::String Format = "{number}";
		int Digits = 3;
		/// The decal's name, empty for none.
		cs::String Decal;
	};

	/// <summary>One faction's style: a FactionStyle per scale (fighter kits for small craft, capital kits for the rest),
	/// its palette and its markings.</summary>
	struct SPACEGODCORE_API FactionStyleProfile
	{
		cs::String Id = "neutral";
		/// The faction's name it matches, empty for a fallback.
		cs::String Faction;
		/// The PolityKind name it is the fallback for, empty when it names a faction.
		cs::String Kind;
		FactionStyle Fighter, Capital;
		FactionPalette Palette;
		FactionMarkings Markings;

		/// <summary>The style for <paramref name="cls"/>'s scale.</summary>
		const FactionStyle& For(ShipClass cls) const;
		/// <summary>The hull number a hull of seed <paramref name="seed"/> carries, e.g. "SFS-042".</summary>
		cs::String HullNumber(uint64_t seed) const;
	};

	/// <summary>Data/FactionStyles.json read back. Pure data, no engine types: load it once and keep it.</summary>
	class SPACEGODCORE_API FactionStyleBook final
	{
	public:
		/// <summary>Reads the styles at <paramref name="file"/>; a missing or broken file gives an empty book (every
		/// faction neutral) whose <see cref="Problem"/> says why.</summary>
		static FactionStyleBook Load(const cs::String& file);

		/// <summary>The profile of the faction named <paramref name="factionName"/>, else the fallback for its polity kind
		/// (<paramref name="kindName"/>, as ToString(PolityKind) names it), else neutral.</summary>
		const FactionStyleProfile& For(const cs::String& factionName, const cs::String& kindName) const;
		/// <summary>The assembler's style for a hull of class <paramref name="cls"/> built by that faction.</summary>
		const FactionStyle& For(const cs::String& factionName, const cs::String& kindName, ShipClass cls) const;
		/// <summary>The profile with id <paramref name="id"/> (case-insensitive), or null.</summary>
		const FactionStyleProfile* Find(const cs::String& id) const;

		const std::vector<FactionStyleProfile>& Profiles() const { return profiles; }
		const cs::String& Path() const { return path; }
		const cs::String& Problem() const { return problem; }
		bool Loaded() const { return !profiles.empty(); }

	private:
		std::vector<FactionStyleProfile> profiles;
		FactionStyleProfile neutral;
		cs::String path;
		cs::String problem;
	};
}
