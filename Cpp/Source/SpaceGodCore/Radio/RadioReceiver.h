#pragma once
// US-T627: one local listener's radio schedule (US-T575). A binary min-heap of (arrival, log index) over the listener's
// system RadioLog: Emit schedules a new message in O(log n) only when its class's reach covers the listener; Advance pops
// what has arrived (at most MaxPopsPerFrame a frame), re-solving each against the listener's current path; Rebase rebuilds
// the heap from the log's buckets after a discontinuity in O(buckets + kept). Log indices are never reused, so an entry
// whose message the log has retired or evicted is simply skipped.
// View-local: not saved, not in the lockstep frames or the bench digests, never draws from the simulation RNG.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Radio/RadioLog.h"
#include "Radio/RadioMessage.h"
#include "Radio/Wavefront.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	/// <summary>A message heard this frame.</summary>
	struct RadioArrival
	{
		uint64_t Index = 0;
		/// <summary>The logged message; valid until the log is next appended to, retired or cleared.</summary>
		const RadioMessage* Message = nullptr;
		double ArrivalJd = 0;
		/// <summary>How far the listener was from the origin when the front reached it, km.</summary>
		double DistanceKm = 0;
		/// <summary>How long ago it was sent, days (now - SentJd).</summary>
		double AgeDays = 0;
	};

	class SPACEGODCORE_API RadioReceiver
	{
	public:
		static constexpr int MaxPopsPerFrame = 64;

		/// <summary>
		/// Schedules the message at log <paramref name="index"/>, just sent, when the listener at <paramref name="listenerKm"/>
		/// is within the class's R_max plus the travel margin of its origin. Returns whether it was scheduled. O(log n).
		/// </summary>
		bool Emit(uint64_t index, const RadioMessage& message, const DVec3& listenerKm);

		/// <summary>
		/// Pops the entries whose arrival is at or before <paramref name="nowJd"/>, re-solving each against
		/// <paramref name="listenerAt"/>(jd) (heliocentric km): one whose front has not reached the moved listener yet is
		/// pushed back, one heard beyond its reach is dropped, the rest are appended to <paramref name="arrivals"/>. Past
		/// <paramref name="maxPops"/> the due entries left wait for the next frame and are counted in OverflowCount.
		/// Returns how many arrived. O(k log n).
		/// </summary>
		template <typename ListenerAt>
		int Advance(const RadioLog& log, double nowJd, const ListenerAt& listenerAt, std::vector<RadioArrival>& arrivals, int maxPops = MaxPopsPerFrame)
		{
			int heard = 0;
			for (int pops = 0; pops < maxPops && !heap_.empty() && heap_.front().ArrivalJd <= nowJd; pops++)
			{
				Entry e = PopTop();
				const RadioMessage* m = log.Get(e.Index);
				if (!m)
				{
					skipped_++;
					continue;
				}
				double t = Wavefront::ArrivalJd(m->OriginKm, m->SentJd, listenerAt, nowJd);
				double distanceKm = (listenerAt(t) - m->OriginKm).Length();
				if (distanceKm > m->MaxRangeKm() + travelMarginKm_)
				{
					skipped_++;
					continue;
				}
				if (t > nowJd)
				{
					Push({ t, m->SentJd, e.Index });
					continue;
				}
				RadioArrival a;
				a.Index = e.Index;
				a.Message = m;
				a.ArrivalJd = t;
				a.DistanceKm = distanceKm;
				a.AgeDays = nowJd - m->SentJd;
				arrivals.push_back(a);
				heard++;
			}
			overflow_ += CountDue(nowJd);
			return heard;
		}

		/// <summary>
		/// Rebuilds the schedule from <paramref name="log"/> after a discontinuity (FTL, system change, rewind, load,
		/// tour, teleport): buckets that Wavefront::BucketMayArrive rules out (every front already past the listener, or
		/// the listener beyond their reach) are skipped whole; each kept message is solved against
		/// <paramref name="listenerAt"/> and scheduled when its front is still to arrive within reach. O(buckets + kept).
		/// </summary>
		template <typename ListenerAt>
		void Rebase(const RadioLog& log, const ListenerAt& listenerAt, double nowJd)
		{
			heap_.clear();
			DVec3 hereKm = listenerAt(nowJd);
			for (const RadioBucket& b : log.Buckets())
			{
				double lastHeardJd = b.LastSentJd + LightSpeed::DelayDays(b.MaxRangeKm + travelMarginKm_);
				if (!Wavefront::BucketMayArrive(b, hereKm, nowJd, lastHeardJd))
					continue;
				for (uint64_t i = b.First; i < b.Last; i++)
				{
					const RadioMessage* m = log.Get(i);
					if (!m)
						continue;
					double t = Wavefront::ArrivalJd(m->OriginKm, m->SentJd, listenerAt, nowJd);
					if (t < nowJd || (listenerAt(t) - m->OriginKm).Length() > m->MaxRangeKm() + travelMarginKm_)
						continue;
					heap_.push_back({ t, m->SentJd, i });
				}
			}
			Heapify();
		}

		/// <summary>Drops every entry sent after <paramref name="jd"/>, for a rewind. O(n).</summary>
		void DropAfter(double jd);

		void Clear() { heap_.clear(); }

		/// <summary>How far the listener may move while a front is in flight, km: widens every range gate.</summary>
		double TravelMarginKm() const { return travelMarginKm_; }
		void SetTravelMarginKm(double km) { travelMarginKm_ = cs::Math::Max(0.0, km); }

		int Count() const { return static_cast<int>(heap_.size()); }
		/// <summary>The earliest scheduled arrival, or +infinity when nothing is scheduled.</summary>
		double NextArrivalJd() const;

		/// <summary>Due entries left waiting by the per-frame pop cap, summed over frames.</summary>
		uint64_t OverflowCount() const { return overflow_; }
		/// <summary>Entries dropped on pop: retired or evicted from the log, or heard beyond their reach.</summary>
		uint64_t SkippedCount() const { return skipped_; }
		/// <summary>Messages Emit left unscheduled because the listener was beyond their reach.</summary>
		uint64_t OutOfRangeCount() const { return outOfRange_; }

	private:
		struct Entry
		{
			double ArrivalJd = 0;
			double SentJd = 0;
			uint64_t Index = 0;
		};

		void Push(const Entry& e);
		Entry PopTop();
		void Heapify();
		uint64_t CountDue(double nowJd) const;

		std::vector<Entry> heap_;
		double travelMarginKm_ = 0;
		uint64_t overflow_ = 0;
		uint64_t skipped_ = 0;
		uint64_t outOfRange_ = 0;
	};
}
