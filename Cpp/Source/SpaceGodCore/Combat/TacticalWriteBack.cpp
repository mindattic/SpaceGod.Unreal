// US-T809: the battles the player took part in write back to the strategic sim (see Combat/TacticalWriteBack.h).

#include "Combat/TacticalWriteBack.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Dogfighting.h"
#include "Combat/War.h"
#include "Law/Law.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		void BookMorale(const cs::Ref<Faction>& f, double delta)
		{
			if (f == nullptr || delta == 0) return;
			f->CrewMorale = cs::Math::Max(Crews::MinMorale, cs::Math::Min(Crews::MaxMorale, f->CrewMorale + delta));
		}

		bool BookedCarrier(const cs::Ref<BattleUnit>& u)
		{
			return u->Kind == UnitKind::Ship && u->Combat != nullptr && u->Combat->Spec != nullptr && u->Combat->Spec->Squadrons > 0;
		}
	}

	cs::Ref<TacticalWriteBack> TacticalWriteBack::Find(const cs::Ref<World>& world)
	{
		return world != nullptr && world->War != nullptr ? world->War->WriteBack : nullptr;
	}

	cs::Ref<BattleContribution> TacticalWriteBack::Of(const cs::Ref<World>& world, int battleId)
	{
		auto book = Find(world);
		if (book == nullptr) return nullptr;
		for (const auto& r : book->Records)
			if (r->BattleId == battleId) return r;
		return nullptr;
	}

	cs::Ref<BattleContribution> TacticalWriteBack::OnResolved(const cs::Ref<World>& world, const cs::Ref<Battle>& b, double jd)
	{
		if (world == nullptr || world->War == nullptr || b == nullptr) return nullptr;
		// The first hull the player flew, in unit order: the side they flew on and whose pilot earns the standing.
		cs::Ref<BattleUnit> flown;
		for (const auto& u : b->Units)
			if (u->PlayerFlown)
			{
				flown = u;
				break;
			}
		if (flown == nullptr) return nullptr;
		int side = flown->FoughtOn();
		if (side < 0 || side > 1) return nullptr;

		auto r = cs::New<BattleContribution>();
		r->BattleId = b->Id;
		r->Name = b->Name;
		r->Jd = jd;
		r->Side = side;
		r->For = b->SideLeaders[side];
		r->Against = b->SideLeaders[1 - side];
		r->Won = b->Winner == side;
		r->Lost = b->Winner == 1 - side;
		int brokenAlive = 0;
		for (const auto& u : b->Units)
		{
			int fought = u->FoughtOn();
			if (fought < 0 || fought > 1) continue;
			bool fighter = u->Kind == UnitKind::Fighter;
			if (fought == side)
			{
				if (u->Dead) (fighter ? r->CraftLost : r->Losses)++;
				else if (!u->Captured && BookedCarrier(u)) r->CarriersSaved++;
				continue;
			}
			if (u->Dead) (fighter ? r->CraftKills : r->Kills)++;
			if (!WeakPoints::IsCapital(u)) continue;
			bool disabled = u->Combat != nullptr && u->Combat->Disabled();
			if (u->Dead || u->Captured || disabled)
			{
				r->CapitalsCrippled++;
				if (!u->Dead) brokenAlive++;
			}
		}

		r->MoraleFor = (r->Won ? WinMorale : 0) - (r->Lost ? LossMorale : 0) + r->CarriersSaved * CarrierMorale + r->CapitalsCrippled * CapitalMorale;
		r->MoraleAgainst = -brokenAlive * BrokenCapitalMorale;
		r->StandingFor = (r->Won ? WinStanding : 0) + r->Kills * KillStanding + r->CraftKills * CraftKillStanding
			+ r->CapitalsCrippled * CapitalStanding + r->CarriersSaved * CarrierStanding;
		r->StandingAgainst = -(r->Kills * KillEnmity + r->CapitalsCrippled * CapitalEnmity);
		if (r->For == r->Against) r->MoraleAgainst = r->StandingAgainst = 0;

		BookMorale(r->For, r->MoraleFor);
		BookMorale(r->Against, r->MoraleAgainst);
		if (world->Law != nullptr)
		{
			auto pilot = flown->Ship != nullptr ? flown->Ship : flown->CraftHull != nullptr ? flown->CraftHull : world->PilotedShip;
			world->Law->Credit(pilot, r->For, r->StandingFor);
			if (r->Against != r->For) world->Law->Credit(pilot, r->Against, r->StandingAgainst);
		}

		auto& book = world->War->WriteBack;
		if (book == nullptr) book = cs::New<TacticalWriteBack>();
		book->Records.Add(r);
		if (book->Records.Count() > HistoryCap) book->Records.RemoveAt(0);
		return r;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(BattleContribution, "SpaceGod.Core.BattleContribution",
		SG_F(BattleId), SG_F(Name), SG_F(Jd), SG_F(Side), SG_F(For), SG_F(Against), SG_F(Won), SG_F(Lost),
		SG_F(Kills), SG_F(CraftKills), SG_F(Losses), SG_F(CraftLost), SG_F(CapitalsCrippled), SG_F(CarriersSaved),
		SG_F(MoraleFor), SG_F(MoraleAgainst), SG_F(StandingFor), SG_F(StandingAgainst))

	SG_SERIALIZE(TacticalWriteBack, "SpaceGod.Core.TacticalWriteBack",
		SG_F(Records))
}
