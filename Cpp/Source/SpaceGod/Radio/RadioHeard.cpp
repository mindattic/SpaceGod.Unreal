// US-T674: the radio lines the local listener has heard (see RadioHeard.h).

#include "Radio/RadioHeard.h"

#include "Comms/Comms.h"
#include "Comms/Pilots.h"
#include "Comms/VoiceCasting.h"
#include "Fleet.h"
#include "Information/LightSpeed.h"
#include "Information/Ranges.h"
#include "Radio/ChatterLines.h"
#include "Systems/Galaxy.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnitySpaceApp.h"
#include "Voice/VoiceArbiter.h"
#include "Voice/CommsVoice.h"
#include "World.h"

#include <limits>
namespace SpaceGod::Radio
{
	using namespace SpaceGod::Core;

	RadioHeard::RadioHeard(UnitySpaceApp& app) : app(app)
	{
		app.GameRadio().AddSink(this);
		// US-T703: a radio line the arbiter's full queue drops is counted into the host's digest.
		Voice::VoiceArbiter::SetDigest([this](const Voice::RadioLine& evicted)
		{
			this->app.GameRadio().Digest().Count(static_cast<uint32_t>(evicted.ChannelId), static_cast<uint16_t>(evicted.Situation),
				RadioDigestKind::Evicted, false);
		});
	}

	RadioHeard::~RadioHeard()
	{
		Voice::VoiceArbiter::SetDigest(nullptr);
		app.GameRadio().RemoveSink(this);
	}

	void RadioHeard::AddDigest(const std::vector<RadioDigestLine>& digest, double jd)
	{
		if (digest.empty()) return;
		const RadioDigestLine* best = nullptr;
		for (const RadioDigestLine& d : digest)
		{
			HeardLine line;
			line.Text = d.Text;
			line.Kind = Tone::Digest;
			line.ArrivalJd = jd; // DropAfter rewinds it like any other line
			line.Heard = Time::unscaledTime;
			if (static_cast<int>(lines.size()) >= Capacity) lines.pop_front();
			lines.push_back(std::move(line));
			if (d.Text.empty()) continue;
			const bool better = best == nullptr || (d.Distress > 0 && best->Distress == 0)
				|| ((d.Distress > 0) == (best->Distress > 0) && d.Total > best->Total);
			if (better) best = &d;
		}

		// One summary spoken, at the lowest score, only into silence and not more than once every DigestSpeakSeconds.
		const float now = Time::unscaledTime;
		if (best == nullptr || !Voice::CommsVoice::Idle() || Voice::VoiceArbiter::Waiting() != 0) return;
		if (lastDigestSpoken >= 0.0f && now - lastDigestSpoken < DigestSpeakSeconds) return;
		Voice::RadioLine spoken;
		spoken.Line.Who = cs::String("radio");
		spoken.Line.Text = best->Text;
		spoken.Score = std::numeric_limits<float>::lowest();
		spoken.Priority = 0;
		spoken.ChannelId = static_cast<int>(best->ChannelId);
		spoken.Situation = static_cast<int>(ChatterSituation::Count);
		Voice::VoiceArbiter::Radio(std::move(spoken));
		lastDigestSpoken = now;
	}

	void RadioHeard::OnRadioArrival(const RadioHost& host, const RadioArrival& a, const RadioReceptionResult& reception)
	{
		if (a.Message == nullptr || reception.Route == RadioRoute::Digest) return;
		const RadioMessage& m = *a.Message;
		const RadioChannel* channel = app.ActiveSystem != nullptr && host.SystemId() >= 0
			? channels.For(app.ActiveSystem, host.SystemId()).Find(m.ChannelId) : nullptr;

		HeardLine line;
		cs::String tag = channel != nullptr ? cs::Cat(channel->Name, " ", cs::Format("{0:0.0##}", channel->FrequencyMhz)) : cs::Cat("ch ", m.ChannelId);
		const cs::Ref<World> world = app.ActiveSystem != nullptr ? app.ActiveSystem->World : nullptr;
		cs::String text = m.TextSlot != RadioMessage::NoText && host.Log() != nullptr ? host.Log()->Text(a.Index)
			: ChatterLines::Realise(m, world, a.ArrivalJd, channel != nullptr ? channel->Kind : RadioChannelKind::Civil);
		cs::String sender = m.EmitterKey == 0 ? cs::String() : cs::Cat(ChatterLines::SenderName(world, m.EmitterKey), ": ");
		line.Text = cs::Cat("[", tag, "] ", sender, text, "  - sent ", LightSpeed::Describe(a.AgeDays), " ago, ", Ranges::Spoken(a.DistanceKm));
		line.Kind = (channel != nullptr && channel->Kind == RadioChannelKind::Pirate) || m.Priority >= 3 ? Tone::Hostile
			: channel != nullptr && channel->Kind == RadioChannelKind::Guard ? Tone::Wary
			: channel != nullptr && channel->Kind == RadioChannelKind::Fleet ? Tone::Friendly
			: Tone::Neutral;
		line.ArrivalJd = a.ArrivalJd;
		line.Heard = Time::unscaledTime;
		if (static_cast<int>(lines.size()) >= Capacity) lines.pop_front();
		lines.push_back(std::move(line));

		// US-T685: a Spoken arrival goes to the voice arbiter, in its emitter's cast voice, garbled as it was heard.
		if (reception.Route != RadioRoute::Spoken || text.empty()) return;
		Voice::RadioLine spoken;
		spoken.Line.Who = m.EmitterKey == 0 ? cs::String("radio") : ChatterLines::SenderName(world, m.EmitterKey);
		spoken.Line.Text = reception.Clarity == RadioClarity::Noisy ? RadioReception::Garble(text, m.Id) : text;
		spoken.Line.Voice = VoiceOf(world, m);
		spoken.Score = static_cast<float>(reception.Score);
		spoken.Priority = m.Priority;
		spoken.ChannelId = m.ChannelId;
		spoken.Situation = m.Situation;
		spoken.Urgent = (channel != nullptr && channel->Kind == RadioChannelKind::Guard && m.Priority >= 3)
			|| (m.AddresseeKey != 0 && m.AddresseeKey == host.Listener().UnitKey);
		Voice::VoiceArbiter::Radio(std::move(spoken));
	}

	VoiceSpec RadioHeard::VoiceOf(const cs::Ref<World>& world, const RadioMessage& m)
	{
		// ChatterLines' SenderOf: a tagged key names a comms party; their met pilot, a station's traffic control, else the
		// message's faction.
		if (world == nullptr || world->Fleet == nullptr) return VoiceSpec();
		VoiceCasting cast(world->Fleet->Factions());
		if ((m.EmitterKey >> 32) == 1)
		{
			CommsParty party = Comms::Resolve(world, static_cast<int>(static_cast<uint32_t>(m.EmitterKey)));
			if (party.Ship != nullptr)
			{
				auto pilot = Characters::Of(world)->Find(party.Ship);
				if (pilot != nullptr) return cast.Of(pilot);
			}
			else if (party.Station != nullptr) return cast.Of(party.Station);
		}
		cs::Ref<Faction> faction;
		if (m.FactionId >= 0 && m.FactionId < world->Fleet->Factions().Count()) faction = world->Fleet->Factions()[m.FactionId];
		return cast.Of(faction);
	}

	void RadioHeard::DropAfter(double jd)
	{
		while (!lines.empty() && lines.back().ArrivalJd > jd) lines.pop_back();
	}
}
