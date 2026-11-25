#pragma once
// US-T812: the pilot's career and the missions the war offers them. C++-only (no Unity.SpaceGod counterpart).
//
// The pilot is one person across every hull they fly: a callsign, a rank earned by merit, a tally of kills and a row of
// medals. Their tally comes only from the fights they flew in, as the tactical write-back books them (US-T809,
// TacticalWriteBack::OnResolved hands each BattleContribution here), so a fight nobody flew changes nothing.
//
// Missions are not scripted: each is something the A-life is already doing, offered as a sortie with a briefing. A merchant
// asking for an escort down a dangerous lane (EventDirector's ConvoyEscort) is an escort; raiders lying in wait off a lane
// (PirateAmbush) an intercept; a fight under way with a station on one side a defence of it, and one with a capital on one
// side a strike on that capital. Taken up, a mission is flown through "Fly this" (UnitySpaceApp::AcceptMission seats the
// player through TakeSeat), and it is judged by what the fight booked: the debriefing reads the write-back's record.
//
// Offered from World::UpdateAdventures, the player-present path (recorded for co-op and rewind like the adventures), every
// ScanIntervalDays, from the war's battles and the director's open opportunities in list order, with no random draws:
// every replay and lockstep peer offers the same. The AI-only bench never runs it, so War::Career stays null there.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleContribution;
	class Faction;
	class Opportunity;
	class Ship;
	class World;

	/// <summary>What a <see cref="Mission"/> asks of the pilot.</summary>
	enum class MissionKind : int
	{
		/// <summary>See a merchant down a dangerous lane, and bring it through any fight on the way.</summary>
		EscortConvoy,

		/// <summary>Break up a raider band lying in wait off a lane.</summary>
		InterceptRaid,

		/// <summary>Cripple an enemy capital in a fight under way: destroyed, taken or left disabled.</summary>
		StrikeCapital,

		/// <summary>Hold a fight under way at a station until the station's side holds the field.</summary>
		DefendStation,
	};
	SPACEGODCORE_API const char* ToString(MissionKind v);

	/// <summary>Where a <see cref="Mission"/> is in its life.</summary>
	enum class MissionState : int
	{
		/// <summary>On the board, briefed, waiting for the pilot.</summary>
		Offered,

		/// <summary>Taken up: the pilot is flying it.</summary>
		Accepted,

		/// <summary>Flown and won.</summary>
		Succeeded,

		/// <summary>Flown and lost, or taken up and never flown before the situation ended.</summary>
		Failed,
	};
	SPACEGODCORE_API const char* ToString(MissionState v);

	/// <summary>One sortie made from something the A-life is doing (US-T812).</summary>
	class SPACEGODCORE_API Mission final
	{
	public:
		int Id = 0;
		MissionKind Kind = MissionKind::EscortConvoy;
		MissionState State = MissionState::Offered;

		/// <summary>The thing it was made from ("battle:12", "opportunity:4"): never offered twice.</summary>
		cs::String SourceKey;

		/// <summary>The operation's name ("Operation Hammerfall"), and the briefing read before launch.</summary>
		cs::String Title;
		cs::String Briefing;

		/// <summary>What was said after: the result, the tally, any medal or promotion. Empty until settled.</summary>
		cs::String Debriefing;

		double OfferedJd = 0, ExpiresJd = 0, ResolvedJd = 0;

		/// <summary>Who sends the pilot (and whose regard a success earns), and who they fly against (null for raiders).</summary>
		cs::Ref<Faction> Patron, Enemy;

		/// <summary>The ship it is about: the merchant to escort, the lead raider, the capital to strike (null for a station).</summary>
		cs::Ref<SpaceGod::Core::Ship> Ship;

		/// <summary>The fight to fly into: the one it was made from, or the one its ship is drawn into later.</summary>
		cs::Ref<SpaceGod::Core::Battle> Battle;

		/// <summary>The side of <see cref="Battle"/> the pilot flies for (-1 until a fight is known).</summary>
		int Side = -1;

		/// <summary>The opportunity it was made from (an escort or an intercept), whose outcome settles it if no fight does.</summary>
		cs::Ref<SpaceGod::Core::Opportunity> Opportunity;

		/// <summary>What the fight booked for the pilot (<see cref="TacticalWriteBack"/>), once flown.</summary>
		cs::Ref<BattleContribution> Contribution;

		bool IsOpen() const { return State == MissionState::Offered || State == MissionState::Accepted; }
	};

	/// <summary>A medal pinned on the pilot (US-T812).</summary>
	class SPACEGODCORE_API PilotMedal final
	{
	public:
		cs::String Name;
		cs::String Citation;
		double Jd = 0;
		int BattleId = 0;
	};

	/// <summary>
	/// The pilot's career (US-T812), held on <see cref="War.Career"/>: made the first time a player is present
	/// (<see cref="World.UpdateAdventures"/>) or flies a fight, so the AI-only bench (and an older save) has none.
	/// </summary>
	class SPACEGODCORE_API PilotCareer final
	{
	public:
		cs::String Callsign = "Alpha One";

		/// <summary>Index into <see cref="RankName"/>, earned by <see cref="Merit"/> (<see cref="RankAt"/>).</summary>
		int Rank = 0;
		double Merit = 0;

		/// <summary>Fights flown, won and lost.</summary>
		int Sorties = 0, Victories = 0, Defeats = 0;

		/// <summary>Enemy hulls and craft destroyed, capitals crippled, own carriers brought through, own craft lost.</summary>
		int Kills = 0, CraftKills = 0, CapitalsCrippled = 0, CarriersSaved = 0, CraftLost = 0;

		/// <summary>Missions flown to success and to failure.</summary>
		int MissionsWon = 0, MissionsFailed = 0;

		cs::List<cs::Ref<PilotMedal>> Medals;

		/// <summary>The board: open missions (offered or accepted), oldest first, at most <see cref="MaxOpen"/>.</summary>
		cs::List<cs::Ref<Mission>> Missions;

		/// <summary>Settled missions, oldest first, at most <see cref="HistoryCap"/>; the newest's debriefing is the one shown.</summary>
		cs::List<cs::Ref<Mission>> History;

		/// <summary>The newest debriefing not yet read: the HUD opens the mission board on it and clears this. Not saved (the
		/// HUD's alone, so a peer reading it never moves the world's state).</summary>
		bool DebriefPending = false;

		int NextId = 1;
		double NextScanJd = 0;

		static constexpr int MaxOpen = 4;
		static constexpr int HistoryCap = 30;
		static constexpr int RankCount = 8;

		/// <summary>How often the board is refreshed from the war and the opportunities, days (as the director's scans).</summary>
		static constexpr double ScanIntervalDays = 0.002;

		/// <summary>How long an offered mission made from a fight stays on the board if the fight outlasts it, days.</summary>
		static constexpr double OfferDays = 0.5;

		/// <summary>Merit a mission flown to success earns on top of the fight's own (<see cref="BattleContribution.StandingFor"/>).</summary>
		static constexpr double MissionMerit = 10;

		/// <summary>Standing with the patron a mission flown to success earns (<see cref="Law.Credit"/>).</summary>
		static constexpr double MissionStanding = 3;

		/// <summary>The career if it exists, never making it.</summary>
		static cs::Ref<PilotCareer> Find(const cs::Ref<World>& world);

		/// <summary>The career, made if the world has a war and none yet.</summary>
		static cs::Ref<PilotCareer> Of(const cs::Ref<World>& world);

		/// <summary>"Ensign" ... "Rear Admiral".</summary>
		static const char* RankName(int rank);

		/// <summary>The rank <paramref name="merit"/> earns.</summary>
		static int RankAt(double merit);

		/// <summary>The mission numbered <paramref name="id"/> on the board, or null.</summary>
		cs::Ref<Mission> OpenMission(int id) const;

		/// <summary>The mission taken up, or null.</summary>
		cs::Ref<Mission> AcceptedMission() const;

		/// <summary>
		/// Refreshes the board at <paramref name="jd"/> (every <see cref="ScanIntervalDays"/>): settles taken-up missions whose
		/// situation ended without a fight to judge them, drops offers that lapsed, and offers new ones from the war's fights and
		/// the director's opportunities. Called by <see cref="World.UpdateAdventures"/> only, so only with a player present.
		/// </summary>
		static void Update(const cs::Ref<World>& world, double jd);

		/// <summary>Takes up mission <paramref name="m"/> (any other taken up goes back on the board). False if it is not open.</summary>
		bool Accept(const cs::Ref<Mission>& m);

		/// <summary>
		/// A fight the player flew in was booked (<see cref="TacticalWriteBack.OnResolved"/>): the tally, any mission it
		/// settles (with its debriefing), medals and rank. Deterministic, no random draws.
		/// </summary>
		static void OnFlown(const cs::Ref<World>& world, const cs::Ref<Battle>& b, const cs::Ref<BattleContribution>& r, double jd);

	private:
		void Offer(const cs::Ref<World>& world, double jd);
		bool Known(const cs::String& key) const;
		cs::Ref<Mission> Add(MissionKind kind, const cs::String& key, double jd, double expiresJd);
		void Settle(const cs::Ref<World>& world, const cs::Ref<Mission>& m, bool success, const cs::String& why, double jd, cs::String notes = cs::String());
		void Award(const cs::String& name, const cs::String& citation, double jd, int battleId, cs::String& notes);
	};
}
