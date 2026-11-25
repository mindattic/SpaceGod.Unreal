// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Gunnery.cs (see docs/MIGRATION.md).

#include "Combat/Gunnery.h"

#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/PilotFlight.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& Gunnery::HeatPerDamage()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("guns", "heatPerDamage", "Heat per damage", 0.01, 0, 0.05, 0.0005, "",
			TuningScope::Simulation, "The share of full heat a player's guns build per point of damage they fire.");
		return s;
	}

	const cs::Ref<RangeSetting>& Gunnery::HeatCoolPerS()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("guns", "heatCoolPerS", "Heat shed a second", 0.26, 0.01, 2, 0.01, "",
			TuningScope::Simulation, "The share of full heat a player's guns shed each second at the even power share.");
		return s;
	}

	const cs::Ref<RangeSetting>& Gunnery::HeatRecoverBelow()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("guns", "heatRecoverBelow", "Overheat recovers below", 0.35, 0, 0.95, 0.05, "",
			TuningScope::Simulation, "How far overheated guns must cool before they fire again, a share of full heat.");
		return s;
	}

	const cs::Ref<RangeSetting>& Gunnery::HeadOnLock()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("missiles", "headOnLock", "Head-on lock time", 1.8, 1, 4, 0.1, "x",
			TuningScope::Simulation, "How many times the base lock time a missile lock takes on a target met head-on rather than from astern.");
		return s;
	}

	double Gunnery::Aspect(DVec3 seekerPos, DVec3 targetPos, DVec3 targetForward)
	{
		auto from = seekerPos - targetPos;
		if (from.LengthSquared() < 1e-12 || targetForward.LengthSquared() < 1e-12) return 0.5;
		return cs::Math::Max(0.0, cs::Math::Min(1.0, (1 + DVec3::Dot(from.Normalized(), targetForward.Normalized())) / 2));
	}

	double Gunnery::LockSecondsAt(double aspect)
	{
		return LockSeconds * (1 + (cs::Math::Max(1.0, HeadOnLock()->Value) - 1) * cs::Math::Max(0.0, cs::Math::Min(1.0, aspect)));
	}

	double Gunnery::ToneIntervalS(double progress)
	{
		if (progress >= 1) return 0;
		double p = cs::Math::Max(0.0, progress);
		return ToneSlowS + (ToneFastS - ToneSlowS) * p;
	}

	double Gunnery::HitRadius(const cs::Ref<BattleUnit>& u) { return cs::Math::Max(MinHitRadiusKm, u->Radius()); }

	double Gunnery::RaySphere(DVec3 origin, DVec3 dir, double maxT, DVec3 centre, double radius)
	{
		auto oc = origin - centre;
		double b = DVec3::Dot(oc, dir);
		double c = oc.LengthSquared() - radius * radius;
		if (c <= 0) return 0;
		if (b > 0) return -1;
		double disc = b * b - c;
		if (disc < 0) return -1;
		double t = -b - cs::Math::Sqrt(disc);
		return t <= maxT ? t : -1;
	}

	double Gunnery::InterceptTime(DVec3 relPos, DVec3 relVel, double speed)
	{
		// |relPos + relVel t| = speed t  =>  (v·v - s²) t² + 2 (p·v) t + p·p = 0
		double a = relVel.LengthSquared() - speed * speed;
		double b = 2 * DVec3::Dot(relPos, relVel);
		double c = relPos.LengthSquared();
		if (cs::Math::Abs(a) < 1e-12) return b < 0 ? -c / b : cs::Double::NaN;
		double disc = b * b - 4 * a * c;
		if (disc < 0) return cs::Double::NaN;
		double sq = cs::Math::Sqrt(disc);
		double t1 = (-b - sq) / (2 * a), t2 = (-b + sq) / (2 * a);
		double t = cs::Math::Min(t1, t2);
		if (t <= 0) t = cs::Math::Max(t1, t2);
		return t > 0 ? t : cs::Double::NaN;
	}

	DVec3 Gunnery::LeadPoint(DVec3 shooterPos, DVec3 shooterVel, DVec3 targetPos, DVec3 targetVel, double speed)
	{
		auto relVel = targetVel - shooterVel;
		double t = InterceptTime(targetPos - shooterPos, relVel, speed);
		return cs::Double::IsNaN(t) ? targetPos : targetPos + relVel * t;
	}

	DVec3 Gunnery::Gimbal(DVec3 aim, DVec3 forward, double gimbalDeg)
	{
		return PilotFlight::ClampAim(aim, forward, gimbalDeg * Astro::Deg2Rad);
	}

	DVec3 Gunnery::Scatter(DVec3 dir, double sigmaRad, cs::Random& rng)
	{
		if (sigmaRad <= 0) return dir;
		auto across = PilotState::LevelUp(dir);
		across = PilotFlight::RotateAbout(across, dir, rng.NextDouble() * 2 * cs::Math::PI);
		// A normal-ish spread (sum of uniforms), so most shots land near the line and a few stray far.
		// (Three NextDouble calls, left to right, as C# evaluates them.)
		double r1 = rng.NextDouble();
		double r2 = rng.NextDouble();
		double r3 = rng.NextDouble();
		double angle = (r1 + r2 + r3 - 1.5) * 2 * sigmaRad;
		return PilotFlight::RotateAbout(dir, across, angle).Normalized();
	}
}
