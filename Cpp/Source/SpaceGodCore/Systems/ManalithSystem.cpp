// US-T458: Nightglass, the system of the Manalith (see ManalithSystem.h).

#include "Systems/ManalithSystem.h"
#include "Systems/Galaxy.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/StarSystemSpec.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "OrbitalElements.h"
#include "World.h"

namespace SpaceGod::Core
{
	bool ManalithSystem::IsSatellite(const BodyDefinition& body)
	{
		return body.Kind == BodyKind::Station && body.Name == SatelliteName && body.ParentName == PlanetName;
	}

	cs::List<cs::Ref<StarEntry>> ManalithSystem::StarsNear(DVec3 centreLy, double radiusLy, int seed)
	{
		(void)seed;
		cs::List<cs::Ref<StarEntry>> r;
		if ((PositionLy() - centreLy).Length() > radiusLy) return r;
		auto e = cs::New<StarEntry>();
		e->Name = SystemName; e->PositionLy = PositionLy(); e->Spectral = "G4V";
		e->MassSolar = 0.97; e->RadiusSolar = 0.96; e->LuminositySolar = 0.86; e->TeffK = 5640;
		e->Tag = "manalith";
		r.Add(e);
		return r;
	}

	cs::Ref<SystemSpec> ManalithSystem::Spec()
	{
		auto star = cs::New<StarSpec>();
		star->Name = SystemName; star->Spectral = "G4V"; star->MassSolar = 0.97; star->RadiusSolar = 0.96; star->LuminositySolar = 0.86;
		star->TeffK = 5640; star->RotationDays = 27;
		star->Summary = "Fiction. A quiet yellow dwarf a little cooler than the Sun, alone in an empty reach 6.4 light-years out. No gate has ever been built to it.";

		auto spec = cs::New<SystemSpec>();
		spec->Name = SystemName; spec->NeighbourName = SystemName;
		spec->RaDeg = RaDeg; spec->DecDeg = DecDeg; spec->DistanceLy = DistanceLy;
		spec->Primary = StellarNode::Single(star);
		spec->Summary = "Reached only by FTL: an Earthlike world, its pale moon and the Manalith tumbling above it, shedding light.";

		// Wendmere sits where Earth would round a star this bright: 0.93 AU gives it Earth's sunlight (L / a^2 = 1).
		auto p = cs::New<PlanetSpec>();
		p->Name = PlanetName; p->Host = SystemName; p->AAu = 0.927;
		p->PeriodDays = OrbitalElements::PeriodFromGm(p->AAu * Astro::AuKm, StarSystemBuilder::SunGm * star->MassSolar);
		p->E = 0.012; p->MassEarth = 1.04; p->MassIsMinimum = false; p->RadiusEarth = 1.02; p->MeanAnomalyDeg = 130;
		p->Status = BodyStatus::Fictional; p->Source = "Game layer (US-T458)";
		p->Color = Rgb(0.3f, 0.46f, 0.66f); p->RotationHours = 26.3;
		p->AtmosphereColor = Rgb(0.42f, 0.66f, 1.0f); p->AtmosphereStrength = 0.65f;
		p->Summary = "Fiction. An Earthlike world of blue seas, green continents and white weather, with a pale moon like Earth's. "
			"The Manalith tumbles above it, raining shards that burn up as faint blue meteors.";
		spec->Planets.Add(p);
		return spec;
	}

	cs::Ref<SystemSpec> ManalithSystem::Define(const cs::Ref<StarEntry>& star, int seed)
	{
		(void)seed;
		return star != nullptr && Gateless(star->Name) ? Spec() : nullptr;
	}

	cs::List<cs::Ref<BodyDefinition>> ManalithSystem::PlanetSatellites()
	{
		cs::List<cs::Ref<BodyDefinition>> r;
		{
			// Pale Wend: the Moon's size and mass on the Moon's orbit, so Wendmere's tides and nights are Earth's.
			auto b = cs::New<BodyDefinition>();
			b->Name = MoonName; b->Kind = BodyKind::Moon; b->ParentName = PlanetName; b->RadiusKm = 1737.4; b->Gm = 4902.8;
			b->Elements = OrbitalElements::FromClassical(Astro::J2000, 384400, 0.0549, 5.145, 40, 80, 210, 27.321661);
			b->PoleRaDeg = 269.99; b->PoleDecDeg = 66.54; b->RotationHours = 655.72; b->FacesParent = true;
			b->Color = Rgb(0.64f, 0.63f, 0.62f); b->Texture = "moon_color"; b->NormalMap = "moon_normal";
			b->Status = BodyStatus::Fictional;
			b->Summary = "Fiction. Wendmere's moon, grey and cratered and tidally locked: a near twin of Earth's.";
			r.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = SatelliteName; b->Kind = BodyKind::Station; b->ParentName = PlanetName; b->RadiusKm = SatelliteRadiusKm; b->Gm = 1e-7;
			double gm = StarSystemBuilder::EarthGm * 1.04;
			b->Elements = OrbitalElements::FromClassical(Astro::J2000, SatelliteOrbitKm, 0.004, SatelliteInclinationDeg, 75, 0, 35,
				OrbitalElements::PeriodFromGm(SatelliteOrbitKm, gm));
			// The view tumbles it on its own clock (ManalithView); the body itself keeps a slow spin for anything that asks.
			// Its meridian is kept on Wendmere (FacesParent): the view takes that as "down", the way its shards fall.
			b->PoleRaDeg = 0; b->PoleDecDeg = 90; b->RotationHours = 2; b->FacesParent = true;
			b->Color = Rgb(0.2f, 0.18f, 0.26f);
			b->Status = BodyStatus::Fictional;
			b->Summary = "Fiction. A crystal-veined mass 3 km across, black glass shot through with mana blue, tumbling slowly in orbit and "
				"shedding shards as it goes. Nobody knows who put it there, or whether anyone did.";
			r.Add(b);
		}
		return r;
	}

	cs::Ref<StarSystem> ManalithSystem::Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities)
	{
		(void)star;
		auto built = StarSystemBuilder::Build(spec);
		for (const auto& b : built->Bodies)
			if (b->Name == PlanetName)
			{
				// Earth's own maps: blue seas, green and brown land, white cloud.
				b->Texture = "earth_color";
				b->PrimeMeridianDeg = 70;
			}
		built->Bodies.AddRange(PlanetSatellites());
		auto catalog = cs::New<BodyCatalog>(built->Bodies);
		for (const auto& d : built->Discs) d->HostIndex = (*catalog)[d->HostName]->Index;
		auto eph = cs::New<SpaceGod::Core::Ephemeris>(catalog);
		eph->Update(jd);
		auto world = SpaceGod::Core::World::CreateUnsettled(eph, jd, StarSystem::SeedOf(spec->Name), polities);
		double span = StarSystem::OverviewSpan(catalog);
		return cs::New<StarSystem>(spec->Name, spec->PositionLy(), eph, world, spec, built->Discs, span);
	}
}
