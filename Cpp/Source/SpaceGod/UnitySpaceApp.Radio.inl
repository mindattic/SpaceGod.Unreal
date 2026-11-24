// UnitySpaceApp.Radio.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Radio.cpp.
//
// US-T656: the radio host (Radio/RadioHost.h, US-T631) wired in. RadioTick runs once a frame with the listener (the unit
// held, else the camera's frame: CommsHere's HelioKm), the active system and the clock; a load resets it, an FTL arrival
// and a showcase tour stop rebase it (the host itself also catches the clock going back and a jump faster than c/2).
// sg.Radio (RadioCommand.cpp) prints its state and emits a test message on Guard.

public:
	SpaceGod::Radio::RadioHost& GameRadio() { return radio; }
	const SpaceGod::Radio::RadioHost& GameRadio() const { return radio; }

	/// <summary>Once a frame: the host's Tick at the listener, in the active system, at the clock's Jd.</summary>
	void RadioTick();
	/// <summary>The listener jumped (FTL arrival, a tour stop): the schedule is rebuilt at the next tick.</summary>
	void RadioRebase() { radio.Rebase(); }
	/// <summary>A load: the radio history restarts empty.</summary>
	void RadioReset();

private:
	SpaceGod::Radio::RadioHost radio;
	/// <summary>The active system RadioTick last saw and its index in the galaxy (looked up only when it changes).</summary>
	cs::Ref<Core::StarSystem> radioSystem;
	int radioSystemIndex = -1;
