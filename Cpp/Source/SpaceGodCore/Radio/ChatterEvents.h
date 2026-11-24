#pragma once
// US-T643: event-driven radio chatter (US-T577). The world's notable events (World::Events) go out on the air in the
// listener's system: a battle as callouts on the factions' nets plus an alert on Guard, a capture on the captor's net, a
// loss as distress on Guard, a launch on the port's Traffic channel, the law on a faction's net (else Civil), the economy
// and the markets on Civil. Info, Opportunity, Diplomacy and Society events are not chatter.
// The events are read from a cursor by Jd, never by index, so the log trimming its oldest entries cannot break it: each
// pump walks back from the newest event only as far as the cursor, so it costs O(new events), and it takes at most
// MaxPerPump of them, the rest waiting for the next pump; a backlog past MaxBacklog (a time warp) is skipped.
// TODO(US-T577): docking and undocking traffic. The world raises no arrival or departure at a settlement (no Fleet or
// World event for a ship docking or leaving), so only WorldEventKind::Launch reaches Traffic; hook a Fleet::Docked /
// Fleet::Undocked event here when the fleet raises one.
// Read-only and view-local: it never changes the world, nothing here enters the simulation, the lockstep frames, saves
// or the bench digests, and nothing draws from the simulation's random stream.

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"

#include <vector>

namespace SpaceGod::Core
{
	class StarSystem;
	class World;

	class SPACEGODCORE_API ChatterEvents final
	{
	public:
		/// <summary>The most events one pump turns into chatter; the rest wait for the next pump.</summary>
		static constexpr int MaxPerPump = 8;
		/// <summary>The most unread events kept: an older backlog (a time warp, a burst) is skipped, not replayed.</summary>
		static constexpr int MaxBacklog = 64;

		/// <summary>One message: realised from its Situation when heard when Text is empty, else Text as is.</summary>
		struct Chatter
		{
			RadioMessage Message;
			cs::String Text;
		};

		/// <summary>
		/// The chatter for the events <paramref name="at"/> (galaxy index <paramref name="systemId"/>) logged since the last
		/// pump, oldest first, appended to <paramref name="out"/>. The first pump on a world, or after the clock went back
		/// before the cursor, starts from now: old events are not news. O(new events), at most MaxPerPump of them.
		/// </summary>
		void Pump(const cs::Ref<StarSystem>& at, int systemId, double jd, std::vector<Chatter>& out);

		/// <summary>Forgets the cursor (a load, a new listener).</summary>
		void Clear()
		{
			world = nullptr;
			channels.Clear();
		}

	private:
		void Translate(const StarSystem& at, const RadioChannelSet& set, int systemId, int eventIndex, std::vector<Chatter>& out) const;

		RadioChannels channels;
		/// <summary>The world the cursor reads (compared only, never dereferenced).</summary>
		const World* world = nullptr;
		/// <summary>The newest event read, and how many events at exactly that Jd were read.</summary>
		double cursorJd = 0;
		int readAtCursor = 0;
	};
}
