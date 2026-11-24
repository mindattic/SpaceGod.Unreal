#pragma once
// US-T675: what radio chatter says (US-T642). A message without literal text is realised only when it is heard, from its
// Situation and Args: a line from the situation's pool (a channel kind's own pool first: a Pirate net's idle talk and
// battle calls, a Fleet net's idle talk), clipped or loose by the sender's Chatter trait, with an opener or sign-off by
// their Warmth. The variant is a hash of the message id, never the simulation's random stream, so every listener hears
// the same words. A slot that cannot be filled (a sender, settlement or good not found) gives a generic line, never "".
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and no LLM is asked.

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"

#include <cstdint>

namespace SpaceGod::Core
{
	struct CommsParty;
	class World;

	/// <summary>What a chatter message is about (<see cref="RadioMessage.Situation"/>). Idle is 0, so an unset message is banter.</summary>
	enum class ChatterSituation : uint16_t
	{
		Idle,
		DockingRequest,
		Clearance,
		Departure,
		PatrolCheckIn,
		ContactReport,
		TradeQuote,
		ConvoyCall,
		Distress,
		BattleCallout,
		BattleOver,
		PirateTaunt,
		BountyNotice,
		RelayBulletin,
		Count
	};
	SPACEGODCORE_API const char* ToString(ChatterSituation v);

	/// <summary>
	/// The chatter line pools and their slots: {callsign} the sender, {station} Args[0] a settlement id, {good} Args[1] a
	/// Good, {price} Args[2] credits, {range} Args[3] km (CommsRuler::Words), {bearing} the heliocentric longitude the
	/// message was sent from, {faction} FactionId as an index into the world's Fleet factions.
	/// </summary>
	class SPACEGODCORE_API ChatterLines final
	{
	public:
		/// <summary>Variants per pool and phrasing.</summary>
		static constexpr int Variants = 8;

		/// <summary>The message's EmitterKey for a party: (1 &lt;&lt; 32) | its comms key, so 0 stays anonymous.</summary>
		static uint64_t EmitterKey(const CommsParty& party);

		/// <summary>Who sent it, as the log names them ("Kestrel, Sefa Corwin", "Ceres Hub Traffic"); "" when anonymous,
		/// "unidentified" when the key no longer resolves.</summary>
		static cs::String SenderName(const cs::Ref<World>& world, uint64_t emitterKey);

		/// <summary>The message's words, made now that it is heard on a channel of <paramref name="kind"/>. Never empty.</summary>
		static cs::String Realise(const RadioMessage& message, const cs::Ref<World>& world, double jd, RadioChannelKind kind = RadioChannelKind::Civil);
	};
}
