// US-T969: Light Society's social graph in compressed sparse rows.

#include "LightSociety/Graph.h"

#include <algorithm>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t GraphTag = FourCC('G', 'R', 'P', 'H');
	}

	void Graph::BuildFrom(uint32_t nodes, std::span<const Edge> edges)
	{
		std::vector<Edge> sorted;
		sorted.reserve(edges.size());
		for (Edge e : edges)
		{
			if (e.a == e.b || e.a >= nodes || e.b >= nodes) continue;
			if (e.a > e.b) std::swap(e.a, e.b);
			sorted.push_back(e);
		}
		std::sort(sorted.begin(), sorted.end(), [](const Edge& x, const Edge& y)
		{
			if (x.a != y.a) return x.a < y.a;
			if (x.b != y.b) return x.b < y.b;
			if (x.weight != y.weight) return x.weight > y.weight;
			return x.kind < y.kind;
		});
		sorted.erase(std::unique(sorted.begin(), sorted.end(), [](const Edge& x, const Edge& y)
		{
			return x.a == y.a && x.b == y.b;
		}), sorted.end());

		offsets.assign(static_cast<size_t>(nodes) + 1, 0);
		for (const Edge& e : sorted)
		{
			offsets[e.a + 1]++;
			offsets[e.b + 1]++;
		}
		for (size_t i = 1; i < offsets.size(); i++) offsets[i] += offsets[i - 1];

		// Sorted by (a, b), each row fills with its lower neighbours first and its higher ones after, both ascending.
		targets.assign(sorted.size() * 2, 0);
		weights.assign(sorted.size() * 2, 0);
		kinds.assign(sorted.size() * 2, 0);
		std::vector<uint32_t> cursor(offsets.begin(), offsets.end() - 1);
		const auto put = [&](AgentId from, AgentId to, const Edge& e)
		{
			const uint32_t at = cursor[from]++;
			targets[at] = to;
			weights[at] = e.weight;
			kinds[at] = e.kind;
		};
		for (const Edge& e : sorted)
		{
			put(e.a, e.b, e);
			put(e.b, e.a, e);
		}
	}

	void Graph::Rebuild(AgentStore& store, std::span<const Edge> added)
	{
		std::vector<Edge> edges = Edges();
		std::erase_if(edges, [&](const Edge& e) { return !store.IsActive(e.a) || !store.IsActive(e.b); });
		for (const Edge& e : added)
			if (store.IsActive(e.a) && store.IsActive(e.b)) edges.push_back(e);
		BuildFrom(store.Capacity(), edges);
		BindRows(store);
	}

	void Graph::BindRows(AgentStore& store) const
	{
		const uint32_t nodes = NodeCount();
		store.ForEachActive([&](AgentId id) { store.Row(id) = id < nodes ? id : AgentStore::NoRow; });
	}

	void Graph::DropEdgesAmong(std::span<const AgentId> nodes)
	{
		std::vector<uint8_t> marked(NodeCount(), 0);
		for (const AgentId id : nodes)
			if (id < marked.size()) marked[id] = 1;
		std::vector<Edge> edges = Edges();
		std::erase_if(edges, [&](const Edge& e) { return marked[e.a] && marked[e.b]; });
		BuildFrom(NodeCount(), edges);
	}

	std::vector<Edge> Graph::Edges() const
	{
		std::vector<Edge> edges;
		edges.reserve(EdgeCount());
		for (AgentId a = 0; a < NodeCount(); a++)
		{
			for (uint32_t i = offsets[a]; i < offsets[a + 1]; i++)
				if (targets[i] > a) edges.push_back({a, targets[i], kinds[i], weights[i]});
		}
		return edges;
	}

	void Graph::Write(Writer& w) const
	{
		w.U32(GraphTag);
		w.Array(std::span<const uint32_t>(offsets));
		w.Array(std::span<const AgentId>(targets));
		w.Array(std::span<const uint8_t>(weights));
		w.Array(std::span<const uint8_t>(kinds));
	}

	bool Graph::Read(Reader& r)
	{
		if (!r.Expect(GraphTag)) return false;
		Graph g;
		r.Array(g.offsets, MaxAgents + 1);
		r.Array(g.targets, 0xFFFFFFFFu);
		r.Array(g.weights, 0xFFFFFFFFu);
		r.Array(g.kinds, 0xFFFFFFFFu);
		if (!r.Ok()) return false;

		// Offsets must run from 0 up to the target count without falling; targets must be in range, rows strictly
		// ascending and every tie present both ways.
		const size_t n = g.offsets.size();
		const size_t m = g.targets.size();
		bool valid = n >= 1 && g.offsets[0] == 0 && g.offsets[n - 1] == m && m % 2 == 0 && g.weights.size() == m
			&& g.kinds.size() == m;
		for (size_t i = 1; valid && i < n; i++) valid = g.offsets[i] >= g.offsets[i - 1];
		for (AgentId a = 0; valid && a + 1 < n; a++)
		{
			for (uint32_t i = g.offsets[a]; valid && i < g.offsets[a + 1]; i++)
			{
				const AgentId b = g.targets[i];
				valid = static_cast<size_t>(b) + 1 < n && b != a && (i == g.offsets[a] || g.targets[i - 1] < b);
				if (!valid) break;
				const auto row = g.Neighbours(b);
				const auto it = std::lower_bound(row.begin(), row.end(), a);
				valid = it != row.end() && *it == a;
				if (valid)
				{
					const size_t j = g.offsets[b] + static_cast<size_t>(it - row.begin());
					valid = g.weights[j] == g.weights[i] && g.kinds[j] == g.kinds[i];
				}
			}
		}
		if (!valid)
		{
			r.Fail();
			return false;
		}

		*this = std::move(g);
		return true;
	}
}
