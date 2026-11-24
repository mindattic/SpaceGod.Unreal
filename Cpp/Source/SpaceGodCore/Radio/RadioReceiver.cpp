#include "Radio/RadioReceiver.h"

#include <algorithm>
#include <limits>

namespace SpaceGod::Core
{
	namespace
	{
		// std::*_heap keep the largest first; ordering by "later arrives first" makes it a min-heap of arrival.
		template <typename T>
		bool ArrivesLater(const T& a, const T& b) { return a.ArrivalJd > b.ArrivalJd; }
	}

	bool RadioReceiver::Emit(uint64_t index, const RadioMessage& message, const DVec3& listenerKm)
	{
		double distanceKm = (listenerKm - message.OriginKm).Length();
		if (distanceKm > message.MaxRangeKm() + travelMarginKm_)
		{
			outOfRange_++;
			return false;
		}
		Push({ message.SentJd + LightSpeed::DelayDays(distanceKm), message.SentJd, index });
		return true;
	}

	void RadioReceiver::DropAfter(double jd)
	{
		heap_.erase(std::remove_if(heap_.begin(), heap_.end(), [jd](const Entry& e) { return e.SentJd > jd; }), heap_.end());
		Heapify();
	}

	double RadioReceiver::NextArrivalJd() const
	{
		return heap_.empty() ? std::numeric_limits<double>::infinity() : heap_.front().ArrivalJd;
	}

	void RadioReceiver::Push(const Entry& e)
	{
		heap_.push_back(e);
		std::push_heap(heap_.begin(), heap_.end(), ArrivesLater<Entry>);
	}

	RadioReceiver::Entry RadioReceiver::PopTop()
	{
		std::pop_heap(heap_.begin(), heap_.end(), ArrivesLater<Entry>);
		Entry e = heap_.back();
		heap_.pop_back();
		return e;
	}

	void RadioReceiver::Heapify()
	{
		std::make_heap(heap_.begin(), heap_.end(), ArrivesLater<Entry>);
	}

	uint64_t RadioReceiver::CountDue(double nowJd) const
	{
		// Only the due part of the heap is walked: a node not yet due has no due children.
		uint64_t due = 0;
		std::vector<size_t> open;
		if (!heap_.empty() && heap_[0].ArrivalJd <= nowJd)
			open.push_back(0);
		while (!open.empty())
		{
			size_t i = open.back();
			open.pop_back();
			due++;
			for (size_t c = 2 * i + 1; c <= 2 * i + 2 && c < heap_.size(); c++)
				if (heap_[c].ArrivalJd <= nowJd)
					open.push_back(c);
		}
		return due;
	}
}
