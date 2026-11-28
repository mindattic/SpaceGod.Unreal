#include "Modules/ModuleManager.h"
#include "CaptureFixedStep.h"
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
			// US-T944: -capture-fixed steps the game 1/fps per rendered frame for a tape that never falls behind.
			SpaceGod::CaptureFixedStep::Install();
		}
	};
}

IMPLEMENT_PRIMARY_GAME_MODULE(FSpaceGodModule, SpaceGod, "SpaceGod");
