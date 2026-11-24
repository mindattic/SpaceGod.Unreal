// US-T658: the frame-rate counter (declared in Hud.Main.inl), drawn in the top right corner under the top edge's drawers
// while Settings > Interface > Show FPS counter is on. Frames are counted and the text rebuilt twice a second.

#include "UI/Hud.h"

#include "InterfaceOptions.h"

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float FpsRefreshSeconds = 0.5f;
		constexpr float FpsMargin = 6;
	}

	void Hud::DrawFps()
	{
		if (!InterfaceOptions::ShowsFps())
		{
			fpsFrames = 0;
			fpsText = cs::String();
			return;
		}
		if (Time::frameCount != fpsLastFrame)
		{
			fpsLastFrame = Time::frameCount;
			fpsFrames++;
		}
		float now = Time::unscaledTime;
		if (fpsText.empty() && fpsFrames <= 1) fpsSince = now;
		if (now - fpsSince >= FpsRefreshSeconds)
		{
			fpsText = cs::Cat(Mathf::RoundToInt((fpsFrames - 1) / (now - fpsSince)), " FPS");
			fpsFrames = 1;
			fpsSince = now;
		}
		if (fpsText.empty()) return;

		float w = label->CalcSize(GUIContent(fpsText)).x;
		float y = FpsMargin;
		auto top = drawers.LastEdgeExtent(HudDock::Edge::Top);
		if (!top.Empty()) y = Mathf::Max(y, top.Bottom() + FpsMargin);
		DrawLabel(fpsText, Vector2(Screen::width - FpsMargin - w, y), Dim, false);
	}
}
