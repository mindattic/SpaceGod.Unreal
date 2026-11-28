// US-T969: Light Society's graph generators.

#include "LightSociety/Graph.h"
#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace MindAttic::Society
{
	namespace
	{
		/// <summary>Draws a node waits for its m distinct targets before it settles for fewer.</summary>
		constexpr uint32_t AttemptsPerTarget = 64;

		size_t Pick(uint64_t h, size_t count)
		{
			const size_t i = static_cast<size_t>(Rng::Unit(h) * static_cast<double>(count));
			return i < count ? i : count - 1;
		}
	}

	void BarabasiAlbert(std::span<const AgentId> nodes, uint32_t m, uint64_t seed, uint32_t op, std::vector<Edge>& out,
		EdgeKind kind, uint8_t weight)
	{
		if (m == 0 || nodes.size() < 2) return;
		m = static_cast<uint32_t>(std::min<size_t>(m, nodes.size() - 1));
		const uint8_t k = static_cast<uint8_t>(kind);
		const size_t core = std::min<size_t>(static_cast<size_t>(m) + 1, nodes.size());
		Clique(nodes.first(core), kind, weight, out);

		// Every tie's two ends, so a uniform pick from this list picks a node in proportion to its degree.
		std::vector<AgentId> ends;
		ends.reserve(core * (core - 1) + (nodes.size() - core) * 2 * m);
		for (size_t i = 0; i < core; i++)
			ends.insert(ends.end(), core - 1, nodes[i]);

		std::vector<AgentId> picked;
		picked.reserve(m);
		for (size_t i = core; i < nodes.size(); i++)
		{
			const AgentId node = nodes[i];
			picked.clear();
			const size_t existing = ends.size();
			for (uint32_t attempt = 0; picked.size() < m && attempt < m * AttemptsPerTarget; attempt++)
			{
				const AgentId target = ends[Pick(Rng::Hash(seed, node, attempt, op), existing)];
				if (std::find(picked.begin(), picked.end(), target) == picked.end()) picked.push_back(target);
			}
			for (const AgentId target : picked)
			{
				out.push_back({node, target, k, weight});
				ends.push_back(node);
				ends.push_back(target);
			}
		}
	}

	void Clique(std::span<const AgentId> members, EdgeKind kind, uint8_t weight, std::vector<Edge>& out)
	{
		const uint8_t k = static_cast<uint8_t>(kind);
		for (size_t i = 0; i < members.size(); i++)
		{
			for (size_t j = i + 1; j < members.size(); j++)
				out.push_back({members[i], members[j], k, weight});
		}
	}

	void CrossGroup(std::span<const AgentId> from, std::span<const AgentId> to, uint32_t links, uint64_t seed, uint32_t op,
		std::vector<Edge>& out, uint8_t weight)
	{
		if (from.empty() || to.empty()) return;
		const uint8_t k = static_cast<uint8_t>(EdgeKind::CrossGroup);
		for (uint32_t i = 0; i < links; i++)
		{
			const AgentId a = from[Pick(Rng::Hash(seed, i, 0, op), from.size())];
			const AgentId b = to[Pick(Rng::Hash(seed, i, 1, op), to.size())];
			out.push_back({a, b, k, weight});
		}
	}

	std::vector<AgentId> TopByDegree(const Graph& graph, std::span<const AgentId> candidates, double fraction)
	{
		std::vector<AgentId> ranked(candidates.begin(), candidates.end());
		const double f = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
		const size_t count = std::min(ranked.size(), static_cast<size_t>(std::llround(f * static_cast<double>(ranked.size()))));
		std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(count), ranked.end(),
			[&](AgentId x, AgentId y)
			{
				const uint32_t dx = graph.Degree(x);
				const uint32_t dy = graph.Degree(y);
				return dx != dy ? dx > dy : x < y;
			});
		ranked.resize(count);
		return ranked;
	}
}
