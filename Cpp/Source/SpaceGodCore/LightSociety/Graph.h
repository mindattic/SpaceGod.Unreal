#pragma once
// US-T969: Light Society's social graph in compressed sparse rows, and the generators that wire a society up.

#include "LightSociety/AgentStore.h"
#include "LightSociety/Codec.h"
#include "LightSociety/Types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>
	/// Undirected ties in compressed sparse rows: a node is an <see cref="AgentId"/>, its row is
	/// targets[offsets[id] .. offsets[id + 1]) in ascending id order, with a weight and an <see cref="EdgeKind"/> beside
	/// each target. Every tie is stored both ways. Built whole from an edge list, so the layout is a pure function of the
	/// edges (whatever their order) and never of the history that produced them.
	/// </summary>
	class Graph
	{
	public:
		/// <summary>
		/// Replaces the graph with <paramref name="edges"/> over nodes 0 .. <paramref name="nodes"/> - 1. Self-loops and
		/// edges past the last node are dropped; a pair tied more than once keeps one tie, its heaviest (the lowest kind
		/// among equally heavy ones).
		/// </summary>
		void BuildFrom(uint32_t nodes, std::span<const Edge> edges);

		/// <summary>
		/// Rebuilds after agents joined or left: keeps the ties between agents still active, adds <paramref name="added"/>,
		/// spans every slot in the store and points each active agent's row at itself.
		/// </summary>
		void Rebuild(AgentStore& store, std::span<const Edge> added);

		/// <summary>Sets every active agent's <see cref="AgentStore::Row"/>: its id when the graph spans it, else NoRow.</summary>
		void BindRows(AgentStore& store) const;

		/// <summary>Drops every tie whose both ends are in <paramref name="nodes"/> (the paper's influencer-influencer ties).</summary>
		void DropEdgesAmong(std::span<const AgentId> nodes);

		uint32_t NodeCount() const { return static_cast<uint32_t>(offsets.size() - 1); }
		/// <summary>Undirected ties (each is stored twice).</summary>
		uint32_t EdgeCount() const { return static_cast<uint32_t>(targets.size() / 2); }

		uint32_t Degree(AgentId id) const { return id < NodeCount() ? offsets[id + 1] - offsets[id] : 0; }
		/// <summary>The agent's neighbours in ascending id order; empty for an agent the graph does not span.</summary>
		std::span<const AgentId> Neighbours(AgentId id) const { return Row(targets, id); }
		/// <summary>The weight of each tie in <see cref="Neighbours"/>, index for index.</summary>
		std::span<const uint8_t> Weights(AgentId id) const { return Row(weights, id); }
		/// <summary>The <see cref="EdgeKind"/> of each tie in <see cref="Neighbours"/>, index for index.</summary>
		std::span<const uint8_t> Kinds(AgentId id) const { return Row(kinds, id); }

		std::span<const uint32_t> Offsets() const { return offsets; }
		std::span<const AgentId> Targets() const { return targets; }

		/// <summary>Every tie once, with a &lt; b, sorted by (a, b).</summary>
		std::vector<Edge> Edges() const;

		/// <summary>Appends the graph as one section of a blob.</summary>
		void Write(Writer& w) const;
		/// <summary>Reads a section <see cref="Write"/> wrote; on failure the reader fails and the graph is unchanged.</summary>
		bool Read(Reader& r);

	private:
		template <class T>
		std::span<const T> Row(const std::vector<T>& column, AgentId id) const
		{
			if (id >= NodeCount()) return {};
			return {column.data() + offsets[id], offsets[id + 1] - offsets[id]};
		}

		std::vector<uint32_t> offsets{0};
		std::vector<AgentId> targets;
		std::vector<uint8_t> weights;
		std::vector<uint8_t> kinds;
	};

	// Generators (Generators.cpp). Each appends ties to out; every draw is Rng::Hash(seed, agent, attempt, op), so the
	// result is a pure function of the arguments. Pass a distinct op per call that shares a seed.

	/// <summary>
	/// Barabasi-Albert preferential attachment over <paramref name="nodes"/> in their given order: the first m + 1 form a
	/// clique, then each later node ties to m distinct earlier ones, each picked with probability proportional to degree.
	/// </summary>
	void BarabasiAlbert(std::span<const AgentId> nodes, uint32_t m, uint64_t seed, uint32_t op, std::vector<Edge>& out,
		EdgeKind kind = EdgeKind::Social, uint8_t weight = 255);

	/// <summary>Ties every pair of <paramref name="members"/> (a household or a business): k(k-1)/2 ties.</summary>
	void Clique(std::span<const AgentId> members, EdgeKind kind, uint8_t weight, std::vector<Edge>& out);

	/// <summary>
	/// <paramref name="links"/> sparse ties between two groups, each joining a member of <paramref name="from"/> to one of
	/// <paramref name="to"/> picked uniformly.
	/// </summary>
	void CrossGroup(std::span<const AgentId> from, std::span<const AgentId> to, uint32_t links, uint64_t seed, uint32_t op,
		std::vector<Edge>& out, uint8_t weight = 255);

	/// <summary>
	/// The round(<paramref name="fraction"/> x n) best connected of <paramref name="candidates"/> (the influencers), by
	/// degree high to low, ties by id low to high.
	/// </summary>
	std::vector<AgentId> TopByDegree(const Graph& graph, std::span<const AgentId> candidates, double fraction);
}
