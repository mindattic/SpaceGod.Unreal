#pragma once
// US-T452: the vehicle data file (Data/Vehicles/Vehicles.json, docs/Vehicles.md): where the game finds it, and
// sg.ExportVehicles, which writes it (and a mesh per vehicle) from the code that builds the models.

#include "CoreMinimal.h"

namespace SpaceGod::VehicleExport
{
	/// <summary>Finds the vehicle data file and hands its path to Core::VehicleData, before anything reads a hull design
	/// (the module's start up): SPACEGOD_VEHICLES, else the package's Vehicles/Vehicles.json, else the repo's
	/// Data/Vehicles/Vehicles.json; -NoVehicleData for none.</summary>
	void FindData();

	/// <summary>The repo's Data/Vehicles folder (sg.ExportVehicles' default).</summary>
	FString RepoDataDir();
}
