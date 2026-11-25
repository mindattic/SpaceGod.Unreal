// Hud radar (US-T853): in a fight, a FreeSpace/Squadrons-style 3D radar in its own drawer up the right edge -- every hull and
// craft around the one the controls hold on a plan-view ellipse in that hull's own frame (nose up the disc, right to the
// right), each on a stalk to its height above or below, in the stance colours, the lock ringed; the nearest guided round
// homing on you and the seeker's and the hostiles' lock state; your own shields as four arcs round the disc (FORE, AFT,
// PORT, STBD by where each emitter sits, a core emitter counted in all four); and the reactor's pips and the boost pool.
// Declared in Hud.Drawer.inl.
//
// Read only: one ReadLock and one CraftPower::Read a frame, nothing written back, so the Bench is unchanged. The disc is
// scaled by the craft's own gun reach (ArrowReachKm), so it reads the same at the gameplay and the realistic scale; a
// contact beyond it sits on the rim, faded. Nothing is allocated in the units loop.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Gunnery.h"
#include "Embodiment/CraftPower.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float RadarW = 232, RadarPad = 10, RadarRowH = 18;
		constexpr float RadarInner = 96, RadarTilt = 0.45f, RadarStalk = 0.4f;
		constexpr float RadarDiscH = 2 * (RadarInner * (RadarTilt + RadarStalk)) + 12;
		constexpr float RadarPipRowH = 14;
		constexpr Color RadarShieldTint{0.45f, 0.7f, 1.0f, 1.0f};

		void RadarDot(Vector2 p, float size, Color c)
		{
			auto old = GUI::color;
			GUI::color = c;
			GUI::DrawTexture(Rect(p.x - size * 0.5f, p.y - size * 0.5f, size, size), Icons::White());
			GUI::color = old;
		}

		/// Dots along the ellipse from angle a0 to a1 (radians, 0 screen right, clockwise down); the first share of them lit.
		void RadarArc(Vector2 centre, float rx, float ry, float a0, float a1, int dots, float lit, Color on, Color off)
		{
			for (int i = 0; i < dots; i++)
			{
				float t = dots > 1 ? i / static_cast<float>(dots - 1) : 0.5f;
				float a = a0 + (a1 - a0) * t;
				RadarDot(Vector2(centre.x + Mathf::Cos(a) * rx, centre.y + Mathf::Sin(a) * ry), 2.5f, t <= lit ? on : off);
			}
		}

		/// The shield quadrant an emitter covers (hull frame: right, up, forward): 0 FORE, 1 STBD, 2 AFT, 3 PORT, -1 CORE.
		/// As the target card's sides, a dorsal or ventral emitter going to the nearer of the four.
		int RadarQuadrant(const SubsystemDef& d)
		{
			double ax = cs::Math::Abs(d.X), ay = cs::Math::Abs(d.Y), az = cs::Math::Abs(d.Z);
			if (ax < 1e-6 && ay < 1e-6 && az < 1e-6) return -1;
			if (az >= ax) return d.Z > 0 ? 0 : 2;
			return d.X > 0 ? 1 : 3;
		}

		Color RadarFaded(Color c, float alpha) { return Color(c.r, c.g, c.b, alpha); }
	}

	void Hud::DrawRadar()
	{
		if (app->Travel->Active || app->InFtlCruise()) return;
		cs::Ref<Battle> battle;
		auto me = HeldUnit(battle);
		if (me == nullptr || battle == nullptr || !me->Present()) return;

		auto power = CraftPower::Read(app->HeldSeat());
		float h = 6 + RadarRowH + RadarDiscH + RadarRowH * 2 + (power.Shares ? RadarPipRowH * 4 + 4 : 0) + 6;
		auto r = Place(RadarW, h);
		if (Event::current().type != EventType::Repaint) return;

		auto lock = battle->ReadLock(me);
		double reach = cs::Math::Max(ArrowReachKm(me), 1e-6);
		float now = Time::unscaledTime;
		float x = r.x + RadarPad, w = RadarW - 2 * RadarPad, y = r.y + 6;
		DrawLabel(FitText(cs::Cat("RADAR  \xC2\xB7  ", RangeText(reach)), minorLabel, w), Vector2(x, y), Accent, true);
		y += RadarRowH;

		// The hull's frame: nose up the disc, right to the right, its roof out of the screen.
		DVec3 fwd = me->Forward.Normalized(), right, up;
		me->Basis(right, up);
		float rx = RadarInner, ry = RadarInner * RadarTilt, stalk = RadarInner * RadarStalk;
		auto centre = Vector2(r.x + RadarW * 0.5f, y + RadarDiscH * 0.5f);
		// Base (on the plane) and tip (at its height) of a point rel km off, and whether it lies past the reach.
		auto project = [&](const DVec3& rel, Vector2& base, Vector2& tip)
		{
			double km = rel.Length();
			double s = km > reach ? reach / km : 1.0;
			double lr = DVec3::Dot(rel, right) * s / reach, lf = DVec3::Dot(rel, fwd) * s / reach, lu = DVec3::Dot(rel, up) * s / reach;
			base = Vector2(centre.x + static_cast<float>(lr) * rx, centre.y - static_cast<float>(lf) * ry);
			tip = Vector2(base.x, base.y - static_cast<float>(lu) * stalk);
			return km > reach;
		};

		// The disc: its rim, half reach, the nose and beam lines.
		RadarArc(centre, rx, ry, 0, 2 * Mathf::PI, 48, 1, RadarFaded(Dim, 0.5f), RadarFaded(Dim, 0.5f));
		RadarArc(centre, rx * 0.5f, ry * 0.5f, 0, 2 * Mathf::PI, 24, 1, RadarFaded(Dim, 0.3f), RadarFaded(Dim, 0.3f));
		DrawLine(Vector2(centre.x, centre.y - ry), Vector2(centre.x, centre.y + ry), RadarFaded(Dim, 0.2f), 1.0f);
		DrawLine(Vector2(centre.x - rx, centre.y), Vector2(centre.x + rx, centre.y), RadarFaded(Dim, 0.2f), 1.0f);

		// Own shields, quadrant by quadrant round the rim: lit to the mean share of their emitters, a lost quadrant red.
		if (me->Combat != nullptr)
		{
			double share[4] = {0, 0, 0, 0};
			int count[4] = {0, 0, 0, 0};
			for (const auto& s : me->Combat->Subsystems)
			{
				if (s == nullptr || s->Def == nullptr || s->Kind() != SubsystemKind::ShieldGenerator) continue;
				double f = s->Destroyed() ? 0 : s->ShieldFraction();
				int q = RadarQuadrant(*s->Def);
				for (int k = 0; k < 4; k++)
					if (q < 0 || q == k)
					{
						share[k] += f;
						count[k]++;
					}
			}
			const float centres[4] = {-Mathf::PI * 0.5f, 0, Mathf::PI * 0.5f, Mathf::PI}; // FORE up, STBD right, AFT down, PORT left
			const float half = Mathf::PI * 0.2f;
			for (int k = 0; k < 4; k++)
			{
				if (count[k] == 0) continue;
				float f = static_cast<float>(share[k] / count[k]);
				Color on = f <= 0 ? HostileColor : f < 0.35f ? Color(1.0f, 0.62f, 0.32f, 1.0f) : RadarShieldTint;
				RadarArc(centre, rx + 6, ry + 6, centres[k] - half, centres[k] + half, 12, f <= 0 ? 1.0f : f, on, RadarFaded(Dim, 0.25f));
			}
		}

		// Every hull and craft present: a stalk from the plane to its height, a dot sized by its kind, the lock ringed.
		for (const auto& u : battle->Units)
		{
			if (u == nullptr || u == me || !u->Present()) continue;
			Vector2 base, tip;
			bool past = project(u->Pos - me->Pos, base, tip);
			Color c = battle->Hostile(me, u) ? HostileColor : u->Side == me->Side ? ArrowFriendlyColor : ArrowNeutralColor;
			if (past || u->Captured || u->Surrendering) c = RadarFaded(c, 0.4f);
			DrawLine(base, tip, RadarFaded(c, c.a * 0.5f), 1.0f);
			RadarDot(base, 2, RadarFaded(c, c.a * 0.6f));
			RadarDot(tip, u->Kind == UnitKind::Fighter ? 4.0f : 6.0f, c);
			if (u == me->PlayerLockTarget) DrawRing(tip, 7, LockColor);
		}

		// The nearest guided round homing on you, flashing.
		bool flash = Mathf::Repeat(now * 6, 1.0f) < 0.6f;
		if (lock.Incoming > 0)
		{
			Vector2 base, tip;
			project(lock.NearestFrom * lock.NearestKm, base, tip);
			DrawLine(centre, tip, RadarFaded(HostileColor, 0.35f), 1.0f);
			if (flash) RadarDot(tip, 6, HostileColor);
		}

		// You: a bright dot with a tick along the nose.
		DrawLine(centre, centre + Vector2(0, -9), Accent, 1.5f);
		RadarDot(centre, 6, Accent);
		y += RadarDiscH;

		// The rounds coming in, else the hostiles locking you up.
		if (lock.Incoming > 0)
			DrawLabel(FitText(cs::Cat("MISSILE  ", RangeText(lock.NearestKm), "  \xC2\xB7  ", cs::Fmt(lock.NearestEtaS, "0.0"), " s",
				lock.Incoming > 1 ? cs::Cat("  \xC2\xB7  ", lock.Incoming, " inbound") : cs::String()), minorLabel, w), Vector2(x, y),
				flash ? HostileColor : RadarFaded(HostileColor, 0.6f), true);
		else if (lock.LockingOn > 0)
			DrawLabel(FitText(cs::Cat("LOCKING ON", lock.LockingOn > 1 ? cs::Cat("  \xC2\xB7  ", lock.LockingOn, " hostiles") : cs::String()), minorLabel, w),
				Vector2(x, y), flash ? Color(1.0f, 0.62f, 0.32f, 1.0f) : Dim, true);
		else
			DrawLabel("NO THREAT LOCK", Vector2(x, y), Dim, true);
		y += RadarRowH;

		// Your seeker: beeping as the tone does while it builds, solid once locked.
		if (lock.State == SeekerState::Locked)
			DrawLabel("SEEKER  \xC2\xB7  LOCKED", Vector2(x, y), LockColor, true);
		else if (lock.State == SeekerState::Seeking)
		{
			bool beep = lock.ToneIntervalS <= 0 || Mathf::Repeat(now, static_cast<float>(lock.ToneIntervalS)) < static_cast<float>(lock.ToneIntervalS) * 0.5f;
			DrawLabel(cs::Cat("SEEKER  \xC2\xB7  SEEKING ", Mathf::RoundToInt(static_cast<float>(Mathf::Clamp01(static_cast<float>(lock.Progress)) * 100)), "%"),
				Vector2(x, y), beep ? LockColor : Dim, true);
		}
		else
			DrawLabel("SEEKER  \xC2\xB7  no lock", Vector2(x, y), Dim, true);
		y += RadarRowH;

		// The reactor's pips and the boost pool.
		if (!power.Shares) return;
		y += 4;
		const char* names[] = { "W", "S", "E" };
		const int pips[] = { power.Weapons, power.Shields, power.Engines };
		const Color colors[] = { Color(1.0f, 0.62f, 0.32f), Color(0.45f, 0.7f, 1.0f), Color(0.55f, 1.0f, 0.62f) };
		constexpr float capW = 16, gap = 3;
		float segW = (w - capW - gap * (PowerDistribution::MaxPips - 1)) / PowerDistribution::MaxPips;
		auto old = GUI::color;
		for (int k = 0; k < 3; k++)
		{
			DrawLabel(names[k], Vector2(x, y - 2), colors[k], true);
			for (int i = 0; i < PowerDistribution::MaxPips; i++)
			{
				GUI::color = i < pips[k] ? colors[k] : Color(0, 0, 0, 0.45f);
				GUI::DrawTexture(Rect(x + capW + i * (segW + gap), y + 3, segW, RadarPipRowH - 6), Icons::White());
			}
			y += RadarPipRowH;
		}
		DrawLabel("B", Vector2(x, y - 2), Accent, true);
		GUI::color = Color(0, 0, 0, 0.45f);
		GUI::DrawTexture(Rect(x + capW, y + 3, w - capW, RadarPipRowH - 6), Icons::White());
		GUI::color = Accent;
		GUI::DrawTexture(Rect(x + capW, y + 3, (w - capW) * Mathf::Clamp01(static_cast<float>(power.Boost)), RadarPipRowH - 6), Icons::White());
		GUI::color = old;
	}
}
