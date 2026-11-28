#pragma once
// US-T970: Light Society's event queue Q, a min-heap over (time, priority, sequence) so the pop order is a total order
// whatever the push history, and the paper's event aggregation: every event sharing one (time, priority) pops as one
// concurrent batch, grouped by kind.

#include "LightSociety/Codec.h"
#include "LightSociety/Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>An event in the queue with the sequence number it was given when pushed.</summary>
	struct QueuedEvent
	{
		EnvEvent event;
		uint64_t seq = 0;
	};

	/// <summary>
	/// Events in (time, priority, seq) order: earlier time first, then lower priority value, then earlier push.
	/// </summary>
	class EventQueue
	{
	public:
		/// <summary>Adds an event and returns the sequence number it was given.</summary>
		uint64_t Push(const EnvEvent& e);

		bool Empty() const { return heap.empty(); }
		size_t Size() const { return heap.size(); }
		/// <summary>The next event to pop; the queue must not be empty.</summary>
		const QueuedEvent& Top() const { return heap.front(); }

		/// <summary>
		/// Pops every event sharing the next (time, priority) into <paramref name="out"/> (cleared first), grouped by kind
		/// in ascending kind order and in push order within a kind.
		/// </summary>
		void PopBatch(std::vector<QueuedEvent>& out);

		void Clear();

		/// <summary>Appends the queue as one section of a blob.</summary>
		void Write(Writer& w) const;
		/// <summary>Reads a section <see cref="Write"/> wrote; on failure the reader fails and the queue is unchanged.</summary>
		bool Read(Reader& r);

	private:
		std::vector<QueuedEvent> heap;
		uint64_t nextSeq = 0;
	};

	void WriteEvent(Writer& w, const EnvEvent& e);
	/// <summary>Reads what <see cref="WriteEvent"/> wrote; fails the reader on an unknown scope or a non-finite value.</summary>
	EnvEvent ReadEvent(Reader& r);
}
