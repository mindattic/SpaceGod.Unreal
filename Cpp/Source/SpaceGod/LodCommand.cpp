// US-T719: sg.Lod, the render budget's latest frame: how many ships it drew in each tier against that tier's cap, how
// many a cap pushed a tier down, how many were culled for being too small or past the horizon, and what ranking them cost.

#include "RenderBudget.h"
#include "SpaceGodLog.h"

#include "HAL/IConsoleManager.h"

namespace
{
	FAutoConsoleCommand GLodCommand(
		TEXT("sg.Lod"),
		TEXT("US-T719: the render budget's latest frame: ships per tier (Full, Simplified, Icon, Culled) against each cap, those a cap demoted, and the game-thread cost of ranking them."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			const auto& budget = SpaceGod::Core::RenderBudget::Ships();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Lod: ships: %s"), UTF8_TO_TCHAR(budget.Describe().c_str()));
		}));
}
