#pragma once
// US-T869: the design a warship was built to - its pattern's name, class, assembler seed, faction style and the
// shipyard tier it came out of - held on Ship::Design and saved with it (older saves load without one).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <cstdint>

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	class Faction;
	class FactionStyleBook;
	class World;

	/// <summary>A hull's design: deterministic from (faction, class, tier), so no cs::Random draw is spent on it.</summary>
	class SPACEGODCORE_API ShipDesign final
	{
	public:
		/// <summary>e.g. "Halberd Mk II".</summary>
		cs::String Name;
		ShipClass Class{};
		/// <summary>The ModularAssembler seed: FNV-1a of (faction name, class, tier).</summary>
		uint64_t Seed = 0;
		/// <summary>The FactionStyleBook profile id the hull is styled by, "neutral" when no book or no match.</summary>
		cs::String Style = "neutral";
		/// <summary>Its faction's Shipyards research level when it was laid down.</summary>
		int Tier = 0;

	private:
		SG_SERIALIZABLE(ShipDesign);
	};

	/// <summary>Makes the designs. Pure and deterministic; the style book is registered once at startup.</summary>
	class SPACEGODCORE_API ShipDesigns final
	{
	public:
		/// <summary>The design <paramref name="faction"/> builds a <paramref name="cls"/> to at shipyard tier <paramref name="tier"/>.</summary>
		static cs::Ref<ShipDesign> For(const cs::Ref<Faction>& faction, ShipClass cls, int tier);
		/// <summary>As above, at the faction's current Shipyards research level in <paramref name="world"/> (0 without one).</summary>
		static cs::Ref<ShipDesign> For(const World& world, const cs::Ref<Faction>& faction, ShipClass cls);
		/// <summary>The shipyard tier: <paramref name="faction"/>'s level in ResearchTrack::Shipyards().</summary>
		static int TierOf(const World& world, const cs::Ref<Faction>& faction);
		/// <summary>Registers the faction style book (Data/FactionStyles.json) every design is styled from; null clears it.</summary>
		static void UseStyles(cs::Ref<const FactionStyleBook> book);
		/// <summary>The registered book, or null.</summary>
		static cs::Ref<const FactionStyleBook> Styles();
		/// <summary>"I", "II", ... for 1..3999 (empty otherwise).</summary>
		static cs::String Roman(int n);
	};
}
