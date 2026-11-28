// US-T968: Light Society's columnar agent store.

#include "LightSociety/AgentStore.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t StoreTag = FourCC('S', 'T', 'O', 'R');
		/// <summary>Bytes of one profile in the blob.</summary>
		constexpr size_t ProfileBytes = 8 + 4 + 4 + 4 + 3 + 5 + 2;
		const MemoryEntry EmptyMemory{};
	}

	void AgentStore::Reset(uint32_t topicCount, uint32_t memoryK)
	{
		*this = AgentStore();
		topics = std::clamp(topicCount, 1u, MaxTopics);
		memK = std::clamp(memoryK, 1u, MaxMemory);
	}

	AgentId AgentStore::Join(const Profile& profile)
	{
		AgentId id;
		if (!freeSlots.empty())
		{
			id = freeSlots.back();
			freeSlots.pop_back();
		}
		else
		{
			if (capacity >= MaxAgents) return NoAgent;
			id = capacity++;
			profiles.emplace_back();
			stance.resize(static_cast<size_t>(capacity) * topics);
			confidence.resize(static_cast<size_t>(capacity) * topics);
			memory.resize(static_cast<size_t>(capacity) * memK);
			memoryHead.push_back(0);
			trust.push_back(0);
			express.push_back(0);
			group.push_back(NoGroup);
			row.push_back(NoRow);
			if ((id >> 6) >= active.size()) active.push_back(0);
		}

		profiles[id] = profile;
		std::fill_n(stance.begin() + static_cast<ptrdiff_t>(id) * topics, topics, int16_t{0});
		std::fill_n(confidence.begin() + static_cast<ptrdiff_t>(id) * topics, topics, uint8_t{0});
		std::fill_n(memory.begin() + static_cast<ptrdiff_t>(id) * memK, memK, MemoryEntry{});
		memoryHead[id] = 0;
		trust[id] = NeutralTrust;
		express[id] = 0;
		group[id] = profile.group;
		row[id] = NoRow;
		active[id >> 6] |= uint64_t{1} << (id & 63);
		activeCount++;
		return id;
	}

	bool AgentStore::Leave(AgentId id)
	{
		if (!IsActive(id)) return false;
		active[id >> 6] &= ~(uint64_t{1} << (id & 63));
		activeCount--;
		freeSlots.push_back(id);
		return true;
	}

	void AgentStore::Remember(AgentId id, const MemoryEntry& entry)
	{
		uint8_t& head = memoryHead[id];
		memory[static_cast<size_t>(id) * memK + head] = entry;
		head = static_cast<uint8_t>((head + 1u) % memK);
	}

	const MemoryEntry& AgentStore::Recall(AgentId id, uint32_t i) const
	{
		if (i >= memK) return EmptyMemory;
		const uint32_t slot = (memoryHead[id] + memK - 1 - i) % memK;
		return memory[static_cast<size_t>(id) * memK + slot];
	}

	void AgentStore::Write(Writer& w) const
	{
		w.U32(StoreTag);
		w.U32(topics);
		w.U32(memK);
		w.U32(capacity);
		w.U32(activeCount);
		for (const Profile& p : profiles)
		{
			w.U64(p.externalId);
			w.U32(p.group);
			w.U32(p.household);
			w.U32(p.business);
			w.U8(p.age);
			w.U8(p.cls);
			w.U8(p.edu);
			for (const uint8_t b : p.big5) w.U8(b);
			w.U16(p.traits);
		}
		for (const int16_t s : stance) w.I16(s);
		for (const uint8_t c : confidence) w.U8(c);
		for (const MemoryEntry& m : memory)
		{
			w.U16(m.kind);
			w.U8(m.topic);
			w.I8(m.valence);
			w.U16(m.day);
		}
		for (size_t i = 0; i < capacity; i++)
		{
			w.U8(memoryHead[i]);
			w.U8(trust[i]);
			w.U8(express[i]);
			w.U32(group[i]);
			w.U32(row[i]);
		}
		for (const uint64_t word : active) w.U64(word);
		w.Array(std::span<const AgentId>(freeSlots));
	}

	bool AgentStore::Read(Reader& r)
	{
		if (!r.Expect(StoreTag)) return false;
		AgentStore s;
		s.topics = r.U32();
		s.memK = r.U32();
		if (!r.Ok() || s.topics < 1 || s.topics > MaxTopics || s.memK < 1 || s.memK > MaxMemory)
		{
			r.Fail();
			return false;
		}
		const size_t perAgent = ProfileBytes + s.topics * 3u + s.memK * 6u + 11u;
		s.capacity = r.Count(perAgent, MaxAgents);
		const uint32_t storedActive = r.U32();
		if (!r.Ok()) return false;

		const size_t n = s.capacity;
		s.profiles.resize(n);
		for (Profile& p : s.profiles)
		{
			p.externalId = r.U64();
			p.group = r.U32();
			p.household = r.U32();
			p.business = r.U32();
			p.age = r.U8();
			p.cls = r.U8();
			p.edu = r.U8();
			for (uint8_t& b : p.big5) b = r.U8();
			p.traits = r.U16();
		}
		s.stance.resize(n * s.topics);
		for (int16_t& v : s.stance) v = r.I16();
		s.confidence.resize(n * s.topics);
		for (uint8_t& v : s.confidence) v = r.U8();
		s.memory.resize(n * s.memK);
		for (MemoryEntry& m : s.memory)
		{
			m.kind = r.U16();
			m.topic = r.U8();
			m.valence = r.I8();
			m.day = r.U16();
		}
		s.memoryHead.resize(n);
		s.trust.resize(n);
		s.express.resize(n);
		s.group.resize(n);
		s.row.resize(n);
		for (size_t i = 0; i < n; i++)
		{
			s.memoryHead[i] = r.U8();
			s.trust[i] = r.U8();
			s.express[i] = r.U8();
			s.group[i] = r.U32();
			s.row[i] = r.U32();
			if (s.memoryHead[i] >= s.memK) r.Fail();
		}
		s.active.resize((n + 63) / 64);
		for (uint64_t& word : s.active) word = r.U64();
		r.Array(s.freeSlots, s.capacity);
		if (!r.Ok()) return false;

		// The active set and the free list must partition the slots exactly.
		uint32_t count = 0;
		for (size_t i = 0; i < s.active.size(); i++)
		{
			const uint64_t word = s.active[i];
			const size_t bits = std::min<size_t>(64, n - i * 64);
			if (bits < 64 && (word >> bits) != 0) r.Fail();
			count += static_cast<uint32_t>(std::popcount(word));
		}
		s.activeCount = count;
		if (count != storedActive || s.freeSlots.size() != n - count) r.Fail();
		std::vector<bool> seen(n);
		for (const AgentId id : s.freeSlots)
		{
			if (id >= n || s.IsActive(id) || seen[id])
			{
				r.Fail();
				break;
			}
			seen[id] = true;
		}
		if (!r.Ok()) return false;

		*this = std::move(s);
		return true;
	}
}
