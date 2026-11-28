// US-T972: Light Society's knowledge distillation.

#include "LightSociety/Distill.h"

#include "LightSociety/Rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t ProfileSalt = 0x400u;
		constexpr uint32_t TopicSalt = 0x500u;
		constexpr uint32_t RowSalt = 0x600u;
		constexpr uint32_t SplitSalt = 0x700u;
		constexpr uint32_t RolloutSalt = 0x800u;

		constexpr uint32_t MaxClasses = 4;
		constexpr uint32_t ShareClasses = 4;
		constexpr uint32_t NoClass = 0xFFFFFFFFu;
		/// <summary>Where a rolled-out agent's stance sits once it lands in a class.</summary>
		constexpr float ClassCenter[3] = {-0.6f, 0.0f, 0.6f};

		/// <summary>The profile fields of a feature row, each in [0, 1].</summary>
		struct Who
		{
			float edu = 0;
			float cls = 0;
			float age = 0;
			float openness = 0;
			float agreeableness = 0;
			float neuroticism = 0;
			float trust = 0;
		};

		float Draw(uint64_t seed, uint32_t i, uint32_t j, uint32_t salt)
		{
			return static_cast<float>(Rng::Unit(Rng::Hash(seed, i, j, salt)));
		}

		Who DrawWho(uint64_t seed, uint32_t i)
		{
			Who w;
			w.edu = Draw(seed, i, 0, ProfileSalt);
			w.cls = Draw(seed, i, 1, ProfileSalt);
			w.age = 0.15f + 0.7f * Draw(seed, i, 2, ProfileSalt);
			w.openness = Draw(seed, i, 3, ProfileSalt);
			w.agreeableness = Draw(seed, i, 4, ProfileSalt);
			w.neuroticism = Draw(seed, i, 5, ProfileSalt);
			w.trust = Draw(seed, i, 6, ProfileSalt);
			return w;
		}

		std::vector<TopicSpec> DrawTopics(uint64_t seed, uint32_t count)
		{
			std::vector<TopicSpec> drawn(count);
			for (uint32_t t = 0; t < count; t++)
			{
				drawn[t].id = static_cast<uint8_t>(t);
				drawn[t].priorMean = 0.5f * (2.0f * Draw(seed, t, 0, TopicSalt) - 1.0f);
				drawn[t].stubbornness = Draw(seed, t, 1, TopicSalt);
			}
			return drawn;
		}

		/// <summary>Sweeps profiles x stances x topics, asks the teacher, and keeps its answers.</summary>
		TrainSet Capture(const IBackend& teacher, Op op, std::span<const Who> who, std::span<const TopicSpec> topicSpecs,
			const CaptureConfig& config)
		{
			TrainSet set;
			set.op = op;
			if (op >= Op::Count_ || !teacher.Supports(op) || who.empty() || topicSpecs.empty() || config.stances == 0) return set;
			const uint32_t profiles = static_cast<uint32_t>(who.size());
			const uint32_t topicCount = static_cast<uint32_t>(topicSpecs.size());
			const uint32_t salt = RowSalt | static_cast<uint32_t>(op);
			const bool persuasion = op == Op::Stance || op == Op::Shift;
			set.features.assign(static_cast<size_t>(profiles) * config.stances * topicCount * FeatureCount, 0.0f);
			for (uint32_t p = 0; p < profiles; p++)
			{
				const Who& self = who[p];
				for (uint32_t k = 0; k < config.stances; k++)
				{
					const float step = config.stances > 1 ? static_cast<float>(k) / static_cast<float>(config.stances - 1) : 0.5f;
					for (uint32_t t = 0; t < topicCount; t++)
					{
						const uint32_t i = (p * config.stances + k) * topicCount + t;
						float* row = set.features.data() + static_cast<size_t>(i) * FeatureCount;
						row[Feature::SelfConfidence] = Draw(config.seed, i, 0, salt);
						row[Feature::SelfEducation] = self.edu;
						row[Feature::SelfClass] = self.cls;
						row[Feature::SelfAge] = self.age;
						row[Feature::SelfOpenness] = self.openness;
						row[Feature::SelfAgreeableness] = self.agreeableness;
						row[Feature::SelfNeuroticism] = self.neuroticism;
						row[Feature::SelfTrust] = self.trust;
						const bool speech = !persuasion || Draw(config.seed, i, 1, salt) < 0.5f;
						if (speech)
						{
							row[Feature::OtherEducation] = Draw(config.seed, i, 2, salt);
							row[Feature::OtherClass] = Draw(config.seed, i, 3, salt);
							row[Feature::OtherAge] = 0.15f + 0.7f * Draw(config.seed, i, 4, salt);
						}
						if (persuasion)
						{
							row[Feature::SelfStance] = 2.0f * step - 1.0f;
							row[Feature::OtherStance] = 2.0f * Draw(config.seed, i, 5, salt) - 1.0f;
							row[Feature::Reach] = Draw(config.seed, i, 6, salt);
							row[Feature::Stubbornness] = topicSpecs[t].stubbornness;
							row[Feature::TopicPrior] = topicSpecs[t].priorMean;
							row[Feature::Speech] = speech ? 1.0f : 0.0f;
						}
						else
						{
							row[Feature::Amount] = step;
							row[Feature::Reciprocity] = Draw(config.seed, i, 7, salt);
							row[Feature::Progress] = Draw(config.seed, i, 8, salt);
						}
						for (uint32_t f = 0; f < FeatureCount; f++) row[f] = QuantizeFeature(row[f]);
					}
				}
			}
			OpBatch batch;
			batch.op = op;
			batch.features = set.features;
			OpOut out;
			teacher.Run(batch, out);
			set.targets = std::move(out.values);
			return set;
		}
	}

	uint32_t OutputClasses(Op op)
	{
		switch (op)
		{
		case Op::Stance: return 3;
		case Op::Shift: return 3;
		case Op::UltimatumAnswer: return 2;
		default: return ShareClasses;
		}
	}

	uint32_t OutputClass(Op op, std::span<const float> row, std::span<const float> out)
	{
		if (IsClassOp(op))
		{
			uint32_t best = 0;
			for (uint32_t k = 1; k < out.size(); k++)
			{
				if (out[k] > out[best]) best = k;
			}
			return best;
		}
		if (op == Op::Shift) return StanceClassOf(row[Feature::SelfStance] + out[0]);
		const float share = std::clamp(out[0], 0.0f, 1.0f);
		return std::min(ShareClasses - 1, static_cast<uint32_t>(share * static_cast<float>(ShareClasses)));
	}

	float MacroF1(Op op, std::span<const float> features, std::span<const float> predicted, std::span<const float> truth)
	{
		const uint32_t w = OutputWidth(op);
		const uint32_t classes = std::min(OutputClasses(op), MaxClasses);
		const size_t rows = std::min({features.size() / FeatureCount, predicted.size() / w, truth.size() / w});
		std::array<uint32_t, MaxClasses> tp = {};
		std::array<uint32_t, MaxClasses> fp = {};
		std::array<uint32_t, MaxClasses> fn = {};
		for (size_t r = 0; r < rows; r++)
		{
			const std::span<const float> row = features.subspan(r * FeatureCount, FeatureCount);
			const uint32_t p = std::min(OutputClass(op, row, predicted.subspan(r * w, w)), classes - 1);
			const uint32_t t = std::min(OutputClass(op, row, truth.subspan(r * w, w)), classes - 1);
			if (p == t)
			{
				tp[p]++;
			}
			else
			{
				fp[p]++;
				fn[t]++;
			}
		}
		double sum = 0.0;
		uint32_t counted = 0;
		for (uint32_t c = 0; c < classes; c++)
		{
			const uint32_t d = 2 * tp[c] + fp[c] + fn[c];
			if (d == 0) continue;
			sum += 2.0 * tp[c] / d;
			counted++;
		}
		return counted ? static_cast<float>(sum / counted) : 0.0f;
	}

	float ChangeRate(const IBackend& backend, Op op, std::span<const float> starts, uint32_t agents, uint32_t rounds, uint64_t seed)
	{
		if (op >= Op::Count_ || !backend.Supports(op) || rounds == 0) return 0.0f;
		const uint32_t n = static_cast<uint32_t>(std::min<size_t>(agents, starts.size() / FeatureCount));
		if (n == 0) return 0.0f;
		const uint32_t w = OutputWidth(op);
		const uint32_t salt = RolloutSalt | static_cast<uint32_t>(op);
		const bool persuasion = op == Op::Stance || op == Op::Shift;
		std::vector<float> rows(starts.begin(), starts.begin() + static_cast<std::ptrdiff_t>(n) * FeatureCount);
		std::vector<uint32_t> last(n, NoClass);
		if (persuasion)
		{
			for (uint32_t i = 0; i < n; i++) last[i] = StanceClassOf(rows[static_cast<size_t>(i) * FeatureCount + Feature::SelfStance]);
		}

		OpBatch batch;
		batch.op = op;
		batch.features = rows;
		OpOut out;
		uint64_t changes = 0;
		uint64_t steps = 0;
		for (uint32_t r = 0; r < rounds; r++)
		{
			if (!persuasion)
			{
				const float progress = QuantizeFeature(static_cast<float>(r) / static_cast<float>(rounds));
				for (uint32_t i = 0; i < n; i++) rows[static_cast<size_t>(i) * FeatureCount + Feature::Progress] = progress;
			}
			backend.Run(batch, out);
			for (uint32_t i = 0; i < n; i++)
			{
				const std::span<float> row(rows.data() + static_cast<size_t>(i) * FeatureCount, FeatureCount);
				const std::span<const float> o = out.Row(op, i);
				uint32_t next = 0;
				if (IsClassOp(op))
				{
					// Both backends rolled with one seed share this draw, so their rates differ by their answers alone.
					const double u = Rng::Unit(Rng::Hash(seed, i, r, salt));
					double acc = 0.0;
					next = w - 1;
					for (uint32_t c = 0; c < w; c++)
					{
						acc += o[c];
						if (u < acc)
						{
							next = c;
							break;
						}
					}
				}
				else
				{
					next = OutputClass(op, row, o);
				}
				if (op == Op::Stance)
				{
					row[Feature::SelfStance] = QuantizeFeature(ClassCenter[std::min(next, 2u)]);
				}
				else if (op == Op::Shift)
				{
					row[Feature::SelfStance] = QuantizeFeature(std::clamp(row[Feature::SelfStance] + o[0], -1.0f, 1.0f));
					row[Feature::SelfConfidence] = QuantizeFeature(std::clamp(row[Feature::SelfConfidence] + o[1], 0.0f, 1.0f));
				}
				if (last[i] != NoClass)
				{
					steps++;
					if (next != last[i]) changes++;
				}
				last[i] = next;
			}
		}
		return steps ? static_cast<float>(static_cast<double>(changes) / static_cast<double>(steps)) : 0.0f;
	}

	TrainSet CaptureSynthetic(const IBackend& teacher, Op op, const CaptureConfig& config)
	{
		std::vector<Who> who(config.profiles);
		for (uint32_t p = 0; p < config.profiles; p++) who[p] = DrawWho(config.seed, p);
		const std::vector<TopicSpec> topicSpecs = DrawTopics(config.seed, config.topics);
		return Capture(teacher, op, who, topicSpecs, config);
	}

	TrainSet CaptureFromStore(const IBackend& teacher, Op op, const AgentStore& store, std::span<const TopicSpec> topics,
		const CaptureConfig& config)
	{
		std::vector<AgentId> active;
		active.reserve(store.ActiveCount());
		for (AgentId id = 0; id < store.Capacity(); id++)
		{
			if (store.IsActive(id)) active.push_back(id);
		}
		const uint32_t pick = std::min(config.profiles, static_cast<uint32_t>(active.size()));
		std::vector<Who> who(pick);
		for (uint32_t p = 0; p < pick; p++)
		{
			const AgentId id = active[static_cast<size_t>(p) * active.size() / pick];
			const Profile& profile = store.GetProfile(id);
			Who& w = who[p];
			w.edu = FromU8(profile.edu);
			w.cls = FromU8(profile.cls);
			w.age = std::min(1.0f, static_cast<float>(profile.age) / 100.0f);
			w.openness = FromU8(profile.big5[0]);
			w.agreeableness = FromU8(profile.big5[3]);
			w.neuroticism = FromU8(profile.big5[4]);
			w.trust = FromU8(store.Trust(id));
		}
		if (topics.empty())
		{
			const std::vector<TopicSpec> drawn = DrawTopics(config.seed, config.topics);
			return Capture(teacher, op, who, drawn, config);
		}
		return Capture(teacher, op, who, topics.first(std::min<size_t>(topics.size(), config.topics)), config);
	}

	DistillResult Distill(const IBackend& teacher, const TrainSet& samples, const DistillConfig& config)
	{
		DistillResult result;
		const Op op = samples.op;
		const uint32_t count = samples.Rows();
		result.metrics.samples = count;
		if (op >= Op::Count_ || !teacher.Supports(op) || samples.targets.size() != static_cast<size_t>(count) * OutputWidth(op)) return result;

		const uint32_t w = OutputWidth(op);
		const uint32_t splitSalt = SplitSalt | static_cast<uint32_t>(op);
		std::vector<uint32_t> train;
		std::vector<float> heldFeatures;
		std::vector<float> heldTargets;
		for (uint32_t i = 0; i < count; i++)
		{
			if (Rng::Roll(Rng::Hash(config.seed, i, 0, splitSalt), static_cast<double>(config.heldOutShare)))
			{
				const std::span<const float> row = samples.Row(i);
				const std::span<const float> target = samples.Target(i);
				heldFeatures.insert(heldFeatures.end(), row.begin(), row.end());
				heldTargets.insert(heldTargets.end(), target.begin(), target.end());
			}
			else
			{
				train.push_back(i);
			}
		}
		const uint32_t held = static_cast<uint32_t>(heldTargets.size() / w);
		result.metrics.heldOut = held;
		if (train.empty() || held == 0) return result;

		struct Candidate
		{
			Surrogate model;
			SurrogateArch arch = SurrogateArch::Softmax;
			uint32_t epoch = 0;
			float macroF1 = 0;
		};
		std::vector<Candidate> candidates;
		TrainConfig training;
		training.seed = config.seed;
		training.learningRate = config.learningRate;
		training.dropout = config.dropout;
		OpBatch heldBatch;
		heldBatch.op = op;
		heldBatch.features = heldFeatures;
		OpOut predicted;
		float best = 0.0f;
		for (uint32_t a = 0; a < static_cast<uint32_t>(SurrogateArch::Count_); a++)
		{
			const SurrogateArch arch = static_cast<SurrogateArch>(a);
			Surrogate model;
			model.Init(op, arch, config.seed);
			for (uint32_t e = 0; e < config.epochs; e++)
			{
				model.Epoch(samples, train, training, e);
				model.Run(heldBatch, predicted);
				const float f1 = MacroF1(op, heldFeatures, predicted.values, heldTargets);
				best = std::max(best, f1);
				candidates.push_back(Candidate{model, arch, e, f1});
			}
		}
		if (candidates.empty()) return result;

		const float teacherRate = ChangeRate(teacher, op, heldFeatures, config.rolloutAgents, config.rolloutRounds, config.seed);
		size_t chosen = candidates.size();
		float chosenGap = 0.0f;
		for (size_t c = 0; c < candidates.size(); c++)
		{
			if (candidates[c].macroF1 < best - config.f1Eps) continue;
			result.metrics.shortlisted++;
			const float rate = ChangeRate(candidates[c].model, op, heldFeatures, config.rolloutAgents, config.rolloutRounds, config.seed);
			const float gap = std::fabs(rate - teacherRate);
			if (chosen == candidates.size() || gap < chosenGap)
			{
				chosen = c;
				chosenGap = gap;
			}
		}

		Candidate& pick = candidates[chosen];
		result.ok = true;
		result.backend = std::move(pick.model);
		result.metrics.macroF1 = pick.macroF1;
		result.metrics.changeRateGap = chosenGap;
		result.metrics.arch = pick.arch;
		result.metrics.epoch = pick.epoch;
		result.metrics.candidates = static_cast<uint32_t>(candidates.size());
		return result;
	}
}
