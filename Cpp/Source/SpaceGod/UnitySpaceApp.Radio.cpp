// UnitySpaceApp.Radio.cpp -- US-T656: the radio host's per-frame tick and its resets (see UnitySpaceApp.Radio.inl).

#include "UnitySpaceApp.h"

#include "SimClock.h"
#include "Systems/Galaxy.h"

namespace SpaceGod
{
	using namespace SpaceGod::Core;

	void UnitySpaceApp::RadioTick()
	{
		if (Clock == nullptr) return;
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
		radio.Tick(radioSystemIndex, CommsHere().HelioKm, Clock->Jd);
	}

	void UnitySpaceApp::RadioReset()
	{
		radio.Reset();
		radioSystem = nullptr;
		radioSystemIndex = -1;
	}
}
