// US-T815: the Kardashev readout (see Kardashev.h).

#include "Systems/Kardashev.h"

#include "Assembly/ModularAssembler.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Ships.h"
#include "Systems/Polities.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr double KardashevSecondsPerDay = 86400;

		/// Joules a tonne of each industry's output takes: bulk mining about 0.5 GJ, lit hydroponics 30 GJ, smelting 25 GJ
		/// (steel today), machining and munitions 50 GJ, drugs 200 GJ, chips a terajoule.
		double KardashevJoulesPerTonne(IndustryKind kind)
		{
			switch (kind)
			{
				case IndustryKind::Mine: return 5e8;
				case IndustryKind::Farm: return 3e10;
				case IndustryKind::PharmaLab: return 2e11;
				case IndustryKind::Smelter: return 2.5e10;
				case IndustryKind::ElectronicsFab: return 1e12;
				case IndustryKind::MachineWorks: return 5e10;
				case IndustryKind::MunitionsPlant: return 5e10;
				case IndustryKind::Shipyard: return 0;
			}
			return 0;
		}

		/// What a structure in orbit draws: a yard's cradle and a works' furnaces in gigawatts, a habitat ring's half, a
		/// depot's pumps and a gateway's docks less, the real spacecraft next to nothing (the ISS runs on about 100 kW).
		double KardashevWattsOf(StructureKind kind)
		{
			switch (kind)
			{
				case StructureKind::Shipyard: return 2e9;
				case StructureKind::Factory: return 1e9;
				case StructureKind::Habitat: return 5e8;
				case StructureKind::Gateway: return 2e8;
				case StructureKind::Depot: return 5e7;
				case StructureKind::CrewedStation: return 1e5;
				case StructureKind::Relay: return 1e4;
				case StructureKind::Telescope: return 2e3;
				case StructureKind::Probe: return 1e3;
			}
			return 0;
		}

		/// Rising: destroyers for a settled system or two, battleships for a power with real traffic between its worlds. A
		/// class's first step opens it; a later one names where it is built at its heaviest (Kardashev::Tiered).
		const KardashevMilestone KardashevSteps[] = {
			{ ShipClass::Destroyer, 0.30, "destroyers" },
			{ ShipClass::Cruiser, 0.40, "cruisers" },
			{ ShipClass::Carrier, 0.50, "carriers" },
			{ ShipClass::Battleship, 0.60, "battleships" },
			{ ShipClass::Carrier, 0.50 + Kardashev::HeftSpan, "heavy carriers" },
			{ ShipClass::Battleship, 0.60 + Kardashev::HeftSpan, "heavy battleships" },
		};
	}

	KardashevReading Kardashev::Of(const Polities& polities, const cs::Ref<Faction>& f)
	{
		KardashevReading r;
		if (f == nullptr) return r;
		const int he3 = static_cast<int>(Good::HeliumThree);
		for (const auto& w : polities.Worlds())
		{
			if (w == nullptr) continue;
			if (w->Economy != nullptr)
				for (const auto& s : w->Economy->Settlements)
				{
					if (s == nullptr || s->Owner != f) continue;
					r.PeopleW += std::max(0.0, s->Population) * WattsPerPerson;
					for (const auto& fac : s->Facilities)
					{
						if (fac == nullptr || fac->Recipe == nullptr) continue;
						double work = fac->Level * fac->Utilisation;
						if (work <= 0) continue;
						if (fac->Recipe->Kind == IndustryKind::Shipyard)
						{
							r.IndustryW += work * ShipyardWattsPerLevel;
							continue;
						}
						double tonnes = 0;
						for (const auto& o : fac->Recipe->Outputs) tonnes += std::get<1>(o);
						r.IndustryW += work * tonnes * KardashevJoulesPerTonne(fac->Recipe->Kind) / KardashevSecondsPerDay;
					}
					if (s->PropellantDemand.Count() > he3)
						r.DrivesW += std::max(0.0, s->PropellantDemand[he3]) * HeliumThreeJoulesPerTonne / KardashevSecondsPerDay;
				}
			if (w->Infrastructure != nullptr)
				for (const auto& st : w->Infrastructure->Structures)
					if (st != nullptr && st->Owner() == f) r.StructuresW += KardashevWattsOf(st->Kind);
		}
		r.Index = IndexOf(r.Watts());
		return r;
	}

	double Kardashev::IndexOf(double watts)
	{
		return watts > 1e6 ? (std::log10(watts) - 6) / 10 : 0;
	}

	const KardashevMilestone* Kardashev::Milestones(int& count)
	{
		count = static_cast<int>(sizeof(KardashevSteps) / sizeof(KardashevSteps[0]));
		return KardashevSteps;
	}

	double Kardashev::UnlockIndex(ShipClass cls)
	{
		for (const auto& m : KardashevSteps)
			if (m.Class == cls) return m.Index;
		return 0;
	}

	const KardashevMilestone* Kardashev::Next(double index)
	{
		for (const auto& m : KardashevSteps)
			if (m.Index > index) return &m;
		return nullptr;
	}

	FactionStyle Kardashev::Tiered(const FactionStyle& style, ShipClass cls, double index)
	{
		FactionStyle tiered = style;
		double unlock = UnlockIndex(cls);
		if (unlock <= 0 || index < unlock) return tiered;
		double heft = std::clamp((index - unlock) / HeftSpan, 0.0, 1.0);
		tiered.Length = std::max(style.Length, style.Length + (1 - style.Length) * heft);
		tiered.Armament = std::max(style.Armament, style.Armament + (1 - style.Armament) * heft);
		return tiered;
	}

	cs::String Kardashev::Power(double watts)
	{
		if (watts >= 1e15) return cs::Cat(cs::Fmt(watts / 1e15, "0.0"), " PW");
		if (watts >= 1e12) return cs::Cat(cs::Fmt(watts / 1e12, "0.0"), " TW");
		if (watts >= 1e9) return cs::Cat(cs::Fmt(watts / 1e9, "0"), " GW");
		return cs::Cat(cs::Fmt(watts / 1e6, "0"), " MW");
	}
}
