// US-T637: the HUD's caption strip (declared in Hud.Main.inl): Audio::Captions' queue drawn centred above the bottom edge's
// drawers and tabs, oldest on top, each line on its own backdrop at Settings > Accessibility's caption size and background
// opacity. Drawn with the HUD's other panels, so under the UI scale and hidden with the HUD (and on the full screens).

#include "UI/Hud.h"

#include "AccessibilityOptions.h"
#include "Audio/Captions.h"
#include "UI/Icons.h"

namespace SpaceGod::UI
{
	namespace
	{
		constexpr int FontSizes[3] = {14, 18, 24}; // Small, Medium, Large
		constexpr float FadeSeconds = 0.5f; // a caption fades out over its last half second
		constexpr float PadX = 10, PadY = 3, LineGap = 3, Margin = 16;
	}

	void Hud::DrawCaptions()
	{
		if (AccessibilityOptions::CaptionsShown() == AccessibilityOptions::CaptionLevel::Off) return;
		const auto& showing = Audio::Captions::Showing();
		if (showing.Count() == 0) return;

		int scale = static_cast<int>(AccessibilityOptions::CaptionsScale());
		auto& style = captionStyles[scale];
		if (!style)
		{
			style = UiStyle::TextStyle(FontSizes[scale], UiStyle::Text);
			style->wordWrap = true;
			style->alignment = TextAnchor::UpperCenter;
		}
		float backdrop = AccessibilityOptions::CaptionsBackdrop();

		float sw = static_cast<float>(Screen::width), sh = static_cast<float>(Screen::height);
		float bottom = sh;
		auto edge = drawers.LastEdgeExtent(HudDock::Edge::Bottom);
		if (!edge.Empty()) bottom = Mathf::Min(bottom, edge.y);
		auto tabs = drawers.LastTabExtent(HudDock::Edge::Bottom);
		if (!tabs.Empty()) bottom = Mathf::Min(bottom, tabs.y);
		float maxW = Mathf::Min(sw - 2 * Margin, FontSizes[scale] * 48.0f);

		auto old = GUI::color;
		float y = bottom - Margin;
		for (int i = showing.Count() - 1; i >= 0; i--)
		{
			const auto& c = showing[i];
			auto content = GUIContent(c.Kind == Audio::Captions::CaptionKind::Sound ? cs::Cat("[", c.Text, "]")
				: c.Speaker.empty() ? c.Text : cs::Cat(c.Speaker, ": ", c.Text));
			float w = Mathf::Min(style->CalcSize(content).x, maxW - 2 * PadX);
			float h = style->CalcHeight(content, w);
			y -= h + 2 * PadY;
			auto box = Rect((sw - w) * 0.5f - PadX, y, w + 2 * PadX, h + 2 * PadY);
			float a = Mathf::Clamp01(c.SecondsLeft / FadeSeconds);
			if (backdrop > 0)
			{
				GUI::color = Color(UiStyle::Ink.r, UiStyle::Ink.g, UiStyle::Ink.b, backdrop * a * old.a);
				GUI::DrawTexture(box, Icons::White());
			}
			GUI::color = Color(old.r, old.g, old.b, a * old.a);
			style->normal.textColor = c.Kind == Audio::Captions::CaptionKind::Sound ? UiStyle::Dim : UiStyle::Text;
			GUI::Label(Rect(box.x + PadX, box.y + PadY, w, h), content, style);
			y -= LineGap;
		}
		GUI::color = old;
	}
}
