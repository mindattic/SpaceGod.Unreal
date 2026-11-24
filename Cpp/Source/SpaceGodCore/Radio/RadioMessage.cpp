#include "Radio/RadioMessage.h"

#include <array>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr int RadioClassCount = static_cast<int>(RadioPower::Count);

		// Watts. With the 1e-13 W/km^2 floor: suit ~2 million km, ship ~0.5 AU, station ~3 AU (the inner system),
		// fleet command ~6 AU, guard beacon ~19 AU.
		constexpr std::array<double, RadioClassCount> RadioEirp = { 5.0, 7000.0, 250000.0, 1.0e6, 1.0e7 };

		int RadioClassIndex(RadioPower power)
		{
			int i = static_cast<int>(power);
			return i < 0 || i >= RadioClassCount ? static_cast<int>(RadioPower::Ship) : i;
		}

		const std::array<double, RadioClassCount>& RadioRanges()
		{
			static const std::array<double, RadioClassCount> ranges = []
			{
				std::array<double, RadioClassCount> r{};
				for (int i = 0; i < RadioClassCount; i++)
					r[i] = cs::Math::Sqrt(RadioEirp[i] / (4.0 * cs::Math::PI * RadioPowerTable::NoiseFloorWPerKm2));
				return r;
			}();
			return ranges;
		}
	}

	double RadioPowerTable::EirpW(RadioPower power) { return RadioEirp[RadioClassIndex(power)]; }

	double RadioPowerTable::MaxRangeKm(RadioPower power) { return RadioRanges()[RadioClassIndex(power)]; }

	double RadioPowerTable::LongestRangeKm()
	{
		double longest = 0;
		for (double r : RadioRanges())
			longest = cs::Math::Max(longest, r);
		return longest;
	}
}
