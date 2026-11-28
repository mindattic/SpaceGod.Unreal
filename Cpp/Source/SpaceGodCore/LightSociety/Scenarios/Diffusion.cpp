// US-T973: the influencer-influencee opinion diffusion scenario (paper 2.3, 4.5).

#include "LightSociety/Scenarios/Scenario.h"

#include "LightSociety/Backend.h"
#include "LightSociety/Graph.h"
#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		// Random-draw operations of this scenario.
		constexpr uint32_t OpProfile = 0x5100;
		constexpr uint32_t OpGraph = 0x5101;
		constexpr uint32_t OpSeed = 0x5102;
		constexpr uint32_t OpConfidence = 0x5103;
		constexpr uint32_t OpFire = 0x5104;
		/// <summary>Salt of the seed for a tie's move draw, whose op is the influencer.</summary>
		constexpr uint64_t MoveSalt = 0x5105;

		// A discrete stance (StanceClass: 0 disagree, 1 neutral, 2 agree) as the teacher reads it: where the agent
		// stands, and how hard an influencer of that class pushes.
		constexpr float ClassStance[3] = {-0.6f, 0.0f, 0.6f};
		constexpr float ClassPush[3] = {-1.0f, 0.0f, 1.0f};

		struct Tie
		{
			AgentId influencer = NoAgent;
			AgentId influencee = NoAgent;
			uint8_t weight = 255;
		};

		struct Move
		{
			AgentId agent = NoAgent;
			uint8_t to = 1;
		};

		struct ChunkOut
		{
			std::vector<float> features;
			std::vector<SampleKey> keys;
			OpOut answer;
			std::vector<Move> moves;
		};

		class Diffusion final : public Scenario
		{
		public:
			explicit Diffusion(const DiffusionSpec& s) : spec(s) {}

			const char* Name() const override { return "diffusion"; }
			void Setup(const Config& cfg, Router& r, ChunkPool* p) override;
			bool Step() override;
			uint32_t Round() const override { return round; }
			const Series& Output() const override { return series; }

		private:
			void FillRow(const Tie& tie, std::span<float> row) const;
			void Resolve(ChunkOut& out, uint32_t first, uint32_t last) const;

			DiffusionSpec spec;
			Config config;
			Router* router = nullptr;
			ChunkPool* pool = nullptr;
			Graph graph;
			std::vector<Profile> profiles;
			std::vector<uint8_t> classes;
			std::vector<uint8_t> confidence;
			/// <summary>Ascending ids.</summary>
			std::vector<AgentId> influencers;
			std::vector<uint8_t> isInfluencer;
			std::vector<Tie> ties;
			std::vector<ChunkOut> outs;
			std::vector<uint8_t> moved;
			uint32_t round = 0;
			Series series;
		};

		void Diffusion::Setup(const Config& cfg, Router& r, ChunkPool* p)
		{
			config = cfg;
			config.chunk = std::max(config.chunk, 1u);
			router = &r;
			pool = p;
			round = 0;
			series = Series{};
			series.columns = {"round", "fired", "rows", "agree", "neutral", "disagree", "a_n", "a_d", "n_a", "n_d", "d_a", "d_n"};

			const uint32_t m = std::max(spec.m, 1u);
			const uint32_t n = std::min(std::max(spec.agents, m + 2), MaxAgents);
			profiles.resize(n);
			for (uint32_t i = 0; i < n; i++) profiles[i] = SyntheticProfile(config.seed, i, OpProfile);

			std::vector<AgentId> nodes(n);
			std::iota(nodes.begin(), nodes.end(), 0u);
			std::vector<Edge> edges;
			BarabasiAlbert(nodes, m, config.seed, OpGraph, edges);
			graph.BuildFrom(n, edges);
			influencers = TopByDegree(graph, nodes, std::clamp(spec.influencerShare, 0.0, 1.0));
			graph.DropEdgesAmong(influencers);
			std::sort(influencers.begin(), influencers.end());
			isInfluencer.assign(n, 0);
			for (const AgentId a : influencers) isInfluencer[a] = 1;

			classes.resize(n);
			confidence.resize(n);
			for (uint32_t i = 0; i < n; i++)
			{
				const double u = Rng::Unit(Rng::Hash(config.seed, i, 0, OpSeed));
				switch (spec.seeding)
				{
				case Seeding::AgreeNeutral: classes[i] = static_cast<uint8_t>(u < 0.5 ? 2 : 1); break;
				case Seeding::DisagreeNeutral: classes[i] = static_cast<uint8_t>(u < 0.5 ? 0 : 1); break;
				default: classes[i] = static_cast<uint8_t>(std::min(2.0, u * 3.0)); break;
				}
				confidence[i] = ToU8(static_cast<float>(0.3 + 0.4 * Rng::Unit(Rng::Hash(config.seed, i, 0, OpConfidence))));
			}
			moved.assign(n, 0);
		}

		void Diffusion::FillRow(const Tie& tie, std::span<float> row) const
		{
			std::fill(row.begin(), row.end(), 0.0f);
			const Profile& self = profiles[tie.influencee];
			const Profile& other = profiles[tie.influencer];
			row[Feature::SelfStance] = ClassStance[classes[tie.influencee]];
			row[Feature::SelfConfidence] = FromU8(confidence[tie.influencee]);
			row[Feature::SelfEducation] = FromU8(self.edu);
			row[Feature::SelfClass] = FromU8(self.cls);
			row[Feature::SelfAge] = std::min(1.0f, static_cast<float>(self.age) / 100.0f);
			row[Feature::SelfOpenness] = FromU8(self.big5[0]);
			row[Feature::SelfAgreeableness] = FromU8(self.big5[3]);
			row[Feature::SelfNeuroticism] = FromU8(self.big5[4]);
			row[Feature::SelfTrust] = FromU8(self.big5[3]);
			row[Feature::OtherStance] = ClassPush[classes[tie.influencer]];
			row[Feature::OtherEducation] = FromU8(other.edu);
			row[Feature::OtherClass] = FromU8(other.cls);
			row[Feature::OtherAge] = std::min(1.0f, static_cast<float>(other.age) / 100.0f);
			row[Feature::Reach] = std::clamp(spec.magnitude, 0.0f, 1.0f) * FromU8(tie.weight);
			row[Feature::Stubbornness] = std::clamp(spec.stubbornness, 0.0f, 1.0f);
			row[Feature::TopicPrior] = std::clamp(spec.topicPrior, -1.0f, 1.0f);
			row[Feature::Speech] = 1.0f;
		}

		void Diffusion::Resolve(ChunkOut& out, uint32_t first, uint32_t last) const
		{
			const uint32_t count = last - first;
			out.features.assign(static_cast<size_t>(count) * FeatureCount, 0.0f);
			out.keys.resize(count);
			out.moves.clear();
			for (uint32_t i = 0; i < count; i++)
			{
				const Tie& tie = ties[first + i];
				FillRow(tie, std::span<float>(out.features).subspan(static_cast<size_t>(i) * FeatureCount, FeatureCount));
				out.keys[i] = {tie.influencee, round};
			}
			router->Route(config, OpBatch{Op::Stance, out.features}, out.keys, out.answer);
			for (uint32_t i = 0; i < count; i++)
			{
				const Tie& tie = ties[first + i];
				const std::span<const float> p = std::as_const(out.answer).Row(Op::Stance, i);
				const double u = Rng::Unit(Rng::Hash(config.seed ^ MoveSalt, tie.influencee, round, tie.influencer));
				const uint8_t to = static_cast<uint8_t>(u < p[0] ? 0 : (u < static_cast<double>(p[0]) + p[1] ? 1 : 2));
				if (to != classes[tie.influencee]) out.moves.push_back({tie.influencee, to});
			}
		}

		bool Diffusion::Step()
		{
			if (!router || round >= spec.rounds) return false;
			round++;

			// The influencers that fire: the k with the lowest draws this round, in id order.
			const uint32_t total = static_cast<uint32_t>(influencers.size());
			const uint32_t k = total == 0 ? 0
				: std::clamp(static_cast<uint32_t>(std::llround(std::clamp(spec.fireShare, 0.0, 1.0) * total)), 1u, total);
			std::vector<std::pair<uint64_t, AgentId>> draws;
			draws.reserve(total);
			for (const AgentId a : influencers) draws.emplace_back(Rng::Hash(config.seed, a, round, OpFire), a);
			std::nth_element(draws.begin(), draws.begin() + k, draws.end());
			std::vector<AgentId> fired;
			fired.reserve(k);
			for (uint32_t i = 0; i < k; i++) fired.push_back(draws[i].second);
			std::sort(fired.begin(), fired.end());

			// One aggregated batch at one timestamp: every fired influencer at every one of its influencees.
			ties.clear();
			for (const AgentId f : fired)
			{
				const std::span<const AgentId> to = graph.Neighbours(f);
				const std::span<const uint8_t> w = graph.Weights(f);
				for (size_t j = 0; j < to.size(); j++)
				{
					if (!isInfluencer[to[j]]) ties.push_back({f, to[j], w[j]});
				}
			}

			uint32_t moves[3][3] = {};
			ForChunks(pool, static_cast<uint32_t>(ties.size()), config.chunk, outs,
				[this](ChunkOut& out, uint32_t first, uint32_t last) { Resolve(out, first, last); },
				[&](const ChunkOut& out) {
					for (const Move& mv : out.moves)
					{
						if (moved[mv.agent]) continue;
						moved[mv.agent] = 1;
						moves[classes[mv.agent]][mv.to]++;
						classes[mv.agent] = mv.to;
					}
				});
			for (const Tie& tie : ties) moved[tie.influencee] = 0;

			uint32_t held[3] = {};
			uint32_t influencees = 0;
			for (uint32_t a = 0; a < classes.size(); a++)
			{
				if (isInfluencer[a]) continue;
				held[classes[a]]++;
				influencees++;
			}
			const double scale = influencees ? 1.0 / influencees : 0.0;
			const double row[] = {static_cast<double>(round), static_cast<double>(k), static_cast<double>(ties.size()),
				held[2] * scale, held[1] * scale, held[0] * scale,
				static_cast<double>(moves[2][1]), static_cast<double>(moves[2][0]), static_cast<double>(moves[1][2]),
				static_cast<double>(moves[1][0]), static_cast<double>(moves[0][2]), static_cast<double>(moves[0][1])};
			series.Add(row);
			return true;
		}
	}

	std::unique_ptr<Scenario> MakeDiffusion(const DiffusionSpec& spec)
	{
		return std::make_unique<Diffusion>(spec);
	}
}
