#include "Modules/ModuleManager.h"
#include "SpaceGodLog.h"
#include "VehicleExport.h"

DEFINE_LOG_CATEGORY(LogSpaceGod);

namespace
{
	class FSpaceGodModule : public FDefaultGameModuleImpl
	{
	public:
		void StartupModule() override
		{
			FDefaultGameModuleImpl::StartupModule();
			// US-T452: the vehicle data file is found before anything reads a hull design (docs/Vehicles.md).
			SpaceGod::VehicleExport::FindData();
		}
	};
}

IMPLEMENT_PRIMARY_GAME_MODULE(FSpaceGodModule, SpaceGod, "SpaceGod");
