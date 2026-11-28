// US-T971: Light Society's router.

#include "LightSociety/Router.h"

#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace MindAttic::Society
{
	namespace
	{
		// Random-draw operations; the op or topic goes in the low byte.
		constexpr uint32_t OpRoute = 0x1000;
		constexpr uint32_t OpSpeak = 0x1100;

		/// <summary>A cache key: op, tier, then every feature's quantized step.</summary>
		constexpr uint32_t KeyBytes = 2 + 2 * FeatureCount;

		float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		void MakeKey(Op op, BackendKind kind, std::span<const float> row, uint8_t* key)
		{
			key[0] = static_cast<uint8_t>(op);
			key[1] = static_cast<uint8_t>(kind);
			for (uint32_t f = 0; f < FeatureCount; f++)
			{
				const int16_t q = FeatureStep(row[f]);
				std::memcpy(key + 2 + 2 * f, &q, 2);
			}
		}
	}

	Router::Router()
	{
		cache.Reset(DefaultCacheEntries, KeyBytes, MaxOutputs);
	}

	bool Router::Register(BackendKind kind, Op op, const IBackend* backend)
	{
		const bool ok = table.Register(kind, op, backend);
		const std::lock_guard<std::mutex> hold(cacheLock);
		cache.Clear();
		return ok;
	}

	void Router::RegisterAll(BackendKind kind, const IBackend* backend)
	{
		table.RegisterAll(kind, backend);
		const std::lock_guard<std::mutex> hold(cacheLock);
		cache.Clear();
	}

	void Router::SetCacheEntries(uint32_t entries)
	{
		const std::lock_guard<std::mutex> hold(cacheLock);
		cache.Reset(entries, KeyBytes, MaxOutputs);
	}

	uint64_t Router::CacheHits() const
	{
		const std::lock_guard<std::mutex> hold(cacheLock);
		return cache.Hits();
	}

	uint64_t Router::CacheMisses() const
	{
		const std::lock_guard<std::mutex> hold(cacheLock);
		return cache.Misses();
	}

	const IBackend& Router::Resolve(BackendKind kind, Op op) const
	{
		if (const IBackend* b = table.Find(kind, op)) return *b;
		if (const IBackend* b = table.Find(BackendKind::Teacher, op)) return *b;
		return teacher;
	}

	BackendKind Router::Pick(const Config& config, Op op, SampleKey key) const
	{
		const BackendKind cheap = table.Find(BackendKind::Lookup, op) ? BackendKind::Lookup
			: (table.Find(BackendKind::Surrogate, op) ? BackendKind::Surrogate : BackendKind::Teacher);
		if (cheap == BackendKind::Teacher) return BackendKind::Teacher;
		double w = 0.0;
		if (config.policy == Policy::AllSurrogate) w = 1.0;
		else if (config.policy == Policy::Weighted && std::isfinite(config.surrogateWeight)) w = Clamp(config.surrogateWeight, 0.0f, 1.0f);
		if (w <= 0.0) return BackendKind::Teacher;
		return Rng::Roll(config.seed, key.agent, key.round, OpRoute | static_cast<uint32_t>(op), w) ? cheap : BackendKind::Teacher;
	}

	void Router::Route(const Config& config, const OpBatch& batch, std::span<const SampleKey> keys, OpOut& out)
	{
		const Op op = batch.op;
		const uint32_t rows = batch.Rows();
		const uint32_t width = OutputWidth(op);
		out.values.assign(static_cast<size_t>(rows) * width, 0.0f);
		if (rows == 0 || op >= Op::Count_) return;

		std::vector<float> features(static_cast<size_t>(rows) * FeatureCount);
		for (size_t i = 0; i < features.size(); i++) features[i] = QuantizeFeature(batch.features[i]);
		std::vector<uint8_t> tier(rows);
		for (uint32_t i = 0; i < rows; i++) tier[i] = static_cast<uint8_t>(Pick(config, op, i < keys.size() ? keys[i] : SampleKey{}));

		const bool cached = cache.Capacity() > 0;
		std::vector<uint8_t> key(KeyBytes);
		std::vector<uint32_t> misses;
		std::vector<float> missFeatures;
		std::vector<uint8_t> missKeys;
		OpOut answer;
		for (uint32_t k = 0; k < BackendKindCount; k++)
		{
			const BackendKind kind = static_cast<BackendKind>(k);
			misses.clear();
			missFeatures.clear();
			missKeys.clear();
			{
				std::unique_lock<std::mutex> hold(cacheLock, std::defer_lock);
				if (cached) hold.lock();
				for (uint32_t i = 0; i < rows; i++)
				{
					if (tier[i] != k) continue;
					const std::span<const float> row(features.data() + static_cast<size_t>(i) * FeatureCount, FeatureCount);
					if (cached)
					{
						MakeKey(op, kind, row, key.data());
						float hit[MaxOutputs];
						if (cache.Find(key, hit))
						{
							std::copy(hit, hit + width, out.values.begin() + static_cast<std::ptrdiff_t>(i) * width);
							continue;
						}
						missKeys.insert(missKeys.end(), key.begin(), key.end());
					}
					misses.push_back(i);
					missFeatures.insert(missFeatures.end(), row.begin(), row.end());
				}
			}
			if (misses.empty()) continue;

			Resolve(kind, op).Run(OpBatch{op, missFeatures}, answer);
			const uint32_t got = std::min(static_cast<uint32_t>(misses.size()), static_cast<uint32_t>(answer.values.size() / width));
			for (uint32_t m = 0; m < got; m++)
			{
				const std::span<const float> a = answer.Row(op, m);
				std::copy(a.begin(), a.end(), out.values.begin() + static_cast<std::ptrdiff_t>(misses[m]) * width);
			}
			if (cached)
			{
				const std::lock_guard<std::mutex> hold(cacheLock);
				float value[MaxOutputs] = {};
				for (uint32_t m = 0; m < got; m++)
				{
					const std::span<const float> a = answer.Row(op, m);
					std::copy(a.begin(), a.end(), value);
					cache.Insert(std::span<const uint8_t>(missKeys.data() + static_cast<size_t>(m) * KeyBytes, KeyBytes), value);
				}
			}
		}
	}

	void Router::PersuasionRow(const PolicyBatch& batch, const Perception& p, std::span<float> row)
	{
		const EnvEvent& e = batch.events[p.event].event;
		const uint32_t t = e.topic;
		const TopicSpec& spec = batch.topics[t];
		const AgentStore& store = batch.store;
		const AgentId a = p.agent;
		const Profile& self = store.GetProfile(a);
		const bool speech = batch.kind == SpeakKind;

		std::fill(row.begin(), row.end(), 0.0f);
		row[Feature::SelfStance] = FromQ15(store.Stance(a, t));
		row[Feature::SelfConfidence] = FromU8(store.Confidence(a, t));
		row[Feature::SelfEducation] = FromU8(self.edu);
		row[Feature::SelfClass] = FromU8(self.cls);
		row[Feature::SelfAge] = std::min(1.0f, static_cast<float>(self.age) / 100.0f);
		row[Feature::SelfOpenness] = FromU8(self.big5[0]);
		row[Feature::SelfAgreeableness] = FromU8(self.big5[3]);
		row[Feature::SelfNeuroticism] = FromU8(self.big5[4]);
		row[Feature::SelfTrust] = FromU8(store.Trust(a));
		row[Feature::OtherStance] = e.push;
		if (speech && store.IsActive(e.source))
		{
			const Profile& other = store.GetProfile(e.source);
			row[Feature::OtherEducation] = FromU8(other.edu);
			row[Feature::OtherClass] = FromU8(other.cls);
			row[Feature::OtherAge] = std::min(1.0f, static_cast<float>(other.age) / 100.0f);
		}
		row[Feature::Reach] = e.magnitude * FromU8(p.weight);
		row[Feature::Stubbornness] = spec.stubbornness;
		row[Feature::TopicPrior] = spec.priorMean;
		row[Feature::Speech] = speech ? 1.0f : 0.0f;
	}

	void Router::Decide(const PolicyBatch& batch, PolicyOut& out)
	{
		std::vector<uint32_t> heard;
		heard.reserve(batch.perceptions.size());
		for (uint32_t i = 0; i < batch.perceptions.size(); i++)
		{
			if (batch.events[batch.perceptions[i].event].event.topic < batch.topics.size()) heard.push_back(i);
		}
		const uint32_t n = static_cast<uint32_t>(heard.size());
		if (n == 0) return;

		std::vector<float> features(static_cast<size_t>(n) * FeatureCount);
		std::vector<SampleKey> keys(n);
		for (uint32_t i = 0; i < n; i++)
		{
			const Perception& p = batch.perceptions[heard[i]];
			PersuasionRow(batch, p, std::span<float>(features.data() + static_cast<size_t>(i) * FeatureCount, FeatureCount));
			keys[i] = {p.agent, batch.round};
		}
		OpOut answer;
		Route(batch.config, OpBatch{Op::Shift, features}, keys, answer);

		for (uint32_t i = 0; i < n; i++)
		{
			const Perception& p = batch.perceptions[heard[i]];
			const EnvEvent& e = batch.events[p.event].event;
			const uint32_t t = e.topic;
			const AgentId a = p.agent;
			const std::span<const float> step = answer.Row(Op::Shift, i);
			const float ds = step[0];
			const float dc = step[1];
			const float reach = e.magnitude * FromU8(p.weight);
			const int8_t valence = static_cast<int8_t>(std::lround(Clamp(e.push * reach, -1.0f, 1.0f) * 127.0f));
			out.changes.push_back({a, static_cast<uint8_t>(t), ds, dc, e.kind, valence});

			const float half = e.magnitude * 0.5f;
			const double speakChance = FromU8(batch.store.Express(a)) * 0.25;
			if (half >= 0.05f && std::fabs(ds) > 0.01f && Rng::Roll(batch.config.seed, a, batch.round, OpSpeak | t, speakChance))
			{
				EnvEvent say;
				say.time = e.time + 1.0 / batch.config.slotsPerDay;
				say.priority = e.priority;
				say.kind = SpeakKind;
				say.topic = e.topic;
				say.push = Clamp(FromQ15(batch.store.Stance(a, t)) + ds, -1.0f, 1.0f);
				say.magnitude = half;
				say.scope = Scope::Neighbours;
				say.target = a;
				say.source = a;
				out.emit.push_back(say);
			}
		}
	}
}
