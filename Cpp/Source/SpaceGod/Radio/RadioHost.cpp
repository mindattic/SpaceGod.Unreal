#include "Radio/RadioHost.h"

#include "Information/LightSpeed.h"

#include <algorithm>

namespace SpaceGod::Radio
{
	using namespace SpaceGod::Core;

	void RadioHost::Tick(int system, const DVec3& here, double now)
	{
		if (system != systemId)
		{
			// A new system: its own log, made on its first message; the old one's fronts cannot cross the stars.
			log.reset();
			receiver.Clear();
			systemId = system;
			rebasePending = false;
		}
		else if (started && now < jd)
		{
			// The clock went back (a rewind, a load): what was sent after it never was.
			rebasePending = true;
		}
		else if (started)
		{
			double dtSeconds = (now - jd) * 86400.0;
			double movedKm = (here - listenerKm).Length();
			if (movedKm > MinTravelMarginKm && movedKm > LightSpeed::SpeedKmS * JumpFractionOfLight * dtSeconds)
				rebasePending = true;
			else if (now > jd)
				velocityKmPerDay = (here - listenerKm) * (1.0 / (now - jd));
		}
		bool wentBack = started && now < jd;
		if (rebasePending || !started) velocityKmPerDay = DVec3();
		listenerKm = here;
		jd = now;
		started = true;
		if (log == nullptr)
			return;

		// A front may be in flight for the longest reach's light time; the listener may cover its speed times that meanwhile.
		double speedKmS = velocityKmPerDay.Length() / 86400.0;
		double flightSeconds = RadioPowerTable::LongestRangeKm() / LightSpeed::SpeedKmS;
		receiver.SetTravelMarginKm(cs::Math::Min(RadioPowerTable::LongestRangeKm(), cs::Math::Max(MinTravelMarginKm, speedKmS * flightSeconds)));

		log->Retire(jd);
		if (rebasePending)
		{
			if (wentBack) log->DropAfter(jd);
			DoRebase();
			if (wentBack) receiver.DropAfter(jd);
		}

		arrivals.clear();
		receiver.Advance(*log, jd, [this](double atJd) { return ListenerAt(atJd); }, arrivals);
		for (const RadioArrival& a : arrivals)
		{
			// US-T679: the tuner hears it first (activity for its scan); muted, unlistened or encrypted traffic stops here,
			// while a message on no channel of the set (Ignored, ChannelId 0) is routed as if there were no tuner.
			RadioAcceptance acceptance = RadioAcceptance::Heard;
			if (tuner != nullptr && listener.Channels != nullptr)
			{
				acceptance = tuner->Receive(*a.Message, *listener.Channels, nullptr, a.ArrivalJd);
				if (acceptance == RadioAcceptance::CountedOnly) continue;
			}
			heard++;
			RadioReceptionResult reception;
			if (acceptance == RadioAcceptance::Ignored)
			{
				RadioListener open = listener;
				open.Channels = nullptr;
				reception = RadioReception::Route(*a.Message, a.DistanceKm, open);
			}
			else
				reception = RadioReception::Route(*a.Message, a.DistanceKm, listener);
			routed[static_cast<size_t>(reception.Route)]++;
			if (reception.Route == RadioRoute::Ignored) continue;
			if (reception.Route == RadioRoute::Subtitled || reception.Route == RadioRoute::Spoken)
			{
				RecentEntry r;
				r.Message = *a.Message;
				r.Text = log->Text(a.Index);
				if (reception.Clarity == RadioClarity::Noisy) r.Text = RadioReception::Garble(r.Text, a.Message->Id);
				r.Reception = reception;
				r.ArrivalJd = a.ArrivalJd;
				r.DistanceKm = a.DistanceKm;
				r.AgeDays = a.AgeDays;
				if (static_cast<int>(recent.size()) >= RecentCapacity) recent.erase(recent.begin());
				recent.push_back(std::move(r));
			}
			for (IRadioSink* sink : sinks) sink->OnRadioArrival(*this, a, reception);
		}
	}

	uint64_t RadioHost::Emit(const RadioMessage& message)
	{
		if (!started || systemId < 0 || message.SystemId != systemId) return NoIndex;
		if (log == nullptr) log = std::make_shared<RadioLog>();
		return Schedule(log->Append(message));
	}

	uint64_t RadioHost::EmitText(const RadioMessage& message, const cs::String& text)
	{
		if (!started || systemId < 0 || message.SystemId != systemId) return NoIndex;
		if (log == nullptr) log = std::make_shared<RadioLog>();
		return Schedule(log->AppendText(message, text));
	}

	uint64_t RadioHost::Schedule(uint64_t index)
	{
		if (const RadioMessage* m = log->Get(index)) receiver.Emit(index, *m, listenerKm);
		return index;
	}

	void RadioHost::Reset()
	{
		log.reset();
		receiver.Clear();
		recent.clear();
		started = false;
		rebasePending = false;
		velocityKmPerDay = DVec3();
	}

	void RadioHost::AddSink(IRadioSink* sink)
	{
		if (sink != nullptr && std::find(sinks.begin(), sinks.end(), sink) == sinks.end()) sinks.push_back(sink);
	}

	void RadioHost::RemoveSink(IRadioSink* sink)
	{
		sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
	}

	void RadioHost::DoRebase()
	{
		rebasePending = false;
		rebases++;
		receiver.Rebase(*log, [this](double atJd) { return ListenerAt(atJd); }, jd);
	}
}
