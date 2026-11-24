#pragma once
// US-T648: the radio's digest (US-T578). Arrivals that are not shown one by one (a low score's overflow, CountedOnly
// traffic, lines the voice arbiter evicted, Unreadable ones) are counted per channel and situation in fixed arrays, never
// stored; every PeriodDays of game time (or every WarpedPeriodSeconds of real time while warped) each channel with counts
// yields one summary line, such as "Ceres Traffic: 23 messages in the last 1.0 h, mostly docking clearances; 1 distress
// call" or "Fleet 233.4: encrypted traffic, 12 bursts". Count is O(1); Take is O(channels with counts).
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and nothing draws
// from the simulation RNG.

#include "Cs/Cs.h"
#include "Radio/ChatterLines.h"
#include "Radio/RadioChannels.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace SpaceGod::Core
{
	/// <summary>Why an arrival went to the digest.</summary>
	enum class RadioDigestKind : uint8_t
	{
		/// <summary>Routed to Digest by its score or priority.</summary>
		Overflow,
		/// <summary>The tuner counted it only (not listened to, or encrypted to the listener).</summary>
		CountedOnly,
		/// <summary>The voice arbiter's full queue dropped it.</summary>
		Evicted,
		/// <summary>Too faint to read.</summary>
		Unreadable,
		Count
	};

	/// <summary>One channel's summary for a period.</summary>
	struct RadioDigestLine
	{
		uint32_t ChannelId = 0;
		cs::String Text;
		uint32_t Total = 0;
		uint32_t Distress = 0;
	};

	class SPACEGODCORE_API RadioDigest
	{
	public:
		/// <summary>The summary period, game days (10 minutes).</summary>
		static constexpr double PeriodDays = 10.0 / 1440.0;
		/// <summary>The summary period while time is warped, real seconds.</summary>
		static constexpr double WarpedPeriodSeconds = 30.0;
		/// <summary>Situation buckets: every ChatterSituation, then one for anything else.</summary>
		static constexpr int SituationBuckets = static_cast<int>(ChatterSituation::Count) + 1;

		/// <summary>Counts one arrival. O(1).</summary>
		void Count(uint32_t channelId, uint16_t situation, RadioDigestKind kind, bool encrypted);

		/// <summary>
		/// When the period is over at game <paramref name="jd"/> and real <paramref name="realSeconds"/> (WarpedPeriodSeconds
		/// when <paramref name="warped"/>), appends one line per channel with counts to <paramref name="out"/> and starts the
		/// next period. The clock going back (a rewind, a load) drops the counts and restarts the period. Channel names come
		/// from <paramref name="channels"/> ("ch N" without one).
		/// </summary>
		void Take(double jd, double realSeconds, bool warped, const RadioChannelSet* channels, std::vector<RadioDigestLine>& out);

		/// <summary>Drops the counts and restarts the period at the next Take.</summary>
		void Clear();

		/// <summary>Arrivals counted since the period began.</summary>
		uint64_t Pending() const { return pending; }

		/// <summary>A situation bucket as a plural noun ("docking clearances"); the last bucket is "other traffic".</summary>
		static const char* SituationPlural(int bucket);

	private:
		struct Entry
		{
			uint32_t ChannelId = 0;
			uint32_t Total = 0;
			uint32_t Encrypted = 0;
			std::array<uint32_t, SituationBuckets> Situations{};
			std::array<uint32_t, static_cast<size_t>(RadioDigestKind::Count)> Kinds{};
		};

		static cs::String Summary(const Entry& e, const cs::String& name, double periodDays);

		std::vector<Entry> entries;
		std::unordered_map<uint32_t, size_t> byChannel;
		uint64_t pending = 0;
		bool started = false;
		double periodJd = 0;
		double periodReal = 0;
	};
}
