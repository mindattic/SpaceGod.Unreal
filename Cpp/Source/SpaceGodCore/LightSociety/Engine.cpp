// US-T970: Light Society's engine.

#include "LightSociety/Engine.h"

#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t EngineTag = FourCC('E', 'N', 'G', 'N');

		// Random-draw operations; the topic goes in the low byte.
		constexpr uint32_t OpStance = 0x100;
		constexpr uint32_t OpConfidence = 0x200;
		constexpr uint32_t OpDrift = 0x300;
		constexpr uint32_t OpSpeak = 0x400;

		/// <summary>Most directed links a blob's graph may hold.</summary>
		constexpr uint32_t MaxLinks = 1u << 28;

		float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// <summary>0 skeptic, 1 neutral, 2 supporter.</summary>
		uint8_t ClassOf(int16_t q)
		{
			const float s = FromQ15(q);
			return s > SupporterStance ? 2 : (s < -SupporterStance ? 0 : 1);
		}

		/// <summary>Calls f(first, last) for each fixed-size chunk of [0, n), in order.</summary>
		template <class F>
		void ForEachChunk(uint32_t n, uint32_t chunk, F&& f)
		{
			for (uint32_t first = 0; first < n; first += chunk) f(first, std::min(n, first + chunk));
		}
	}

	void DefaultPolicy::Decide(const PolicyBatch& batch, PolicyOut& out)
	{
		const bool speech = batch.kind == SpeakKind;
		for (const Perception& p : batch.perceptions)
		{
			const EnvEvent& e = batch.events[p.event].event;
			const uint32_t t = e.topic;
			if (t >= batch.topics.size()) continue;
			const TopicSpec& spec = batch.topics[t];
			const AgentId a = p.agent;
			const float s = FromQ15(batch.store.Stance(a, t));
			const float c = FromU8(batch.store.Confidence(a, t));
			const float edu = FromU8(batch.store.GetProfile(a).edu);
			const float gap = e.push - s;
			const float reach = e.magnitude * FromU8(p.weight);
			const float give = 1.0f - spec.stubbornness;

			float ds;
			float dc;
			if (speech && std::fabs(gap) > 1.0f - 0.5f * c)
			{
				// Too far off to be heard out: reactance, a small push the other way that hardens the agent.
				ds = -0.1f * gap * reach * give;
				dc = 0.02f * reach;
			}
			else
			{
				ds = gap * reach * give * (1.0f - 0.5f * c) * (1.0f - 0.5f * edu);
				dc = (e.push * s >= 0.0f ? 0.05f : -0.03f) * reach;
			}
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
				say.push = Clamp(s + ds, -1.0f, 1.0f);
				say.magnitude = half;
				say.scope = Scope::Neighbours;
				say.target = a;
				say.source = a;
				out.emit.push_back(say);
			}
		}
	}

	void Engine::Init(const Config& cfg, std::span<const Profile> profiles, std::span<const Edge> edges, std::span<const TopicSpec> topicSpecs)
	{
		IPolicy* keep = external;
		*this = Engine();
		external = keep;

		config = cfg;
		config.topics = std::clamp(config.topics, 1u, MaxTopics);
		config.memoryK = std::clamp(config.memoryK, 1u, MaxMemory);
		config.slotsPerDay = std::max(config.slotsPerDay, 1u);
		config.chunk = std::max(config.chunk, 1u);
		topics.resize(config.topics);
		for (uint32_t t = 0; t < config.topics; t++)
		{
			TopicSpec spec = t < topicSpecs.size() ? topicSpecs[t] : TopicSpec{};
			spec.id = static_cast<uint8_t>(t);
			spec.priorMean = std::isfinite(spec.priorMean) ? Clamp(spec.priorMean, -1.0f, 1.0f) : 0.0f;
			spec.priorSpread = std::isfinite(spec.priorSpread) ? Clamp(spec.priorSpread, 0.0f, 2.0f) : 0.0f;
			spec.stubbornness = std::isfinite(spec.stubbornness) ? Clamp(spec.stubbornness, 0.0f, 1.0f) : 0.0f;
			topics[t] = spec;
		}
		drift.assign(config.topics, 0.0f);

		store.Reset(config.topics, config.memoryK);
		for (const Profile& p : profiles)
		{
			const AgentId id = store.Join(p);
			if (id == NoAgent) break;
			store.Row(id) = id;
			store.Express(id) = p.big5[2];
			store.Trust(id) = p.big5[3];
			for (uint32_t t = 0; t < config.topics; t++)
			{
				const double u = Rng::Unit(Rng::Hash(config.seed, id, 0, OpStance | t));
				store.Stance(id, t) = ToQ15(topics[t].priorMean + topics[t].priorSpread * static_cast<float>(2.0 * u - 1.0));
				const double v = Rng::Unit(Rng::Hash(config.seed, id, 0, OpConfidence | t));
				store.Confidence(id, t) = ToU8(static_cast<float>(0.3 + 0.4 * v));
			}
			groups.push_back(p.group);
		}
		std::sort(groups.begin(), groups.end());
		groups.erase(std::unique(groups.begin(), groups.end()), groups.end());

		// The graph in CSR, both directions of every edge, each row sorted by target.
		const uint32_t n = store.Capacity();
		std::vector<Edge> links;
		for (const Edge& e : edges)
		{
			if (e.a >= n || e.b >= n || e.a == e.b) continue;
			links.push_back(e);
			links.push_back({e.b, e.a, e.kind, e.weight});
		}
		std::sort(links.begin(), links.end(), [](const Edge& x, const Edge& y) {
			if (x.a != y.a) return x.a < y.a;
			if (x.b != y.b) return x.b < y.b;
			if (x.weight != y.weight) return x.weight < y.weight;
			return x.kind < y.kind;
		});
		offsets.assign(static_cast<size_t>(n) + 1, 0);
		for (const Edge& e : links) offsets[e.a + 1]++;
		for (uint32_t i = 0; i < n; i++) offsets[i + 1] += offsets[i];
		targets.reserve(links.size());
		weights.reserve(links.size());
		for (const Edge& e : links)
		{
			targets.push_back(e.b);
			weights.push_back(e.weight);
		}

		TakeReadout();
	}

	bool Engine::Admit(EnvEvent& e) const
	{
		if (e.topic >= config.topics || !std::isfinite(e.time) || !std::isfinite(e.push) || !std::isfinite(e.magnitude)
			|| static_cast<uint8_t>(e.scope) > static_cast<uint8_t>(Scope::Neighbours))
			return false;
		if (e.time < now) e.time = now;
		e.push = Clamp(e.push, -1.0f, 1.0f);
		e.magnitude = Clamp(e.magnitude, 0.0f, 1.0f);
		return true;
	}

	uint32_t Engine::Inject(std::span<const EnvEvent> events)
	{
		uint32_t queued = 0;
		for (EnvEvent e : events)
		{
			if (!Admit(e)) continue;
			queue.Push(e);
			queued++;
		}
		return queued;
	}

	void Engine::StepTo(double time)
	{
		if (!std::isfinite(time)) return;
		for (;;)
		{
			const double tick = static_cast<double>(day) + 1.0;
			if (!queue.Empty() && queue.Top().event.time < tick && queue.Top().event.time <= time) RunBatch();
			else if (tick <= time) EndDay();
			else break;
		}
		if (time > now) now = time;
	}

	void Engine::RunBatch()
	{
		queue.PopBatch(batch);
		now = batch.front().event.time;
		round++;
		IPolicy& policy = external ? *external : builtIn;

		for (size_t first = 0; first < batch.size();)
		{
			const uint16_t kind = batch[first].event.kind;
			size_t last = first;
			while (last < batch.size() && batch[last].event.kind == kind) last++;
			const std::span<const QueuedEvent> events(batch.data() + first, last - first);
			first = last;

			Perceive(events, perceptions);
			const uint32_t count = static_cast<uint32_t>(perceptions.size());
			const uint32_t chunks = (count + config.chunk - 1) / config.chunk;
			if (outs.size() < chunks) outs.resize(chunks);
			uint32_t k = 0;
			ForEachChunk(count, config.chunk, [&](uint32_t from, uint32_t to) {
				PolicyOut& out = outs[k++];
				out.Clear();
				const PolicyBatch call{store, config, topics, kind, events,
					std::span<const Perception>(perceptions.data() + from, to - from), round};
				policy.Decide(call, out);
			});
			for (uint32_t i = 0; i < chunks; i++) Apply(outs[i]);
		}
	}

	void Engine::Perceive(std::span<const QueuedEvent> events, std::vector<Perception>& out) const
	{
		out.clear();
		for (uint32_t i = 0; i < events.size(); i++)
		{
			const EnvEvent& e = events[i].event;
			switch (e.scope)
			{
			case Scope::All:
				store.ForEachActive([&](AgentId a) { out.push_back({i, a, 255}); });
				break;
			case Scope::Group:
				store.ForEachActive([&](AgentId a) {
					if (store.Group(a) == e.target) out.push_back({i, a, 255});
				});
				break;
			case Scope::Agent:
				if (store.IsActive(e.target)) out.push_back({i, e.target, 255});
				break;
			case Scope::Neighbours:
			{
				if (!store.IsActive(e.target)) break;
				const uint32_t row = store.Row(e.target);
				if (row == AgentStore::NoRow || row + 1 >= offsets.size()) break;
				for (uint32_t k = offsets[row]; k < offsets[row + 1]; k++)
				{
					if (store.IsActive(targets[k])) out.push_back({i, targets[k], weights[k]});
				}
				break;
			}
			}
		}
	}

	void Engine::Apply(const PolicyOut& out)
	{
		const uint16_t today = static_cast<uint16_t>(std::min<uint32_t>(day, 0xFFFF));
		for (const StanceChange& c : out.changes)
		{
			if (!store.IsActive(c.agent) || c.topic >= config.topics || !std::isfinite(c.dStance) || !std::isfinite(c.dConfidence)) continue;
			int16_t& s = store.Stance(c.agent, c.topic);
			s = ToQ15(FromQ15(s) + c.dStance);
			uint8_t& conf = store.Confidence(c.agent, c.topic);
			conf = ToU8(FromU8(conf) + c.dConfidence);
			if (c.kind != 0) store.Remember(c.agent, {c.kind, c.topic, c.valence, today});
		}
		for (EnvEvent e : out.emit)
		{
			if (Admit(e)) queue.Push(e);
		}
	}

	void Engine::EndDay()
	{
		day++;
		now = static_cast<double>(day);
		EvolveEnvironment();
		ForEachChunk(store.Capacity(), config.chunk, [&](uint32_t first, uint32_t last) { EvolveAgents(first, last); });
		TakeReadout();
	}

	void Engine::EvolveEnvironment()
	{
		// f_E: each topic's environment takes a mean-reverting random step.
		for (uint32_t t = 0; t < config.topics; t++)
		{
			const double u = Rng::Unit(Rng::Hash(config.seed, NoAgent, day, OpDrift | t));
			drift[t] = Clamp(drift[t] * 0.98f + 0.05f * static_cast<float>(2.0 * u - 1.0), -1.0f, 1.0f);
		}
	}

	void Engine::EvolveAgents(uint32_t first, uint32_t last)
	{
		// f_A, each agent on its own: memories fade, confidence relaxes toward the middle, stances lean with the
		// environment, the desire to speak settles back to temperament, and a year makes the agent a year older.
		const bool birthday = day % 365 == 0;
		store.ForEachActiveIn(first, last, [&](AgentId a) {
			for (MemoryEntry& m : store.Memory(a))
			{
				if (m.kind == 0) continue;
				const int v = m.valence;
				const int step = std::max(1, std::abs(v) / 16);
				const int faded = v > 0 ? std::max(0, v - step) : std::min(0, v + step);
				if (faded == 0) m = MemoryEntry{};
				else m.valence = static_cast<int8_t>(faded);
			}
			for (uint32_t t = 0; t < config.topics; t++)
			{
				uint8_t& c = store.Confidence(a, t);
				c = static_cast<uint8_t>(c + (128 - static_cast<int>(c)) / 32);
				int16_t& q = store.Stance(a, t);
				const float s = FromQ15(q);
				q = ToQ15(s + (drift[t] - s) * 0.002f * (1.0f - topics[t].stubbornness));
			}
			Profile& p = store.MutableProfile(a);
			uint8_t& express = store.Express(a);
			express = static_cast<uint8_t>(express + (static_cast<int>(p.big5[2]) - static_cast<int>(express)) / 8);
			if (birthday && p.age < 255) p.age++;
		});
	}

	void Engine::TakeReadout()
	{
		// Integer sums, so adding chunks up in any order gives the same readout.
		struct Acc
		{
			int64_t sum = 0;
			int64_t square = 0;
			int64_t confidence = 0;
			uint32_t count = 0;
			uint32_t share[3] = {};
			uint32_t changed = 0;
		};
		const uint32_t T = config.topics;
		std::vector<Acc> acc(groups.size() * T);
		const bool first = classes.empty();
		classes.resize(static_cast<size_t>(store.Capacity()) * T, 1);

		store.ForEachActive([&](AgentId a) {
			const auto it = std::lower_bound(groups.begin(), groups.end(), store.Group(a));
			if (it == groups.end() || *it != store.Group(a)) return;
			const size_t g = static_cast<size_t>(it - groups.begin());
			for (uint32_t t = 0; t < T; t++)
			{
				Acc& c = acc[g * T + t];
				const int16_t q = store.Stance(a, t);
				const uint8_t cls = ClassOf(q);
				uint8_t& before = classes[static_cast<size_t>(a) * T + t];
				c.sum += q;
				c.square += static_cast<int64_t>(q) * q;
				c.confidence += store.Confidence(a, t);
				c.count++;
				c.share[cls]++;
				if (!first && cls != before) c.changed++;
				before = cls;
			}
		});

		readout.day = day;
		readout.topics = T;
		readout.groups = groups;
		readout.cells.assign(acc.size(), TopicReadout{});
		for (size_t i = 0; i < acc.size(); i++)
		{
			const Acc& c = acc[i];
			TopicReadout& r = readout.cells[i];
			r.count = c.count;
			if (c.count == 0) continue;
			const double n = c.count;
			const double mean = static_cast<double>(c.sum) / n / 32767.0;
			const double meanSquare = static_cast<double>(c.square) / n / (32767.0 * 32767.0);
			r.mean = static_cast<float>(mean);
			r.variance = static_cast<float>(std::max(0.0, meanSquare - mean * mean));
			r.skeptic = static_cast<float>(c.share[0] / n);
			r.neutral = static_cast<float>(c.share[1] / n);
			r.supporter = static_cast<float>(c.share[2] / n);
			r.confidence = static_cast<float>(static_cast<double>(c.confidence) / n / 255.0);
			r.changeRate = static_cast<float>(c.changed / n);
		}
	}

	void Engine::Write(Writer& w) const
	{
		w.U32(EngineTag);
		w.U64(config.seed);
		w.U32(config.slotsPerDay);
		w.U32(config.memoryK);
		w.U32(config.topics);
		w.U32(config.chunk);
		w.U8(static_cast<uint8_t>(config.policy));
		w.F32(config.surrogateWeight);
		for (const TopicSpec& t : topics)
		{
			w.F32(t.priorMean);
			w.F32(t.priorSpread);
			w.F32(t.stubbornness);
		}
		w.F64(now);
		w.U32(day);
		w.U32(round);
		for (const float d : drift) w.F32(d);
		store.Write(w);
		w.Array(std::span<const uint32_t>(offsets));
		w.Array(std::span<const AgentId>(targets));
		w.Array(std::span<const uint8_t>(weights));
		queue.Write(w);
		w.Array(std::span<const GroupId>(groups));
		w.Array(std::span<const uint8_t>(classes));
		w.U32(readout.day);
		for (const TopicReadout& r : readout.cells)
		{
			w.U32(r.count);
			w.F32(r.mean);
			w.F32(r.supporter);
			w.F32(r.neutral);
			w.F32(r.skeptic);
			w.F32(r.confidence);
			w.F32(r.variance);
			w.F32(r.changeRate);
		}
	}

	bool Engine::Read(Reader& r)
	{
		if (!r.Expect(EngineTag)) return false;
		Engine s;
		s.config.seed = r.U64();
		s.config.slotsPerDay = r.U32();
		s.config.memoryK = r.U32();
		s.config.topics = r.U32();
		s.config.chunk = r.U32();
		const uint8_t policy = r.U8();
		s.config.surrogateWeight = r.F32();
		const uint32_t T = s.config.topics;
		if (!r.Ok() || T < 1 || T > MaxTopics || s.config.slotsPerDay < 1 || s.config.chunk < 1 || s.config.memoryK < 1
			|| s.config.memoryK > MaxMemory || policy > static_cast<uint8_t>(Policy::Weighted) || !std::isfinite(s.config.surrogateWeight))
		{
			r.Fail();
			return false;
		}
		s.config.policy = static_cast<Policy>(policy);
		s.topics.resize(T);
		for (uint32_t t = 0; t < T; t++)
		{
			TopicSpec& spec = s.topics[t];
			spec.id = static_cast<uint8_t>(t);
			spec.priorMean = r.F32();
			spec.priorSpread = r.F32();
			spec.stubbornness = r.F32();
			if (!std::isfinite(spec.priorMean) || !std::isfinite(spec.priorSpread) || !std::isfinite(spec.stubbornness)) r.Fail();
		}
		s.now = r.F64();
		s.day = r.U32();
		s.round = r.U32();
		s.drift.resize(T);
		for (float& d : s.drift)
		{
			d = r.F32();
			if (!std::isfinite(d)) r.Fail();
		}
		if (!r.Ok() || !std::isfinite(s.now) || s.now < s.day || s.now >= static_cast<double>(s.day) + 1.0)
		{
			r.Fail();
			return false;
		}
		if (!s.store.Read(r)) return false;
		const uint32_t n = s.store.Capacity();
		if (s.store.Topics() != T || s.store.MemoryK() != s.config.memoryK) r.Fail();
		r.Array(s.offsets, MaxAgents + 1);
		r.Array(s.targets, MaxLinks);
		r.Array(s.weights, MaxLinks);
		if (!r.Ok()) return false;
		bool graphOk = s.offsets.size() == static_cast<size_t>(n) + 1 && s.offsets[0] == 0
			&& s.offsets.back() == s.targets.size() && s.weights.size() == s.targets.size();
		for (size_t i = 1; graphOk && i < s.offsets.size(); i++) graphOk = s.offsets[i - 1] <= s.offsets[i];
		for (size_t i = 0; graphOk && i < s.targets.size(); i++) graphOk = s.targets[i] < n;
		for (uint32_t a = 0; graphOk && a < n; a++) graphOk = s.store.Row(a) == AgentStore::NoRow || s.store.Row(a) < n;
		if (!graphOk)
		{
			r.Fail();
			return false;
		}
		if (!s.queue.Read(r)) return false;
		r.Array(s.groups, MaxAgents);
		r.Array(s.classes, MaxAgents * MaxTopics);
		if (!r.Ok()) return false;
		bool restOk = s.classes.empty() || s.classes.size() == static_cast<size_t>(n) * T;
		for (size_t i = 1; restOk && i < s.groups.size(); i++) restOk = s.groups[i - 1] < s.groups[i];
		for (size_t i = 0; restOk && i < s.classes.size(); i++) restOk = s.classes[i] <= 2;
		if (!restOk)
		{
			r.Fail();
			return false;
		}
		s.readout.day = r.U32();
		s.readout.topics = T;
		s.readout.groups = s.groups;
		const uint32_t cells = static_cast<uint32_t>(s.groups.size() * T);
		if (!r.Ok() || cells > r.Remaining() / 32)
		{
			r.Fail();
			return false;
		}
		s.readout.cells.resize(cells);
		for (TopicReadout& c : s.readout.cells)
		{
			c.count = r.U32();
			c.mean = r.F32();
			c.supporter = r.F32();
			c.neutral = r.F32();
			c.skeptic = r.F32();
			c.confidence = r.F32();
			c.variance = r.F32();
			c.changeRate = r.F32();
		}
		if (!r.Ok()) return false;

		s.external = external;
		*this = std::move(s);
		return true;
	}
}
