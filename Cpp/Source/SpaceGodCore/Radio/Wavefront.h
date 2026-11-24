#pragma once
// US-T623: when a radio front reaches a listener (US-T575). Arrival is analytic, never stepped: t = sentJd + |L(t) - O| / c,
// solved by fixed point. A listener is far slower than light, so two iterations put the error below a millisecond.
// BucketMayArrive is the shell test that lets a receiver skip a whole time bucket of the RadioLog without reading it.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Information/LightSpeed.h"

namespace SpaceGod::Core
{
	struct RadioBucket;

	class SPACEGODCORE_API Wavefront
	{
	public:
		static constexpr int Iterations = 2;

		/// <summary>
		/// The Julian date a front sent from <paramref name="origin"/> (heliocentric km) at <paramref name="sentJd"/>
		/// reaches a listener whose position is <paramref name="listenerAt"/>(jd) (heliocentric km). The first guess
		/// uses the listener's position at <paramref name="guessJd"/> (pass the current date when there is one).
		/// </summary>
		template <typename ListenerAt>
		static double ArrivalJd(const DVec3& origin, double sentJd, const ListenerAt& listenerAt, double guessJd)
		{
			double t = sentJd + LightSpeed::DelayDays(listenerAt(guessJd), origin);
			for (int i = 0; i < Iterations; i++)
				t = sentJd + LightSpeed::DelayDays(listenerAt(t), origin);
			return t;
		}

		/// <summary>ArrivalJd with the first guess taken from where the listener was when the message was sent.</summary>
		template <typename ListenerAt>
		static double ArrivalJd(const DVec3& origin, double sentJd, const ListenerAt& listenerAt)
		{
			return ArrivalJd(origin, sentJd, listenerAt, sentJd);
		}

		/// <summary>How far a front sent at <paramref name="sentJd"/> has travelled by <paramref name="atJd"/>, km (0 before it is sent).</summary>
		static double FrontRadiusKm(double sentJd, double atJd);

		/// <summary>The nearest and farthest a point is from an axis-aligned box, km.</summary>
		static void DistanceRangeKm(const DVec3& point, const DVec3& minKm, const DVec3& maxKm, double& dMinKm, double& dMaxKm);

		/// <summary>
		/// Whether any front in <paramref name="bucket"/> may reach <paramref name="listenerKm"/> during
		/// [<paramref name="fromJd"/>, <paramref name="toJd"/>]: the shell its fronts sweep, from c(fromJd - LastSent) to
		/// c(toJd - FirstSent), must meet the listener's distance interval [dmin, dmax] to the bucket's origin box, and
		/// dmin must lie within the bucket's longest reach. Conservative: true may still hold no arrival, false never misses one.
		/// </summary>
		static bool BucketMayArrive(const RadioBucket& bucket, const DVec3& listenerKm, double fromJd, double toJd);
	};
}
