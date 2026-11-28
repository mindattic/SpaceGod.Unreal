// US-T973: the continuous stance scenario (paper 2.4.4, 4.7): speak, heard, end-of-day reflection, on the engine.

#include "LightSociety/Scenarios/Scenario.h"

#include "LightSociety/Engine.h"
#include "LightSociety/Graph.h"
#include "LightSociety/Readout.h"
#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		// Random-draw operations of this scenario.
		constexpr uint32_t OpProfile = 0x5200;
		constexpr uint32_t OpGraph = 0x5201;
		constexpr uint32_t OpPick = 0x5202;

		/// <summary>The event of an agent's end-of-day reflection on what it heard; it reaches only that agent.</summary>
		constexpr uint16_t ReflectKind = 0xFFFE;

		/// <summary>
		/// f_pi of the scenario: speech goes to the inner policy (the router) and is heard, not passed on (the scenario
		/// picks its speakers itself); a reflection moves the agent toward what it heard and firms it up as it agrees.
		/// </summary>
		class Listener final : public IPolicy
		{
		public:
			IPolicy* inner = nullptr;

			void Decide(const PolicyBatch& batch, PolicyOut& out) override
			{
				if (batch.kind != ReflectKind)
				{
					if (inner) inner->Decide(batch, out);
					out.emit.clear();
					return;
				}
				for (const Perception& p : batch.perceptions)
				{
					const EnvEvent& e = batch.events[p.event].event;
					if (e.topic >= batch.store.Topics()) continue;
					const float s = FromQ15(batch.store.Stance(p.agent, e.topic));
					const float c = FromU8(batch.store.Confidence(p.agent, e.topic));
					const float gap = e.push - s;
					const float ds = gap * e.magnitude * (1.0f - 0.5f * c);
					const float dc = 0.05f * e.magnitude * (1.0f - std::fabs(gap));
					out.changes.push_back({p.agent, e.topic, ds, dc, 0, 0});
				}
			}
		};

		struct Pick
		{
			double key = 0;
			AgentId agent = NoAgent;
		};

		struct ChunkOut
		{
			std::vector<EnvEvent> reflections;
			double sum = 0;
			double sumSq = 0;
			uint32_t count = 0;
			uint32_t supporters = 0;
			uint32_t skeptics = 0;
		};

		class Continuous final : public Scenario
		{
		public:
			explicit Continuous(const ContinuousSpec& s) : spec(s) {}

			const char* Name() const override { return "continuous"; }
			void Setup(const Config& cfg, Router& r, ChunkPool* p) override;
			bool Step() override;
			uint32_t Round() const override { return engine.Day(); }
			const Series& Output() const override { return series; }

		private:
			void Wire(uint32_t n, std::vector<Edge>& edges) const;
			void Speak(uint32_t slot);
			void Record(uint32_t heard);

			ContinuousSpec spec;
			Config config;
			Router* router = nullptr;
			ChunkPool* pool = nullptr;
			Engine engine;
			Listener listener;
			std::vector<Pick> picks;
			std::vector<EnvEvent> events;
			std::vector<ChunkOut> outs;
			Series series;
		};

		void Continuous::Wire(uint32_t n, std::vector<Edge>& edges) const
		{
			edges.clear();
			switch (spec.graph)
			{
			case Substrate::ErdosRenyi:
			{
				// G(n, M): M uniform pairs, repeats and self-loops dropped.
				const double pairs = 0.5 * static_cast<double>(n) * (n - 1);
				const uint64_t want = static_cast<uint64_t>(std::clamp(std::isfinite(spec.meanDegree) ? 0.5 * spec.meanDegree * n : 0.0, 0.0, std::min(pairs, 1.0e7)));
				std::vector<Edge> drawn;
				drawn.reserve(static_cast<size_t>(want));
				for (uint64_t k = 0; k < want; k++)
				{
					const uint32_t lo = static_cast<uint32_t>(k);
					const uint32_t hi = static_cast<uint32_t>(k >> 32);
					const AgentId a = static_cast<AgentId>(Rng::Hash(config.seed, lo, hi, OpGraph) % n);
					const AgentId b = static_cast<AgentId>(Rng::Hash(config.seed, lo, hi, OpGraph + 0x10000) % n);
					drawn.push_back({a, b, static_cast<uint8_t>(EdgeKind::Social), 255});
				}
				Graph g;
				g.BuildFrom(n, drawn);
				edges = g.Edges();
				break;
			}
			case Substrate::Tree:
			{
				const uint32_t branching = std::max(spec.branching, 1u);
				for (uint32_t i = 1; i < n; i++) edges.push_back({(i - 1) / branching, i, static_cast<uint8_t>(EdgeKind::Social), 255});
				break;
			}
			default:
			{
				std::vector<AgentId> nodes(n);
				std::iota(nodes.begin(), nodes.end(), 0u);
				BarabasiAlbert(nodes, std::max(spec.m, 1u), config.seed, OpGraph, edges);
				break;
			}
			}
		}

		void Continuous::Setup(const Config& cfg, Router& r, ChunkPool* p)
		{
			config = cfg;
			config.topics = 1;
			config.slotsPerDay = std::max(config.slotsPerDay, 1u);
			config.chunk = std::max(config.chunk, 1u);
			// Room in the memory ring for everything one agent can hear in a day, up to the store's limit.
			const uint64_t perDay = static_cast<uint64_t>(config.slotsPerDay) * std::max(spec.speakersPerSlot, 1u);
			config.memoryK = static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(perDay, config.memoryK), MaxMemory));
			router = &r;
			pool = p;
			series = Series{};
			series.columns = {"day", "mean", "sigma", "supporter", "skeptic", "heard"};

			const uint32_t n = std::min(std::max(spec.agents, std::max(spec.m, 1u) + 2), MaxAgents);
			std::vector<Profile> profiles(n);
			for (uint32_t i = 0; i < n; i++) profiles[i] = SyntheticProfile(config.seed, i, OpProfile);
			std::vector<Edge> edges;
			Wire(n, edges);

			TopicSpec topic;
			topic.id = 0;
			topic.priorMean = spec.priorMean;
			topic.priorSpread = spec.priorSpread;
			topic.stubbornness = spec.stubbornness;
			listener.inner = &r;
			engine.SetPolicy(&listener);
			engine.Init(config, profiles, edges, std::span<const TopicSpec>(&topic, 1));
			Record(0);
		}

		void Continuous::Speak(uint32_t slot)
		{
			// Expression desire: the outgoing, the committed and the sure speak up; a weighted draw without replacement
			// (the largest log(u) / w win, ties to the lower id).
			const AgentStore& store = engine.Store();
			const uint32_t slots = engine.GetConfig().slotsPerDay;
			const uint32_t draw = engine.Day() * slots + slot;
			picks.clear();
			store.ForEachActive([&](AgentId a) {
				const float s = FromQ15(store.Stance(a, 0));
				const float c = FromU8(store.Confidence(a, 0));
				const double w = 0.05 + FromU8(store.Express(a)) * (0.5 + 0.5 * std::fabs(s)) * (0.5 + 0.5 * c);
				const double u = Rng::Unit(Rng::Hash(config.seed, a, draw, OpPick));
				picks.push_back({std::log(u + 1e-300) / w, a});
			});
			const size_t k = std::min<size_t>(std::max(spec.speakersPerSlot, 1u), picks.size());
			std::partial_sort(picks.begin(), picks.begin() + static_cast<std::ptrdiff_t>(k), picks.end(), [](const Pick& x, const Pick& y) {
				return x.key != y.key ? x.key > y.key : x.agent < y.agent;
			});

			const double at = engine.Day() + static_cast<double>(slot) / slots;
			events.clear();
			for (size_t i = 0; i < k; i++)
			{
				const AgentId a = picks[i].agent;
				EnvEvent e;
				e.time = at;
				e.kind = SpeakKind;
				e.topic = 0;
				e.push = FromQ15(store.Stance(a, 0));
				e.magnitude = spec.magnitude;
				e.scope = Scope::Neighbours;
				e.target = a;
				e.source = a;
				events.push_back(e);
			}
			engine.Inject(events);
			engine.StepTo(at);
		}

		void Continuous::Record(uint32_t heard)
		{
			const AgentStore& store = engine.Store();
			ForChunks(pool, store.Capacity(), config.chunk, outs,
				[&store](ChunkOut& out, uint32_t first, uint32_t last) {
					out = ChunkOut{};
					store.ForEachActiveIn(first, last, [&](AgentId a) {
						const double s = FromQ15(store.Stance(a, 0));
						out.sum += s;
						out.sumSq += s * s;
						out.count++;
						if (s > SupporterStance) out.supporters++;
						else if (s < -SupporterStance) out.skeptics++;
					});
				},
				[](const ChunkOut&) {});
			ChunkOut all;
			for (uint32_t i = 0; i < ChunkPool::Chunks(store.Capacity(), config.chunk); i++)
			{
				all.sum += outs[i].sum;
				all.sumSq += outs[i].sumSq;
				all.count += outs[i].count;
				all.supporters += outs[i].supporters;
				all.skeptics += outs[i].skeptics;
			}
			const double n = all.count ? static_cast<double>(all.count) : 1.0;
			const double mean = all.sum / n;
			const double sigma = std::sqrt(std::max(0.0, all.sumSq / n - mean * mean));
			const double row[] = {static_cast<double>(engine.Day()), mean, sigma, all.supporters / n, all.skeptics / n,
				static_cast<double>(heard)};
			series.Add(row);
		}

		bool Continuous::Step()
		{
			if (!router || engine.Day() >= spec.days) return false;
			const uint32_t slots = engine.GetConfig().slotsPerDay;
			for (uint32_t slot = 0; slot < slots; slot++) Speak(slot);

			// Reflection, before the day ends: every agent that heard speech today, toward the mean of what it heard
			// (each memory's valence is push x reach x 127, and reach is the speech's magnitude on full-weight ties).
			const AgentStore& store = engine.Store();
			const uint16_t today = static_cast<uint16_t>(std::min<uint32_t>(engine.Day(), 0xFFFF));
			const float reach = std::clamp(spec.magnitude, 0.0f, 1.0f);
			const double when = engine.Day() + (slots - 0.5) / slots;
			const float reflect = std::clamp(spec.reflect, 0.0f, 1.0f);
			uint32_t heard = 0;
			events.clear();
			if (reach > 0.001f)
			{
				ForChunks(pool, store.Capacity(), config.chunk, outs,
					[&](ChunkOut& out, uint32_t first, uint32_t last) {
						out.reflections.clear();
						store.ForEachActiveIn(first, last, [&](AgentId a) {
							int32_t sum = 0;
							int32_t count = 0;
							for (uint32_t i = 0; i < store.MemoryK(); i++)
							{
								const MemoryEntry& m = store.Recall(a, i);
								if (m.kind != SpeakKind || m.topic != 0 || m.day != today) continue;
								sum += m.valence;
								count++;
							}
							if (count == 0) return;
							EnvEvent e;
							e.time = when;
							e.kind = ReflectKind;
							e.topic = 0;
							e.push = std::clamp(static_cast<float>(sum) / (static_cast<float>(count) * 127.0f * reach), -1.0f, 1.0f);
							e.magnitude = reflect;
							e.scope = Scope::Agent;
							e.target = a;
							e.source = a;
							out.reflections.push_back(e);
						});
					},
					[&](const ChunkOut& out) {
						events.insert(events.end(), out.reflections.begin(), out.reflections.end());
						heard += static_cast<uint32_t>(out.reflections.size());
					});
			}
			engine.Inject(events);
			engine.StepTo(static_cast<double>(engine.Day()) + 1.0);
			Record(heard);
			return true;
		}
	}

	std::unique_ptr<Scenario> MakeContinuous(const ContinuousSpec& spec)
	{
		return std::make_unique<Continuous>(spec);
	}
}
