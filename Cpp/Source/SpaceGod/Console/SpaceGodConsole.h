#pragma once

// US-T621: the engine's console class ([/Script/Engine.Engine] ConsoleClassName). Unreal's UConsole keeps its own
// history, scrollback and autocomplete; this subclass only changes what opens and closes it: a press of a keyboard key
// held by the Console key action (KeyBindings, the backtick by default) steps None -> Typing -> Open -> None while
// Options > Developer > Console is on (GameConsole::Enabled), instead of [/Script/Engine.InputSettings] ConsoleKeys
// (emptied in PostInitProperties, so BaseInput.ini's Tilde cannot open it). With the setting off the key passes
// through to the game, and a console left open closes.

#include "CoreMinimal.h"
#include "Engine/Console.h"
#include "SpaceGodConsole.generated.h"

UCLASS()
class USpaceGodConsole : public UConsole
{
	GENERATED_BODY()

public:
	virtual void PostInitProperties() override;
	virtual bool InputKey(FInputDeviceId DeviceId, FKey Key, EInputEvent Event, float AmountDepressed = 1.f, bool bGamepad = false) override;
	virtual void PostRender_Console(UCanvas* Canvas) override;

private:
	/// <summary>A keyboard binding of the Console action is on <paramref name="key"/> with exactly the modifiers held now.</summary>
	static bool IsToggleKey(const FKey& key);

	/// <summary>
	/// US-T625: the game's own autocomplete lines (RegisterConsoleAutoCompleteEntries, rebuilt each time the console
	/// opens): sg.Set per setting with its label, value and default, sg.Toggle per toggle, sg.Cheat per dev menu
	/// category and entry, and sg.Settings lockin, unlock and list.
	/// </summary>
	static void AddAutoCompleteEntries(TArray<FAutoCompleteCommand>& list);
};
