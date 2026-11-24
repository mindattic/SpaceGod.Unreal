// US-T675: chatter line templates and sender names (see ChatterLines.h).

#include "Radio/ChatterLines.h"

#include "Comms/Comms.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Law/Law.h"
#include "Ships.h"
#include "World.h"

#include <array>
#include <cmath>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr int SituationCount = static_cast<int>(ChatterSituation::Count);
		constexpr uint64_t EmitterTag = 1ull << 32;
		constexpr uint32_t VariantSalt = 0x43485454u, OpenerSalt = 0x4f50454eu, TraitSalt = 0x54524154u;

		struct LinePool
		{
			std::array<const char*, ChatterLines::Variants> Clipped;
			std::array<const char*, ChatterLines::Variants> Loose;
		};

		// In ChatterSituation's order.
		const std::array<LinePool, SituationCount> Pools = { {
			// Idle
			{ { {
				"{callsign}, radio check.",
				"{callsign} on station, nothing to report.",
				"Quiet out here. {callsign}.",
				"{callsign}, holding course for {station}.",
				"{callsign}, hauling {good}. All normal.",
				"Net check. {callsign} reading you five by five.",
				"{callsign}, systems nominal.",
				"{callsign}, coasting. Nothing on scope.",
			} }, { {
				"Anyone else bored out of their skull? {callsign} here, nothing but stars all shift.",
				"{callsign} here. Coffee's gone cold and {station} is still a long way off.",
				"Third shift on {good} runs this week. {callsign}, if anyone's counting.",
				"Hey, has anyone actually eaten at {station}? {callsign} wants to know.",
				"{callsign} here, just checking the radio still works. Talk to me, somebody.",
				"Long haul to {station}. {callsign} would trade the cargo for a decent chair.",
				"Did someone leave a carrier keyed? Lot of hiss on the net. {callsign} listening.",
				"{callsign} here. Saw a comet tail earlier, prettiest thing all month.",
			} } },
			// DockingRequest
			{ { {
				"{station} Traffic, {callsign}, request docking.",
				"{station} Traffic, {callsign} inbound, requesting a berth.",
				"{callsign} to {station}, request approach and docking.",
				"{station}, {callsign}, inbound for docking, {range} out.",
				"{station} Traffic, {callsign}, with {good}, request berth.",
				"{callsign}, {range} from {station}, request docking clearance.",
				"{station}, {callsign}, ready for approach.",
				"{station} Traffic, {callsign}, inbound, request pad assignment.",
			} }, { {
				"{station} Traffic, {callsign} here. Got room for one more? We're {range} out.",
				"Hello {station}, {callsign} coming in. Any chance of a berth near the bar?",
				"{station}, it's {callsign} again. We'd love to dock if you can squeeze us in.",
				"{callsign} calling {station}. Hold full of {good} and we'd like to put down.",
				"Evening {station}, {callsign} looking for a berth. We'll be gentle with the paint.",
				"{station} Traffic, {callsign} on approach. Tell us where to park, please.",
				"{callsign} to {station}, we're tired and {range} out. Docking, please.",
				"Knock knock, {station}. {callsign} would like to come aboard.",
			} } },
			// Clearance
			{ { {
				"{callsign}, {station} Traffic, cleared to dock.",
				"{callsign}, cleared for approach. Follow the beacon.",
				"{callsign}, {station}, berth assigned, cleared in.",
				"{callsign}, cleared to dock, hold speed under limits.",
				"{station} Traffic to {callsign}, approach approved.",
				"{callsign}, docking approved. Watch the outbound lane.",
				"{callsign}, cleared. Report at the outer marker.",
				"{callsign}, {station}, you are cleared to land.",
			} }, { {
				"{callsign}, {station} here. You're cleared in, mind the traffic on the way.",
				"Come on in, {callsign}. Berth's ready and the kettle's on.",
				"{callsign}, you're good to dock. Nice and slow this time, please.",
				"{station} Traffic to {callsign}: cleared. Welcome back.",
				"{callsign}, we've got you. Follow the lights and don't scratch anything.",
				"All yours, {callsign}. {station} says come on down.",
				"{callsign}, you're cleared. Dock crew's waiting, they get grumpy.",
				"{callsign}, approach approved. Good to hear a familiar voice.",
			} } },
			// Departure
			{ { {
				"{station} Traffic, {callsign}, departing.",
				"{callsign} clear of {station}, outbound.",
				"{callsign}, undocked, leaving the zone.",
				"{station}, {callsign}, request departure.",
				"{callsign}, outbound from {station} with {good}.",
				"{callsign}, departure complete, switching off this channel.",
				"{station} Traffic, {callsign} clear of the pads.",
				"{callsign}, leaving {station} control.",
			} }, { {
				"{station}, {callsign} heading out. Thanks for the berth.",
				"{callsign} off the pad. See you next run, {station}.",
				"That's us gone, {station}. {callsign} out, with a hold of {good}.",
				"{callsign} leaving {station}. Keep the lights on for us.",
				"Bye {station}. {callsign} heading back into the dark.",
				"{callsign} clear and outbound. Tell the dock crew thanks.",
				"Off we go. {callsign} leaving {station}, wish us luck.",
				"{station}, {callsign} departing. Same time next week.",
			} } },
			// PatrolCheckIn
			{ { {
				"{faction} patrol {callsign}, on station, bearing {bearing}.",
				"{callsign}, patrol check-in. Sector clear.",
				"{callsign} reporting, bearing {bearing}, all quiet.",
				"{faction} control, {callsign} on patrol, nothing to report.",
				"{callsign}, checkpoint reached, continuing sweep.",
				"{callsign}, patrol leg complete, bearing {bearing}.",
				"{faction} patrol {callsign}, lane clear for {range}.",
				"{callsign} on watch. No contacts.",
			} }, { {
				"{callsign} checking in for {faction}. Sweep's clean, bearing {bearing}.",
				"Control, {callsign}. Nothing out here but rocks and regret.",
				"{callsign} here, patrol's dull as ever. Lane's clear for {range}.",
				"{faction} control, it's {callsign}. Nothing to shoot at, sorry.",
				"{callsign} on the beat. Quiet out at bearing {bearing}.",
				"Patrol {callsign} checking in. All we've seen is a freighter with bad paint.",
				"{callsign} for {faction}: all clear, all bored.",
				"{callsign} here, still flying circles at bearing {bearing}.",
			} } },
			// ContactReport
			{ { {
				"Contact, {range}, bearing {bearing}.",
				"{callsign}, unknown contact at {range}.",
				"{callsign}, track bearing {bearing}, {range}.",
				"Contact report: one hull, {range}, bearing {bearing}.",
				"{callsign}, bogey bearing {bearing}.",
				"{callsign}, contact closing, {range}.",
				"Unidentified contact, bearing {bearing}, {range}.",
				"{callsign}, new track at {range}.",
			} }, { {
				"Heads up, something out at {range}, bearing {bearing}.",
				"{callsign} here, got a blip at {range}. Anyone know who that is?",
				"Somebody's running dark at bearing {bearing}, {range} out. Eyes open.",
				"{callsign} seeing a contact {range} off. Not answering hails.",
				"Funny return on scope, bearing {bearing}. {callsign} keeping an eye on it.",
				"Who's that at {range}? {callsign} would like a name.",
				"{callsign} reporting a hull at bearing {bearing}. Could be nothing.",
				"Contact at {range}, folks. {callsign} doesn't like the look of it.",
			} } },
			// TradeQuote
			{ { {
				"{station}: {good}, {price} a tonne.",
				"{good} at {station}, {price}.",
				"Quote: {good}, {price}, at {station}.",
				"Buying {good}, {price} a tonne, {station}.",
				"{station} market: {good} {price}.",
				"Selling {good}, {price} a tonne.",
				"{good} going for {price} at {station}.",
				"{station}, {good}, {price}, while stocks last.",
			} }, { {
				"Word is {station} pays {price} a tonne for {good}. Not bad.",
				"{good} at {price} over at {station}. Somebody's getting rich.",
				"If you've got {good}, {station} wants it, {price} a tonne.",
				"Saw {good} at {price} at {station}. Daylight robbery.",
				"{station}'s offering {price} for {good}. I'd go, if I had any.",
				"Hot tip: {good}, {station}, {price}. You didn't hear it from me.",
				"{good}'s up to {price} at {station}. Load up while you can.",
				"Anyone hauling {good}? {station} will give you {price} a tonne.",
			} } },
			// ConvoyCall
			{ { {
				"Convoy, {callsign}, form up on me.",
				"{callsign}, convoy to {station}, close up.",
				"{callsign} lead, convoy holding speed.",
				"Convoy {callsign}, tighten formation.",
				"{callsign}, escorts take station.",
				"{callsign}, convoy with {good}, departing for {station}.",
				"{callsign}, keep intervals, convoy proceeding.",
				"{callsign} to convoy, maintain course for {station}.",
			} }, { {
				"Alright convoy, {callsign} here. Stay close and nobody gets hurt.",
				"{callsign} leading the run to {station}. Keep up, stragglers.",
				"Form up on {callsign}, folks. {good} doesn't haul itself.",
				"Convoy, {callsign}. Escorts, keep your eyes on the dark bits.",
				"{callsign} here, we're bound for {station}. Nobody wander off.",
				"Tighten up, convoy. {callsign} doesn't want to write any letters home.",
				"{callsign} to the convoy: steady as she goes, {station} by morning.",
				"Convoy, {callsign}. If you see pirates, shout. Loudly.",
			} } },
			// Distress
			{ { {
				"Mayday, mayday, {callsign}, bearing {bearing}.",
				"Mayday, {callsign}, hull breach, request assistance.",
				"{callsign}, mayday, under attack, bearing {bearing}.",
				"Mayday, {callsign}, drive failure, {range} from {station}.",
				"{callsign}, declaring emergency, bearing {bearing}.",
				"Mayday, {callsign}, losing pressure.",
				"{callsign}, mayday, any station respond.",
				"Mayday, {callsign}, bearing {bearing}, need help now.",
			} }, { {
				"Mayday! {callsign}, we're hit, anyone out there?",
				"Please, anyone, {callsign} needs help, bearing {bearing}!",
				"{callsign} here, we're in trouble, drive's gone, please respond!",
				"Mayday, mayday, this is {callsign}, they're all over us!",
				"Help, {callsign}, we're venting air, bearing {bearing}!",
				"Anyone hearing this, {callsign} is going down, {range} out from {station}!",
				"{callsign}, mayday, we can't hold them off much longer!",
				"This is {callsign}, we need help, any ship, please!",
			} } },
			// BattleCallout
			{ { {
				"{callsign}, engaging, bearing {bearing}.",
				"{callsign}, weapons free.",
				"{callsign}, target {range}, firing.",
				"{callsign}, hostile {faction} hull, engaging.",
				"{callsign}, break, break, incoming.",
				"{callsign}, on your six, {range}.",
				"{callsign}, shields down, still in it.",
				"{callsign}, splash one.",
			} }, { {
				"{callsign} going in! Cover me!",
				"Got one on my tail, {callsign} needs help!",
				"{faction} scum at bearing {bearing}, {callsign} engaging!",
				"{callsign} taking hits, somebody get that thing off me!",
				"Watch it, they're coming round, {range} and closing!",
				"{callsign} here, I've got the big one, keep the small fry off me.",
				"Ha! Got him! {callsign} scores!",
				"{callsign} hurting but still flying. Keep shooting!",
			} } },
			// BattleOver
			{ { {
				"{callsign}, area clear.",
				"{callsign}, engagement over, assessing damage.",
				"{callsign}, hostiles withdrawn.",
				"{callsign}, {faction} forces retreating.",
				"{callsign}, all targets down.",
				"{callsign}, ceasefire, standing down.",
				"{callsign}, fight's over, regrouping.",
				"{callsign}, weapons cold.",
			} }, { {
				"That's it, they're gone. {callsign} still in one piece, mostly.",
				"{callsign} here. We made it. Somebody buy me a drink.",
				"{faction}'s running home. Nice flying, everyone.",
				"Done. {callsign} counting holes in the hull.",
				"All quiet again. {callsign} needs a long sit down.",
				"{callsign} here, that was too close. Everyone alright?",
				"They've had enough. {callsign} standing down.",
				"Well, that was exciting. {callsign} out.",
			} } },
			// PirateTaunt
			{ { {
				"Cut your drive. Dump the {good}.",
				"{faction} hull, heave to.",
				"Drop your cargo and live.",
				"Last warning. Cargo out.",
				"Stop or burn.",
				"{good}. Now.",
				"You're ours. Power down.",
				"No escorts here. Stop.",
			} }, { {
				"Nice hold of {good} you've got there. Be a shame if something happened to it.",
				"Hey {faction}, your escorts went home. Want to talk about your cargo?",
				"Run if you like. We like it when they run.",
				"Dump the {good} and we'll pretend we never met.",
				"{callsign} here, and you're in our lane, friend.",
				"We can do this the easy way, or the loud way.",
				"Tell {faction} they owe us a toll. You're paying it.",
				"Come on, slow down. We only want what's in the hold.",
			} } },
			// BountyNotice
			{ { {
				"{faction} bounty: {callsign}, {price}.",
				"Wanted by {faction}: {callsign}. Reward {price}.",
				"Bounty posted at {station}, {price}.",
				"{faction} notice: {callsign} wanted, {price}.",
				"Reward {price} for {callsign}, alive.",
				"{station}: bounty {price}, see the board.",
				"{faction} pays {price} for {callsign}.",
				"Bounty: {callsign}. {price}. Report to {station}.",
			} }, { {
				"{faction} wants {callsign} badly. {price} badly.",
				"Anyone fancy {price}? {faction} has a job for you, name of {callsign}.",
				"Heard there's {price} on {callsign}'s head at {station}.",
				"{callsign}'s got a price on them now. {price}, says {faction}.",
				"Bounty board at {station} has a fat one: {price}.",
				"Someone bring in {callsign} and I'll buy the round. {price}!",
				"{faction} is paying {price}. Must have really upset somebody.",
				"Easy money, they say. {price} for {callsign}. Never easy.",
			} } },
			// RelayBulletin
			{ { {
				"Relay bulletin: {station} reports {good} at {price}.",
				"Relay: {faction} notice to all ships.",
				"Bulletin: {station} market open, {good} {price}.",
				"Relay: lanes to {station} open.",
				"Relay bulletin, {faction}: patrols increased.",
				"Bulletin: {good} shortage at {station}.",
				"Relay: {station} docking delays expected.",
				"Relay, {faction}: notice to traders.",
			} }, { {
				"From the relay: {station} is paying {price} for {good} this week.",
				"Relay news: {faction} says patrols are up. Behave, everybody.",
				"Bulletin says {station}'s short on {good}. Prices to match.",
				"Relay chatter: {faction} has something to announce, as usual.",
				"Long-range relay: {station} wants traders. Bring {good}.",
				"Relay's saying {station} has queues to the moon. Plan ahead.",
				"News off the relay: {faction} is hiring. Pay's not bad.",
				"Relay bulletin, for what it's worth: {good} at {price}.",
			} } },
		} };

		const LinePool PirateIdle = { { {
			"{callsign}, on the hunt.",
			"{callsign}, no prey yet.",
			"{callsign}, lane's empty.",
			"{callsign}, watching the {station} run.",
			"{callsign}, waiting on a fat one.",
			"{callsign}, in position.",
			"{callsign}, nobody's carrying {good} today.",
			"{callsign}, holding dark.",
		} }, { {
			"{callsign} here, slow day. Even the freighters are hiding.",
			"Anyone seen a juicy hauler? {callsign}'s getting hungry.",
			"{callsign} parked off {station}, waiting for someone stupid.",
			"Word is there's {good} coming through. {callsign} wants first pick.",
			"{callsign} here, the boss is in a mood again. Keep your heads down.",
			"Quiet out here. Too quiet. {callsign} smells a patrol.",
			"{callsign} bored. Somebody start a fight.",
			"{callsign} here, last haul paid for drinks for a week. Let's do it again.",
		} } };

		const LinePool PirateBattle = { { {
			"{callsign}, hit them.",
			"{callsign}, take the drive out.",
			"{callsign}, don't scratch the cargo.",
			"{callsign}, cut them off.",
			"{callsign}, flank them, {range}.",
			"{callsign}, they're breaking, chase.",
			"{callsign}, {faction} escort, kill it.",
			"{callsign}, board when they're dead in space.",
		} }, { {
			"Get 'em! {callsign} wants that cargo intact!",
			"{callsign} here, they're running! Run them down!",
			"Ha! {faction} sent amateurs. {callsign} going in!",
			"Watch the hold, idiots, the hold's what we're paid for!",
			"{callsign} taking fire, somebody kill that escort!",
			"Box them in at bearing {bearing}, nobody gets away!",
			"{callsign} hit, still flying, still angry!",
			"Shoot the engines, not the cargo! {callsign} means it!",
		} } };

		const LinePool FleetIdle = { { {
			"{callsign}, on station.",
			"{callsign}, status green.",
			"{callsign}, holding position, bearing {bearing}.",
			"{faction} net, {callsign} checking in.",
			"{callsign}, awaiting orders.",
			"{callsign}, picket set.",
			"{callsign}, comms check, {faction} net.",
			"{callsign}, no change.",
		} }, { {
			"{callsign} on the {faction} net. Same view as yesterday.",
			"{callsign} here, holding. Anyone know when we rotate home?",
			"Fleet net, {callsign}. All green, all quiet, all bored.",
			"{callsign} on picket at bearing {bearing}. Somebody tell me a joke.",
			"{callsign} here, still waiting on those orders, {faction}.",
			"{faction} net: {callsign}'s galley ran out of real coffee.",
			"{callsign}, status green. Morale amber.",
			"{callsign} checking in. Nothing moving but us.",
		} } };

		// When a slot cannot be filled. In ChatterSituation's order.
		const std::array<const char*, SituationCount> Generic = { {
			"Radio check.",
			"Requesting docking.",
			"Cleared to dock.",
			"Departing.",
			"Patrol checking in, all quiet.",
			"Unknown contact on scope.",
			"Prices are moving at the markets.",
			"Convoy, form up.",
			"Mayday, mayday, any station respond.",
			"Engaging hostiles.",
			"Area clear, standing down.",
			"Cut your drive and stand by to be boarded.",
			"New bounty posted.",
			"Relay bulletin to all ships.",
		} };

		const std::array<const char*, 6> WarmOpeners = { { "{greeting}, all. ", "Hello the net. ", "{greeting}. ", "Hi there. ", "{greeting}, everyone. ", "Hey, folks. " } };
		const std::array<const char*, 4> WarmSignOffs = { { " Fly safe.", " Take care out there.", " Cheers.", " Safe lanes." } };
		const std::array<const char*, 3> ColdSignOffs = { { " Out.", " End.", " That is all." } };

		/// <summary>The sender a key names, and their pilot if one has been met (never meets one).</summary>
		struct Sender
		{
			CommsParty Party;
			cs::Ref<NamedPilot> Pilot;
		};

		Sender SenderOf(const cs::Ref<World>& world, uint64_t key)
		{
			Sender s;
			if (world == nullptr || (key >> 32) != 1) return s;
			s.Party = Comms::Resolve(world, static_cast<int>(static_cast<uint32_t>(key)));
			if (s.Party.Ship != nullptr && world->Law != nullptr && world->Law->Pilots != nullptr) s.Pilot = world->Law->Pilots->Find(s.Party.Ship);
			return s;
		}

		double Unit(uint32_t h) { return h / 4294967296.0; }

		/// <summary>The slots' fills; an empty one cannot be said.</summary>
		struct Fills
		{
			cs::String Callsign, Station, Good, Price, Range, Bearing, Faction;

			const cs::String* Find(std::string_view key) const
			{
				if (key == "callsign") return &Callsign;
				if (key == "station") return &Station;
				if (key == "good") return &Good;
				if (key == "price") return &Price;
				if (key == "range") return &Range;
				if (key == "bearing") return &Bearing;
				if (key == "faction") return &Faction;
				return nullptr;
			}
		};

		/// <summary>The template with its slots filled, or false when one of them has no fill.</summary>
		bool Fill(std::string_view line, const Fills& f, cs::String& out)
		{
			out.clear();
			size_t i = 0;
			while (i < line.size())
			{
				size_t open = line.find('{', i);
				if (open == std::string_view::npos) { out.append(line.substr(i)); break; }
				size_t close = line.find('}', open);
				if (close == std::string_view::npos) return false;
				const cs::String* v = f.Find(line.substr(open + 1, close - open - 1));
				if (v == nullptr || v->empty()) return false;
				out.append(line.substr(i, open - i));
				out.append(*v);
				i = close + 1;
			}
			return true;
		}

		Fills FillsFor(const RadioMessage& m, const cs::Ref<World>& world, const Sender& sender)
		{
			Fills f;
			if (sender.Pilot != nullptr && !sender.Pilot->Callsign.empty()) f.Callsign = sender.Pilot->Callsign;
			else if (sender.Party.Ship != nullptr) f.Callsign = sender.Party.Ship->Name;
			else if (sender.Party.Station != nullptr) f.Callsign = sender.Party.Station->Name;

			if (world != nullptr && world->Economy != nullptr)
				for (const auto& s : world->Economy->Settlements)
					if (s != nullptr && s->Id == m.Args[0]) { f.Station = s->Name; break; }
			if (m.Args[1] >= 0 && m.Args[1] < GoodInfo::Count)
			{
				const auto& info = GoodInfo::Of(static_cast<Good>(m.Args[1]));
				if (info != nullptr) f.Good = cs::Strings::ToLowerInvariant(info->Name);
			}
			if (m.Args[2] > 0) f.Price = cs::Cat(m.Args[2], " credits");
			if (m.Args[3] > 0) f.Range = CommsRuler::Words(m.Args[3]);
			if (m.OriginKm.X != 0 || m.OriginKm.Y != 0)
			{
				double deg = std::atan2(m.OriginKm.Y, m.OriginKm.X) * 180.0 / 3.14159265358979323846;
				f.Bearing = cs::Format("{0:000}", static_cast<int>(std::floor(deg < 0 ? deg + 360 : deg)) % 360);
			}
			if (world != nullptr && world->Fleet != nullptr && m.FactionId >= 0 && m.FactionId < world->Fleet->Factions().Count())
			{
				const auto& faction = world->Fleet->Factions()[m.FactionId];
				if (faction != nullptr) f.Faction = faction->Name;
			}
			return f;
		}

		const LinePool& PoolFor(ChatterSituation situation, RadioChannelKind kind)
		{
			if (kind == RadioChannelKind::Pirate && situation == ChatterSituation::Idle) return PirateIdle;
			if (kind == RadioChannelKind::Pirate && situation == ChatterSituation::BattleCallout) return PirateBattle;
			if (kind == RadioChannelKind::Fleet && situation == ChatterSituation::Idle) return FleetIdle;
			return Pools[static_cast<int>(situation)];
		}

		cs::String Greeting(double jd)
		{
			double hour = (jd + 0.5 - std::floor(jd + 0.5)) * 24;
			return hour < 12 ? "Morning" : hour < 18 ? "Afternoon" : "Evening";
		}
	}

	const char* ToString(ChatterSituation v)
	{
		switch (v)
		{
		case ChatterSituation::Idle: return "Idle";
		case ChatterSituation::DockingRequest: return "DockingRequest";
		case ChatterSituation::Clearance: return "Clearance";
		case ChatterSituation::Departure: return "Departure";
		case ChatterSituation::PatrolCheckIn: return "PatrolCheckIn";
		case ChatterSituation::ContactReport: return "ContactReport";
		case ChatterSituation::TradeQuote: return "TradeQuote";
		case ChatterSituation::ConvoyCall: return "ConvoyCall";
		case ChatterSituation::Distress: return "Distress";
		case ChatterSituation::BattleCallout: return "BattleCallout";
		case ChatterSituation::BattleOver: return "BattleOver";
		case ChatterSituation::PirateTaunt: return "PirateTaunt";
		case ChatterSituation::BountyNotice: return "BountyNotice";
		case ChatterSituation::RelayBulletin: return "RelayBulletin";
		default: return "Count";
		}
	}

	uint64_t ChatterLines::EmitterKey(const CommsParty& party)
	{
		return party.IsNone() ? 0 : EmitterTag | static_cast<uint32_t>(party.Key());
	}

	cs::String ChatterLines::SenderName(const cs::Ref<World>& world, uint64_t emitterKey)
	{
		if (emitterKey == 0) return cs::String();
		Sender s = SenderOf(world, emitterKey);
		if (s.Party.Ship != nullptr) return s.Pilot != nullptr ? cs::Cat(s.Party.Ship->Name, ", ", s.Pilot->Name) : s.Party.Ship->Name;
		if (s.Party.Station != nullptr) return cs::Cat(s.Party.Station->Name, " Traffic");
		return "unidentified";
	}

	cs::String ChatterLines::Realise(const RadioMessage& message, const cs::Ref<World>& world, double jd, RadioChannelKind kind)
	{
		ChatterSituation situation = message.Situation < SituationCount ? static_cast<ChatterSituation>(message.Situation) : ChatterSituation::Idle;
		Sender sender = SenderOf(world, message.EmitterKey);

		// A met pilot talks as their traits say; traffic control is formal; anyone else keeps a manner of their own key's.
		double chatter, warmth;
		if (sender.Pilot != nullptr) { chatter = sender.Pilot->Chatter; warmth = sender.Pilot->Warmth; }
		else if (sender.Party.IsStation()) { chatter = 0.2; warmth = 0.5; }
		else
		{
			uint32_t h = Characters::Mix(static_cast<uint32_t>(message.EmitterKey) ^ TraitSalt);
			chatter = Unit(h);
			warmth = Unit(Characters::Mix(h));
		}

		const LinePool& pool = PoolFor(situation, kind);
		const auto& lines = chatter < 0.5 ? pool.Clipped : pool.Loose;
		Fills fills = FillsFor(message, world, sender);
		uint32_t pick = Characters::Mix(message.Id ^ VariantSalt);

		cs::String text;
		bool filled = false;
		for (int i = 0; i < Variants && !filled; i++)
			filled = Fill(lines[(pick + i) % Variants], fills, text);
		if (!filled) text = Generic[static_cast<int>(situation)];

		bool urgent = message.Priority >= 2 || situation == ChatterSituation::Distress || situation == ChatterSituation::BattleCallout
			|| situation == ChatterSituation::PirateTaunt || kind == RadioChannelKind::Pirate;
		if (urgent) return text;
		uint32_t manner = Characters::Mix(message.Id ^ OpenerSalt);
		if (warmth > 0.66)
		{
			if ((manner & 1) == 0) text = cs::Cat(cs::Strings::Replace(WarmOpeners[(manner >> 1) % WarmOpeners.size()], "{greeting}", Greeting(jd)), text);
			else text = cs::Cat(text, WarmSignOffs[(manner >> 1) % WarmSignOffs.size()]);
		}
		else if (warmth < 0.33 && (manner & 1) == 0)
			text = cs::Cat(text, ColdSignOffs[(manner >> 1) % ColdSignOffs.size()]);
		return text;
	}
}
