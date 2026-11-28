// US-T970: Light Society's event queue.

#include "LightSociety/EventQueue.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t QueueTag = FourCC('Q', 'U', 'E', 'U');
		/// <summary>Bytes of one queued event in the blob.</summary>
		constexpr size_t EventBytes = 8 + 1 + 2 + 1 + 4 + 4 + 1 + 4 + 4 + 8;
		/// <summary>Most events a blob's queue may hold.</summary>
		constexpr uint32_t MaxQueued = 1u << 26;

		/// <summary>True when <paramref name="a"/> pops after <paramref name="b"/>: the heap's ordering.</summary>
		bool Later(const QueuedEvent& a, const QueuedEvent& b)
		{
			if (a.event.time != b.event.time) return a.event.time > b.event.time;
			if (a.event.priority != b.event.priority) return a.event.priority > b.event.priority;
			return a.seq > b.seq;
		}
	}

	uint64_t EventQueue::Push(const EnvEvent& e)
	{
		heap.push_back({e, nextSeq});
		std::push_heap(heap.begin(), heap.end(), Later);
		return nextSeq++;
	}

	void EventQueue::PopBatch(std::vector<QueuedEvent>& out)
	{
		out.clear();
		if (heap.empty()) return;
		const double time = heap.front().event.time;
		const uint8_t priority = heap.front().event.priority;
		while (!heap.empty() && heap.front().event.time == time && heap.front().event.priority == priority)
		{
			std::pop_heap(heap.begin(), heap.end(), Later);
			out.push_back(heap.back());
			heap.pop_back();
		}
		// Popped in seq order; a stable sort by kind keeps push order within each kind.
		std::stable_sort(out.begin(), out.end(), [](const QueuedEvent& a, const QueuedEvent& b) { return a.event.kind < b.event.kind; });
	}

	void EventQueue::Clear()
	{
		heap.clear();
		nextSeq = 0;
	}

	void WriteEvent(Writer& w, const EnvEvent& e)
	{
		w.F64(e.time);
		w.U8(e.priority);
		w.U16(e.kind);
		w.U8(e.topic);
		w.F32(e.push);
		w.F32(e.magnitude);
		w.U8(static_cast<uint8_t>(e.scope));
		w.U32(e.target);
		w.U32(e.source);
	}

	EnvEvent ReadEvent(Reader& r)
	{
		EnvEvent e;
		e.time = r.F64();
		e.priority = r.U8();
		e.kind = r.U16();
		e.topic = r.U8();
		e.push = r.F32();
		e.magnitude = r.F32();
		const uint8_t scope = r.U8();
		e.target = r.U32();
		e.source = r.U32();
		if (scope > static_cast<uint8_t>(Scope::Neighbours) || e.topic >= MaxTopics || !std::isfinite(e.time)
			|| !std::isfinite(e.push) || !std::isfinite(e.magnitude))
		{
			r.Fail();
			return EnvEvent{};
		}
		e.scope = static_cast<Scope>(scope);
		return e;
	}

	void EventQueue::Write(Writer& w) const
	{
		w.U32(QueueTag);
		w.U64(nextSeq);
		w.U32(static_cast<uint32_t>(heap.size()));
		for (const QueuedEvent& q : heap)
		{
			WriteEvent(w, q.event);
			w.U64(q.seq);
		}
	}

	bool EventQueue::Read(Reader& r)
	{
		if (!r.Expect(QueueTag)) return false;
		const uint64_t seq = r.U64();
		const uint32_t n = r.Count(EventBytes, MaxQueued);
		if (!r.Ok()) return false;
		std::vector<QueuedEvent> h(n);
		for (QueuedEvent& q : h)
		{
			q.event = ReadEvent(r);
			q.seq = r.U64();
			if (q.seq >= seq) r.Fail();
		}
		if (!r.Ok()) return false;
		std::vector<uint64_t> seqs;
		seqs.reserve(n);
		for (const QueuedEvent& q : h) seqs.push_back(q.seq);
		std::sort(seqs.begin(), seqs.end());
		if (!std::is_heap(h.begin(), h.end(), Later) || std::adjacent_find(seqs.begin(), seqs.end()) != seqs.end())
		{
			r.Fail();
			return false;
		}
		heap = std::move(h);
		nextSeq = seq;
		return true;
	}
}
