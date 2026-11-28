#pragma once
// US-T970: Light Society's engine, the paper's M = <D, T, S_A, S_E, V, Q, F> with F = {f_I, f_P, f_pi, f_A, f_E, f_U}:
// f_I builds the society from profiles, edges and topics; events pop from Q in (time, priority, seq) batches, f_P picks
// who perceives each, f_pi (an IPolicy) decides stance changes and new events, f_U applies them; on every whole day f_E
// drifts the environment, f_A evolves every agent (memory decay, confidence relaxation, ageing) and the readout is taken.
// Deterministic: counter-based random numbers only, fixed iteration order, and work split into fixed-size chunks that are
// combined in chunk order, so running the chunks on threads (US-T973) cannot change a result.

#include "LightSociety/AgentStore.h"
#include "LightSociety/Codec.h"
#include "LightSociety/EventQueue.h"
#include "LightSociety/Readout.h"
#include "LightSociety/Types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>The kind of the event an agent emits when it speaks on a topic; it reaches the speaker's neighbours.</summary>
	inline constexpr uint16_t SpeakKind = 0xFFFF;

	/// <summary>Read-only CSR access to the social graph: the neighbours of row r are targets[offsets[r] .. offsets[r+1]).</summary>
	struct Neighbours
	{
		std::span<const uint32_t> offsets;
		std::span<const AgentId> targets;
		std::span<const uint8_t> weights;
	};

	/// <summary>One agent perceiving one event of a batch.</summary>
	struct Perception
	{
		/// <summary>Index into <see cref="PolicyBatch::events"/>.</summary>
		uint32_t event = 0;
		AgentId agent = NoAgent;
		/// <summary>How strongly it reaches the agent (the edge weight for a neighbour, 255 for a direct hit).</summary>
		uint8_t weight = 255;
	};

	/// <summary>A change f_pi decides for one agent on one topic; f_U applies it.</summary>
	struct StanceChange
	{
		AgentId agent = NoAgent;
		uint8_t topic = 0;
		float dStance = 0;
		float dConfidence = 0;
		/// <summary>What the agent remembers of it: the event kind (0 remembers nothing) and how it felt, in [-127, 127].</summary>
		uint16_t kind = 0;
		int8_t valence = 0;
	};

	/// <summary>One vectorized call to the policy: every perception of one chunk of the events of one kind in a batch.</summary>
	struct PolicyBatch
	{
		const AgentStore& store;
		const Config& config;
		std::span<const TopicSpec> topics;
		uint16_t kind = 0;
		/// <summary>The batch's events of this kind, in push order.</summary>
		std::span<const QueuedEvent> events;
		std::span<const Perception> perceptions;
		/// <summary>The batch number: the round argument of every random draw in it.</summary>
		uint32_t round = 0;
	};

	struct PolicyOut
	{
		std::vector<StanceChange> changes;
		/// <summary>New events, pushed to the queue in order after the batch.</summary>
		std::vector<EnvEvent> emit;

		void Clear()
		{
			changes.clear();
			emit.clear();
		}
	};

	/// <summary>
	/// f_pi. Decide reads the state the batch started with and writes only to its own out, so chunks of one batch may run
	/// at the same time. The router (US-T971) implements it over teacher, surrogate and lookup backends.
	/// </summary>
	class IPolicy
	{
	public:
		virtual ~IPolicy() = default;
		virtual void Decide(const PolicyBatch& batch, PolicyOut& out) = 0;
	};

	/// <summary>
	/// The built-in f_pi: bounded-confidence persuasion (a pull toward the event's push scaled by its magnitude, the edge
	/// weight, the topic's stubbornness, the agent's confidence and education; speech from too far off backfires), and an
	/// agent moved by an event may speak of it to its neighbours in the next slot, at half the magnitude.
	/// </summary>
	class DefaultPolicy final : public IPolicy
	{
	public:
		void Decide(const PolicyBatch& batch, PolicyOut& out) override;
	};

	class Engine
	{
	public:
		/// <summary>
		/// f_I: a fresh society at day 0, one agent per profile (agent i is profiles[i]) with stances drawn around each
		/// topic's prior, linked by the edges (ones naming an unknown agent or a self-loop are dropped). Topics beyond
		/// config.topics are ignored; missing ones get a neutral spec.
		/// </summary>
		void Init(const Config& config, std::span<const Profile> profiles, std::span<const Edge> edges, std::span<const TopicSpec> topics);

		/// <summary>Sets f_pi; null restores the built-in <see cref="DefaultPolicy"/>. Not owned, not saved.</summary>
		void SetPolicy(IPolicy* policy) { external = policy; }

		/// <summary>
		/// Queues events from the environment. Ones on an unknown topic, or with a non-finite time, push or magnitude, are
		/// skipped; ones in the past are moved to now; push and magnitude are clamped. Returns how many were queued.
		/// </summary>
		uint32_t Inject(std::span<const EnvEvent> events);

		/// <summary>Runs every event batch and day boundary up to and including <paramref name="time"/> (in days).</summary>
		void StepTo(double time);

		double Now() const { return now; }
		uint32_t Day() const { return day; }
		const Config& GetConfig() const { return config; }
		std::span<const TopicSpec> Topics() const { return topics; }
		const AgentStore& Store() const { return store; }
		const EventQueue& Queue() const { return queue; }
		/// <summary>The social graph; an agent's row is its id.</summary>
		Neighbours Links() const { return {offsets, targets, weights}; }
		/// <summary>The environment's level on a topic, in [-1, 1] (f_E).</summary>
		float Drift(uint32_t topic) const { return topic < drift.size() ? drift[topic] : 0.0f; }
		/// <summary>The readout taken at the end of the last whole day.</summary>
		const Readout& Latest() const { return readout; }

		/// <summary>Appends the engine (config, topics, clock, store, graph, queue, environment, readout) as one section.</summary>
		void Write(Writer& w) const;
		/// <summary>Reads a section <see cref="Write"/> wrote; on failure the reader fails and the engine is unchanged.</summary>
		bool Read(Reader& r);

	private:
		void RunBatch();
		void Perceive(std::span<const QueuedEvent> events, std::vector<Perception>& out) const;
		void Apply(const PolicyOut& out);
		void EndDay();
		void EvolveEnvironment();
		void EvolveAgents(uint32_t first, uint32_t last);
		void TakeReadout();
		bool Admit(EnvEvent& e) const;

		Config config;
		std::vector<TopicSpec> topics;
		AgentStore store;
		std::vector<uint32_t> offsets;
		std::vector<AgentId> targets;
		std::vector<uint8_t> weights;
		EventQueue queue;
		double now = 0;
		uint32_t day = 0;
		uint32_t round = 0;
		std::vector<float> drift;
		/// <summary>Every group an agent was ever in, ascending.</summary>
		std::vector<GroupId> groups;
		/// <summary>Each agent's class per topic at the last readout: 0 skeptic, 1 neutral, 2 supporter.</summary>
		std::vector<uint8_t> classes;
		Readout readout;

		IPolicy* external = nullptr;
		DefaultPolicy builtIn;
		std::vector<QueuedEvent> batch;
		std::vector<Perception> perceptions;
		std::vector<PolicyOut> outs;
	};
}
