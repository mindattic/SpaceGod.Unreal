#pragma once
// US-T970: Light Society's readout, the paper's aggregate over agent states: per group and topic, where stances sit and
// how fast they move.

#include "LightSociety/Types.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>A stance above this is a supporter's, below its negation a skeptic's, else neutral.</summary>
	inline constexpr float SupporterStance = 0.2f;

	/// <summary>One group's stances on one topic.</summary>
	struct TopicReadout
	{
		/// <summary>Agents counted.</summary>
		uint32_t count = 0;
		float mean = 0;
		float supporter = 0;
		float neutral = 0;
		float skeptic = 0;
		/// <summary>Mean confidence, in [0, 1].</summary>
		float confidence = 0;
		float variance = 0;
		/// <summary>Share of agents whose class (supporter, neutral, skeptic) changed since the readout before.</summary>
		float changeRate = 0;
	};

	/// <summary>Every group's stance on every topic at the end of one day.</summary>
	struct Readout
	{
		/// <summary>The day it was read at.</summary>
		uint32_t day = 0;
		uint32_t topics = 0;
		/// <summary>Groups in ascending id order.</summary>
		std::vector<GroupId> groups;
		/// <summary>Group-major: cells[g * topics + t].</summary>
		std::vector<TopicReadout> cells;

		/// <summary>The cell of a group and topic, or null when the group holds no agents or the topic is out of range.</summary>
		const TopicReadout* Find(GroupId group, uint32_t topic) const
		{
			const auto it = std::lower_bound(groups.begin(), groups.end(), group);
			if (it == groups.end() || *it != group || topic >= topics) return nullptr;
			return &cells[static_cast<size_t>(it - groups.begin()) * topics + topic];
		}
	};
}
