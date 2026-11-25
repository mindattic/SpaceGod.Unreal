// US-T812: the pilot's career and the missions the war offers them (see Combat/PilotCareer.h).

#include "Combat/PilotCareer.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/TacticalWriteBack.h"
#include "Combat/War.h"
#include "Events/EventDirector.h"
#include "Events/Opportunity.h"
#include "Law/Law.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		constexpr const char* CareerOperationWords[] = {
			"Hammerfall", "Lantern", "Iron Veil", "Nightjar", "Cold Harbor", "Sabre", "Long Watch", "Ember",
			"Black Tide", "Kestrel", "Bulwark", "Far Light", "Thunderhead", "Silent Oath", "Gauntlet", "Meridian",
		};

		/// The merit at which each rank is reached (RankCount entries, the first 0).
		constexpr double CareerRankMerit[] = { 0, 20, 50, 100, 180, 300, 500, 800 };

		constexpr const char* CareerRankNames[] = {
			"Ensign", "Lieutenant (j.g.)", "Lieutenant", "Lieutenant Commander", "Commander", "Captain", "Commodore", "Rear Admiral",
		};

		cs::String CareerName(const cs::Ref<Faction>& f, const char* none)
		{
			return f != nullptr && !f->Name.empty() ? f->Name : cs::String(none);
		}

		cs::String CareerShipName(const cs::Ref<Ship>& s)
		{
			return s != nullptr && !s->Name.empty() ? s->Name : cs::String("the target");
		}

		bool CareerAlive(const cs::Ref<BattleUnit>& u)
		{
			return !u->Dead && !u->Captured && (u->Combat == nullptr || !u->Combat->Disabled());
		}

		/// A station's side in a fight under way (its first standing platform, in unit order), or -1.
		int CareerStationSide(const cs::Ref<Battle>& b)
		{
			for (const auto& u : b->Units)
				if (u->Kind == UnitKind::Platform && u->Platform != nullptr && CareerAlive(u) && u->Side >= 0 && u->Side <= 1) return u->Side;
			return -1;
		}

		/// The first standing capital in a fight under way, in unit order, or null.
		cs::Ref<BattleUnit> CareerCapital(const cs::Ref<Battle>& b)
		{
			for (const auto& u : b->Units)
				if (u->Kind == UnitKind::Ship && u->Ship != nullptr && u->Side >= 0 && u->Side <= 1 && CareerAlive(u) && WeakPoints::IsCapital(u)) return u;
			return nullptr;
		}
	}

	const char* ToString(MissionKind v)
	{
		switch (v)
		{
		case MissionKind::EscortConvoy: return "EscortConvoy";
		case MissionKind::InterceptRaid: return "InterceptRaid";
		case MissionKind::StrikeCapital: return "StrikeCapital";
		case MissionKind::DefendStation: return "DefendStation";
		}
		return "?";
	}

	const char* ToString(MissionState v)
	{
		switch (v)
		{
		case MissionState::Offered: return "Offered";
		case MissionState::Accepted: return "Accepted";
		case MissionState::Succeeded: return "Succeeded";
		case MissionState::Failed: return "Failed";
		}
		return "?";
	}

	cs::Ref<PilotCareer> PilotCareer::Find(const cs::Ref<World>& world)
	{
		return world != nullptr && world->War != nullptr ? world->War->Career : nullptr;
	}

	cs::Ref<PilotCareer> PilotCareer::Of(const cs::Ref<World>& world)
	{
		if (world == nullptr || world->War == nullptr) return nullptr;
		auto& c = world->War->Career;
		if (c == nullptr) c = cs::New<PilotCareer>();
		return c;
	}

	const char* PilotCareer::RankName(int rank)
	{
		return CareerRankNames[cs::Math::Max(0, cs::Math::Min(RankCount - 1, rank))];
	}

	int PilotCareer::RankAt(double merit)
	{
		int rank = 0;
		for (int i = 1; i < RankCount; i++)
			if (merit >= CareerRankMerit[i]) rank = i;
		return rank;
	}

	cs::Ref<Mission> PilotCareer::OpenMission(int id) const
	{
		for (const auto& m : Missions)
			if (m->Id == id) return m;
		return nullptr;
	}

	cs::Ref<Mission> PilotCareer::AcceptedMission() const
	{
		for (const auto& m : Missions)
			if (m->State == MissionState::Accepted) return m;
		return nullptr;
	}

	bool PilotCareer::Accept(const cs::Ref<Mission>& m)
	{
		if (m == nullptr || !m->IsOpen() || !Missions.Contains(m)) return false;
		for (const auto& o : Missions)
			if (o->State == MissionState::Accepted) o->State = MissionState::Offered;
		m->State = MissionState::Accepted;
		return true;
	}

	bool PilotCareer::Known(const cs::String& key) const
	{
		for (const auto& m : Missions)
			if (m->SourceKey == key) return true;
		for (const auto& m : History)
			if (m->SourceKey == key) return true;
		return false;
	}

	cs::Ref<Mission> PilotCareer::Add(MissionKind kind, const cs::String& key, double jd, double expiresJd)
	{
		auto m = cs::New<Mission>();
		m->Id = NextId++;
		m->Kind = kind;
		m->SourceKey = key;
		m->OfferedJd = jd;
		m->ExpiresJd = expiresJd;
		constexpr unsigned words = static_cast<unsigned>(sizeof(CareerOperationWords) / sizeof(CareerOperationWords[0]));
		m->Title = cs::Cat("Operation ", CareerOperationWords[static_cast<unsigned>(m->Id * 7 + static_cast<int>(kind)) % words]);
		Missions.Add(m);
		return m;
	}

	void PilotCareer::Update(const cs::Ref<World>& world, double jd)
	{
		if (world == nullptr || world->War == nullptr) return;
		auto existing = Find(world);
		if (existing != nullptr && jd < existing->NextScanJd) return;
		auto c = Of(world);
		c->NextScanJd = jd + ScanIntervalDays;

		// The taken-up mission's ship drawn into a fight: that fight is the one to fly.
		for (int i = c->Missions.Count() - 1; i >= 0; i--)
		{
			auto m = c->Missions[i];
			if (m->Battle == nullptr && m->Ship != nullptr && m->Ship->Battle != nullptr && !m->Ship->Battle->Over)
			{
				m->Battle = m->Ship->Battle;
				auto u = m->Battle->UnitOf(m->Ship);
				if (u != nullptr) m->Side = m->Kind == MissionKind::EscortConvoy ? u->Side : 1 - u->Side;
			}
			bool fightOver = m->Battle != nullptr && m->Battle->Over;
			bool opportunityOver = m->Opportunity != nullptr && !m->Opportunity->IsActive();
			if (m->State == MissionState::Accepted)
			{
				// Settled by the fight's booking (OnFlown) when flown; a fight over without one was never flown.
				if (fightOver) c->Settle(world, m, false, "The fight ended without you in it.", jd);
				else if (m->Battle == nullptr && opportunityOver)
				{
					bool done = m->Opportunity->State == OpportunityState::Completed;
					c->Settle(world, m, done, done ? cs::String("Seen through.") : cs::Cat("It ended without you: ", m->Opportunity->Outcome), jd);
				}
				continue;
			}
			if (fightOver || (m->Battle == nullptr && opportunityOver) || jd >= m->ExpiresJd || (m->Ship != nullptr && m->Ship->Destroyed))
				c->Missions.RemoveAt(i);
		}
		c->Offer(world, jd);
	}

	void PilotCareer::Offer(const cs::Ref<World>& world, double jd)
	{
		// Fights under way, in the war's order: a station to hold, else a capital to break.
		for (const auto& b : world->War->Battles)
		{
			if (Missions.Count() >= MaxOpen) return;
			if (b->Over) continue;
			auto key = cs::Cat("battle:", b->Id);
			if (Known(key)) continue;
			int station = CareerStationSide(b);
			if (station >= 0)
			{
				auto patron = b->SideLeaders[station];
				if (patron == nullptr) continue;
				auto m = Add(MissionKind::DefendStation, key, jd, jd + OfferDays);
				m->Patron = patron;
				m->Enemy = b->SideLeaders[1 - station];
				m->Battle = b;
				m->Side = station;
				m->Briefing = cs::Cat(m->Title, ": DEFEND STATION\n",
					"Command: ", CareerName(patron, "Station command"), "\n",
					"Situation: ", b->Name, " is under attack by ", CareerName(m->Enemy, "hostile forces"), ". The station's guns cannot hold alone.\n",
					"Objectives:\n  1. Launch into ", b->Name, " and engage the attackers.\n  2. Keep the station standing until its side holds the field.\n",
					"Debrief: success if ", CareerName(patron, "the defenders"), " hold the field.");
				continue;
			}
			auto capital = CareerCapital(b);
			if (capital == nullptr) continue;
			int side = 1 - capital->Side;
			auto patron = b->SideLeaders[side];
			if (patron == nullptr) continue;
			auto m = Add(MissionKind::StrikeCapital, key, jd, jd + OfferDays);
			m->Patron = patron;
			m->Enemy = b->SideLeaders[capital->Side];
			m->Ship = capital->Ship;
			m->Battle = b;
			m->Side = side;
			m->Briefing = cs::Cat(m->Title, ": STRIKE CAPITAL\n",
				"Command: ", CareerName(patron, "Fleet command"), "\n",
				"Situation: the ", CareerName(m->Enemy, "enemy"), " capital ", CareerShipName(m->Ship), " anchors their line in ", b->Name, ".\n",
				"Objectives:\n  1. Launch into ", b->Name, " and close on ", CareerShipName(m->Ship), ".\n  2. Destroy, take or disable an enemy capital.\n",
				"Debrief: success if at least one enemy capital is crippled.");
		}

		// The director's open opportunities, in its order: escorts and intercepts.
		auto director = world->Adventures;
		if (director == nullptr) return;
		for (const auto& op : director->Active)
		{
			if (Missions.Count() >= MaxOpen) return;
			bool escort = op->Kind == OpportunityKind::ConvoyEscort;
			if (!escort && op->Kind != OpportunityKind::PirateAmbush) continue;
			if (op->Ship == nullptr || op->Ship->Destroyed) continue;
			auto key = cs::Cat("opportunity:", op->Id);
			if (Known(key)) continue;
			auto m = Add(escort ? MissionKind::EscortConvoy : MissionKind::InterceptRaid, key, jd, op->ExpiresJd);
			m->Patron = op->Patron;
			m->Ship = op->Ship;
			m->Opportunity = op;
			if (escort)
			{
				m->Enemy = nullptr;
				m->Briefing = cs::Cat(m->Title, ": ESCORT CONVOY\n",
					"Command: ", CareerName(op->Patron, "The convoy's owners"), "\n",
					"Situation: ", CareerShipName(op->Ship), " is riding a lane with a record of raids. ", op->Title, ".\n",
					"Objectives:\n  1. Join ", CareerShipName(op->Ship), " and ride with it to the far ramp.\n  2. If it is set upon, bring it through the fight.\n",
					"Debrief: success if ", CareerShipName(op->Ship), " arrives, or comes through its fight.");
			}
			else
			{
				m->Enemy = op->Ship->Faction;
				m->Briefing = cs::Cat(m->Title, ": INTERCEPT RAID\n",
					"Command: ", CareerName(op->Patron, "The lane patrol"), "\n",
					"Situation: raiders led by ", CareerShipName(op->Ship), " are lying in wait off the lane. ", op->Title, ".\n",
					"Objectives:\n  1. Find the band before it strikes.\n  2. Break it: hold the field when the shooting stops.\n",
					"Debrief: success if the raiders are driven off or destroyed.");
			}
		}
	}

	void PilotCareer::Award(const cs::String& name, const cs::String& citation, double jd, int battleId, cs::String& notes)
	{
		for (const auto& m : Medals)
			if (m->Name == name) return;
		auto medal = cs::New<PilotMedal>();
		medal->Name = name;
		medal->Citation = citation;
		medal->Jd = jd;
		medal->BattleId = battleId;
		Medals.Add(medal);
		notes += cs::Cat("Medal awarded: ", name, " - ", citation, "\n");
	}

	void PilotCareer::Settle(const cs::Ref<World>& world, const cs::Ref<Mission>& m, bool success, const cs::String& why, double jd, cs::String notes)
	{
		m->State = success ? MissionState::Succeeded : MissionState::Failed;
		m->ResolvedJd = jd;
		if (success)
		{
			MissionsWon++;
			Merit += MissionMerit;
			if (world->Law != nullptr && m->Patron != nullptr) world->Law->Credit(world->PilotedShip, m->Patron, MissionStanding);
			if (MissionsWon >= 5) Award("Distinguished Service Medal", "five missions flown to success", jd, m->Battle != nullptr ? m->Battle->Id : 0, notes);
		}
		else MissionsFailed++;
		int rank = RankAt(Merit);
		if (rank > Rank)
		{
			Rank = rank;
			notes += cs::Cat("Promoted to ", RankName(Rank), ".\n");
		}

		auto r = m->Contribution;
		m->Debriefing = cs::Cat("DEBRIEFING: ", m->Title, "\n", "Pilot: ", RankName(Rank), " \"", Callsign, "\"\n",
			"Result: ", success ? "MISSION ACCOMPLISHED" : "MISSION FAILED", ". ", why, "\n");
		if (r != nullptr)
			m->Debriefing += cs::Cat("Engagement: ", r->Name, r->Won ? " (field held)" : r->Lost ? " (field lost)" : " (no victor)", "\n",
				"Kills: ", r->Kills, " hulls, ", r->CraftKills, " craft. Capitals crippled: ", r->CapitalsCrippled, ". Carriers brought through: ",
				r->CarriersSaved, "\n", "Losses: ", r->Losses, " hulls, ", r->CraftLost, " craft\n");
		m->Debriefing += notes;

		Missions.Remove(m);
		History.Add(m);
		if (History.Count() > HistoryCap) History.RemoveAt(0);
		DebriefPending = true;
	}

	void PilotCareer::OnFlown(const cs::Ref<World>& world, const cs::Ref<Battle>& b, const cs::Ref<BattleContribution>& r, double jd)
	{
		if (world == nullptr || b == nullptr || r == nullptr) return;
		auto c = Of(world);
		if (c == nullptr) return;
		c->Sorties++;
		if (r->Won) c->Victories++;
		if (r->Lost) c->Defeats++;
		c->Kills += r->Kills;
		c->CraftKills += r->CraftKills;
		c->CapitalsCrippled += r->CapitalsCrippled;
		c->CarriersSaved += r->CarriersSaved;
		c->CraftLost += r->CraftLost;
		c->Merit += cs::Math::Max(0.0, r->StandingFor);

		cs::String notes;
		c->Award("Combat Wings", "first sortie flown", jd, b->Id, notes);
		if (c->Kills + c->CraftKills >= 5) c->Award("Ace's Star", "five kills", jd, b->Id, notes);
		if (r->CapitalsCrippled > 0) c->Award("Capital Strike Cross", cs::Cat("an enemy capital crippled in ", r->Name), jd, b->Id, notes);
		if (r->CarriersSaved > 0 && r->Won) c->Award("Carrier Shield", cs::Cat("carriers brought through ", r->Name), jd, b->Id, notes);
		if (r->Won && r->Kills >= 3) c->Award("Medal of Valor", cs::Cat(r->Kills, " hulls destroyed in ", r->Name), jd, b->Id, notes);

		// The mission this fight settles: the one taken up whose fight it was (or whose ship fought in it).
		cs::Ref<Mission> m;
		for (const auto& o : c->Missions)
			if (o->State == MissionState::Accepted && (o->Battle == b || (o->Ship != nullptr && b->UnitOf(o->Ship) != nullptr)))
			{
				m = o;
				break;
			}
		if (m == nullptr)
		{
			int rank = RankAt(c->Merit);
			if (rank > c->Rank)
			{
				c->Rank = rank;
				notes += cs::Cat("Promoted to ", RankName(c->Rank), ".\n");
			}
			if (!notes.empty())
			{
				// A fight flown off the books still has its debriefing when it earned something.
				auto sortie = cs::New<Mission>();
				sortie->Id = c->NextId++;
				sortie->State = r->Lost ? MissionState::Failed : MissionState::Succeeded;
				sortie->SourceKey = cs::Cat("battle:", b->Id);
				sortie->Title = cs::Cat("Sortie: ", r->Name);
				sortie->Battle = b;
				sortie->Side = r->Side;
				sortie->Contribution = r;
				sortie->OfferedJd = sortie->ResolvedJd = jd;
				sortie->Debriefing = cs::Cat("DEBRIEFING: ", sortie->Title, "\n", "Pilot: ", RankName(c->Rank), " \"", c->Callsign, "\"\n",
					"Kills: ", r->Kills, " hulls, ", r->CraftKills, " craft. Capitals crippled: ", r->CapitalsCrippled, "\n", notes);
				c->History.Add(sortie);
				if (c->History.Count() > HistoryCap) c->History.RemoveAt(0);
				c->DebriefPending = true;
			}
			return;
		}

		m->Battle = b;
		m->Contribution = r;
		bool shipSafe = m->Ship == nullptr || (!m->Ship->Destroyed && b->UnitOf(m->Ship) != nullptr && !b->UnitOf(m->Ship)->Captured);
		bool success = false;
		cs::String why;
		switch (m->Kind)
		{
		case MissionKind::EscortConvoy:
			success = shipSafe && !r->Lost;
			why = success ? cs::Cat(CareerShipName(m->Ship), " came through.") : cs::Cat(CareerShipName(m->Ship), " was lost.");
			break;
		case MissionKind::InterceptRaid:
			success = r->Won;
			why = success ? cs::String("The raiders were broken.") : cs::String("The raiders held.");
			break;
		case MissionKind::StrikeCapital:
			success = r->CapitalsCrippled > 0;
			why = success ? cs::String("An enemy capital was crippled.") : cs::String("No enemy capital was crippled.");
			break;
		case MissionKind::DefendStation:
			success = r->Won;
			why = success ? cs::String("The station's side held the field.") : cs::String("The station's side did not hold the field.");
			break;
		}
		// Medals earned in the fight are read in the mission's debriefing, before its own.
		c->Settle(world, m, success, why, jd, notes);
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(MissionKind, "SpaceGod.Core.MissionKind")
	SG_ENUM(MissionState, "SpaceGod.Core.MissionState")

	SG_SERIALIZE(Mission, "SpaceGod.Core.Mission",
		SG_F(Id), SG_F(Kind), SG_F(State), SG_F(SourceKey), SG_F(Title), SG_F(Briefing), SG_F(Debriefing),
		SG_F(OfferedJd), SG_F(ExpiresJd), SG_F(ResolvedJd), SG_F(Patron), SG_F(Enemy), SG_F(Ship), SG_F(Battle), SG_F(Side),
		SG_F(Opportunity), SG_F(Contribution))

	SG_SERIALIZE(PilotMedal, "SpaceGod.Core.PilotMedal",
		SG_F(Name), SG_F(Citation), SG_F(Jd), SG_F(BattleId))

	SG_SERIALIZE(PilotCareer, "SpaceGod.Core.PilotCareer",
		SG_F(Callsign), SG_F(Rank), SG_F(Merit), SG_F(Sorties), SG_F(Victories), SG_F(Defeats), SG_F(Kills), SG_F(CraftKills),
		SG_F(CapitalsCrippled), SG_F(CarriersSaved), SG_F(CraftLost), SG_F(MissionsWon), SG_F(MissionsFailed), SG_F(Medals),
		SG_F(Missions), SG_F(History), SG_F(NextId), SG_F(NextScanJd))
}
