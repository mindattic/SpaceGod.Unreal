// US-T156: the one source of every range the player reads (Ranges.h).

#include "Information/Ranges.h"

namespace SpaceGod::Core
{
	namespace
	{
		// v (at least 1) rounded to three significant figures, with the decimals that keeps.
		cs::String RangesThreeFigures(double v)
		{
			double step = cs::Math::Pow(10, cs::Math::Floor(cs::Math::Log10(v)) - 2);
			v = cs::Math::Round(v / step) * step;
			return cs::Fmt(v, v < 10 ? "0.00" : v < 100 ? "0.0" : "N0");
		}
	}

	cs::String Ranges::Readout(double km)
	{
		if (!(km >= 0.9995)) return cs::Cat(cs::Fmt(km * 1000, "N0"), " m");
		if (km < 999500) return cs::Cat(RangesThreeFigures(km), " km");
		if (km < 999.5e6) return cs::Cat(RangesThreeFigures(km / 1e6), " million km");
		return cs::Cat(RangesThreeFigures(km / 1e9), " billion km");
	}

	cs::String Ranges::Spoken(double km)
	{
		const double au = 1.495978707e8;
		if (km < 1) return cs::Cat(cs::Fmt(km * 1000, "N0"), " metres");
		if (km < 1000) return cs::Cat(cs::Fmt(km, km < 10 ? "0.#" : "0"), km < 1.5 ? " klick" : " klicks");
		if (km < 1e6) return cs::Cat(cs::Fmt(km / 1000, km < 1e4 ? "0.#" : "0"), " thousand klicks");
		if (km < 0.05 * au) return cs::Cat(cs::Fmt(km / 1e6, "0.#"), " million klicks");
		return cs::Cat(cs::Fmt(km / au, km < au ? "0.##" : "0.#"), " AU");
	}
}
