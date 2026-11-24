// US-T621: see SpaceGodConsole.h.

#include "Console/SpaceGodConsole.h"

#include "Console/GameConsole.h"
#include "Input/KeyBindings.h"
#include "Input/UnrealInputDevice.h"
#include "Settings/GameSetting.h"

#include "Framework/Application/SlateApplication.h"

using namespace SpaceGod;
using namespace SpaceGod::Input;

bool USpaceGodConsole::IsToggleKey(const FKey& key)
{
	const FModifierKeysState state = FSlateApplication::Get().GetModifierKeys();
	const int held = (state.IsShiftDown() ? KeyMods::Shift : 0) | (state.IsControlDown() ? KeyMods::Ctrl : 0) | (state.IsAltDown() ? KeyMods::Alt : 0);
	const cs::Ref<KeyBindingSetting>& binding = KeyBindings::Binding(KeyAction::Console);
	for (const InputBinding& slot : binding->Value)
		if (slot.source == BindSource::Key && slot.mods == held && UnrealInputDevice::KeyOf(static_cast<Key>(slot.code)) == key)
			return true;
	return false;
}

bool USpaceGodConsole::InputKey(FInputDeviceId DeviceId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	if (Event == IE_Pressed && !bGamepad && GameConsole::Enabled()->Value && IsToggleKey(Key))
	{
		static const FName Typing("Typing"), Open("Open");
		if (ConsoleState == NAME_None) FakeGotoState(Typing);
		else if (ConsoleState == Typing) FakeGotoState(Open);
		else FakeGotoState(NAME_None);
		bCaptureKeyInput = true;
		return true;
	}
	return Super::InputKey(DeviceId, Key, Event, AmountDepressed, bGamepad);
}

void USpaceGodConsole::PostRender_Console(UCanvas* Canvas)
{
	if (ConsoleState != NAME_None && !GameConsole::Enabled()->Value)
		FakeGotoState(NAME_None);
	Super::PostRender_Console(Canvas);
}
