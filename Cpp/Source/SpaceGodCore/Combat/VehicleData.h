#pragma once
// US-T452: Data/Vehicles/Vehicles.json read back -- engine ports and hardpoint placements an editor (SpaceGod.Studio) moved.

#include "Cs/Cs.h"
#include "DVec3.h"

#include <vector>

namespace SpaceGod::Core
{
	class SubsystemDef;

	/// <summary>
	/// The vehicle data file (US-T452, docs/Vehicles.md), written by the game's sg.ExportVehicles and edited by SpaceGod.Studio:
	/// what the game reads back from it. Two things, everything else in the file being for the editor:
	/// <list type="bullet">
	/// <item>a vehicle's engine ports (<see cref="PortsOf"/>, by vehicle id, e.g. "usc:GalacticLeopard31"), which the
	/// views mark on the model in place of their built-in tables;</item>
	/// <item>a hull design's hardpoint placements (<see cref="Apply"/>, by layout id, e.g. "ship/Battleship/Battleship"):
	/// position, facing aft, cone axis and half-angle of each hardpoint the code declares, matched by index and kind.</item>
	/// </list>
	/// Loaded once, on first use, from the path given to <see cref="SetPath"/> (the game's module does that at start up, the
	/// bench from --vehicles), else from the SPACEGOD_VEHICLES environment variable; with neither, or a file that is missing,
	/// does not parse or is not version 1, nothing is overridden and the built-in tables stand. Values are taken exactly as
	/// written (no normalising), so a file exported from the built-in tables reproduces them bit for bit.
	/// </summary>
	class SPACEGODCORE_API VehicleData final
	{
	public:
		/// <summary>One engine nozzle, in the model frame (hull lengths, nose +Z, up +Y).</summary>
		struct Port
		{
			double Pos[3] = { 0, 0, 0 };
			double Exhaust[3] = { 0, 0, -1 };
			double Radius = 0.02;
		};

		/// <summary>Where the file is read from; call before anything reads a hull design (an empty path: no file). Later
		/// calls, once loaded, change nothing.</summary>
		static void SetPath(const cs::String& path);

		/// <summary>The data, loaded on first use.</summary>
		static const VehicleData& Get();

		/// <summary>The engine ports the file gives vehicle <paramref name="id"/>, or null when it gives none (keep the
		/// built-in ones). An empty list is a vehicle with no nozzles.</summary>
		const std::vector<Port>* PortsOf(const cs::String& id) const;

		/// <summary>Puts the file's placements of layout <paramref name="layoutId"/> onto <paramref name="layout"/>, a design's
		/// own hardpoints as just declared (before anything derived is added): each entry matched by index and kind. Returns
		/// the hardpoints it set.</summary>
		int Apply(const cs::String& layoutId, const cs::List<cs::Ref<SubsystemDef>>& layout) const;

		/// <summary>Convenience: <see cref="Get"/>().<see cref="Apply"/>.</summary>
		static int ApplyTo(const cs::String& layoutId, const cs::List<cs::Ref<SubsystemDef>>& layout) { return Get().Apply(layoutId, layout); }

		bool Loaded() const { return loaded; }
		/// <summary>What the simulation runs on, as 16 hex digits: an FNV-1a hash of the loaded file's bytes, or "builtin" with
		/// none. Hardpoint placements are simulation data, so two co-op peers (and two bench runs) compare it.</summary>
		const cs::String& Hash() const { return hash; }
		const cs::String& Path() const { return path; }
		/// <summary>Why nothing was loaded, or empty.</summary>
		const cs::String& Problem() const { return problem; }
		/// <summary>Entries left out (bad values, kinds that no longer match), for the log.</summary>
		const cs::List<cs::String>& Ignored() const { return ignored; }
		int VehicleCount() const { return static_cast<int>(ports.size()); }
		int LayoutCount() const { return static_cast<int>(layouts.size()); }
		/// <summary>Hardpoints <see cref="Apply"/> has set so far (every design reads its own once).</summary>
		int Applied() const { return applied; }

	private:
		struct Placement
		{
			int Index = -1;
			cs::String Kind;
			DVec3 Pos;
			bool FacesAft = false;
			bool HasAxis = false;
			DVec3 Axis;
			bool HasHalf = false;
			double Half = 0;
		};

		bool loaded = false;
		cs::String path, problem, hash = "builtin";
		mutable cs::List<cs::String> ignored;
		mutable int applied = 0;
		std::vector<std::pair<cs::String, std::vector<Port>>> ports;
		std::vector<std::pair<cs::String, std::vector<Placement>>> layouts;

		void Load(const cs::String& file);
	};
}
