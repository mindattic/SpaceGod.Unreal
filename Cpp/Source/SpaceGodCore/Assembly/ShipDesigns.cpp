#include "Assembly/ShipDesigns.h"

#include "Assembly/FactionStyles.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

#include <mutex>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr const char* DesignWords[] = {
			"Halberd", "Corsair", "Vigil", "Tempest", "Aegis", "Harrier", "Bastion", "Lancer", "Sentinel", "Raptor",
			"Talon", "Warden", "Spectre", "Onager", "Falchion", "Basilisk", "Trident", "Vanguard", "Cutlass", "Gorgon",
			"Hydra", "Pike", "Mantis", "Paladin", "Scimitar", "Wyvern", "Ironclad", "Starling", "Glaive", "Bulwark",
			"Cerberus", "Kestrel",
		};

		constexpr uint64_t FnvOffset = 14695981039346656037ull;
		constexpr uint64_t FnvPrime = 1099511628211ull;

		uint64_t FnvBytes(uint64_t h, const void* data, size_t n)
		{
			const auto* p = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= FnvPrime; }
			return h;
		}

		std::mutex stylesLock;
		cs::Ref<const FactionStyleBook> styles;
	}

	cs::Ref<ShipDesign> ShipDesigns::For(const cs::Ref<Faction>& faction, ShipClass cls, int tier)
	{
		auto d = cs::Ref<ShipDesign>(new ShipDesign());
		d->Class = cls;
		d->Tier = tier;
		const cs::String factionName = faction != nullptr ? faction->Name : cs::String();
		// FNV-1a of (faction name, '\0', class, tier): stable across runs and platforms, no cs::Random draw.
		uint64_t h = FnvBytes(FnvOffset, factionName.data(), factionName.size());
		h = FnvBytes(h, "", 1);
		const int32_t c = static_cast<int32_t>(cls), t = tier;
		h = FnvBytes(h, &c, sizeof c);
		h = FnvBytes(h, &t, sizeof t);
		d->Seed = h;
		constexpr size_t words = sizeof DesignWords / sizeof DesignWords[0];
		// Tier 0 (no Shipyards research yet) is the Mk I pattern.
		d->Name = cs::Cat(DesignWords[(h >> 17) % words], " Mk ", Roman(tier < 0 ? 1 : tier + 1));
		auto book = Styles();
		if (book != nullptr && faction != nullptr)
		{
			const auto& profile = book->For(faction->Name, cs::String(ToString(faction->Kind)));
			if (!profile.Id.empty()) d->Style = profile.Id;
		}
		return d;
	}

	int ShipDesigns::TierOf(const World& world, const cs::Ref<Faction>& faction)
	{
		if (faction == nullptr || world.Polities == nullptr || world.Polities->Command == nullptr) return 0;
		return world.Polities->Command->Level(faction, ResearchTrack::Shipyards());
	}

	cs::Ref<ShipDesign> ShipDesigns::For(const World& world, const cs::Ref<Faction>& faction, ShipClass cls)
	{
		return For(faction, cls, TierOf(world, faction));
	}

	void ShipDesigns::UseStyles(cs::Ref<const FactionStyleBook> book)
	{
		std::lock_guard<std::mutex> hold(stylesLock);
		styles = std::move(book);
	}

	cs::Ref<const FactionStyleBook> ShipDesigns::Styles()
	{
		std::lock_guard<std::mutex> hold(stylesLock);
		return styles;
	}

	cs::String ShipDesigns::Roman(int n)
	{
		if (n <= 0 || n >= 4000) return cs::String();
		static const int values[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
		static const char* digits[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
		cs::String s;
		for (int i = 0; i < 13; i++)
			while (n >= values[i]) { s += digits[i]; n -= values[i]; }
		return s;
	}

	SG_SERIALIZE(ShipDesign, "SpaceGod.Core.ShipDesign",
		SG_F(Name), SG_F(Class), SG_F(Seed), SG_F(Style), SG_F(Tier))
}
