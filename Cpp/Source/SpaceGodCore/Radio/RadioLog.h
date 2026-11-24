#pragma once
// US-T623: a star system's radio history (US-T575). An append-only ring of RadioMessage ordered by SentJd, indexed by
// 15-game-minute time buckets that each know their [First, Last) range, their origins' bounding box and the longest reach
// among their classes, so a receiver can skip whole buckets (Wavefront::BucketMayArrive) instead of looking at messages.
// Append is O(1); Retire is amortised O(1) per message; nothing walks the messages per frame.
// View-local: not saved, not in the lockstep frames or the bench digests, never draws from the simulation RNG.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Radio/RadioMessage.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace SpaceGod::Core
{
	/// <summary>The messages sent in one 15-game-minute window.</summary>
	struct SPACEGODCORE_API RadioBucket
	{
		/// <summary>The window's index: floor(SentJd / RadioLog::BucketDays).</summary>
		int64_t Key = 0;
		/// <summary>Log indices [First, Last) of the messages still held.</summary>
		uint64_t First = 0;
		uint64_t Last = 0;
		double FirstSentJd = 0;
		double LastSentJd = 0;
		/// <summary>The bounding box of the messages' origins, heliocentric km (never shrinks when the cap evicts).</summary>
		DVec3 MinKm;
		DVec3 MaxKm;
		/// <summary>The longest R_max among the messages' power classes, km.</summary>
		double MaxRangeKm = 0;

		uint64_t Count() const { return Last - First; }
	};

	class SPACEGODCORE_API RadioLog
	{
	public:
		static constexpr uint64_t Capacity = 8192;
		/// <summary>Literal-text slots held at once; the oldest text is dropped when they run out.</summary>
		static constexpr uint32_t TextCapacity = 1024;
		static constexpr double BucketDays = 15.0 / 1440.0;

		RadioLog();

		/// <summary>
		/// Logs a message and returns its log index (monotonic, never reused). A SentJd earlier than the last one is
		/// clamped up to it, keeping the ring in time order. When the ring is full the oldest message is evicted.
		/// </summary>
		uint64_t Append(const RadioMessage& message);

		/// <summary>Append, carrying literal text instead of a template (held until the message is retired or evicted).</summary>
		uint64_t AppendText(const RadioMessage& message, const cs::String& text);

		/// <summary>
		/// Drops every leading bucket whose fronts have all passed beyond its classes' R_max by nowJd; returns how many
		/// messages it dropped (also added to RetiredCount).
		/// </summary>
		uint64_t Retire(double nowJd);

		/// <summary>The message at a log index, or null once it is retired or evicted.</summary>
		const RadioMessage* Get(uint64_t index) const;

		/// <summary>The literal text of the message at a log index; empty when it has none (or it has been dropped).</summary>
		const cs::String& Text(uint64_t index) const;

		uint64_t FirstIndex() const { return first_; }
		uint64_t EndIndex() const { return end_; }
		uint64_t Count() const { return end_ - first_; }
		const std::deque<RadioBucket>& Buckets() const { return buckets_; }

		/// <summary>Messages dropped by Retire, and by the cap, since the log was made.</summary>
		uint64_t RetiredCount() const { return retired_; }
		uint64_t EvictedCount() const { return evicted_; }

		/// <summary>Drops every message sent after jd (a clock rewind: they never were), freeing their indices and texts, and sets the last SentJd back to the newest one kept.</summary>
		void DropAfter(double jd);

		void Clear();

	private:
		struct TextEntry
		{
			uint64_t Owner = 0;
			bool Used = false;
			cs::String Text;
		};

		void DropOldest(uint64_t until);

		std::vector<RadioMessage> ring_;
		std::vector<TextEntry> texts_;
		std::deque<RadioBucket> buckets_;
		uint64_t first_ = 0;
		uint64_t end_ = 0;
		uint32_t nextText_ = 0;
		double lastSentJd_ = 0;
		uint64_t retired_ = 0;
		uint64_t evicted_ = 0;
	};
}
