// US-T674: the HUD's radio log (declared in Hud.Radio.inl): the newest lines of UnitySpaceApp::RadioHeardLines drawn bottom
// left above the bottom edge's drawers and tabs, oldest on top, each on its own backdrop and fading out once it has been
// shown a while. Drawn with the HUD's other panels, so under the UI scale and hidden with the HUD (and on the full screens).

#include "UI/Hud.h"

#include "PlayerOptions.h"
#include "Radio/RadioHeard.h"
#include "UI/Icons.h"
#include "UnitySpaceApp.h"

namespace SpaceGod::UI
{
	namespace
	{
		constexpr int ShownLines = 8;
		constexpr float HoldSeconds = 18, FadeSeconds = 2; // a line shows for 18 s, then fades over 2
		constexpr float PadX = 8, PadY = 2, LineGap = 2, Margin = 16, Backdrop = 0.55f;

		Color ToneColor(Radio::RadioHeard::Tone tone, float alpha)
		{
			switch (tone)
			{
			case Radio::RadioHeard::Tone::Hostile: return PlayerOptions::HostileColor(alpha);
			case Radio::RadioHeard::Tone::Wary: return PlayerOptions::WaryColor(alpha);
			case Radio::RadioHeard::Tone::Friendly: return PlayerOptions::FriendlyColor(alpha);
			case Radio::RadioHeard::Tone::Digest: return PlayerOptions::NeutralColor(alpha * 0.6f); // US-T703: a summary, dimmed
			default: return PlayerOptions::NeutralColor(alpha);
			}
		}
	}

	void Hud::DrawRadioLog()
	{
		const auto& lines = app->RadioHeardLines().Lines();
		if (lines.empty()) return;
		float now = Time::unscaledTime;
		int first = static_cast<int>(lines.size());
		while (first > 0 && static_cast<int>(lines.size()) - first < ShownLines && now - lines[first - 1].Heard < HoldSeconds + FadeSeconds) first--;
		if (first == static_cast<int>(lines.size())) return;

		if (!radioLogStyle)
		{
			radioLogStyle = UiStyle::TextStyle(UiStyle::Type::Body);
			radioLogStyle->wordWrap = true;
			radioLogStyle->alignment = TextAnchor::UpperLeft;
		}

		float sw = static_cast<float>(Screen::width), sh = static_cast<float>(Screen::height);
		float bottom = sh;
		auto edge = drawers.LastEdgeExtent(HudDock::Edge::Bottom);
		if (!edge.Empty()) bottom = Mathf::Min(bottom, edge.y);
		auto tabs = drawers.LastTabExtent(HudDock::Edge::Bottom);
		if (!tabs.Empty()) bottom = Mathf::Min(bottom, tabs.y);
		float maxW = Mathf::Min(sw * 0.4f, UiStyle::Type::Body * 48.0f);

		auto old = GUI::color;
		float y = bottom - Margin;
		for (int i = static_cast<int>(lines.size()) - 1; i >= first; i--)
		{
			const auto& line = lines[i];
			auto content = GUIContent(line.Text);
			float w = Mathf::Min(radioLogStyle->CalcSize(content).x, maxW - 2 * PadX);
			float h = radioLogStyle->CalcHeight(content, w);
			y -= h + 2 * PadY;
			auto box = Rect(Margin, y, w + 2 * PadX, h + 2 * PadY);
			float a = Mathf::Clamp01((HoldSeconds + FadeSeconds - (now - line.Heard)) / FadeSeconds);
			GUI::color = Color(UiStyle::Ink.r, UiStyle::Ink.g, UiStyle::Ink.b, Backdrop * a * old.a);
			GUI::DrawTexture(box, Icons::White());
			GUI::color = Color(old.r, old.g, old.b, a * old.a);
			radioLogStyle->normal.textColor = ToneColor(line.Kind, 1);
			GUI::Label(Rect(box.x + PadX, box.y + PadY, w, h), content, radioLogStyle);
			y -= LineGap;
		}
		GUI::color = old;
	}
}
