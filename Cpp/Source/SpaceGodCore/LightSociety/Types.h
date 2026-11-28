#pragma once
// US-T968: Light Society's plain data (docs/research/LightSociety-design.md). Game-agnostic, std only.

#include <cmath>
#include <cstdint>

namespace MindAttic::Society
{
	/// <summary>An agent's slot in the <see cref="AgentStore"/>: dense, reused after the agent leaves.</summary>
	using AgentId = uint32_t;
	/// <summary>A group of agents the host reads results by (a settlement in SpaceGod).</summary>
	using GroupId = uint32_t;

	inline constexpr AgentId NoAgent = 0xFFFFFFFFu;
	inline constexpr GroupId NoGroup = 0xFFFFFFFFu;
	/// <summary>Household and business ids of an agent with neither.</summary>
	inline constexpr uint32_t NoUnit = 0xFFFFFFFFu;

	/// <summary>Most agents one society can hold (2^20).</summary>
	inline constexpr uint32_t MaxAgents = 1u << 20;
	/// <summary>Most topics an agent holds a stance on.</summary>
	inline constexpr uint32_t MaxTopics = 8;
	/// <summary>Most entries in an agent's memory ring.</summary>
	inline constexpr uint32_t MaxMemory = 32;

	/// <summary>An agent's static profile: who it is, set once when it joins.</summary>
	struct Profile
	{
		/// <summary>The host's own id for the agent (a citizen id), or 0 for a synthetic agent.</summary>
		uint64_t externalId = 0;
		GroupId group = NoGroup;
		uint32_t household = NoUnit;
		uint32_t business = NoUnit;
		uint8_t age = 0;
		/// <summary>Social class (wealth), 0 lowest.</summary>
		uint8_t cls = 0;
		/// <summary>Education, 0 lowest.</summary>
		uint8_t edu = 0;
		/// <summary>Big Five: openness, conscientiousness, extraversion, agreeableness, neuroticism; 0..255.</summary>
		uint8_t big5[5] = {};
		/// <summary>Host-defined trait bits.</summary>
		uint16_t traits = 0;
	};

	/// <summary>A topic agents hold a stance on, and how they start out on it.</summary>
	struct TopicSpec
	{
		uint8_t id = 0;
		/// <summary>Mean starting stance, in [-1, 1].</summary>
		float priorMean = 0;
		/// <summary>Spread of starting stances around the mean.</summary>
		float priorSpread = 0;
		/// <summary>How hard stances on this topic are to move, in [0, 1].</summary>
		float stubbornness = 0;
	};

	enum class EdgeKind : uint8_t
	{
		Social = 0,
		Family = 1,
		Business = 2,
		CrossGroup = 3,
	};

	/// <summary>An undirected social tie between two agents.</summary>
	struct Edge
	{
		AgentId a = NoAgent;
		AgentId b = NoAgent;
		uint8_t kind = 0;
		uint8_t weight = 0;
	};

	/// <summary>Who an <see cref="EnvEvent"/> reaches.</summary>
	enum class Scope : uint8_t
	{
		All = 0,
		Group = 1,
		Agent = 2,
		/// <summary>The social-graph neighbours of the target agent.</summary>
		Neighbours = 3,
	};

	/// <summary>Something the world does to the society (a raid, a battle, a sermon), pushed into its event queue.</summary>
	struct EnvEvent
	{
		/// <summary>Time in days.</summary>
		double time = 0;
		uint8_t priority = 0;
		/// <summary>Host-defined event kind.</summary>
		uint16_t kind = 0;
		uint8_t topic = 0;
		/// <summary>Which way it pushes stances on its topic, in [-1, 1].</summary>
		float push = 0;
		/// <summary>How strongly, in [0, 1].</summary>
		float magnitude = 0;
		Scope scope = Scope::All;
		/// <summary>The group or agent it reaches (by <see cref="scope"/>); unused for <see cref="Scope::All"/>.</summary>
		uint32_t target = 0;
		/// <summary>The agent or host entity it came from.</summary>
		uint32_t source = 0;
	};

	/// <summary>Which backend answers an operation (the mixture-of-models router).</summary>
	enum class Policy : uint8_t
	{
		AllTeacher = 0,
		AllSurrogate = 1,
		/// <summary>Each sample goes to the surrogate with probability <see cref="Config::surrogateWeight"/>.</summary>
		Weighted = 2,
	};

	struct Config
	{
		uint64_t seed = 0;
		uint32_t slotsPerDay = 10;
		uint32_t memoryK = 4;
		/// <summary>Topics in use, at most <see cref="MaxTopics"/>.</summary>
		uint32_t topics = 1;
		/// <summary>Agents per work chunk: fixed, so results never depend on the core count.</summary>
		uint32_t chunk = 4096;
		Policy policy = Policy::AllTeacher;
		float surrogateWeight = 0;
	};

	/// <summary>A stance in [-1, 1] as Q15 fixed point.</summary>
	inline int16_t ToQ15(float stance)
	{
		const float s = stance < -1.0f ? -1.0f : (stance > 1.0f ? 1.0f : stance);
		return static_cast<int16_t>(std::lround(s * 32767.0f));
	}

	inline float FromQ15(int16_t q)
	{
		return static_cast<float>(q) / 32767.0f;
	}

	/// <summary>A value in [0, 1] (confidence, trust) as a byte.</summary>
	inline uint8_t ToU8(float unit)
	{
		const float u = unit < 0.0f ? 0.0f : (unit > 1.0f ? 1.0f : unit);
		return static_cast<uint8_t>(std::lround(u * 255.0f));
	}

	inline float FromU8(uint8_t b)
	{
		return static_cast<float>(b) / 255.0f;
	}
}
