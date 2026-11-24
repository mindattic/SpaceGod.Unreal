// US-T621: see SpaceGodConsole.h.

#include "Console/SpaceGodConsole.h"

#include "Console/GameConsole.h"
#include "GameSettingsHost.h"
#include "Input/KeyBindings.h"
#include "Input/UnrealInputDevice.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "UI/CheatMenu.h"

#include "ConsoleSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"

using namespace SpaceGod;
using namespace SpaceGod::Input;

void USpaceGodConsole::PostInitProperties()
{
	Super::PostInitProperties();
	GetMutableDefault<UInputSettings>()->ConsoleKeys.Empty();
	static const FDelegateHandle bound = RegisterConsoleAutoCompleteEntries.AddStatic(&USpaceGodConsole::AddAutoCompleteEntries);
}

void USpaceGodConsole::AddAutoCompleteEntries(TArray<FAutoCompleteCommand>& list)
{
	const FColor color = GetDefault<UConsoleSettings>()->AutoCompleteCommandColor;
	auto add = [&](const FString& command, const FString& desc)
	{
		FAutoCompleteCommand& entry = list.AddDefaulted_GetRef();
		entry.Command = command;
		entry.Desc = desc;
		entry.Color = color;
	};
	auto T = [](const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); };

	for (const auto& s : GameSettingsHost::Registry()->All())
	{
		FString fallback;
		if (auto t = cs::As<Core::ToggleSetting>(s)) fallback = t->Default ? TEXT("On") : TEXT("Off");
		else if (auto r = cs::As<Core::RangeSetting>(s)) fallback = T(cs::Fmt(r->Default, "R"));
		else if (auto c = cs::As<Core::ChoiceSetting>(s)) fallback = T(c->Options[c->Default]);
		else if (auto x = cs::As<Core::TextSetting>(s)) fallback = TEXT("\"") + T(x->Default) + TEXT("\"");
		const FString key = T(s->Key);
		const FString desc = fallback.IsEmpty() ? FString::Printf(TEXT("%s: %s"), *T(s->Label), *T(s->ValueText()))
			: FString::Printf(TEXT("%s: %s (default %s)"), *T(s->Label), *T(s->ValueText()), *fallback);
		add(TEXT("sg.Set ") + key + TEXT(" "), desc);
		if (cs::As<Core::ToggleSetting>(s)) add(TEXT("sg.Toggle ") + key, desc);
	}

	for (const SpaceGod::UI::CheatRegistry::Category& category : SpaceGod::UI::CheatRegistry::Categories())
	{
		add(FString::Printf(TEXT("sg.Cheat \"%s/"), *T(category.Name)), T(category.Blurb));
		for (const SpaceGod::UI::CheatEntry& entry : category.Entries)
			add(FString::Printf(TEXT("sg.Cheat \"%s/%s\""), *T(category.Name), *T(entry.Label)), T(entry.Detail));
	}

	add(TEXT("sg.Settings lockin"), TEXT("make every setting not at its default a shipped default (or name a group or key)"));
	add(TEXT("sg.Settings unlock all"), TEXT("take shipped defaults out again (or name a group or key)"));
	add(TEXT("sg.Settings list"), TEXT("show the shipped defaults file"));
}

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
		if (ConsoleState == NAME_None)
		{
			BuildRuntimeAutoCompleteList(true);
			FakeGotoState(Typing);
		}
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
