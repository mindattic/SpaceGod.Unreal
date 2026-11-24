// US-T564: other players' names over their ships (CoopNames.h).

#include "UI/CoopNames.h"

#include "UnitySpaceApp.h"
#include "Coop/CoopOptions.h"
#include "Settings/GameSetting.h"
#include "UI/UiStyle.h"
#include "Views/EmbodiedShine.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI::CoopNames
{
	bool DrawOverShip(UnitySpaceApp& app, int seat, const cs::String& name)
	{
		if (!Coop::CoopOptions::ShowPlayerNames()->Value) return false;
		UnitySpaceApp::ShiningUnit unit;
		if (!app.ShiningOf(PlayerId(seat), unit) || !unit.Shows()) return false;
		Vector2 g;
		float depth;
		if (!app.ToGui(unit.Centre, g, depth) || depth <= 0) return false;
		if (g.x < 0 || g.x > Screen::width || g.y < 0 || g.y > Screen::height) return false;

		static cs::Ref<GUIStyle> style;
		if (!style)
		{
			style = UiStyle::TextStyle(UiStyle::Type::Label);
			style->alignment = TextAnchor::LowerCenter;
		}
		float lift = static_cast<float>(unit.Radius / depth * app.PixelsPerUnitAtUnitDistance());
		lift = Mathf::Min(Mathf::Max(lift, 8.0f), static_cast<float>(Screen::height) * 0.25f);
		auto rect = Rect(g.x - 110, g.y - lift - 22, 220, 20);
		for (const auto& u : app.UiRects)
			if (u.Overlaps(rect)) return false; // US-T214: a world tag stays out from under the docked panels

		auto old = GUI::color;
		auto c = Views::EmbodiedShine::SeatColor(seat);
		GUI::color = Color(c.r, c.g, c.b, 1.0f);
		GUI::Label(rect, name, style);
		GUI::color = old;
		return true;
	}
}
