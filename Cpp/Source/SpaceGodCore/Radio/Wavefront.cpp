#include "Radio/Wavefront.h"

#include "Astro.h"
#include "Radio/RadioLog.h"

namespace SpaceGod::Core
{
	double Wavefront::FrontRadiusKm(double sentJd, double atJd)
	{
		return cs::Math::Max(0.0, atJd - sentJd) * Astro::SecondsPerDay * LightSpeed::SpeedKmS;
	}

	void Wavefront::DistanceRangeKm(const DVec3& point, const DVec3& minKm, const DVec3& maxKm, double& dMinKm, double& dMaxKm)
	{
		auto nearest = [](double p, double lo, double hi) { return p < lo ? lo - p : p > hi ? p - hi : 0.0; };
		auto farthest = [](double p, double lo, double hi) { return cs::Math::Max(cs::Math::Abs(p - lo), cs::Math::Abs(p - hi)); };
		DVec3 n(nearest(point.X, minKm.X, maxKm.X), nearest(point.Y, minKm.Y, maxKm.Y), nearest(point.Z, minKm.Z, maxKm.Z));
		DVec3 f(farthest(point.X, minKm.X, maxKm.X), farthest(point.Y, minKm.Y, maxKm.Y), farthest(point.Z, minKm.Z, maxKm.Z));
		dMinKm = n.Length();
		dMaxKm = f.Length();
	}

	bool Wavefront::BucketMayArrive(const RadioBucket& bucket, const DVec3& listenerKm, double fromJd, double toJd)
	{
		if (bucket.Count() == 0 || toJd < bucket.FirstSentJd)
			return false;
		double dMin, dMax;
		DistanceRangeKm(listenerKm, bucket.MinKm, bucket.MaxKm, dMin, dMax);
		if (dMin > bucket.MaxRangeKm)
			return false;
		double innerKm = FrontRadiusKm(bucket.LastSentJd, fromJd);
		double outerKm = FrontRadiusKm(bucket.FirstSentJd, toJd);
		return outerKm >= dMin && innerKm <= dMax;
	}
}
