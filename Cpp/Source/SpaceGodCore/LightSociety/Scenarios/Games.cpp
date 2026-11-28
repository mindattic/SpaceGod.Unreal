// US-T973: the economic games scenario (paper 2.2, 4.2, 4.6): trust, dictator, repeated trust, ultimatum single and iterated.

#include "LightSociety/Scenarios/Scenario.h"

#include "LightSociety/Backend.h"
#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MindAttic::Society
{
	namespace
	{
		// Random-draw operations of this scenario.
		constexpr uint32_t OpProfile = 0x5300;
		constexpr uint32_t OpReject = 0x5301;

		enum class Game : uint8_t
		{
			Trust = 0,
			Dictator = 1,
			RepeatedTrust = 2,
			Ultimatum = 3,
			IteratedUltimatum = 4,
		};

		struct Stage
		{
			Game game = Game::Trust;
			uint32_t iteration = 0;
			uint32_t of = 1;
		};

		// The measures of a round, per pair: send (share of the endowment), return (share of what arrived), give, offer,
		// reject (0 or 1). Send, give and offer are the first mover's; return and reject the second's.
		enum Measure : uint32_t
		{
			Send,
			Return,
			Give,
			Offer,
			Reject,
			MeasureCount,
		};

		/// <summary>Band kinds: all, class, education, age.</summary>
		constexpr uint32_t BandKinds = 4;
		constexpr uint32_t Bands = 3;

		uint32_t BandOf(uint32_t kind, const Profile& p)
		{
			switch (kind)
			{
			case 1: return p.cls / 86u;
			case 2: return p.edu / 86u;
			case 3: return p.age < 30 ? 0u : (p.age < 55 ? 1u : 2u);
			default: return 0;
			}
		}

		void GameRow(const Profile& self, const Profile& other, float amount, float reciprocity, float progress, std::span<float> row)
		{
			std::fill(row.begin(), row.end(), 0.0f);
			row[Feature::SelfEducation] = FromU8(self.edu);
			row[Feature::SelfClass] = FromU8(self.cls);
			row[Feature::SelfAge] = std::min(1.0f, static_cast<float>(self.age) / 100.0f);
			row[Feature::SelfOpenness] = FromU8(self.big5[0]);
			row[Feature::SelfAgreeableness] = FromU8(self.big5[3]);
			row[Feature::SelfNeuroticism] = FromU8(self.big5[4]);
			row[Feature::SelfTrust] = FromU8(self.big5[3]);
			row[Feature::OtherEducation] = FromU8(other.edu);
			row[Feature::OtherClass] = FromU8(other.cls);
			row[Feature::OtherAge] = std::min(1.0f, static_cast<float>(other.age) / 100.0f);
			row[Feature::Amount] = amount;
			row[Feature::Reciprocity] = reciprocity;
			row[Feature::Progress] = progress;
		}

		struct Scratch
		{
			std::vector<float> features;
			std::vector<SampleKey> keys;
			OpOut answer;
		};

		class Games final : public Scenario
		{
		public:
			explicit Games(const GamesSpec& s) : spec(s) {}

			const char* Name() const override { return "games"; }
			void Setup(const Config& cfg, Router& r, ChunkPool* p) override;
			bool Step() override;
			uint32_t Round() const override { return round; }
			const Series& Output() const override { return series; }

		private:
			/// <summary>One decision for every pair in [first, last): row i is pair first + i's mover facing its partner.</summary>
			template <class Fill>
			void Decide(Scratch& s, Op op, uint32_t first, uint32_t last, bool second, Fill&& fill);
			void Play(const Stage& stage, Scratch& s, uint32_t first, uint32_t last);
			void Report(const Stage& stage);

			const Profile& Mover(uint32_t pair, bool second) const { return profiles[2 * pair + (second ? 1 : 0)]; }

			GamesSpec spec;
			Config config;
			Router* router = nullptr;
			ChunkPool* pool = nullptr;
			std::vector<Profile> profiles;
			std::vector<Stage> plan;
			/// <summary>Pair-major: measures[pair * MeasureCount + m], NaN when the round had no such measure.</summary>
			std::vector<double> measures;
			/// <summary>Each pair's last return share in the repeated trust game, 0.5 before the first.</summary>
			std::vector<float> lastReturn;
			std::vector<Scratch> outs;
			uint32_t round = 0;
			Series series;
		};

		void Games::Setup(const Config& cfg, Router& r, ChunkPool* p)
		{
			config = cfg;
			config.chunk = std::max(config.chunk, 1u);
			router = &r;
			pool = p;
			round = 0;
			series = Series{};
			series.columns = {"round", "game", "iteration", "bandKind", "band", "count", "send", "return", "give", "offer", "reject"};

			const uint32_t pairs = std::clamp(spec.pairs, 1u, MaxAgents / 2);
			profiles.resize(static_cast<size_t>(pairs) * 2);
			for (uint32_t i = 0; i < profiles.size(); i++) profiles[i] = SyntheticProfile(config.seed, i, OpProfile);
			measures.assign(static_cast<size_t>(pairs) * MeasureCount, 0.0);
			lastReturn.assign(pairs, 0.5f);

			plan.clear();
			plan.push_back({Game::Trust, 0, 1});
			plan.push_back({Game::Dictator, 0, 1});
			for (uint32_t i = 0; i < spec.repeatedTrustRounds; i++) plan.push_back({Game::RepeatedTrust, i, spec.repeatedTrustRounds});
			plan.push_back({Game::Ultimatum, 0, 1});
			for (uint32_t i = 0; i < spec.ultimatumRounds; i++) plan.push_back({Game::IteratedUltimatum, i, spec.ultimatumRounds});
		}

		template <class Fill>
		void Games::Decide(Scratch& s, Op op, uint32_t first, uint32_t last, bool second, Fill&& fill)
		{
			const uint32_t count = last - first;
			s.features.assign(static_cast<size_t>(count) * FeatureCount, 0.0f);
			s.keys.resize(count);
			for (uint32_t i = 0; i < count; i++)
			{
				const uint32_t pair = first + i;
				fill(pair, std::span<float>(s.features).subspan(static_cast<size_t>(i) * FeatureCount, FeatureCount));
				s.keys[i] = {2 * pair + (second ? 1u : 0u), round};
			}
			router->Route(config, OpBatch{op, s.features}, s.keys, s.answer);
		}

		void Games::Play(const Stage& stage, Scratch& s, uint32_t first, uint32_t last)
		{
			constexpr double None = std::numeric_limits<double>::quiet_NaN();
			const float progress = stage.of > 1 ? static_cast<float>(stage.iteration) / static_cast<float>(stage.of - 1) : 0.0f;
			for (uint32_t pair = first; pair < last; pair++)
			{
				double* m = &measures[static_cast<size_t>(pair) * MeasureCount];
				for (uint32_t k = 0; k < MeasureCount; k++) m[k] = None;
			}
			auto at = [&](uint32_t pair, uint32_t k) -> double& { return measures[static_cast<size_t>(pair) * MeasureCount + k]; };
			auto answer = [&](Op op, uint32_t pair, uint32_t col) { return s.answer.Row(op, pair - first)[col]; };

			switch (stage.game)
			{
			case Game::Trust:
			case Game::RepeatedTrust:
			{
				const bool repeated = stage.game == Game::RepeatedTrust;
				Decide(s, Op::TrustSend, first, last, false, [&](uint32_t pair, std::span<float> row) {
					GameRow(Mover(pair, false), Mover(pair, true), 0.0f, repeated ? lastReturn[pair] : 0.5f, progress, row);
				});
				for (uint32_t pair = first; pair < last; pair++) at(pair, Send) = std::clamp(answer(Op::TrustSend, pair, 0), 0.0f, 1.0f);
				Decide(s, Op::TrustReturn, first, last, true, [&](uint32_t pair, std::span<float> row) {
					GameRow(Mover(pair, true), Mover(pair, false), static_cast<float>(at(pair, Send)), 0.5f, progress, row);
				});
				for (uint32_t pair = first; pair < last; pair++)
				{
					const float back = std::clamp(answer(Op::TrustReturn, pair, 0), 0.0f, 1.0f);
					at(pair, Return) = back;
					if (repeated) lastReturn[pair] = back;
				}
				break;
			}
			case Game::Dictator:
				Decide(s, Op::DictatorGive, first, last, false, [&](uint32_t pair, std::span<float> row) {
					GameRow(Mover(pair, false), Mover(pair, true), 0.0f, 0.5f, 0.0f, row);
				});
				for (uint32_t pair = first; pair < last; pair++) at(pair, Give) = std::clamp(answer(Op::DictatorGive, pair, 0), 0.0f, 1.0f);
				break;
			default:
			{
				Decide(s, Op::UltimatumOffer, first, last, false, [&](uint32_t pair, std::span<float> row) {
					GameRow(Mover(pair, false), Mover(pair, true), 0.0f, 0.5f, progress, row);
				});
				for (uint32_t pair = first; pair < last; pair++) at(pair, Offer) = std::clamp(answer(Op::UltimatumOffer, pair, 0), 0.0f, 1.0f);
				Decide(s, Op::UltimatumAnswer, first, last, true, [&](uint32_t pair, std::span<float> row) {
					GameRow(Mover(pair, true), Mover(pair, false), static_cast<float>(at(pair, Offer)), 0.5f, progress, row);
				});
				for (uint32_t pair = first; pair < last; pair++)
				{
					const double reject = std::clamp(static_cast<double>(answer(Op::UltimatumAnswer, pair, static_cast<uint32_t>(Answer::Reject))), 0.0, 1.0);
					at(pair, Reject) = Rng::Roll(config.seed, 2 * pair + 1, round, OpReject, reject) ? 1.0 : 0.0;
				}
				break;
			}
			}
		}

		void Games::Report(const Stage& stage)
		{
			constexpr double None = std::numeric_limits<double>::quiet_NaN();
			// Per band kind and band: pairs counted, and per measure its sum and how many pairs had it.
			double sums[BandKinds][Bands][MeasureCount] = {};
			uint32_t counts[BandKinds][Bands][MeasureCount] = {};
			uint32_t firsts[BandKinds][Bands] = {};
			const uint32_t pairs = static_cast<uint32_t>(profiles.size() / 2);
			for (uint32_t pair = 0; pair < pairs; pair++)
			{
				for (uint32_t kind = 0; kind < BandKinds; kind++)
				{
					firsts[kind][BandOf(kind, Mover(pair, false))]++;
					for (uint32_t k = 0; k < MeasureCount; k++)
					{
						const double v = measures[static_cast<size_t>(pair) * MeasureCount + k];
						if (std::isnan(v)) continue;
						const uint32_t band = BandOf(kind, Mover(pair, k == Return || k == Reject));
						sums[kind][band][k] += v;
						counts[kind][band][k]++;
					}
				}
			}
			for (uint32_t kind = 0; kind < BandKinds; kind++)
			{
				for (uint32_t band = 0; band < (kind == 0 ? 1u : Bands); band++)
				{
					double row[11] = {static_cast<double>(round), static_cast<double>(static_cast<uint8_t>(stage.game)), static_cast<double>(stage.iteration),
						static_cast<double>(kind), static_cast<double>(band), static_cast<double>(firsts[kind][band])};
					for (uint32_t k = 0; k < MeasureCount; k++)
					{
						const uint32_t c = counts[kind][band][k];
						row[6 + k] = c ? sums[kind][band][k] / c : None;
					}
					row[6 + Send] *= spec.endowment;
					series.Add(row);
				}
			}
		}

		bool Games::Step()
		{
			if (!router || round >= plan.size()) return false;
			const Stage stage = plan[round];
			round++;
			if (stage.game == Game::RepeatedTrust && stage.iteration == 0) std::fill(lastReturn.begin(), lastReturn.end(), 0.5f);
			ForChunks(pool, static_cast<uint32_t>(profiles.size() / 2), config.chunk, outs,
				[&](Scratch& s, uint32_t first, uint32_t last) { Play(stage, s, first, last); },
				[](const Scratch&) {});
			Report(stage);
			return true;
		}
	}

	std::unique_ptr<Scenario> MakeGames(const GamesSpec& spec)
	{
		return std::make_unique<Games>(spec);
	}
}
