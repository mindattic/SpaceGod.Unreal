#pragma once
// US-T973: Light Society's validation scenarios (docs/research/LightSociety-paper.md, "Scenarios validated"): opinion
// diffusion from influencers, continuous speak/heard/reflect dynamics, and the trust, dictator and ultimatum games. Each
// is a pure function of its spec and the config's seed: every draw is Rng::Hash, and chunked passes merge in chunk order.

#include "LightSociety/Router.h"
#include "LightSociety/Rng.h"
#include "LightSociety/Types.h"
#include "LightSociety/Worker.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>A scenario's output: one row of named columns per round, row-major.</summary>
	struct Series
	{
		std::vector<std::string> columns;
		std::vector<double> values;

		uint32_t Width() const { return static_cast<uint32_t>(columns.size()); }
		uint32_t Rows() const { return columns.empty() ? 0 : static_cast<uint32_t>(values.size() / columns.size()); }
		std::span<const double> Row(uint32_t r) const { return std::span<const double>(values).subspan(static_cast<size_t>(r) * Width(), Width()); }
		double At(uint32_t r, uint32_t c) const { return values[static_cast<size_t>(r) * Width() + c]; }
		/// <summary>Appends a row; short rows are padded with 0, long ones cut.</summary>
		void Add(std::span<const double> row)
		{
			for (uint32_t c = 0; c < Width(); c++) values.push_back(c < row.size() ? row[c] : 0.0);
		}
	};

	/// <summary>
	/// A scenario: Setup builds its society from the config (seed, chunk, router policy), each Step runs one round (a
	/// diffusion round, a day, a game) and appends its rows to Output. The router answers every decision; the pool, when
	/// given, runs the chunks (results never depend on its thread count). Both must outlive the scenario's steps.
	/// </summary>
	class Scenario
	{
	public:
		virtual ~Scenario() = default;
		virtual const char* Name() const = 0;
		virtual void Setup(const Config& config, Router& router, ChunkPool* pool) = 0;
		/// <summary>Runs the next round; false when every round has run (nothing is done then).</summary>
		virtual bool Step() = 0;
		/// <summary>Rounds run so far.</summary>
		virtual uint32_t Round() const = 0;
		virtual const Series& Output() const = 0;

		/// <summary>Steps until done or <paramref name="maxRounds"/> more have run.</summary>
		void Run(uint32_t maxRounds = 0xFFFFFFFFu)
		{
			for (uint32_t i = 0; i < maxRounds && Step(); i++) {}
		}
	};

	/// <summary>Starting stances of a diffusion run: half agree and half neutral, half disagree and half neutral, or a third each.</summary>
	enum class Seeding : uint8_t
	{
		AgreeNeutral = 0,
		DisagreeNeutral = 1,
		Random = 2,
	};

	/// <summary>The paper's influencer-influencee diffusion (2.3, 4.5).</summary>
	struct DiffusionSpec
	{
		uint32_t agents = 10000;
		/// <summary>Barabasi-Albert ties per new node.</summary>
		uint32_t m = 3;
		/// <summary>Share of agents, by degree, that are influencers (their ties among themselves are dropped).</summary>
		double influencerShare = 0.2;
		/// <summary>Share of influencers that fire each round (at least one).</summary>
		double fireShare = 0.01;
		Seeding seeding = Seeding::AgreeNeutral;
		uint32_t rounds = 200;
		/// <summary>The topic's prior mean and stubbornness, as the teacher reads them.</summary>
		float topicPrior = 0;
		float stubbornness = 0.3f;
		/// <summary>How hard an influencer pushes, in [0, 1].</summary>
		float magnitude = 1.0f;
	};

	/// <summary>The social graph of a continuous run.</summary>
	enum class Substrate : uint8_t
	{
		BarabasiAlbert = 0,
		ErdosRenyi = 1,
		Tree = 2,
	};

	/// <summary>The paper's continuous stance dynamics (2.4.4, 4.7): speak, heard, end-of-day reflection.</summary>
	struct ContinuousSpec
	{
		uint32_t agents = 2000;
		Substrate graph = Substrate::BarabasiAlbert;
		/// <summary>Barabasi-Albert ties per new node.</summary>
		uint32_t m = 3;
		/// <summary>Erdos-Renyi mean degree.</summary>
		double meanDegree = 6.0;
		/// <summary>Tree children per node.</summary>
		uint32_t branching = 3;
		uint32_t speakersPerSlot = 2;
		uint32_t days = 30;
		float priorMean = 0;
		float priorSpread = 0.6f;
		float stubbornness = 0.2f;
		/// <summary>How hard speech reaches a neighbour, in [0, 1].</summary>
		float magnitude = 0.6f;
		/// <summary>How far the end-of-day reflection moves an agent toward what it heard, in [0, 1].</summary>
		float reflect = 0.3f;
	};

	/// <summary>The paper's economic games (2.2, 4.2, 4.6).</summary>
	struct GamesSpec
	{
		/// <summary>Pairs of agents (first mover, second mover) playing each game.</summary>
		uint32_t pairs = 1000;
		/// <summary>The trust game's endowment.</summary>
		double endowment = 10.0;
		uint32_t repeatedTrustRounds = 7;
		uint32_t ultimatumRounds = 10;
	};

	/// <summary>
	/// Columns: round, fired, rows, agree, neutral, disagree (shares of influencees), then the influencees' class moves
	/// this round, from_to with classes a(gree), n(eutral), d(isagree). Each round a hashed fireShare of the influencers
	/// pushes its class at all its influencees in one same-time batch (one Op::Stance row per tie, through the router);
	/// an influencee takes the first move its rows draw, in row order.
	/// </summary>
	std::unique_ptr<Scenario> MakeDiffusion(const DiffusionSpec& spec);

	/// <summary>
	/// Columns: day, mean, sigma (of stance), supporter, skeptic (shares), heard (agents that heard speech that day).
	/// Runs on an <see cref="Engine"/> with the router as f_pi: each of config.slotsPerDay slots, speakersPerSlot agents
	/// picked by an expression-desire weighting speak to their neighbours (one batch); before the day ends every agent
	/// that heard something reflects on what it heard.
	/// </summary>
	std::unique_ptr<Scenario> MakeContinuous(const ContinuousSpec& spec);

	/// <summary>
	/// One round per game step: trust (once), dictator (once), repeated trust (repeatedTrustRounds), ultimatum (once),
	/// iterated ultimatum (ultimatumRounds). Columns: round, game (0 trust, 1 dictator, 2 repeated trust, 3 ultimatum,
	/// 4 iterated ultimatum), iteration, band kind (0 all, 1 class, 2 education, 3 age), band (0 low, 1 mid, 2 high;
	/// age under 30, 30-54, 55 up), count, send (of the endowment), return (share of what arrived), give, offer,
	/// reject (rate); a column a game does not have is NaN. Send, give and offer are banded by the first mover, return and
	/// reject by the second.
	/// </summary>
	std::unique_ptr<Scenario> MakeGames(const GamesSpec& spec);

	/// <summary>
	/// <see cref="ChunkPool::MapMerge"/> on the pool, or the same chunks one after the other without one: the result is
	/// the same either way.
	/// </summary>
	template <class Out, class Work, class Merge>
	void ForChunks(ChunkPool* pool, uint32_t count, uint32_t chunk, std::vector<Out>& outs, Work&& work, Merge&& merge)
	{
		if (pool)
		{
			pool->MapMerge(count, chunk, outs, work, merge);
			return;
		}
		const uint32_t size = chunk < 1 ? 1 : chunk;
		const uint32_t n = ChunkPool::Chunks(count, size);
		if (outs.size() < n) outs.resize(n);
		for (uint32_t i = 0; i < n; i++)
		{
			const uint64_t first = static_cast<uint64_t>(i) * size;
			const uint64_t last = first + size < count ? first + size : count;
			work(outs[i], static_cast<uint32_t>(first), static_cast<uint32_t>(last));
		}
		for (uint32_t i = 0; i < n; i++) merge(outs[i]);
	}

	/// <summary>A synthetic agent: age 18-80, class, education and Big Five hashed from (seed, id, op).</summary>
	inline Profile SyntheticProfile(uint64_t seed, AgentId id, uint32_t op, GroupId group = 0)
	{
		Profile p;
		p.externalId = 0;
		p.group = group;
		const uint64_t a = Rng::Hash(seed, id, 0, op);
		const uint64_t b = Rng::Hash(seed, id, 1, op);
		p.age = static_cast<uint8_t>(18 + a % 63);
		p.cls = static_cast<uint8_t>(a >> 8);
		p.edu = static_cast<uint8_t>(a >> 16);
		for (uint32_t i = 0; i < 5; i++) p.big5[i] = static_cast<uint8_t>(b >> (8 * i));
		return p;
	}
}
