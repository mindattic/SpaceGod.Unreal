// UnitySpaceApp.Radio.cpp -- US-T656: the radio host's per-frame tick and its resets (see UnitySpaceApp.Radio.inl).

#include "UnitySpaceApp.h"

#include "Audio/GameAudio.h"
#include "Information/News.h"
#include "PlayerOptions.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace SpaceGod
{
	using namespace SpaceGod::Core;

	void UnitySpaceApp::RadioTick()
	{
		if (Clock == nullptr) return;
		radioHeard.DropAfter(Clock->Jd); // a rewind or a load
		cs::Ref<Newswire> news = Galaxy != nullptr && Galaxy->Traffic != nullptr ? Galaxy->Traffic->News : nullptr;
		if (news != radioNews)
		{
			if (radioNews != nullptr) radioNews->Received -= radioNewsToken;
			radioNews = news;
			radioNewsToken = news != nullptr ? (news->Received += [this](cs::Ref<StarSystem> at, cs::Ref<SystemReport> report) { RadioRelayReceived(at, report); }) : 0;
		}
		if (ActiveSystem != radioSystem)
		{
			radioSystem = ActiveSystem;
			radioSystemIndex = Galaxy != nullptr && ActiveSystem != nullptr ? Galaxy->Systems.IndexOf(ActiveSystem) : -1;
		}
		if (ActiveSystem == nullptr)
		{
			radio.Tick(-1, DVec3(), Clock->Jd);
			return;
		}
		if (radioSystemIndex >= 0 && radio.SystemId() == radioSystemIndex && !PlayerOptions::ScriptedRun() && SpaceGod::Audio::GameAudio::Enabled())
		{
			// US-T681: ambient talk (read-only, view-local, its own random stream). A new system or a rebase back-fills what is
			// already in flight first: the log clamps a SentJd earlier than its newest, so nothing newer may go out before it.
			if (!radioAmbientSeeded)
			{
				std::random_device device;
				const uint64_t clock = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				radioAmbient.Seed(((static_cast<uint64_t>(device()) << 32) ^ device()) ^ clock);
				radioAmbientSeeded = true;
			}
			const DVec3 hereKm = CommsHere().HelioKm;
			const double marginKm = radio.Receiver().TravelMarginKm();
			radioAmbientOut.clear();
			if (radio.SystemId() != radioAmbientSystemId || radio.RebaseCount() != radioAmbientRebases)
			{
				radioAmbientSystemId = radio.SystemId();
				radioAmbientRebases = radio.RebaseCount();
				radioAmbient.Clear();
				radioAmbient.BackFill(ActiveSystem, radioSystemIndex, hereKm, marginKm, Clock->Jd, radioAmbientOut);
			}
			radioAmbient.Tick(ActiveSystem, radioSystemIndex, hereKm, marginKm, Clock->Jd, radioAmbientOut);
			for (const auto& m : radioAmbientOut) radio.Emit(m);
		}
		if (radioSystemIndex >= 0 && !PlayerOptions::ScriptedRun())
		{
			// US-T682: the world's events in this system go out as chatter (read-only, view-local).
			radioChatterOut.clear();
			radioChatter.Pump(ActiveSystem, radioSystemIndex, Clock->Jd, radioChatterOut);
			for (const auto& m : radioChatterOut)
			{
				if (m.Text.empty()) radio.Emit(m.Message);
				else radio.EmitText(m.Message, m.Text);
			}
		}
		radio.Tick(radioSystemIndex, CommsHere().HelioKm, Clock->Jd);
	}

	void UnitySpaceApp::RadioReset()
	{
		radio.Reset();
		radioHeard.Clear();
		radioRelay.Clear();
		radioChatter.Clear();
		radioChatterOut.clear();
		radioAmbient.Clear();
		radioAmbientOut.clear();
		radioAmbientSystemId = -1;
		radioAmbientRebases = ~0ull;
		radioSystem = nullptr;
		radioSystemIndex = -1;
	}

	void UnitySpaceApp::RadioRelayReceived(const cs::Ref<StarSystem>& at, const cs::Ref<SystemReport>& report)
	{
		if (at == nullptr || at != ActiveSystem || radioSystemIndex < 0 || report == nullptr) return;
		std::vector<RadioRelay::Bulletin> bulletins;
		radioRelay.Bulletins(at, radioSystemIndex, *report, bulletins);
		for (const auto& b : bulletins) radio.EmitText(b.Message, b.Text);
	}
}
