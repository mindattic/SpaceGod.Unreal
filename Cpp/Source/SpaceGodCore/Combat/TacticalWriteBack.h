#pragma once
// US-T809: the battles the player took part in write back to the strategic sim. C++-only (no Unity.SpaceGod counterpart).
//
// A fight's hulls are already struck off where they die (War::OnUnitDestroyed, the faction's losses), its dead crews
// already wear down their faction's morale (Crews), and its captures go to their captors: that is the fleet strength the
// war sees, the same for every fight. What a fight the player flew in adds is the record of what the player's side did
// (kills, losses, enemy capitals crippled, carriers brought through) and what it is worth to the powers involved: the
// crews of the player's side take heart from a won fight, saved carriers and broken capitals; the enemy's crews lose it
// over capitals broken but not destroyed (the dead are Crews' already); and the pilot's standing rises with the side they
// flew for and falls with the side they fought.
//
// Booked once, as War::Resolve finishes with the battle (every unit's fate final), from the battle's own state in unit
// order, with no random draws: every replay and lockstep peer books the same. A fight nobody flew (every Bench fight)
// books nothing and makes no book: War::WriteBack stays null.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Battle;
	class Faction;
	class World;

	/// <summary>One fight the player flew in (US-T809): who they flew for, what their side did, and what it was worth.</summary>
	class SPACEGODCORE_API BattleContribution final
	{
	public:
		int BattleId = 0;
		cs::String Name;
		double Jd = 0;

		/// <summary>The side the player flew on, and the two sides' leaders.</summary>
		int Side = 0;
		cs::Ref<Faction> For, Against;

		/// <summary>The player's side held the field (<see cref="Battle.Winner"/>), or the enemy did.</summary>
		bool Won = false, Lost = false;

		/// <summary>Enemy hulls destroyed, and enemy craft shot down.</summary>
		int Kills = 0, CraftKills = 0;

		/// <summary>The side's own hulls lost, and its craft lost.</summary>
		int Losses = 0, CraftLost = 0;

		/// <summary>Enemy capitals destroyed, taken or left disabled.</summary>
		int CapitalsCrippled = 0;

		/// <summary>The side's own carriers that came through, neither destroyed nor taken.</summary>
		int CarriersSaved = 0;

		/// <summary>What it did to the two leaders' <see cref="Faction.CrewMorale"/>, and to the pilot's standing with them.</summary>
		double MoraleFor = 0, MoraleAgainst = 0, StandingFor = 0, StandingAgainst = 0;
	};

	/// <summary>
	/// The book of the fights the player flew in (US-T809), held on <see cref="War.WriteBack"/>: made the first time one
	/// ends, so a world the player never fought in (and an older save) has none.
	/// </summary>
	class SPACEGODCORE_API TacticalWriteBack final
	{
	public:
		/// <summary>The newest fights booked, oldest first, at most <see cref="HistoryCap"/>.</summary>
		cs::List<cs::Ref<BattleContribution>> Records;

		static constexpr int HistoryCap = 30;

		/// <summary>Crew morale the player's side gains for a won fight, and loses for a lost one.</summary>
		static constexpr double WinMorale = 0.03;
		static constexpr double LossMorale = 0.02;

		/// <summary>Crew morale the player's side gains for each carrier brought through, and each enemy capital crippled.</summary>
		static constexpr double CarrierMorale = 0.01;
		static constexpr double CapitalMorale = 0.01;

		/// <summary>Crew morale the enemy loses for each of its capitals crippled but not destroyed (its dead are <see cref="Crews"/>').</summary>
		static constexpr double BrokenCapitalMorale = 0.01;

		/// <summary>Standing the pilot earns with the side they flew for: a won fight, each enemy hull, craft and capital, each carrier saved.</summary>
		static constexpr double WinStanding = 2;
		static constexpr double KillStanding = 0.5;
		static constexpr double CraftKillStanding = 0.1;
		static constexpr double CapitalStanding = 1.5;
		static constexpr double CarrierStanding = 1;

		/// <summary>Standing the pilot loses with the side they fought, per hull of its destroyed and capital of its crippled.</summary>
		static constexpr double KillEnmity = 0.5;
		static constexpr double CapitalEnmity = 1;

		/// <summary>The book if it exists, never making it.</summary>
		static cs::Ref<TacticalWriteBack> Find(const cs::Ref<World>& world);

		/// <summary>The fight numbered <paramref name="battleId"/> as booked, or null if the player did not fly in it (or it has aged out).</summary>
		static cs::Ref<BattleContribution> Of(const cs::Ref<World>& world, int battleId);

		/// <summary>
		/// Books <paramref name="b"/> if a unit in it was flown by the player (<see cref="BattleUnit.PlayerFlown"/>) and
		/// writes its worth back: crew morale of both sides' leaders (within <see cref="Crews.MinMorale"/> and
		/// <see cref="Crews.MaxMorale"/>) and the pilot's standing with them (<see cref="Law.Credit"/>; none with a pirate
		/// clan). Nothing at all for a fight the player had no part in. Called by <see cref="War.Resolve"/>.
		/// </summary>
		static cs::Ref<BattleContribution> OnResolved(const cs::Ref<World>& world, const cs::Ref<Battle>& b, double jd);
	};
}
