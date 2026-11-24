// UnitySpaceApp.Radio.cpp -- US-T656: the radio host's per-frame tick and its resets (see UnitySpaceApp.Radio.inl).

#include "UnitySpaceApp.h"

#include "Information/News.h"
#include "PlayerOptions.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"

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
