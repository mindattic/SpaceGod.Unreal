// Hud.Radio.inl -- included INSIDE class Hud (see UI/Hud.h). Body in UI/Hud.Radio.cpp.
// US-T674: the radio log: the last lines the local listener heard (UnitySpaceApp::RadioHeardLines), bottom left above the
// bottom edge's drawers and tabs, each fading out a while after it was heard; the whole history is the Logs screen's
// Radio tab.

private:
	/// <summary>The latest radio lines, oldest on top, coloured by PlayerOptions' stance colours; nothing when none are recent.</summary>
	void DrawRadioLog();
	cs::Ref<GUIStyle> radioLogStyle;
