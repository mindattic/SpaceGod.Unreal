// Hud.Radio.inl -- included INSIDE class Hud (see UI/Hud.h). Body in UI/Hud.Radio.cpp.
// US-T674: the radio log: the last lines the local listener heard (UnitySpaceApp::RadioHeardLines), bottom left above the
// bottom edge's drawers and tabs, each fading out a while after it was heard; the whole history is the Logs screen's
// Radio tab.

private:
	/// <summary>The latest radio lines, oldest on top, coloured by PlayerOptions' stance colours; nothing when none are recent.</summary>
	void DrawRadioLog();
	cs::Ref<GUIStyle> radioLogStyle;

// US-T677: the radio tuner panel (body in UI/Hud.RadioTuner.cpp): Shift+C (KeyAction::RadioTuner) shows or hides it. A dial
// with the tuned frequency, the active system's channels with their activity in the last game hour (RadioTuner::Activity)
// and a lock mark on an encrypted net, per-row Tune / Monitor / Mute, presets 1-8 (a click recalls, Shift+click stores the
// tuned channel) and a Scan toggle. It drives the app's session tuner (UnitySpaceApp::GameRadioTuner) and nothing else.

public:
	/// <summary>The radio tuner key: the panel on or off. True when the press was spent.</summary>
	bool UpdateRadioTunerKey(bool keysFree);

private:
	/// <summary>The tuner panel, while shown and in a system with channels.</summary>
	void DrawRadioTuner();
	bool radioTunerShown = false;
	/// <summary>The first channel row shown (the list scrolls with the wheel over it).</summary>
	int radioTunerScroll = 0;
