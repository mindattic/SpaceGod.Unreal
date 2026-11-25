#pragma once
// US-T815: a civilisation's Kardashev index from the energy it uses (its people, its industry, its ships' fusion drives
// and what it keeps in orbit), and the milestones on that scale that open bigger hulls to the modular assembler.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Faction;
	class Polities;
	struct FactionStyle;
	enum class ShipClass : int;

	/// <summary>What a polity burns, in watts, by where it goes, and the Kardashev index of the whole.</summary>
	struct SPACEGODCORE_API KardashevReading
	{
		/// Homes, life support and transport: <see cref="Kardashev::WattsPerPerson"/> a head (Society).
		double PeopleW = 0;
		/// Its facilities' output at what each kind of work costs a tonne (Economy).
		double IndustryW = 0;
		/// The helium-3 its ports sell to ships' fusion drives, burned (Economy, <see cref="Propulsion"/>).
		double DrivesW = 0;
		/// Its yards, works, habitats and stations in orbit (Infrastructure).
		double StructuresW = 0;
		/// Sagan's interpolation, (log10 W - 6) / 10: 0 at a megawatt, 1 at 10^16 W.
		double Index = 0;

		double Watts() const { return PeopleW + IndustryW + DrivesW + StructuresW; }
	};

	/// <summary>A point on the scale where a hull class opens to the assembler.</summary>
	struct KardashevMilestone
	{
		ShipClass Class;
		double Index;
		/// What it opens, in words ("battleships").
		const char* Name;
	};

	/// <summary>
	/// The Kardashev readout (US-T815). A pure read of the simulation as it stands: no random draws, nothing written, so
	/// reading it never moves a run. Cheap (one pass over the polity's settlements, facilities and orbital structures in
	/// every system); callers still read it a few times a second, not every frame.
	/// </summary>
	class SPACEGODCORE_API Kardashev final
	{
	public:
		/// Watts each person uses: a spacefaring life on airless worlds and in habitats, four times Earth's 2.5 kW today.
		static constexpr double WattsPerPerson = 1e4;
		/// D-He3 fusion: 18.3 MeV for every 3 u of helium-3, about 5.9e17 J a tonne.
		static constexpr double HeliumThreeJoulesPerTonne = 5.9e17;
		/// A yard's work per level, beside its cradle's own structure.
		static constexpr double ShipyardWattsPerLevel = 2e9;
		/// How far past its milestone a capital class is built at its heaviest (longest spine, most guns).
		static constexpr double HeftSpan = 0.15;

		/// <summary>What <paramref name="f"/> burns across every system <paramref name="polities"/> holds.</summary>
		static KardashevReading Of(const Polities& polities, const cs::Ref<Faction>& f);
		/// <summary>The index of <paramref name="watts"/> (0 for a megawatt or less).</summary>
		static double IndexOf(double watts);

		/// <summary>The milestones in rising order, <paramref name="count"/> of them.</summary>
		static const KardashevMilestone* Milestones(int& count);
		/// <summary>The index <paramref name="cls"/> opens at (0: always open).</summary>
		static double UnlockIndex(ShipClass cls);
		static bool Unlocked(ShipClass cls, double index) { return index >= UnlockIndex(cls); }
		/// <summary>The next milestone above <paramref name="index"/>, or null once every one is passed.</summary>
		static const KardashevMilestone* Next(double index);
		/// <summary>
		/// <paramref name="style"/> for a hull of <paramref name="cls"/> built at <paramref name="index"/>: past the class's
		/// milestone its spine grows longer and its guns more, up to the heaviest <see cref="HeftSpan"/> on (heavier
		/// carriers, then battleships). Only ever raises a weight; a class with no milestone is unchanged.
		/// </summary>
		static FactionStyle Tiered(const FactionStyle& style, ShipClass cls, double index);
		/// <summary>Watts in a short form: "320 GW", "4.1 TW".</summary>
		static cs::String Power(double watts);
	};
}
