#pragma once
// US-T968: Light Society's counter-based random numbers. Every draw is a pure function of (seed, agent, round, op), so
// results never depend on thread count, chunk order or how many draws other agents made.

#include <cstdint>

namespace MindAttic::Society::Rng
{
	/// <summary>SplitMix64's finaliser: a bijective 64-bit mix.</summary>
	constexpr uint64_t Mix(uint64_t z)
	{
		z += 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	/// <summary>The draw for one agent in one round of one operation.</summary>
	constexpr uint64_t Hash(uint64_t seed, uint32_t agent, uint32_t round, uint32_t op)
	{
		uint64_t h = Mix(seed);
		h = Mix(h ^ agent);
		return Mix(h ^ ((static_cast<uint64_t>(round) << 32) | op));
	}

	/// <summary>A hash as a double in [0, 1).</summary>
	constexpr double Unit(uint64_t h)
	{
		return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
	}

	/// <summary>True with probability <paramref name="p"/>.</summary>
	constexpr bool Roll(uint64_t h, double p)
	{
		return Unit(h) < p;
	}

	constexpr bool Roll(uint64_t seed, uint32_t agent, uint32_t round, uint32_t op, double p)
	{
		return Roll(Hash(seed, agent, round, op), p);
	}
}
