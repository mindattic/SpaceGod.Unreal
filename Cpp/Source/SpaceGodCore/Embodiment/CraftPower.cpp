// US-T94: power distribution on whatever the player embodies (see CraftPower.h).

#include "Embodiment/CraftPower.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightModels.h"
#include "Combat/Outfitting.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"

namespace SpaceGod::Core
{
	namespace
	{
		/// A unit's reactor: a small craft's own, a hull's or a station's otherwise.
		cs::Ref<PowerDistribution>* SlotOfUnit(const cs::Ref<BattleUnit>& u)
		{
			if (u == nullptr) return nullptr;
			if (u->Kind == UnitKind::Fighter) return &u->Power;
			return u->Combat != nullptr ? &u->Combat->Reactor : nullptr;
		}

		/// Where <paramref name="seat"/>'s split is kept, or null when it shares no reactor.
		cs::Ref<PowerDistribution>* SlotOf(const cs::Ref<IEmbodiable>& seat)
		{
			if (auto f = cs::As<FighterSeat>(seat)) return SlotOfUnit(f->Unit);
			if (auto h = cs::As<ShipHelm>(seat)) return h->Ship != nullptr && h->Ship->Combat != nullptr ? &h->Ship->Combat->Reactor : nullptr;
			// A gun draws on the hull (or station) it is mounted on.
			if (auto t = cs::As<MountedTurret>(seat)) return SlotOfUnit(t->Unit);
			if (auto st = cs::As<StationSeat>(seat)) return st->Combat != nullptr ? &st->Combat->Reactor : nullptr;
			return nullptr;
		}

		/// The hull whose boost <paramref name="seat"/> draws on, or null.
		cs::Ref<ShipCombat> CombatOf(const cs::Ref<IEmbodiable>& seat)
		{
			if (auto f = cs::As<FighterSeat>(seat)) return f->Unit != nullptr ? f->Unit->Combat : nullptr;
			if (auto h = cs::As<ShipHelm>(seat)) return h->Ship != nullptr ? h->Ship->Combat : nullptr;
			if (auto t = cs::As<MountedTurret>(seat)) return t->Unit != nullptr ? t->Unit->Combat : nullptr;
			if (auto st = cs::As<StationSeat>(seat)) return st->Combat;
			return nullptr;
		}
	}

	cs::Ref<PowerDistribution> CraftPower::Of(const cs::Ref<IEmbodiable>& seat, bool create)
	{
		auto slot = SlotOf(seat);
		if (slot == nullptr) return nullptr;
		if (*slot == nullptr && create) *slot = cs::New<PowerDistribution>();
		return *slot;
	}

	bool CraftPower::Shares(const cs::Ref<IEmbodiable>& seat) { return SlotOf(seat) != nullptr; }

	int CraftPower::After(const cs::Ref<IEmbodiable>& seat, int key)
	{
		if (key < 0 || key > static_cast<int>(PowerKey::Balance)) return -1;
		auto slot = SlotOf(seat);
		if (slot == nullptr) return -1; // nothing here shares a reactor
		// Worked out on a copy: the craft changes only when the command lands (on every peer at once in co-op).
		PowerDistribution next = *slot != nullptr ? **slot : PowerDistribution();
		switch (static_cast<PowerKey>(key))
		{
			case PowerKey::Weapons: if (!next.Shift(PowerSystem::Weapons)) return -1; break;
			case PowerKey::Shields: if (!next.Shift(PowerSystem::Shields)) return -1; break;
			case PowerKey::Engines: if (!next.Shift(PowerSystem::Engines)) return -1; break;
			case PowerKey::AllWeapons: next.AllTo(PowerSystem::Weapons); break;
			case PowerKey::AllShields: next.AllTo(PowerSystem::Shields); break;
			case PowerKey::AllEngines: next.AllTo(PowerSystem::Engines); break;
			case PowerKey::Balance: next.Balance(); break;
			default: return -1;
		}
		return next.Code();
	}

	void CraftPower::Apply(const cs::Ref<IEmbodiable>& seat, int code)
	{
		if (code < 0) return;
		auto p = Of(seat, true);
		if (p != nullptr) p->SetCode(code);
	}

	PowerReadout CraftPower::Read(const cs::Ref<IEmbodiable>& seat)
	{
		PowerReadout r;
		auto slot = SlotOf(seat);
		if (slot == nullptr) return r;
		r.Shares = true;
		PowerDistribution even;
		const PowerDistribution& p = *slot != nullptr ? **slot : even;
		r.Weapons = p[PowerSystem::Weapons];
		r.Shields = p[PowerSystem::Shields];
		r.Engines = p[PowerSystem::Engines];
		r.Revision = p.Revision();
		r.WeaponsFactor = p.Factor(PowerSystem::Weapons);
		r.ShieldsFactor = p.Factor(PowerSystem::Shields);
		r.EnginesFactor = p.Factor(PowerSystem::Engines);
		r.TopSpeedFactor = BoostPool::TopSpeedFactor(r.EnginesFactor);
		r.ShieldCapacity = p.ShieldCapacity();
		r.Preset = p.PresetName();
		auto c = CombatOf(seat);
		if (c != nullptr) r.Boost = c->BoostFraction;
		if (auto f = cs::As<FighterSeat>(seat); f != nullptr && f->Unit != nullptr)
		{
			auto craft = f->Unit->Fighter != nullptr ? FighterCraft::Of(f->Unit->Fighter->Role) : FighterCraft::Interceptor();
			r.BoostPoolS = BoostPool::PoolSeconds(craft->Family());
			r.BoostRefillS = BoostPool::RefillSecondsAt(r.EnginesFactor);
		}
		else if (auto h = cs::As<ShipHelm>(seat); h != nullptr && h->Ship != nullptr && Loadout::IsSmallCraft(h->Ship->Spec->Class))
		{
			r.BoostPoolS = BoostPool::PoolSeconds(FlightModelRegistry::Default()->FamilyOf(h->Ship->Spec->Class));
			r.BoostRefillS = BoostPool::RefillSecondsAt(r.EnginesFactor);
		}
		return r;
	}
}
