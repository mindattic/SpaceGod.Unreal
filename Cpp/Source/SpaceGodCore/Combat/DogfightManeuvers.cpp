#include "Combat/DogfightManeuvers.h"
#include "Combat/Battle.h"
#include "Combat/Choreography.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/CombatTuning.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/Gunnery.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/ShipCatalog.h"
#include "Combat/WeakPatches.h"
#include "Difficulty.h"
#include "Ships.h"
#include <cstring>

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(DogfightTactic v)
	{
		switch (v)
		{
			case DogfightTactic::LeadPursuit: return "LeadPursuit";
			case DogfightTactic::LagPursuit: return "LagPursuit";
			case DogfightTactic::FlipAndBurn: return "FlipAndBurn";
			case DogfightTactic::DriftTurn: return "DriftTurn";
			case DogfightTactic::BoomAndZoom: return "BoomAndZoom";
			case DogfightTactic::Jink: return "Jink";
			case DogfightTactic::Scissors: return "Scissors";
			case DogfightTactic::Break: return "Break";
			case DogfightTactic::Extend: return "Extend";
		}
		return "?";
	}

	namespace
	{
		constexpr double DfTwoPi = 6.283185307179586;

		double DfClamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

		DVec3 DfUnit(DVec3 v, DVec3 fallback) { return v.LengthSquared() > 1e-12 ? v.Normalized() : fallback; }

		DVec3 DfCapped(DVec3 v, double cap)
		{
			double l = v.Length();
			return l > cap && l > 1e-12 ? v * (cap / l) : v;
		}

		/// <summary>The fastest round the craft's forward guns fire, km/s (0: beams, or none), as Battle::RoundSpeedOf.</summary>
		double DfRoundSpeed(const cs::Ref<BattleUnit>& u)
		{
			double speed = 0;
			if (u->Combat == nullptr) return 0;
			for (const auto& s : u->Combat->Subsystems)
			{
				if (s == nullptr || s->Def == nullptr) continue;
				auto w = s->Def->Weapon;
				if (s->Destroyed() || w == nullptr || w->Missile || w->AntiSmall || w->Supporting()) continue;
				speed = Max(speed, CombatTuning::MuzzleSpeedKmS(w));
			}
			return speed;
		}

		/// <summary>Where <paramref name="u"/>'s guns must point to hit <paramref name="t"/>: the lead pip.</summary>
		DVec3 DfLeadPip(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t)
		{
			double rounds = DfRoundSpeed(u);
			return rounds > 0 ? Gunnery::LeadPoint(u->Pos, u->Vel, t->Pos, t->Vel, rounds) : t->Pos;
		}

		/// <summary>The way <paramref name="t"/> is going: its track, or its nose when it is all but still.</summary>
		DVec3 DfTrackOf(const cs::Ref<BattleUnit>& t) { return t->Vel.LengthSquared() > 0.05 * 0.05 ? t->Vel.Normalized() : t->Forward; }

		cs::Ref<BattleUnit> DfFind(const cs::Ref<Battle>& b, int id)
		{
			if (id == 0) return nullptr;
			for (const auto& o : b->Units)
				if (o != nullptr && o->Id == id) return o->Present() ? o : nullptr;
			return nullptr;
		}

		bool DfDefensive(DogfightTactic t)
		{
			return t == DogfightTactic::FlipAndBurn || t == DogfightTactic::Jink || t == DogfightTactic::Scissors || t == DogfightTactic::Break
				|| t == DogfightTactic::Extend;
		}

		/// <summary>US-T754: the pilot's skill, 0..1: a carrier craft's own, a fleet light fighter's middling.</summary>
		double DfSkillOf(const cs::Ref<BattleUnit>& u) { return u->Fighter != nullptr ? DfClamp(u->Fighter->Skill, 0.0, 1.0) : 0.5; }

		bool DfSmallCraft(const cs::Ref<BattleUnit>& o) { return o->Kind == UnitKind::Fighter || (o->Kind == UnitKind::Ship && o->LengthKm <= WeakPoints::MaxSmallCraftKm); }

		/// <summary>US-T204: a fleet's own light fighter (a fighter, interceptor or courier hull the battle dogfights,
		/// <see cref="Battle.Dogfights"/>), which has no <see cref="FighterState"/> and carries its dogfight on the unit.</summary>
		bool DfFleetCraft(const cs::Ref<BattleUnit>& u) { return u->Fighter == nullptr && Battle::Dogfights(u); }

		/// <summary>The craft's dogfight state: a carrier craft's on its <see cref="FighterState"/>, a fleet light fighter's
		/// on the unit (US-T204).</summary>
		DogfightManeuvers::State& DfStateOf(const cs::Ref<BattleUnit>& u) { return u->Fighter != nullptr ? u->Fighter->Tactics : u->Tactics; }

		/// <summary>The pilot's seed: a carrier craft's own; a fleet light fighter's from its unit's per-battle wander phase
		/// and id (US-T204), so every hull draws its own stream.</summary>
		double DfSeedOf(const cs::Ref<BattleUnit>& u) { return u->Fighter != nullptr ? u->Fighter->Seed : u->Wander * 7919.0 + u->Id; }
	}

	// ---------------------------------------------------------------- handling

	DogfightManeuvers::Handling DogfightManeuvers::Handling::Of(const cs::Ref<BattleUnit>& u)
	{
		Handling h;
		if (u == nullptr || u->Combat == nullptr || u->Combat->Spec == nullptr) return h;
		auto spec = u->Combat->Spec;
		double engines = PowerDistribution::FactorOf(u, PowerSystem::Engines);
		h.TopSpeed = spec->MaxSpeed * engines;
		h.Accel = spec->Accel * engines;
		// The design's own drive (US-T193): its retros and manoeuvre thrusters as shares of its main drive.
		auto design = CatalogDesign::Of(spec);
		double brakeShare = design.AccelKmS2 > 0 ? design.BrakeKmS2 / design.AccelKmS2 : 0.75;
		double strafeShare = design.AccelKmS2 > 0 ? design.StrafeKmS2 / design.AccelKmS2 : 0.5;
		h.Brake = h.Accel * DfClamp(brakeShare, 0.2, 1.5);
		h.Strafe = h.Accel * DfClamp(strafeShare, 0.1, 1.5);
		// Its class's catalog row: the helm's turn and roll, at a crew's share of them.
		h.TurnDegS = spec->TurnRateDegS;
		h.RollDegS = h.TurnDegS * PilotTuning::DefaultRollFactor;
		if (u->Fighter != nullptr)
		{
			if (auto craft = FighterCraft::Of(u->Fighter->Role))
			{
				auto tune = PilotTuning::Of(craft->Class);
				if (tune.TurnRateDegS > 0)
				{
					h.TurnDegS = tune.TurnRateDegS * Battle::DogfightTurnShare;
					h.RollDegS = tune.RollRateDegS() * Battle::DogfightTurnShare;
				}
			}
		}
		else if (u->Ship != nullptr && u->Ship->Spec != nullptr)
		{
			// US-T204: a fleet light fighter's helm is its hull class's catalog row, at a crew's share of it, as the old
			// crewed dogfight (Battle::FlyCrewed) flew it.
			auto tune = PilotTuning::Of(u->Ship->Spec->Class);
			if (tune.TurnRateDegS > 0)
			{
				h.TurnDegS = tune.TurnRateDegS * Battle::DogfightTurnShare;
				h.RollDegS = tune.RollRateDegS() * Battle::DogfightTurnShare;
			}
		}
		return h;
	}

	// ---------------------------------------------------------------- the pilot's eye

	double DogfightManeuvers::Draw(double seed, State& s)
	{
		// splitmix64 over the pilot's seed and its count of draws: its own stream, untouched by anyone else's.
		uint64_t bits = 0;
		std::memcpy(&bits, &seed, sizeof(bits));
		uint64_t x = bits ^ (static_cast<uint64_t>(++s.Draws) * 0x9E3779B97F4A7C15ull);
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		x = x ^ (x >> 31);
		return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
	}

	double DogfightManeuvers::ReactionSeconds(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& threat)
	{
		double s = u->Fighter != nullptr ? Max(0.0, u->Fighter->Reaction) * ReactionShare : FleetReactionS;
		return Difficulty::ReactionAt(threat != nullptr && threat->PlayerControlled() && !u->PlayerControlled(), s);
	}

	double DogfightManeuvers::AimErrorDeg(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t)
	{
		double deg = AimErrorBaseDeg * (1 - 0.9 * DfSkillOf(u));
		return Difficulty::AimErrorAt(t != nullptr && t->PlayerControlled() && !u->PlayerControlled(), deg);
	}

	cs::Ref<BattleUnit> DogfightManeuvers::OnTail(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> best;
		double bestD = TailKm;
		for (const auto& o : b->Units)
		{
			if (o == nullptr || o == u || !o->Present() || o->Surrendering || o->Captured || !DfSmallCraft(o) || !b->Hostile(u, o)) continue;
			auto rel = u->Pos - o->Pos;
			double d = rel.Length();
			if (d >= bestD || d < 1e-6) continue;
			auto toMe = rel / d;
			// Its nose on this craft, and it astern or off the quarter, not out in front where the guns already bear.
			if (DVec3::Dot(o->Forward, toMe) < 0.85 || DVec3::Dot(u->Forward, -toMe) > 0.2) continue;
			best = o;
			bestD = d;
		}
		return best;
	}

	DogfightTactic DogfightManeuvers::Choose(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, const Handling& h, double roll)
	{
		double skill = DfSkillOf(u);
		double speed = u->Vel.Length();
		if (threat != nullptr)
		{
			double d = (threat->Pos - u->Pos).Length();
			double relSpeed = (threat->Vel - u->Vel).Length();
			// Close behind and carrying way: flip to face it and burn against the way, so it overshoots into the guns.
			if (d < FlipKm && speed > 0.4 * h.TopSpeed && roll < 0.45 + 0.4 * skill) return DogfightTactic::FlipAndBurn;
			// Alongside at much its own speed: reverse into it, again and again, until it is out in front.
			if (d < 1.4 && relSpeed < 0.15) return DogfightTactic::Scissors;
			// US-T754: further back, fast, and it not gaining: run it out of its guns' reach and come back on our terms.
			double gaining = DVec3::Dot(threat->Vel - u->Vel, DfUnit(u->Pos - threat->Pos, u->Forward));
			if (d > ExtendKm && speed > 0.7 * h.TopSpeed && gaining < 0.05 && roll < 0.3 + 0.5 * skill) return DogfightTactic::Extend;
			// A sharp pilot breaks hard across the attacker's line; a green one jinks and hopes.
			if (roll < 0.2 + 0.6 * skill) return DogfightTactic::Break;
			return DogfightTactic::Jink;
		}
		auto rel = t->Pos - u->Pos;
		double d = rel.Length();
		auto toT = DfUnit(rel, u->Forward);
		double closing = -DVec3::Dot(t->Vel - u->Vel, toT);
		double noseCos = DVec3::Dot(u->Forward, toT);
		bool headOn = DVec3::Dot(t->Forward, -toT) > 0.7 && noseCos > 0.5;
		if (d > BoomKm && roll < 0.35 + 0.4 * skill) return DogfightTactic::BoomAndZoom;
		if (headOn && d > 1.0) return DogfightTactic::BoomAndZoom; // a slashing head-on pass, not a joust
		if (d < 1.6 && closing > OvershootKmS) return DogfightTactic::LagPursuit;
		if (d < 2.0 && noseCos < 0.4) return DogfightTactic::DriftTurn;
		return DogfightTactic::LeadPursuit;
	}

	void DogfightManeuvers::Start(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, DogfightTactic tactic, const Handling& h, State& s, double seed)
	{
		double r = Draw(seed, s);
		s.Tactic = tactic;
		s.Time = 0;
		s.StageTime = 0;
		s.Stage = 0;
		s.KickIn = 0;
		s.FoeId = t->Id;
		s.ThreatId = threat != nullptr ? threat->Id : 0;
		DVec3 right, up;
		u->Basis(right, up);
		auto mark = threat != nullptr ? threat : t;
		int markSide = DVec3::Dot(mark->Pos - u->Pos, right) >= 0 ? 1 : -1;
		double flipS = 180.0 / Max(h.TurnDegS, 1.0);
		switch (tactic)
		{
			case DogfightTactic::LeadPursuit: s.Left = 2.5 + r * 2.5; s.Side = 1; break;
			case DogfightTactic::LagPursuit: s.Left = 1.5 + r * 1.5; s.Side = 1; break;
			case DogfightTactic::FlipAndBurn: s.Left = flipS + 2.0 + r * 1.5; s.Side = r < 0.5 ? -1 : 1; break;
			case DogfightTactic::DriftTurn: s.Left = 1.8 + r * 1.6; s.Side = markSide; break;
			case DogfightTactic::BoomAndZoom: s.Left = 16; s.Side = r < 0.5 ? -1 : 1; break;
			case DogfightTactic::Jink: s.Left = 2.0 + r * 2.0; s.Side = r < 0.5 ? -1 : 1; break;
			case DogfightTactic::Scissors: s.Left = 4.0 + r * 3.0; s.Side = markSide; s.KickIn = 1.2 + r * 0.6; break;
			case DogfightTactic::Break: s.Left = 0.5 * flipS + 1.5 + r * 1.0; s.Side = markSide; break;
			case DogfightTactic::Extend: s.Left = 14; s.Side = r < 0.5 ? -1 : 1; break;
		}
	}

	// ---------------------------------------------------------------- flying the manoeuvres

	DogfightManeuvers::Command DogfightManeuvers::Plan(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, const Handling& h, State& s, double dt)
	{
		Command c;
		double seed = DfSeedOf(u);
		auto rel = t->Pos - u->Pos;
		double d = rel.Length();
		auto toT = DfUnit(rel, u->Forward);
		auto lead = DfLeadPip(u, t);
		auto track = DfUnit(u->Vel, u->Forward);
		double speed = u->Vel.Length();
		double top = h.TopSpeed;
		c.Face = lead - u->Pos;
		c.WantVel = toT * top;
		switch (s.Tactic)
		{
			case DogfightTactic::LeadPursuit:
			{
				// Guns on the pip; the track closes on a point just behind its tail, its own velocity fed forward.
				auto behind = t->Pos - DfTrackOf(t) * 0.45;
				c.WantVel = DfCapped(t->Vel + (behind - u->Pos) * 0.9, top);
				c.Boost = d > 1.8;
				break;
			}
			case DogfightTactic::LagPursuit:
			{
				// The track aimed well behind it and no faster than it plus a little: it bleeds the overshoot off.
				auto behind = t->Pos - DfTrackOf(t) * 0.9;
				c.WantVel = DfCapped(t->Vel + (behind - u->Pos) * 0.6, t->Vel.Length() + 0.08);
				if (s.Time > 0.6 && -DVec3::Dot(t->Vel - u->Vel, toT) < 0.05) s.Left = 0;
				break;
			}
			case DogfightTactic::FlipAndBurn:
			{
				auto mark = DfFind(b, s.ThreatId);
				if (mark == nullptr) mark = t;
				auto toMark = DfUnit(mark->Pos + mark->Vel * 0.3 - u->Pos, -track);
				c.Face = s.Stage == 0 ? toMark : DfLeadPip(u, mark) - u->Pos;
				if (s.Stage == 0)
				{
					// The flip: drive cold, momentum carrying it on, end over end onto the pursuer, rolling through it.
					c.WantVel = u->Vel;
					c.Roll = s.Side;
					if (DVec3::Dot(u->Forward, toMark) > 0.9 || s.StageTime > 180.0 / Max(h.TurnDegS, 1.0) + 0.6)
					{
						s.Stage = 1;
						s.StageTime = 0;
					}
				}
				else
				{
					// The burn: main drive lit along the nose, against the way it had, guns on the pursuer as it overshoots.
					c.WantVel = toMark * (0.4 * top);
					c.Boost = true;
				}
				break;
			}
			case DogfightTactic::DriftTurn:
			{
				// Momentum kept, the slide swung round by the manoeuvre thrusters; the nose off the track, on the pip.
				double keep = Max(speed, 0.55 * top);
				auto aim = DfUnit(t->Pos + t->Vel * 1.0 - u->Pos, toT);
				c.WantVel = DfUnit(track * 0.55 + aim * 0.45, aim) * keep;
				c.Roll = 0.5 * s.Side;
				if (s.Time > 0.8 && DVec3::Dot(u->Forward, toT) > 0.95) s.Left = 0;
				break;
			}
			case DogfightTactic::BoomAndZoom:
			{
				auto level = WeakPatches::LevelRoof(toT);
				auto beside = DfUnit(DVec3::Cross(toT, level), DVec3::UnitX) * (0.22 * s.Side);
				if (s.Stage == 0)
				{
					// The boom: boosting in on a line just past its flank, guns on it through the merge.
					auto pass = t->Pos + t->Vel * Min(d / Max(top * BoostFactor, 1e-6), 3.0) + beside;
					if (d > 1.8) c.Face = pass - u->Pos;
					c.WantVel = DfUnit(pass - u->Pos, toT) * (top * BoostFactor);
					c.Boost = true;
					if (d < 0.35 || (s.StageTime > 1.0 && DVec3::Dot(rel, u->Vel) < 0) || s.StageTime > 10)
					{
						s.Stage = 1;
						s.StageTime = 0;
					}
				}
				else if (s.Stage == 1)
				{
					// The zoom: out the far side at speed along its own track, pulling a little high.
					c.Face = track + level * 0.15;
					c.WantVel = track * (top * BoostFactor);
					c.Boost = true;
					if (d > ZoomKm || s.StageTime > 5)
					{
						s.Stage = 2;
						s.StageTime = 0;
					}
				}
				else
				{
					// Back round: flipped onto it and burning, for the next pass or a chase.
					c.Face = toT;
					c.WantVel = toT * top;
					c.Boost = DVec3::Dot(u->Forward, toT) > 0.8;
					c.Roll = s.Side;
					if ((DVec3::Dot(u->Forward, toT) > 0.9 && DVec3::Dot(u->Vel, toT) > 0.3 * top) || s.StageTime > 6) s.Left = 0;
				}
				break;
			}
			case DogfightTactic::Jink:
			{
				// A random walk of hard kicks across its track, each a fresh draw, so the gunner astern never settles.
				s.KickIn -= dt;
				if (s.KickIn <= 0 || s.Kick.LengthSquared() < 1e-12)
				{
					auto level = WeakPatches::LevelRoof(track);
					auto side = DfUnit(DVec3::Cross(track, level), DVec3::UnitX);
					double a = Draw(seed, s) * DfTwoPi;
					s.Kick = side * cs::Math::Cos(a) + level * cs::Math::Sin(a);
					s.KickIn = 0.35 + Draw(seed, s) * 0.5;
					s.Side = DVec3::Dot(s.Kick, side) >= 0 ? 1 : -1;
				}
				auto want = track * top + s.Kick * (0.6 * top);
				if (threat != nullptr)
				{
					// Out of the line of its nose, whichever way is shorter.
					auto off = (u->Pos - threat->Pos) - threat->Forward * DVec3::Dot(u->Pos - threat->Pos, threat->Forward);
					want = want + DfUnit(off, s.Kick) * (0.3 * top);
				}
				c.WantVel = want;
				c.Face = DVec3::Dot(u->Forward, toT) > 0.3 ? lead - u->Pos : DfUnit(want, track) + toT * 0.3;
				c.Boost = true;
				c.Roll = s.Side;
				break;
			}
			case DogfightTactic::Scissors:
			{
				auto mark = threat != nullptr ? threat : DfFind(b, s.ThreatId);
				if (mark == nullptr) mark = t;
				s.KickIn -= dt;
				if (s.KickIn <= 0)
				{
					s.Side = -s.Side;
					s.KickIn = 1.2 + Draw(seed, s) * 0.6;
				}
				auto level = WeakPatches::LevelRoof(track);
				auto side = DfUnit(DVec3::Cross(track, level), DVec3::UnitX) * static_cast<double>(s.Side);
				auto toMark = DfUnit(mark->Pos - u->Pos, -track);
				// Reversing into it, slow: each weave bleeds speed so the attacker slides out ahead.
				c.Face = toMark * 0.6 + side * 0.8;
				c.WantVel = DfUnit(track * 0.7 + side * 0.7, track) * (0.45 * top);
				c.Roll = s.Side;
				if (threat == nullptr && DVec3::Dot(u->Forward, toMark) > 0.6) s.Left = 0;
				break;
			}
			case DogfightTactic::Break:
			{
				// US-T754: the nose hauled round at full rate across the attacker's line, toward its side so it overshoots
				// or must pull a harder turn than it can, the thrusters sliding the hull out of the stream.
				auto mark = threat != nullptr ? threat : DfFind(b, s.ThreatId);
				if (mark == nullptr) mark = t;
				auto toMark = DfUnit(mark->Pos - u->Pos, -track);
				auto across = DfUnit(toMark - mark->Forward * DVec3::Dot(toMark, mark->Forward), DVec3::Cross(mark->Forward, WeakPatches::LevelRoof(mark->Forward)));
				c.Face = across * 0.85 + toMark * 0.35;
				c.WantVel = DfUnit(track * 0.4 + across * 0.9, across) * top;
				c.Boost = true;
				c.Roll = s.Side;
				// Out of its guns' line: done, the fight picked afresh.
				if (s.Time > 0.8 && DVec3::Dot(mark->Forward, DfUnit(u->Pos - mark->Pos, mark->Forward)) < 0.8) s.Left = 0;
				break;
			}
			case DogfightTactic::Extend:
			{
				// US-T754: straight out under boost, a little off the attacker's line, then round again for the fight.
				auto mark = threat != nullptr ? threat : DfFind(b, s.ThreatId);
				if (mark == nullptr) mark = t;
				auto away = DfUnit(u->Pos - mark->Pos, track);
				double gap = (mark->Pos - u->Pos).Length();
				if (s.Stage == 0)
				{
					auto level = WeakPatches::LevelRoof(away);
					auto beside = DfUnit(DVec3::Cross(away, level), DVec3::UnitX) * (0.25 * s.Side);
					auto run = DfUnit(track * 0.6 + away * 0.4 + beside, away);
					c.Face = run;
					c.WantVel = run * (top * BoostFactor);
					c.Boost = true;
					if (gap > ZoomKm || s.StageTime > 7)
					{
						s.Stage = 1;
						s.StageTime = 0;
					}
				}
				else
				{
					c.Face = toT;
					c.WantVel = toT * top;
					c.Boost = DVec3::Dot(u->Forward, toT) > 0.8;
					c.Roll = s.Side;
					if (DVec3::Dot(u->Forward, toT) > 0.9 || s.StageTime > 6) s.Left = 0;
				}
				break;
			}
		}
		return c;
	}

	void DogfightManeuvers::Thrust(const cs::Ref<BattleUnit>& u, const Command& cmd, const Handling& h, double dt)
	{
		double boost = cmd.Boost ? BoostFactor : 1;
		auto face = DfUnit(cmd.Face, u->Forward);
		Battle::TurnToward(u, face, h.TurnDegS, dt);
		auto want = DfCapped(cmd.WantVel, h.TopSpeed * boost);
		// Never out of the fight altogether: past most of the escape radius it burns back in.
		if (u->Pos.Length() > Battle::EscapeKm() * 0.8) want = DfUnit(-u->Pos, u->Forward) * h.TopSpeed;
		auto f = u->Forward;
		auto a = (want - u->Vel) / RespondS;
		// Newtonian: the push along the nose is the main drive's or the retros', the push across it the thrusters'.
		double along = DfClamp(DVec3::Dot(a, f), -h.Brake, h.Accel * boost);
		auto across = DfCapped(a - f * DVec3::Dot(a, f), h.Strafe);
		u->Vel = u->Vel + (f * along + across) * dt;
		u->Pos = u->Pos + u->Vel * dt;
		Choreography::Bank(u, cmd.Roll * h.RollDegS, dt);
	}

	bool DogfightManeuvers::Fly(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double dt)
	{
		if (b == nullptr || u == nullptr || (u->Fighter == nullptr && !DfFleetCraft(u)) || t == nullptr || !t->Present()) return false;
		auto& s = DfStateOf(u);
		double seed = DfSeedOf(u);
		auto h = Handling::Of(u);
		auto threat = OnTail(b, u);
		// US-T754: a new attacker on its tail is acted on only once the pilot has taken it in (one it is already
		// fighting off is not new).
		if (threat == nullptr) s.SeenId = 0;
		else if (threat->Id != s.ThreatId || !DfDefensive(s.Tactic))
		{
			if (threat->Id != s.SeenId)
			{
				s.SeenId = threat->Id;
				s.Seen = 0;
			}
			s.Seen += dt;
			if (s.Seen < ReactionSeconds(u, threat)) threat = nullptr;
		}
		s.Left -= dt;
		s.Time += dt;
		s.StageTime += dt;
		bool rechoose = s.Left <= 0 || s.FoeId != t->Id;
		// Something new on its tail cuts an attack short (not a zoom already carrying it clear).
		if (threat != nullptr && !DfDefensive(s.Tactic) && !(s.Tactic == DogfightTactic::BoomAndZoom && s.Stage == 1)) rechoose = true;
		// Shaken off: back on the attack.
		if (threat == nullptr && (s.Tactic == DogfightTactic::Jink || s.Tactic == DogfightTactic::Scissors || s.Tactic == DogfightTactic::Break) && s.Time > 0.8) rechoose = true;
		if (rechoose) Start(u, t, threat, Choose(u, t, threat, h, Draw(seed, s)), h, s, seed);
		auto cmd = Plan(b, u, t, threat, h, s, dt);
		// US-T754: the pilot's hand, not a turret's: the nose hunts round the point it wants in a slow smooth wander,
		// wider the greener the pilot, so it misses believably and never snaps onto the pip.
		double err = AimErrorDeg(u, t) * (cs::Math::PI / 180.0);
		if (err > 0)
		{
			s.AimPhase += dt * AimWanderHz * DfTwoPi;
			double p = s.AimPhase + seed * 0.001;
			auto face = DfUnit(cmd.Face, u->Forward);
			auto level = WeakPatches::LevelRoof(face);
			auto side = DfUnit(DVec3::Cross(face, level), DVec3::UnitX);
			double x = cs::Math::Sin(p) * cs::Math::Cos(0.37 * p);
			double y = cs::Math::Sin(0.61 * p + 1.3);
			cmd.Face = face + (side * x + level * y) * cs::Math::Tan(err);
		}
		Thrust(u, cmd, h, dt);
		return true;
	}

	cs::Nullable<DogfightTactic> DogfightManeuvers::TacticOf(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return std::nullopt;
		// US-T204: a fleet light fighter is dogfighting while it has a foe (Battle::Dogfight clears it otherwise).
		if (u->Fighter == nullptr) return DfFleetCraft(u) && u->Tactics.FoeId != 0 ? cs::Nullable<DogfightTactic>(u->Tactics.Tactic) : std::nullopt;
		if (u->Fighter->Phase != FighterPhase::Dogfight) return std::nullopt;
		return u->Fighter->Tactics.Tactic;
	}
}
