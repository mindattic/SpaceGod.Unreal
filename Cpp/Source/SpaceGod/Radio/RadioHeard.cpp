// US-T674: the radio lines the local listener has heard (see RadioHeard.h).

#include "Radio/RadioHeard.h"

#include "Information/LightSpeed.h"
#include "Information/Ranges.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnitySpaceApp.h"

namespace SpaceGod::Radio
{
	using namespace SpaceGod::Core;

	RadioHeard::RadioHeard(UnitySpaceApp& app) : app(app)
	{
		app.GameRadio().AddSink(this);
	}

	RadioHeard::~RadioHeard()
	{
		app.GameRadio().RemoveSink(this);
	}

	void RadioHeard::OnRadioArrival(const RadioHost& host, const RadioArrival& a)
	{
		if (a.Message == nullptr) return;
		const RadioMessage& m = *a.Message;
		const RadioChannel* channel = app.ActiveSystem != nullptr && host.SystemId() >= 0
			? channels.For(app.ActiveSystem, host.SystemId()).Find(m.ChannelId) : nullptr;

		HeardLine line;
		cs::String tag = channel != nullptr ? cs::Cat(channel->Name, " ", cs::Format("{0:0.0##}", channel->FrequencyMhz)) : cs::Cat("ch ", m.ChannelId);
		cs::String text = m.TextSlot != RadioMessage::NoText && host.Log() != nullptr ? host.Log()->Text(a.Index)
			: cs::Cat(channel != nullptr ? ToString(channel->Kind) : "Radio", " traffic");
		cs::String sender = m.EmitterKey == 0 ? cs::String() : cs::Cat("#", m.EmitterKey, ": ");
		line.Text = cs::Cat("[", tag, "] ", sender, text, "  - sent ", LightSpeed::Describe(a.AgeDays), " ago, ", Ranges::Spoken(a.DistanceKm));
		line.Kind = (channel != nullptr && channel->Kind == RadioChannelKind::Pirate) || m.Priority >= 3 ? Tone::Hostile
			: channel != nullptr && channel->Kind == RadioChannelKind::Guard ? Tone::Wary
			: channel != nullptr && channel->Kind == RadioChannelKind::Fleet ? Tone::Friendly
			: Tone::Neutral;
		line.ArrivalJd = a.ArrivalJd;
		line.Heard = Time::unscaledTime;
		if (static_cast<int>(lines.size()) >= Capacity) lines.pop_front();
		lines.push_back(std::move(line));
	}

	void RadioHeard::DropAfter(double jd)
	{
		while (!lines.empty() && lines.back().ArrivalJd > jd) lines.pop_back();
	}
}
