#include "Radio/RadioLog.h"

#include "Astro.h"
#include "Information/LightSpeed.h"

#include <limits>

namespace SpaceGod::Core
{
	namespace
	{
		const cs::String EmptyRadioText;
	}

	RadioLog::RadioLog() : ring_(Capacity), texts_(TextCapacity), lastSentJd_(std::numeric_limits<double>::lowest()) {}

	uint64_t RadioLog::Append(const RadioMessage& message)
	{
		if (Count() >= Capacity)
		{
			evicted_++;
			DropOldest(first_ + 1);
		}

		uint64_t index = end_++;
		RadioMessage& m = ring_[index % Capacity];
		m = message;
		if (m.SentJd < lastSentJd_)
			m.SentJd = lastSentJd_;
		lastSentJd_ = m.SentJd;
		m.TextSlot = RadioMessage::NoText;

		int64_t key = static_cast<int64_t>(cs::Math::Floor(m.SentJd / BucketDays));
		if (buckets_.empty() || buckets_.back().Key != key)
		{
			RadioBucket b;
			b.Key = key;
			b.First = index;
			b.FirstSentJd = m.SentJd;
			b.MinKm = m.OriginKm;
			b.MaxKm = m.OriginKm;
			buckets_.push_back(b);
		}
		RadioBucket& b = buckets_.back();
		b.Last = index + 1;
		b.LastSentJd = m.SentJd;
		b.MinKm = DVec3(cs::Math::Min(b.MinKm.X, m.OriginKm.X), cs::Math::Min(b.MinKm.Y, m.OriginKm.Y), cs::Math::Min(b.MinKm.Z, m.OriginKm.Z));
		b.MaxKm = DVec3(cs::Math::Max(b.MaxKm.X, m.OriginKm.X), cs::Math::Max(b.MaxKm.Y, m.OriginKm.Y), cs::Math::Max(b.MaxKm.Z, m.OriginKm.Z));
		b.MaxRangeKm = cs::Math::Max(b.MaxRangeKm, m.MaxRangeKm());
		return index;
	}

	uint64_t RadioLog::AppendText(const RadioMessage& message, const cs::String& text)
	{
		uint64_t index = Append(message);
		uint32_t slot = nextText_;
		nextText_ = (nextText_ + 1) % TextCapacity;
		TextEntry& e = texts_[slot];
		e.Owner = index;
		e.Used = true;
		e.Text = text;
		ring_[index % Capacity].TextSlot = slot;
		return index;
	}

	uint64_t RadioLog::Retire(double nowJd)
	{
		uint64_t dropped = 0;
		while (!buckets_.empty())
		{
			const RadioBucket& b = buckets_.front();
			// The bucket's youngest front is the smallest; once it is past the loudest class's reach, all are.
			double youngestFrontKm = (nowJd - b.LastSentJd) * Astro::SecondsPerDay * LightSpeed::SpeedKmS;
			if (youngestFrontKm <= b.MaxRangeKm)
				break;
			uint64_t until = b.Last;
			dropped += until - first_;
			DropOldest(until);
		}
		retired_ += dropped;
		return dropped;
	}

	const RadioMessage* RadioLog::Get(uint64_t index) const
	{
		return index >= first_ && index < end_ ? &ring_[index % Capacity] : nullptr;
	}

	const cs::String& RadioLog::Text(uint64_t index) const
	{
		const RadioMessage* m = Get(index);
		if (!m || m->TextSlot == RadioMessage::NoText)
			return EmptyRadioText;
		const TextEntry& e = texts_[m->TextSlot];
		return e.Used && e.Owner == index ? e.Text : EmptyRadioText;
	}

	void RadioLog::Clear()
	{
		DropOldest(end_);
		buckets_.clear();
		lastSentJd_ = std::numeric_limits<double>::lowest();
	}

	void RadioLog::DropOldest(uint64_t until)
	{
		for (; first_ < until; first_++)
		{
			uint32_t slot = ring_[first_ % Capacity].TextSlot;
			if (slot == RadioMessage::NoText)
				continue;
			TextEntry& e = texts_[slot];
			if (e.Used && e.Owner == first_)
			{
				e.Used = false;
				e.Text.clear();
			}
		}
		while (!buckets_.empty() && buckets_.front().Last <= first_)
			buckets_.pop_front();
		if (!buckets_.empty() && buckets_.front().First < first_)
			buckets_.front().First = first_;
	}
}
