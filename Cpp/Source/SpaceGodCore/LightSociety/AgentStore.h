#pragma once
// US-T968: Light Society's agent state as a columnar store (one array per field), so a pass over one field for every
// agent is a linear walk of one array.

#include "LightSociety/Codec.h"
#include "LightSociety/Types.h"

#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>One thing an agent remembers. Kind 0 is an empty entry.</summary>
	struct MemoryEntry
	{
		uint16_t kind = 0;
		uint8_t topic = 0;
		int8_t valence = 0;
		uint16_t day = 0;
	};

	/// <summary>
	/// Every agent's state, by column: the static profile; the internal state (a Q15 stance and a confidence byte per
	/// topic, a ring of the last K memories, trust, desire to express); the external state (group, row in the social
	/// graph). Agents join into the most recently freed slot, else a new one, and leave by
	/// clearing their bit in the active set, so ids stay dense and every walk is in ascending id order whatever the
	/// history: deterministic.
	/// </summary>
	class AgentStore
	{
	public:
		/// <summary>Row of an agent not (yet) in the social graph.</summary>
		static constexpr uint32_t NoRow = 0xFFFFFFFFu;
		static constexpr uint8_t NeutralTrust = 128;

		/// <summary>Empties the store and sets its shape; topics and memoryK are clamped to their maxima.</summary>
		void Reset(uint32_t topicCount, uint32_t memoryK);

		/// <summary>Adds an agent with neutral stances; <see cref="NoAgent"/> when the store holds <see cref="MaxAgents"/>.</summary>
		AgentId Join(const Profile& profile);
		/// <summary>Removes an agent; false when it was not active.</summary>
		bool Leave(AgentId id);

		bool IsActive(AgentId id) const
		{
			return id < capacity && ((active[id >> 6] >> (id & 63)) & 1u) != 0;
		}

		/// <summary>Slots in use or free: every id is below this.</summary>
		uint32_t Capacity() const { return capacity; }
		uint32_t ActiveCount() const { return activeCount; }
		uint32_t Topics() const { return topics; }
		uint32_t MemoryK() const { return memK; }

		/// <summary>Calls f(AgentId) for every active agent, in ascending id order.</summary>
		template <class F>
		void ForEachActive(F&& f) const
		{
			ForEachActiveIn(0, capacity, f);
		}

		/// <summary>Calls f(AgentId) for every active agent with first &lt;= id &lt; last, in ascending id order (one chunk).</summary>
		template <class F>
		void ForEachActiveIn(uint32_t first, uint32_t last, F&& f) const
		{
			if (last > capacity) last = capacity;
			for (uint32_t id = first; id < last;)
			{
				uint64_t word = active[id >> 6] >> (id & 63);
				if (word == 0)
				{
					id = (id | 63) + 1;
					continue;
				}
				id += static_cast<uint32_t>(std::countr_zero(word));
				if (id >= last) break;
				f(static_cast<AgentId>(id));
				id++;
			}
		}

		// Static.
		const Profile& GetProfile(AgentId id) const { return profiles[id]; }

		// Internal.
		int16_t& Stance(AgentId id, uint32_t topic) { return stance[static_cast<size_t>(id) * topics + topic]; }
		int16_t Stance(AgentId id, uint32_t topic) const { return stance[static_cast<size_t>(id) * topics + topic]; }
		uint8_t& Confidence(AgentId id, uint32_t topic) { return confidence[static_cast<size_t>(id) * topics + topic]; }
		uint8_t Confidence(AgentId id, uint32_t topic) const { return confidence[static_cast<size_t>(id) * topics + topic]; }
		/// <summary>Every agent's stances, agent-major (Topics() per agent): for vectorized passes.</summary>
		std::span<int16_t> StanceColumn() { return stance; }
		std::span<uint8_t> ConfidenceColumn() { return confidence; }

		/// <summary>Writes a memory over the agent's oldest one.</summary>
		void Remember(AgentId id, const MemoryEntry& entry);
		/// <summary>The agent's i-th most recent memory (0 = newest); an empty entry past what it remembers.</summary>
		const MemoryEntry& Recall(AgentId id, uint32_t i) const;
		/// <summary>The agent's whole ring, in slot order.</summary>
		std::span<MemoryEntry> Memory(AgentId id) { return {memory.data() + static_cast<size_t>(id) * memK, memK}; }

		uint8_t& Trust(AgentId id) { return trust[id]; }
		uint8_t Trust(AgentId id) const { return trust[id]; }
		uint8_t& Express(AgentId id) { return express[id]; }
		uint8_t Express(AgentId id) const { return express[id]; }

		// External.
		GroupId& Group(AgentId id) { return group[id]; }
		GroupId Group(AgentId id) const { return group[id]; }
		uint32_t& Row(AgentId id) { return row[id]; }
		uint32_t Row(AgentId id) const { return row[id]; }

		/// <summary>Appends the store as one section of a blob.</summary>
		void Write(Writer& w) const;
		/// <summary>Reads a section <see cref="Write"/> wrote; on failure the reader fails and the store is unchanged.</summary>
		bool Read(Reader& r);

	private:
		uint32_t topics = 1;
		uint32_t memK = 4;
		uint32_t capacity = 0;
		uint32_t activeCount = 0;

		std::vector<Profile> profiles;
		std::vector<int16_t> stance;
		std::vector<uint8_t> confidence;
		std::vector<MemoryEntry> memory;
		std::vector<uint8_t> memoryHead;
		std::vector<uint8_t> trust;
		std::vector<uint8_t> express;
		std::vector<GroupId> group;
		std::vector<uint32_t> row;

		std::vector<uint64_t> active;
		/// <summary>Freed slots, reused last-freed first.</summary>
		std::vector<AgentId> freeSlots;
	};
}
