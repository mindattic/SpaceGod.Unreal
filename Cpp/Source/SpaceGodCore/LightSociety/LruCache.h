#pragma once
// US-T971: Light Society's answer cache, the paper's exact prompt cache: a key's 64-bit hash finds a slot, the full key
// bytes are compared before a hit counts, and the least recently used entry is evicted when the cache is full. A hit
// returns exactly the values that were inserted, so a cache changes how fast answers come, never what they are.

#include "LightSociety/Rng.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>
	/// Fixed-size keys and values, at most Capacity() entries. Eviction follows the order of calls only, so the same calls
	/// leave the same entries. Not thread-safe.
	/// </summary>
	class LruCache
	{
	public:
		/// <summary>Empties the cache and sets its shape; a capacity of 0 turns it off.</summary>
		void Reset(uint32_t capacity, uint32_t keyBytes, uint32_t valueWidth)
		{
			cap = capacity;
			keySize = keyBytes;
			width = valueWidth;
			keys.assign(static_cast<size_t>(cap) * keySize, 0);
			values.assign(static_cast<size_t>(cap) * width, 0.0f);
			hashes.assign(cap, 0);
			prev.assign(cap, None);
			next.assign(cap, None);
			index.clear();
			index.reserve(cap);
			used = 0;
			head = None;
			tail = None;
			hits = 0;
			misses = 0;
		}

		/// <summary>Drops every entry, keeping the shape.</summary>
		void Clear() { Reset(cap, keySize, width); }

		uint32_t Capacity() const { return cap; }
		uint32_t Size() const { return used; }
		uint32_t KeyBytes() const { return keySize; }
		uint32_t ValueWidth() const { return width; }
		uint64_t Hits() const { return hits; }
		uint64_t Misses() const { return misses; }

		static uint64_t HashKey(std::span<const uint8_t> key)
		{
			uint64_t h = Rng::Mix(key.size());
			size_t i = 0;
			for (; i + 8 <= key.size(); i += 8)
			{
				uint64_t word;
				std::memcpy(&word, key.data() + i, 8);
				h = Rng::Mix(h ^ word);
			}
			if (i < key.size())
			{
				uint64_t word = 0;
				std::memcpy(&word, key.data() + i, key.size() - i);
				h = Rng::Mix(h ^ word);
			}
			return h;
		}

		/// <summary>Copies the key's values into out (ValueWidth() floats) and marks it most recent; false on a miss.</summary>
		bool Find(std::span<const uint8_t> key, std::span<float> out)
		{
			if (cap == 0 || key.size() != keySize || out.size() < width) return false;
			const auto it = index.find(HashKey(key));
			if (it == index.end() || std::memcmp(keys.data() + static_cast<size_t>(it->second) * keySize, key.data(), keySize) != 0)
			{
				misses++;
				return false;
			}
			const uint32_t slot = it->second;
			std::memcpy(out.data(), values.data() + static_cast<size_t>(slot) * width, sizeof(float) * width);
			Unlink(slot);
			PushFront(slot);
			hits++;
			return true;
		}

		/// <summary>
		/// Stores a key's values as the most recent entry, evicting the least recent when full. A key whose hash collides
		/// with another's replaces it.
		/// </summary>
		void Insert(std::span<const uint8_t> key, std::span<const float> value)
		{
			if (cap == 0 || key.size() != keySize || value.size() < width) return;
			const uint64_t h = HashKey(key);
			uint32_t slot;
			const auto it = index.find(h);
			if (it != index.end())
			{
				slot = it->second;
				Unlink(slot);
			}
			else if (used < cap)
			{
				slot = used++;
				index.emplace(h, slot);
			}
			else
			{
				slot = tail;
				Unlink(slot);
				index.erase(hashes[slot]);
				index.emplace(h, slot);
			}
			hashes[slot] = h;
			std::memcpy(keys.data() + static_cast<size_t>(slot) * keySize, key.data(), keySize);
			std::memcpy(values.data() + static_cast<size_t>(slot) * width, value.data(), sizeof(float) * width);
			PushFront(slot);
		}

	private:
		static constexpr uint32_t None = 0xFFFFFFFFu;

		void Unlink(uint32_t slot)
		{
			if (prev[slot] != None) next[prev[slot]] = next[slot];
			else head = next[slot];
			if (next[slot] != None) prev[next[slot]] = prev[slot];
			else tail = prev[slot];
			prev[slot] = None;
			next[slot] = None;
		}

		void PushFront(uint32_t slot)
		{
			prev[slot] = None;
			next[slot] = head;
			if (head != None) prev[head] = slot;
			head = slot;
			if (tail == None) tail = slot;
		}

		uint32_t cap = 0;
		uint32_t keySize = 0;
		uint32_t width = 0;
		std::vector<uint8_t> keys;
		std::vector<float> values;
		std::vector<uint64_t> hashes;
		/// <summary>Recency list: head is the most recent, tail the next to evict.</summary>
		std::vector<uint32_t> prev;
		std::vector<uint32_t> next;
		std::unordered_map<uint64_t, uint32_t> index;
		uint32_t used = 0;
		uint32_t head = None;
		uint32_t tail = None;
		uint64_t hits = 0;
		uint64_t misses = 0;
	};
}
