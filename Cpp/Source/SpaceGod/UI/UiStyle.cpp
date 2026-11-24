#include "UI/UiStyle.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace SpaceGod::UI::UiStyle
{
	namespace
	{
		bool highContrast = false;

		bool Same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

		/// The high-contrast stand-in for a palette colour (any other colour is kept): opaque near-black surfaces, white
		/// edges and text, a bright blue selection with a yellow edge.
		Color Contrast(Color c)
		{
			if (c.a <= 0) return c;
			if (c.r == Ink.r && c.g == Ink.g && c.b == Ink.b) return Color(0.01f, 0.01f, 0.015f, 1);
			if (Same(c, Edge) || Same(c, ControlEdge)) return Color(1, 1, 1, 1);
			if (Same(c, Tick)) return Color(Accent.r, Accent.g, Accent.b, 1);
			if (Same(c, ControlFill)) return Color(0.05f, 0.05f, 0.06f, 1);
			if (Same(c, ControlOnFill)) return Color(0.0f, 0.3f, 0.62f, 1);
			if (Same(c, ControlOnEdge)) return Color(1.0f, 0.9f, 0.15f, 1);
			if (Same(c, FieldFill)) return Color(0, 0, 0, 1);
			if (Same(c, RowHover)) return Color(0.0f, 0.22f, 0.45f, 1);
			if (Same(c, RowSelected)) return Color(0.0f, 0.34f, 0.68f, 1);
			if (Same(c, Shade)) return Color(0, 0, 0, 0.7f);
			if (Same(c, Text) || Same(c, Heading)) return Color(1, 1, 1, 1);
			if (Same(c, Dim)) return Color(0.88f, 0.9f, 0.92f, 1);
			return c;
		}

		Color Look(Color c) { return highContrast ? Contrast(c) : c; }

		/// Every texture built here, with the colours it was asked for, so a change of contrast repaints it in place.
		struct Painted
		{
			std::weak_ptr<Texture2D> texture;
			bool panel;
			Color fill, edge, tick;
		};
		std::vector<Painted> painted;

		/// Every style built here; `saved` holds its text colours from before high contrast was put on it.
		struct Styled
		{
			std::weak_ptr<GUIStyle> style;
			bool contrasted = false;
			std::array<Color, 8> saved{};
		};
		std::vector<Styled> styled;

		std::array<GUIStyleState*, 8> States(GUIStyle& s)
		{
			return { &s.normal, &s.hover, &s.active, &s.focused, &s.onNormal, &s.onHover, &s.onActive, &s.onFocused };
		}

		void Restyle(Styled& entry, GUIStyle& s, bool on)
		{
			if (on == entry.contrasted) return;
			auto states = States(s);
			for (size_t i = 0; i < states.size(); i++)
			{
				if (on)
				{
					entry.saved[i] = states[i]->textColor;
					states[i]->textColor = Contrast(states[i]->textColor);
				}
				else states[i]->textColor = entry.saved[i];
			}
			entry.contrasted = on;
		}

		void PaintFlat(Texture2D& t, Color fill, Color edge)
		{
			constexpr int n = 8;
			fill = Look(fill);
			edge = Look(edge);
			for (int y = 0; y < n; y++)
				for (int x = 0; x < n; x++)
					t.SetPixel(x, y, x == 0 || y == 0 || x == n - 1 || y == n - 1 ? (edge.a > 0 ? edge : fill) : fill);
			t.Apply();
		}

		void PaintPanel(Texture2D& t, Color fill, Color edge, Color tick)
		{
			// 16 x 16, a 9-slice of PanelBorder (6): the corner cells keep their pixels whatever the panel's size, so each
			// corner's tick (the outer edge lit for 5 px each way, the corner itself a 2 px block) stays as crisp on a small
			// readout as on a full menu. The edges between are the hairline; the middle is the fill.
			constexpr int n = 16, tickLen = 5;
			fill = Look(fill);
			edge = Look(edge);
			tick = Look(tick);
			bool hasEdge = edge.a > 0;
			for (int y = 0; y < n; y++)
				for (int x = 0; x < n; x++)
				{
					int dx = x < n / 2 ? x : n - 1 - x; // distance from the nearest vertical edge
					int dy = y < n / 2 ? y : n - 1 - y;
					bool onEdge = dx == 0 || dy == 0;
					bool inTick = (dx == 0 && dy < tickLen) || (dy == 0 && dx < tickLen) || (dx <= 1 && dy <= 1);
					Color c = fill;
					if (inTick && tick.a > 0) c = tick;
					else if (onEdge && hasEdge) c = edge;
					t.SetPixel(x, y, c);
				}
			t.Apply();
		}

		void Repaint(const Painted& p)
		{
			if (auto t = p.texture.lock())
			{
				if (p.panel) PaintPanel(*t, p.fill, p.edge, p.tick);
				else PaintFlat(*t, p.fill, p.edge);
			}
		}

		cs::Ref<Texture2D> NewTexture(int n, bool panel, Color fill, Color edge, Color tick)
		{
			auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
			t->hideFlags = HideFlags::DontSave;
			t->filterMode = FilterMode::Point;
			painted.erase(std::remove_if(painted.begin(), painted.end(), [](const Painted& p) { return p.texture.expired(); }), painted.end());
			painted.push_back(Painted{ t, panel, fill, edge, tick });
			Repaint(painted.back());
			return t;
		}

		cs::Ref<GUIStyle> Track(cs::Ref<GUIStyle> s)
		{
			styled.erase(std::remove_if(styled.begin(), styled.end(), [](const Styled& e) { return e.style.expired(); }), styled.end());
			styled.push_back(Styled{ s });
			Restyle(styled.back(), *s, highContrast);
			return s;
		}

		/// A copy of a style built here, tracked on its own: its text colours as they were before high contrast.
		cs::Ref<GUIStyle> Copy(const cs::Ref<GUIStyle>& from)
		{
			auto s = cs::New<GUIStyle>(*from);
			for (auto& e : styled)
				if (e.contrasted && e.style.lock() == from)
				{
					auto states = States(*s);
					for (size_t i = 0; i < states.size(); i++) states[i]->textColor = e.saved[i];
				}
			return s;
		}
	}

	void SetHighContrast(bool on)
	{
		if (on == highContrast) return;
		highContrast = on;
		for (auto& p : painted) Repaint(p);
		for (auto& e : styled)
			if (auto s = e.style.lock()) Restyle(e, *s, on);
	}

	bool HighContrast() { return highContrast; }

	cs::String Fold(bool open, const cs::String& titleText)
	{
		return cs::Cat(open ? Glyph::FoldOpen : Glyph::FoldShut, "  ", titleText);
	}

	cs::Ref<Texture2D> Flat(Color fill, Color edge)
	{
		return NewTexture(8, false, fill, edge, Color::clear());
	}

	cs::Ref<Texture2D> PanelFrame(Color fill, Color edge, Color tick)
	{
		return NewTexture(16, true, fill, edge, tick);
	}

	cs::Ref<GUIStyle> PanelStyle(const cs::Ref<Texture2D>& frame, int border)
	{
		auto s = cs::New<GUIStyle>();
		s->normal.background = frame;
		s->border = RectOffset(border, border, border, border);
		return Track(s);
	}

	cs::Ref<GUIStyle> TextStyle(int size, Color color, FontStyle fontStyle)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->fontStyle = fontStyle;
		s->normal.textColor = color;
		return Track(s);
	}

	cs::Ref<GUIStyle> CaptionStyle(int size)
	{
		auto s = TextStyle(size, Dim);
		s->wordWrap = true;
		return s;
	}

	cs::Ref<GUIStyle> ButtonStyle(int size, const cs::Ref<Texture2D>& normal, const cs::Ref<Texture2D>& on)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleCenter;
		s->border = RectOffset(2, 2, 2, 2);
		s->normal.background = normal;
		s->normal.textColor = Text;
		s->hover.background = on;
		s->hover.textColor = Heading;
		s->active.background = on;
		s->active.textColor = Accent;
		return Track(s);
	}

	cs::Ref<GUIStyle> ButtonOnStyle(const cs::Ref<GUIStyle>& button, const cs::Ref<Texture2D>& on)
	{
		auto s = Copy(button);
		s->normal.background = on;
		s->normal.textColor = Heading;
		s->fontStyle = FontStyle::Bold;
		return Track(s);
	}

	cs::Ref<GUIStyle> EntryStyle(int size, const cs::Ref<Texture2D>& normal, const cs::Ref<Texture2D>& on)
	{
		auto s = ButtonStyle(size, normal, on);
		s->alignment = TextAnchor::MiddleLeft;
		s->padding = RectOffset(Space::Indent, Space::TextPad, 0, 0);
		return s;
	}

	cs::Ref<GUIStyle> EntrySelectedStyle(const cs::Ref<GUIStyle>& entry, const cs::Ref<Texture2D>& on)
	{
		auto s = Copy(entry);
		s->normal.background = on;
		s->normal.textColor = Accent;
		s->hover.background = on;
		s->hover.textColor = Accent;
		return Track(s);
	}

	cs::Ref<GUIStyle> RowStyle(int size, const cs::Ref<Texture2D>& hover)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleLeft;
		s->padding = RectOffset(Space::TextPad, 4, 0, 0);
		s->clipping = TextClipping::Clip;
		s->normal.textColor = Text;
		s->hover.background = hover;
		s->hover.textColor = Heading;
		return Track(s);
	}

	cs::Ref<GUIStyle> RowSelectedStyle(const cs::Ref<GUIStyle>& row, const cs::Ref<Texture2D>& selected)
	{
		auto s = Copy(row);
		s->normal.background = selected;
		s->normal.textColor = Heading;
		s->fontStyle = FontStyle::Bold;
		return Track(s);
	}

	cs::Ref<GUIStyle> FieldStyle(int size, const cs::Ref<Texture2D>& well)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleLeft;
		s->padding = RectOffset(8, 8, 0, 0);
		s->border = RectOffset(2, 2, 2, 2);
		s->clipping = TextClipping::Clip;
		s->normal.background = well;
		s->normal.textColor = Heading;
		s->focused.background = well;
		s->focused.textColor = Heading;
		s->hover.background = well;
		s->hover.textColor = Heading;
		return Track(s);
	}
}
