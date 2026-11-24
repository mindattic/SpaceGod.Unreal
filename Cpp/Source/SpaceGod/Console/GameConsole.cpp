// US-T620: see GameConsole.h.

#include "Console/GameConsole.h"

#include "Settings/GameSetting.h"

#include "Engine/Console.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		const TCHAR* const StatNames[] = { nullptr, TEXT("FPS"), TEXT("Unit"), TEXT("UnitGraph") };
		int appliedStat = 0;

		void ApplyStat(UWorld* world, int index)
		{
			UGameViewportClient* viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
			if (!viewport || !world) return;
			if (index == 0 && appliedStat == 0) return;
			for (int i = 1; i < 4; i++)
				GEngine->SetEngineStat(world, viewport, StatNames[i], i == index);
			appliedStat = index;
		}

		void ApplyStatNow()
		{
			UWorld* world = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
			ApplyStat(world, GameConsole::StatsOverlay()->Index);
		}

		void ApplyLogVerbosity(int index)
		{
			static const TCHAR* const levels[] = { TEXT("default"), TEXT("Verbose"), TEXT("VeryVerbose") };
			if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("log LogSpaceGod %s"), levels[index]));
		}
	}

	const cs::Ref<ToggleSetting>& GameConsole::Enabled()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("dev.console", SettingGroups::Developer, "Console", true,
			"The ` key opens the developer console: type sg.Help for its commands.", "developer console command backtick tilde");
		return value;
	}

	const cs::Ref<ChoiceSetting>& GameConsole::StatsOverlay()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("dev.statsOverlay", SettingGroups::Developer, "Stats overlay",
			cs::List<cs::String>{ "Off", "FPS", "Unit", "Unit graph" }, 0,
			"Shows the engine's FPS, frame-time (Unit) or frame-time graph stat in the viewport.", "stats overlay fps unit frame time graph stat")
			->Applying([](int) { ApplyStatNow(); });
		return value;
	}

	const cs::Ref<ChoiceSetting>& GameConsole::LogVerbosity()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("dev.logVerbosity", SettingGroups::Developer, "Log verbosity",
			cs::List<cs::String>{ "Normal", "Verbose", "Very verbose" }, 0,
			"How much the game's log category (LogSpaceGod) writes.", "log verbosity verbose output debug")
			->Applying([](int i) { ApplyLogVerbosity(i); });
		return value;
	}

	bool GameConsole::IsOpen()
	{
		const UGameViewportClient* viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
		return viewport && viewport->ViewportConsole && viewport->ViewportConsole->ConsoleState != NAME_None;
	}

	void GameConsole::Declare()
	{
		Enabled();
		StatsOverlay();
		LogVerbosity();
		static const FDelegateHandle handle = FWorldDelegates::OnStartGameInstance.AddLambda([](UGameInstance*) { ApplyStatNow(); });
		static const FDelegateHandle worldHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld*, const UWorld::InitializationValues) { ApplyStatNow(); });
	}
}
