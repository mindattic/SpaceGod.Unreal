#pragma once
// US-T971: Light Society's router, the paper's mixture-of-models engine: f_pi over a backend table. Each sample goes to
// the teacher or the cheaper tier (a lookup table if one is registered for the op, else a surrogate) by the config's
// policy, the batch is split per backend and the answers are merged back in the original order, through an exact cache.

#include "LightSociety/Backend.h"
#include "LightSociety/Engine.h"
#include "LightSociety/LruCache.h"
#include "LightSociety/Teacher.h"
#include "LightSociety/Types.h"

#include <cstdint>
#include <mutex>
#include <span>

namespace MindAttic::Society
{
	/// <summary>Who a sample is for: the agent and round of its routing draw.</summary>
	struct SampleKey
	{
		AgentId agent = NoAgent;
		uint32_t round = 0;
	};

	/// <summary>
	/// Policy AllTeacher sends every sample to the teacher, AllSurrogate every sample to the cheaper tier, and Weighted(w)
	/// a sample to the cheaper tier when Rng::Roll(Hash(seed, agent, round, op)) &lt; w, so the split is the same on every
	/// run and thread count. With no cheaper tier registered for an op, every sample goes to the teacher (the registered
	/// one, else the router's own <see cref="Teacher"/>). Register backends before running, not during; Route and Decide
	/// may then run on several threads at once.
	/// </summary>
	class Router final : public IPolicy
	{
	public:
		/// <summary>Answers the router keeps by default.</summary>
		static constexpr uint32_t DefaultCacheEntries = 1u << 16;

		Router();

		/// <summary>Registers a backend for one op and tier (null unregisters) and empties the cache.</summary>
		bool Register(BackendKind kind, Op op, const IBackend* backend);
		/// <summary>Registers a backend for every op it supports and empties the cache.</summary>
		void RegisterAll(BackendKind kind, const IBackend* backend);
		const BackendTable& Backends() const { return table; }

		/// <summary>Empties the cache and sets how many answers it keeps; 0 turns it off.</summary>
		void SetCacheEntries(uint32_t entries);
		uint64_t CacheHits() const;
		uint64_t CacheMisses() const;

		/// <summary>The tier a sample goes to.</summary>
		BackendKind Pick(const Config& config, Op op, SampleKey key) const;

		/// <summary>
		/// Answers a batch: keys[i] is row i's sample (a missing key routes as <see cref="NoAgent"/> in round 0). Rows are
		/// quantized (QuantizeFeature) before any backend sees them. out.values is sized to Rows() * OutputWidth(op).
		/// </summary>
		void Route(const Config& config, const OpBatch& batch, std::span<const SampleKey> keys, OpOut& out);

		/// <summary>f_pi: one <see cref="Op::Shift"/> row per perception, then the same changes and speech as <see cref="DefaultPolicy"/>.</summary>
		void Decide(const PolicyBatch& batch, PolicyOut& out) override;

		/// <summary>The <see cref="Op::Shift"/> feature row of one perception (unquantized).</summary>
		static void PersuasionRow(const PolicyBatch& batch, const Perception& p, std::span<float> row);

	private:
		const IBackend& Resolve(BackendKind kind, Op op) const;

		BackendTable table;
		Teacher teacher;
		LruCache cache;
		mutable std::mutex cacheLock;
	};
}
