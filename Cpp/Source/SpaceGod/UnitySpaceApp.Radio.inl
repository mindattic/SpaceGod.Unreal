// UnitySpaceApp.Radio.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Radio.cpp.
//
// US-T656: the radio host (Radio/RadioHost.h, US-T631) wired in. RadioTick runs once a frame with the listener (the unit
// held, else the camera's frame: CommsHere's HelioKm), the active system and the clock; a load resets it, an FTL arrival
// and a showcase tour stop rebase it (the host itself also catches the clock going back and a jump faster than c/2).
// sg.Radio (RadioCommand.cpp) prints its state and emits a test message on Guard.

public:
	SpaceGod::Radio::RadioHost& GameRadio() { return radio; }
	const SpaceGod::Radio::RadioHost& GameRadio() const { return radio; }
	/// <summary>US-T674: the lines heard, for the HUD's radio log and the Logs screen's Radio tab.</summary>
	const SpaceGod::Radio::RadioHeard& RadioHeardLines() const { return radioHeard; }

	/// <summary>Once a frame: the host's Tick at the listener, in the active system, at the clock's Jd.</summary>
	void RadioTick();
	/// <summary>The listener jumped (FTL arrival, a tour stop): the schedule is rebuilt at the next tick.</summary>
	void RadioRebase() { radio.Rebase(); }
	/// <summary>A load: the radio history restarts empty.</summary>
	void RadioReset();

private:
	/// <summary>US-T645: a report delivered to the listener's system goes out on its Relay channel (Core::RadioRelay).</summary>
	void RadioRelayReceived(const cs::Ref<Core::StarSystem>& at, const cs::Ref<Core::SystemReport>& report);

	SpaceGod::Radio::RadioHost radio;
	Core::RadioRelay radioRelay;
	/// <summary>The newswire whose Received RadioTick subscribed to (a load or rewind brings another), and the token.</summary>
	cs::Ref<Core::Newswire> radioNews;
	int radioNewsToken = 0;
	/// <summary>The active system RadioTick last saw and its index in the galaxy (looked up only when it changes).</summary>
	cs::Ref<Core::StarSystem> radioSystem;
	int radioSystemIndex = -1;
	/// <summary>US-T674: a sink on radio, so declared after it (gone first); the HUD only reads it.</summary>
	SpaceGod::Radio::RadioHeard radioHeard{*this};
