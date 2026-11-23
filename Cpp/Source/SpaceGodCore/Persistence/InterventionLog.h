#pragma once
// US-T474 (docs/REWIND.md, "intervention log"): what the player did, frame by frame, so a rewind can replay to any tick
// and not only land on a keyframe. Each entry is stamped with the tick it happened on: the frame serial of the app's
// RewindFrame log (UnitySpaceApp.Rewind), so the frame log holds each tick's dt and clock and this log what was done on it.
//
// No callers yet. The log only stores and serialises; it never touches the world.
//
// Text form (Write / Read), one entry a line, each ending in '\n':
// <code>
// <delta ticks> <kind> <seat> <text>
// </code>
// where delta is the tick less the previous entry's (the first entry's is its tick), kind one letter (c control,
// m command, o order, k clock), seat an int (-1: none) and text the rest of the line (left off with its space when
// empty). A session command's text is its CommandCodec line. Ticks and seats are written as invariant ints.

#include "Cs/Cs.h"

#include <cstddef>
#include <map>
#include <vector>

namespace SpaceGod::Core
{
	class IGameCommand;

	enum class InterventionKind
	{
		/// <summary>A seat's control frame (stick, throttle, buttons); one identical to the seat's last is dropped.</summary>
		Control,
		/// <summary>A session command (take, release, host, comms): its CommandCodec line.</summary>
		Command,
		/// <summary>An order given to a unit or fleet.</summary>
		Order,
		/// <summary>A clock change (pause, warp).</summary>
		Clock,
	};

	struct InterventionEntry
	{
		/// <summary>The RewindFrame serial the entry happened on.</summary>
		long long Tick = 0;
		InterventionKind Kind = InterventionKind::Command;
		/// <summary>The seat that acted (-1: none, e.g. a clock change).</summary>
		int Seat = -1;
		/// <summary>What was done, one line (no '\n' or '\r').</summary>
		cs::String Text;

		bool operator==(const InterventionEntry& o) const
		{
			return Tick == o.Tick && Kind == o.Kind && Seat == o.Seat && Text == o.Text;
		}
		bool operator!=(const InterventionEntry& o) const { return !(*this == o); }
	};

	class SPACEGODCORE_API InterventionLog final
	{
	public:
		/// <summary>Adds an entry at tick (not before the last entry's tick; throws ArgumentException if it is, or if the
		/// text holds a line break). A Control entry identical to its seat's previous control frame is dropped: a stick
		/// stays where it is put. Returns whether the entry was kept.</summary>
		bool Add(long long tick, InterventionKind kind, int seat, const cs::String& text);

		/// <summary>A session command as its CommandCodec line: a SeatControlCommand as a Control entry (so it is
		/// dropped when unchanged), anything else as a Command; the seat is the command's player seat.</summary>
		bool AddCommand(long long tick, const cs::Ref<IGameCommand>& command);

		/// <summary>The session command a Command or Control entry holds (CommandCodec::Read of its text).</summary>
		static cs::Ref<IGameCommand> CommandOf(const InterventionEntry& entry);

		/// <summary>Drops every entry at tick or later: the future a rewind replaced.</summary>
		void DropFrom(long long tick);

		/// <summary>Drops the entries before tick, to follow the frame log's horizon. Each seat's last control frame
		/// before tick is kept (at its own tick), since the stick is still where it put it.</summary>
		void DropBefore(long long tick);

		void Clear();

		const std::vector<InterventionEntry>& Entries() const { return entries; }
		size_t Count() const { return entries.size(); }
		bool Empty() const { return entries.empty(); }

		/// <summary>The compact text form (see the top of this file).</summary>
		cs::String Write() const;

		/// <summary>A log read back from Write's text (throws FormatException on a malformed line).</summary>
		static InterventionLog Read(const cs::String& text);

		/// <summary>The bytes Write's text takes.</summary>
		size_t ByteSize() const;

		bool operator==(const InterventionLog& o) const { return entries == o.entries; }
		bool operator!=(const InterventionLog& o) const { return !(*this == o); }

		static char KindLetter(InterventionKind kind);

	private:
		void RebuildLastControl();

		std::vector<InterventionEntry> entries;
		/// <summary>Each seat's last kept control frame's text, for dropping a repeat.</summary>
		std::map<int, cs::String> lastControl;
	};
}
