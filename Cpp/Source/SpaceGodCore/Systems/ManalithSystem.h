#pragma once
// US-T458: Nightglass, the system of the Manalith.

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"
#include "Systems/SystemSources.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	class SystemSpec;
	class StarSystem;
	class Polities;

	/// <summary>
	/// Nightglass (US-T458): a lone yellow star 6.4 light-years from Sol in an empty reach of sky, with an Earthlike world,
	/// Wendmere, its Moonlike moon, Pale Wend, and above Wendmere the Manalith: a crystal-veined relic the size of a
	/// mountain, tumbling slowly on its orbit and shedding shards (GridGame's Manalith, the same model, drawn to
	/// docs/MANALITH_VISUAL_SPEC.md). Fiction, like everything outside Sol.
	///
	/// Observation only (<see cref="ObservationOnly"/>): nothing there can be targeted, focused, landed on, docked at,
	/// mined, scanned or fought over, and nothing is furnished, settled or sent there. It exists to be reached and looked at.
	/// No gate leads there and none is ever built (<see cref="Gateless"/>): the only way in is a ship's own FTL. It is on
	/// the charts from the start (a known star within <see cref="StarDiscovery.KnownToLy"/>), so it is in every FTL target
	/// list and on the galaxy map without being discovered first. Placed where nothing else is: the nearest star to it is
	/// Lalande 21185, 8.2 ly away, so Sol (6.4 ly) is the only system one 7-ly jump reaches it from.
	/// </summary>
	class SPACEGODCORE_API ManalithSystem final : public SystemSource
	{
	public:
		static inline const cs::String SourceName = "Manalith";
		static inline const cs::String SystemName = "Nightglass";
		static inline const cs::String PlanetName = "Wendmere";
		static inline const cs::String MoonName = "Pale Wend";
		static inline const cs::String SatelliteName = "Manalith";

		/// <summary>Where it is on the sky from Sol (ICRS J2000) and how far: an empty direction (RA 5h00m, Dec +45 deg).</summary>
		static constexpr double RaDeg = 75.0;
		static constexpr double DecDeg = 45.0;
		static constexpr double DistanceLy = 6.4;

		/// <summary>The space it keeps clear of other sources' stars.</summary>
		static constexpr double ClearLy = 1;

		/// <summary>The Manalith's radius (km): a crystal mass 3 km across, its longest axis (the spec's D).</summary>
		static constexpr double SatelliteRadiusKm = 1.5;

		/// <summary>Its orbit about Wendmere: radius (km) and tilt to the ecliptic.</summary>
		static constexpr double SatelliteOrbitKm = 21000;
		static constexpr double SatelliteInclinationDeg = 28;

		cs::String Name() const override { return SourceName; }
		/// <summary>Right after the hand-built systems, ahead of the void stations and the catalogue.</summary>
		int Order() const override { return 1; }
		bool ChartedFromStart() const override { return true; }
		bool Claims(DVec3 ly) const override { return (ly - PositionLy()).Length() <= ClearLy; }

		static DVec3 PositionLy() { return Astro::DirectionFromRaDec(RaDeg, DecDeg) * DistanceLy; }

		/// <summary>Whether no gate may ever lead to the system called <paramref name="name"/>.</summary>
		static bool Gateless(const cs::String& name) { return cs::Strings::EqualsOrdinalIgnoreCase(name, SystemName); }

		/// <summary>Whether the system called <paramref name="name"/> is for looking only: no interaction, nothing spawned there.</summary>
		static bool ObservationOnly(const cs::String& name) { return cs::Strings::EqualsOrdinalIgnoreCase(name, SystemName); }

		/// <summary>Whether <paramref name="body"/> is the Manalith (drawn from its imported model, not as a station).</summary>
		static bool IsSatellite(const BodyDefinition& body);

		cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) override;
		cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed) override;
		/// <summary>The star and Wendmere from the spec, then Pale Wend and the Manalith about Wendmere; unsettled and nobody's.</summary>
		cs::Ref<StarSystem> Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities) override;

		/// <summary>The bodies the spec has no words for: the moon and the Manalith, both about <see cref="PlanetName"/>.</summary>
		static cs::List<cs::Ref<BodyDefinition>> PlanetSatellites();

		static cs::Ref<SystemSpec> Spec();
	};
}
