#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Subsystem;
	class ShipCombat;
	class FlightDeck;
	class FighterControl;
	class RangeSetting;
	class ToggleSetting;
	class LodClass;
	enum class FighterPhase : int;

	/// <summary>Where a craft is in its visit to a flight deck (U.SPC-T518, see <see cref="FlightDecks"/>).</summary>
	enum class DeckStage : int
	{
		/// <summary>Flying to the deck's approach gate astern of the stern mouth, or holding off it until the deck is clear.</summary>
		Inbound,

		/// <summary>Cleared to land: on the glide path down the deck's axis, guided in through the stern mouth to its spot.</summary>
		Final,

		/// <summary>Down on the deck, inside the hull: refuelling, rearming and being repaired.</summary>
		OnDeck,

		/// <summary>Thrown down the deck and out of the bow mouth, back into the fight.</summary>
		Launching,
	};
	SPACEGODCORE_API const char* ToString(DeckStage v);

	/// <summary>A craft's visit to one flight deck: which, how far along it is, and how far its service has got.</summary>
	class DeckVisit final
	{
	public:
		cs::Ref<FlightDeck> Deck;
		DeckStage Stage = DeckStage::Inbound;

		/// <summary>Where it is along the deck's axis from the deck's middle, km (negative astern, toward the stern mouth).</summary>
		double Along = 0;

		/// <summary>Where it is off the deck's axis, km in the carrier's frame (X right, Y up; Z unused).</summary>
		DVec3 Offset;

		/// <summary>Where along the deck it puts down (km from the middle): its parking spot, given when it is cleared.</summary>
		double SpotAlong = 0;

		/// <summary>How far its turnaround has got, 0-1 (<see cref="FlightDecks.ServiceSeconds"/> for the whole of it).</summary>
		double Service = 0;

		/// <summary>Its speed down the deck while launching, km/s relative to the carrier.</summary>
		double Speed = 0;

		/// <summary>The phase it flies on in once it is clear of the bow mouth.</summary>
		FighterPhase After = static_cast<FighterPhase>(0); // FighterPhase.Launch

		/// <summary>Seconds it has sat on the deck home for good, until it is struck down into the hangar (<see cref="FlightDecks.StowSeconds"/>).</summary>
		double Stowing = 0;

		/// <summary>Launched again after a turnaround (not with its squadron at the start of the fight): it rejoins whoever leads now.</summary>
		bool Relaunch = false;

		/// <summary>Out of a launch tube (US-T779): the battle second its catapult fires; until then it waits at the tube's breech.</summary>
		double Release = 0;
	};

	/// <summary>
	/// One of a hull's flight decks (U.SPC-T518): a through-hull bay open at both ends, Battlestar Galactica / Star Wars style,
	/// run fore and aft through one of its hangars (<see cref="SubsystemKind.Hangar"/>). Craft come in through the stern mouth,
	/// put down on the deck amidships and go out through the bow mouth. Positions are battle-local km, moving with the hull.
	/// </summary>
	class SPACEGODCORE_API FlightDeck final
	{
	public:
		cs::Ref<BattleUnit> Carrier;

		/// <summary>The hangar the deck runs through: shot away, the deck is closed.</summary>
		cs::Ref<Subsystem> Bay;

		/// <summary>The deck's middle, and its two mouths along the hull's keel, in ship lengths in the hull's frame (right, up, forward).</summary>
		DVec3 MiddleLocal;

		/// <summary>The mouths' distance from the deck's middle along its axis, km (stern negative, bow positive).</summary>
		double SternAlong = 0, BowAlong = 0;

		/// <summary>The deck's axis in the hull's frame (right, up, forward), toward its bow mouth (US-T842), or zero for the
		/// hull's keel: every deck through a hangar without a mouth of its own, and every deck from an older save.</summary>
		DVec3 AxisLocal;

		/// <summary>Its mouths' half-width and half-height, km, or 0 for the deck view's default size (US-T842).</summary>
		double MouthHalfWidthKm = 0, MouthHalfHeightKm = 0;

		/// <summary>Which launch tube this is among its hull's (US-T843), or -1 for a flight deck. A tube only launches: craft
		/// are fired out of its mouth (its bow) but never land in it, so the landing pattern and the cockpit guidance pass it by.</summary>
		int Tube = -1;

		/// <summary>Port, starboard or centreline, for the HUD.</summary>
		cs::String Name;

		/// <summary>The craft cleared to land and on the glide path now; one at a time.</summary>
		cs::Ref<BattleUnit> Final;

		/// <summary>The craft down on the deck, in the order they landed.</summary>
		cs::List<cs::Ref<BattleUnit>> Aboard;

		/// <summary>Battle seconds before the launch lane is clear for the next craft.</summary>
		double NextLaunch = 0;

		/// <summary>A real bay's landing queue (US-T780), first come first served: craft inbound to it, the front wave (as
		/// many as it has spots free) cleared from the gate as they reach it, the waves behind holding astern of it.</summary>
		cs::List<cs::Ref<BattleUnit>> Queue;

		FlightDeck(cs::Ref<BattleUnit> carrier, cs::Ref<Subsystem> bay, cs::String name);

		/// <summary>Open for traffic: its hangar stands and its hull is on the field, not taken.</summary>
		bool Open() const;

		/// <summary>A launch tube (US-T843): craft go out of it and never come in.</summary>
		bool LaunchOnly() const { return Tube >= 0; }

		/// <summary>A real bay (US-T780): a deck through a hangar with a mouth of its own, open at that end only. Craft land
		/// in through the mouth, are trapped and taxi to a spot inside, and launch back out of the same mouth.</summary>
		bool RealBay() const { return Tube < 0 && AxisLocal.LengthSquared() > 1e-12; }

		/// <summary>Which way a landing craft flies down the deck's axis: +1 in through the stern mouth (a keel deck, or a real
		/// bay facing aft), -1 in through the bow (a real bay facing forward or abeam).</summary>
		double Sense() const;

		/// <summary>Where along the axis the mouth craft land through lies, km from the middle.</summary>
		double MouthAlong() const { return Sense() > 0 ? SternAlong : BowAlong; }

		/// <summary>Spots on the deck: <see cref="FlightDecks.Spots"/>, or on a real bay as many as its length holds
		/// (<see cref="FlightDecks.MaxBaySpots"/> at most).</summary>
		int SpotCount() const;

		/// <summary>Where <paramref name="craft"/> stands in the landing queue, joining it at the back if it is not in it yet.</summary>
		int QueuePlace(const cs::Ref<BattleUnit>& craft);

		/// <summary>Craft the deck has on its hands: aboard (taxiing or down), on the glide path and queued to land.</summary>
		int Traffic();

		/// <summary>The deck's middle, battle-local km.</summary>
		DVec3 Middle() const;

		DVec3 Stern() const;
		DVec3 Bow() const;

		/// <summary>The deck's axis, battle-local, toward its bow mouth: the hull's nose for a deck on the keel.</summary>
		DVec3 Axis() const;

		/// <summary>The deck's frame, battle-local: its axis and the right and up across it. The hull's own for a deck on the
		/// keel; off it, right = axis x the hull's up (the hull's right when the axis runs along its up).</summary>
		void Basis(DVec3& axis, DVec3& right, DVec3& up) const;

		/// <summary>The approach gate: on the deck's axis, <see cref="FlightDecks.ApproachKm"/> astern of the stern mouth.</summary>
		DVec3 Gate() const;

		/// <summary>A point <paramref name="along"/> km down the deck's axis from its middle, <paramref name="offset"/> (right, up) off it.</summary>
		DVec3 At(double along, DVec3 offset) const;

		/// <summary>Where battle-local <paramref name="pos"/> lies in the deck's frame: along its axis and off it (right, up).</summary>
		void Frame(DVec3 pos, double& along, DVec3& offset) const;

		/// <summary>Parking spot <paramref name="i"/> along the deck, km from its middle.</summary>
		double SpotAlong(int i) const;

		/// <summary>Whether <paramref name="craft"/> has to hold off: another is on the glide path, or every spot is taken.</summary>
		bool Busy(const cs::Ref<BattleUnit>& craft);
	private:
		/// <summary>Drops from the queue every craft no longer inbound to this deck (shot down, cleared, called off).</summary>
		void PruneQueue();

		FlightDeck() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(FlightDeck); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// Flight decks you fly through in person (U.SPC-T518). A carrier, and any other hull with hangars, has a flight deck run
	/// fore and aft through each of its hangars, open at both ends. Its craft are recovered by flying it, not by a count: a
	/// fighter, bomber or shuttle flies to the approach gate astern of a deck, holds off in the pattern while another is on the
	/// glide path or the deck is full, and once cleared is guided in along the deck's axis through the stern mouth to put down
	/// on its spot; there it refuels, rearms and is repaired (<see cref="ServiceSeconds"/>) and is then thrown down the deck and
	/// out of the bow mouth back into the fight. A pilot comes home to do so when hurt or out of ordnance
	/// (<see cref="WantsService"/>), and every craft lands this way when its carrier recalls it. The player can land a craft
	/// they are flying the same way: lined up on a friendly deck's glide path, slow and nose along its axis, the deck takes it
	/// in (boost aborts), turns it round and launches it again; the cockpit readout and the HUD give the guidance.
	/// A carrier's squadrons also launch through their decks at the start of a fight. Each craft is its own hull (U.SPC-T526):
	/// it docks in the hangar as itself as it puts down (<see cref="FighterAI.TakeAboard"/>) and is undocked again as it
	/// launches (<see cref="TakeOff"/>).
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API FlightDecks
	{
	public:
		static const cs::Ref<RangeSetting>& ServiceSeconds();

		static const cs::Ref<ToggleSetting>& AiRearms();

		static const cs::Ref<RangeSetting>& ServiceHull();

		/// <summary>Launch tubes a hull with hangars gets when its design has none of its own (US-T843): 0, the default, for
		/// none. Laid in two flight pods off the hull's flanks, each a row of tubes firing forward, tier on tier.</summary>
		static const cs::Ref<RangeSetting>& LaunchTubes();

		/// <summary>The render class a hull's flight deck mouths are drawn under (principle 3): only once the hull is big on screen.</summary>
		static const cs::Ref<LodClass>& Lod();

		/// <summary>How long a deck runs either side of its hangar: this many ship lengths, at most <see cref="MaxHalfLengthKm"/>.</summary>
		static constexpr double HalfLengthShipLengths = 0.15; static constexpr double MaxHalfLengthKm = 0.5;

		/// <summary>How far astern of the stern mouth the approach gate lies, km, and how near it a craft must come to be cleared.</summary>
		static constexpr double ApproachKm = 1.2; static constexpr double GateCaptureKm = 0.3;

		/// <summary>Coming in from ahead: how far out from the keel it swings round the hull, in ship lengths of beam plus km clear.</summary>
		static constexpr double BeamShipLengths = 0.15; static constexpr double RoundHullKm = 0.3;

		/// <summary>The holding pattern: how far astern of the gate it circles, and how wide, km.</summary>
		static constexpr double HoldAsternKm = 0.9; static constexpr double HoldRadiusKm = 0.6;

		/// <summary>How long a craft home for good sits on its spot before it is struck down into the hangar, seconds.</summary>
		static constexpr double StowSeconds = 8;

		/// <summary>Craft a deck can turn round at once, and how far apart their spots are along it, km.</summary>
		static constexpr int Spots = 3;
		static constexpr double SpotSpacingKm = 0.08;

		/// <summary>The glide path: the fastest and slowest a craft closes on its spot, km/s relative to the deck, and how quickly
		/// (seconds, e-folding) it is drawn onto the deck's axis.</summary>
		static constexpr double FinalSpeedKmS = 0.12; static constexpr double MinCloseKmS = 0.02; static constexpr double LineUpSeconds = 2.5;

		/// <summary>A launch: seconds between craft out of one deck, how far apart a squadron's column starts down the deck,
		/// and how far clear of the bow mouth a craft is handed back to its pilot, km.</summary>
		static constexpr double LaunchIntervalSeconds = 3; static constexpr double LaunchSpacingKm = 0.07; static constexpr double ExitKm = 0.35;

		/// <summary>The player's capture: how far off the axis, how far round from it (deg), and how fast relative to the
		/// deck (km/s) a craft may be on the glide path for the deck to take it in.</summary>
		static constexpr double CaptureOffAxisKm = 0.15; static constexpr double CaptureAlignDeg = 30; static constexpr double CaptureSpeedKmS = 0.2;

		/// <summary>How far off a friendly deck the cockpit starts giving landing guidance, km.</summary>
		static constexpr double GuideKm = 12;

		/// <summary>A launch tube (US-T843), true scale: its mouth's width and height, the wall between neighbours, km, how long
		/// it runs in from its mouth (this many ship lengths, at most <see cref="MaxTubeLengthKm"/>), and tubes to a tier.</summary>
		static constexpr double TubeWidthKm = 0.018; static constexpr double TubeHeightKm = 0.012; static constexpr double TubeWallKm = 0.006;
		static constexpr double TubeLengthShipLengths = 0.15; static constexpr double MaxTubeLengthKm = 0.2;
		static constexpr int TubesPerTier = 10;

		/// <summary>The launch sequencer (US-T779): every tube fires in waves, one craft each, a wave every
		/// <see cref="TubeCycleSeconds"/>, rippling along each tier <see cref="TubeRippleSeconds"/> a tube; the catapult throws
		/// a craft down its tube at <see cref="CatapultKmS2"/> (or its own thrust, if more).</summary>
		static constexpr double TubeCycleSeconds = 2.5; static constexpr double TubeRippleSeconds = 0.12; static constexpr double CatapultKmS2 = 0.15;

		/// <summary>Out of a tube, a squadron's lead breaks for its join point (US-T779): this far ahead of its carrier and above
		/// it, km, the squadrons' points alternating port and starboard this far apart. It flies on into the fight once its wing
		/// has had <see cref="JoinHoldSeconds"/> to form on it there (within <see cref="JoinCaptureKm"/>), or after twice that.</summary>
		static constexpr double JoinAheadKm = 4; static constexpr double JoinUpKm = 0.6; static constexpr double JoinSpreadKm = 0.8;
		static constexpr double JoinCaptureKm = 0.4; static constexpr double JoinHoldSeconds = 12;

		/// <summary>Recovery into a real bay (US-T780): the speed a craft is trapped down to inside the mouth and taxis at, km/s
		/// relative to the deck; how far astern of the one ahead each wave of the landing queue holds, km; and the most spots a
		/// bay has.</summary>
		static constexpr double TaxiKmS = 0.03; static constexpr double WaveSpacingKm = 0.5; static constexpr int MaxBaySpots = 8;

	private:
		// C#'s ConditionalWeakTable<BattleUnit, FlightDeck[]> and its shared empty array (see FlightDecks.cpp).
		static const cs::List<cs::Ref<FlightDeck>>& None();

	public:
		/// <summary>Every flight deck <paramref name="carrier"/> has (one through each of its hangars), open or not; none for a
		/// fighter, a hull without hangars, or a station.</summary>
		static const cs::List<cs::Ref<FlightDeck>>& Of(const cs::Ref<BattleUnit>& carrier);

		/// <summary>
		/// A deck read back from a save (or a rewind keyframe, or a co-op resync) takes its place among its carrier's decks
		/// (US-T111). The table above is not saved, but a craft's <see cref="DeckVisit"/> saves the deck it is visiting, so
		/// without this a loaded carrier would build fresh decks and the visit would hold an orphan copy the carrier no longer
		/// runs. Called from DeckVisit's SG_LOADED hook: the carrier's decks are built as <see cref="Of"/> builds them, then
		/// the loaded deck replaces the one through its own hangar. Decks no saved visit holds are built afresh, as they hold
		/// no state that outlives a visit. A C++-only addition (the C# original loses the deck the same way).
		/// </summary>
		static void Adopt(const cs::Ref<FlightDeck>& deck);

	private:
		static cs::Ref<cs::List<cs::Ref<FlightDeck>>> Build(const cs::Ref<BattleUnit>& carrier);

		/// <summary>Adds <paramref name="count"/> launch tubes through <paramref name="bay"/> to <paramref name="list"/>, side by
		/// side across <paramref name="across"/> and tier on tier, their mouths on <paramref name="mouth"/> and firing along
		/// <paramref name="axis"/> (hull frame, ship lengths); at most <paramref name="perTier"/> to a tier.</summary>
		static void AddTubes(cs::List<cs::Ref<FlightDeck>>& list, const cs::Ref<BattleUnit>& carrier, const cs::Ref<Subsystem>& bay,
			const cs::String& side, int count, int perTier, DVec3 mouth, DVec3 axis, DVec3 across);

	public:
		/// <summary>Whether <paramref name="carrier"/> has a deck open for traffic.</summary>
		static bool HasOpenDeck(const cs::Ref<BattleUnit>& carrier);

		/// <summary>The craft's visit, or null.</summary>
		static cs::Ref<DeckVisit> VisitOf(const cs::Ref<BattleUnit>& u);

		/// <summary>Down on a deck, inside its hull.</summary>
		static bool OnDeck(const cs::Ref<BattleUnit>& u);

		/// <summary>Down on a deck that will launch it again: the deck open and its hull not running.</summary>
		static bool TurningRound(const cs::Ref<BattleUnit>& u);

		/// <summary>Flying home to a deck or down on one: its squadron does not follow it as a lead (one launching is all but back).</summary>
		static bool Away(const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- the pilot's decision

		/// <summary>
		/// Whether a pilot flies home mid-fight to land, rearm and repair: its home has a deck open and is not running, and
		/// its hull is under <see cref="ServiceHull"/>, or it carried torpedoes or missiles and has none left and is a bomber
		/// or has no working gun to fight on with (US-T180).
		/// </summary>
		static bool WantsService(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>It was built with guided rounds (torpedoes, missiles) and every one of them is spent.</summary>
		static bool OutOfOrdnance(const cs::Ref<ShipCombat>& c);

		/// <summary>US-T180: it still has a working gun (an unguided, damaging mount in the guns group) to fight on with.</summary>
		static bool HasGuns(const cs::Ref<ShipCombat>& c);

		/// <summary>US-T780: whether <paramref name="home"/> can take one more craft in to rearm now. Its wing comes home in
		/// waves: while every open real bay has as much traffic as spots, the rest fight on. Always, on a hull with an open
		/// deck that is not a real bay.</summary>
		static bool RearmRoom(const cs::Ref<BattleUnit>& home);

		// ---------------------------------------------------------------- inbound

		/// <summary>The deck <paramref name="u"/> is flying to on its home (kept once chosen while it stays open), or null when
		/// its home has none open (a station: it lands the old way).</summary>
		static cs::Ref<FlightDeck> Assign(const cs::Ref<BattleUnit>& u);

		/// <summary>Where an inbound craft flies: the approach gate, or round the holding pattern astern of it while the deck is busy.</summary>
		static DVec3 InboundGoal(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck);

		/// <summary>
		/// Clears an inbound craft to land once it is at the gate and the deck is free: it goes onto the glide path
		/// (<see cref="DeckStage.Final"/>) and the deck flies it in from here. True if it was cleared.
		/// </summary>
		static bool TryFinal(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck, double dt);

	private:
		static void ClearToLand(const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck);

	public:
		/// <summary>Calls off a craft's visit (its deck closed, it was recalled to the fight, the player boosted off the glide path).</summary>
		static void Abort(const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- the deck flies it

		/// <summary>
		/// One step of a craft the deck is flying: on the glide path, drawn onto the axis and in through the stern mouth to its
		/// spot, where it puts down; or launching, down the deck and out of the bow mouth. True while the deck has it (its pilot
		/// or the player's stick stand aside); false when it has no such visit, or its deck closed and the visit was called off.
		/// </summary>
		static bool Fly(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

	private:
		/// <summary><see cref="Fly"/> on a real bay (US-T780): down the glide path to its one mouth, trapped inside it down to
		/// <see cref="TaxiKmS"/> (the glide path clear for the next), taxied to its spot; launching, back out of the mouth.</summary>
		static bool FlyBay(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<DeckVisit>& v, double dt);

		/// <summary>Puts a craft down on its spot: inside the hull now (<see cref="BattleUnit.Docked"/>), back in its hangar's
		/// count (<see cref="FighterAI.TakeAboard"/>), and its turnaround begins.</summary>
		static void Touchdown(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<DeckVisit>& v);

		/// <summary>Clear of the bow mouth: its pilot (or the player) has it again, flying on in <see cref="DeckVisit.After"/>,
		/// back in its squadron's vee behind whoever leads it now.</summary>
		static void ClearOfBow(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<DeckVisit>& v);

	public:
		// ---------------------------------------------------------------- on deck

		/// <summary>
		/// The decks' traffic each step (from <see cref="Battle"/>'s step): every craft down on a deck rides with its hull on its
		/// spot and is turned round, hull and drive tank first and then its racks and magazines; done, and with the launch lane
		/// clear, it is launched again (<see cref="TakeOff"/>), unless the shooting is over (only the player's craft goes up
		/// again then), its hull is running, or it is a boarding shuttle home for good.
		/// </summary>
		static void Step(const cs::Ref<Battle>& b, double dt);

		/// <summary>The turnaround <paramref name="share"/> of the way through: hull mended and tank filled that far; once it is
		/// done, every hardpoint mended, every rack and magazine full, shields up and the guns cool.</summary>
		static void Service(const cs::Ref<BattleUnit>& u, double share);

	private:
		/// <summary>Whether <paramref name="craft"/>, down on <paramref name="home"/>'s deck, still holds its berth in the hangar
		/// to launch from (U.SPC-T526): a bay shot away loses the craft in it.</summary>
		static bool Berthed(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft);

	public:
		/// <summary>
		/// A craft leaves its hangar to launch again, the reverse of <see cref="FighterAI.TakeAboard"/>: its own hull undocked
		/// from <paramref name="home"/>'s hangar (U.SPC-T526, <see cref="CarrierWings.Undock"/>). False when the hangar no
		/// longer holds it (it was lost with a bay shot away, or there was no berth for it): it stays aboard.
		/// </summary>
		static bool TakeOff(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft);

		/// <summary>
		/// A craft just out of <paramref name="carrier"/>'s hangar at the start of a fight (or a shuttle sent out) goes out
		/// through a flight deck: placed in the deck's launch lane behind any already in it, so a squadron goes out of the bow
		/// in a column. False when the hull has no deck open (it is left where the launch put it).
		/// </summary>
		static bool Catapult(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<BattleUnit>& craft);

		/// <summary>Where a lead launched out of a tube flies in <see cref="FighterPhase.Launch"/> (US-T779): its squadron's join
		/// point off its carrier, battle-local km. False for any other craft (it flies the old launch goal).</summary>
		static bool JoinGoal(const cs::Ref<BattleUnit>& u, DVec3& goal);

		// ---------------------------------------------------------------- the player

		/// <summary>
		/// A craft the player is flying (from <see cref="FighterAI.FlyManned"/>): the deck flies it while it has it (boosting
		/// off the glide path aborts the landing), and a craft lined up on a friendly deck's glide path, slow and nose along its
		/// axis (<see cref="CaptureOffAxisKm"/>, <see cref="CaptureAlignDeg"/>, <see cref="CaptureSpeedKmS"/>), is taken in. True
		/// while the deck has it.
		/// </summary>
		static bool FlyManned(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FighterControl>& stick, double dt);

		/// <summary>The friendly deck whose glide path <paramref name="u"/> is lined up on closely enough to be taken in, or null.</summary>
		static cs::Ref<FlightDeck> Capturing(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>The deck the cockpit guides <paramref name="u"/> to: the one it is visiting, else the nearest open friendly
		/// one within <see cref="GuideKm"/> (its own carrier's first), or null.</summary>
		static cs::Ref<FlightDeck> GuideDeck(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// The cockpit's landing guidance for <paramref name="u"/> (null with no deck near): on a visit, what the deck is doing
		/// with it; otherwise the deck's name and range to its gate, how far off the glide path it is (right/left, up/down), how
		/// far its nose is off the deck's axis, and its closing speed against the most the deck will take it in at.
		/// </summary>
		// C#'s null (no deck near) is "" here: a cs::String has no null.
		static cs::String Guidance(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	private:
		static cs::String Metres(double km);
		static cs::String Pct(double f);
		SG_SERIALIZABLE(FlightDecks); // ObjectGraph registration (Persistence/Registrations)
	};
}
