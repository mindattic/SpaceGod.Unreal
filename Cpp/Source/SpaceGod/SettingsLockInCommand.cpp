// US-T512: sg.Settings, the shipped defaults (Content/Settings/shipped-defaults.json, docs/PERSISTENCE.md) by hand.
//   sg.Settings lockin [group|key]   every setting there (all of them when none is named) not at its default ships as its default
//   sg.Settings unlock <group|key|all>   back to the factory default
//   sg.Settings list   what the file locks in

#include "SpaceGodLog.h"
#include "HAL/IConsoleManager.h"

#include "GameSettingsHost.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

#include <algorithm>

using SpaceGod::GameSettingsHost;
using namespace SpaceGod::Core;

namespace
{
	FString T(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	cs::String C(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

	/// A group named in any case comes back as the registry spells it; anything else (a key) as typed.
	cs::String Resolve(const FString& name)
	{
		for (const auto& g : GameSettingsHost::Registry()->Groups())
			if (name.Equals(T(g), ESearchCase::IgnoreCase)) return g;
		return C(name);
	}

	/// Whether a setting under <paramref name="key"/> can move what the bench digests: anything outside the groups
	/// that only change how the game looks, sounds or is driven.
	bool Simulated(const cs::String& key)
	{
		auto s = GameSettingsHost::Registry()->Find(key);
		if (s == nullptr) return true;
		for (const char* g : { SettingGroups::Graphics, SettingGroups::RenderPipeline, SettingGroups::RenderBudget, SettingGroups::Controls,
			SettingGroups::Audio, SettingGroups::Interface, SettingGroups::Multiplayer, SettingGroups::Developer })
			if (s->Group == g) return false;
		return true;
	}

	void Report(const TCHAR* verb, const cs::List<cs::String>& keys)
	{
		bool simulated = false;
		const auto file = GameSettingsHost::ReadShippedDefaults();
		for (const auto& key : keys)
		{
			cs::String text;
			file.TryGetValue(key, text);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Settings: %s %s%s"), verb, *T(key), text.empty() ? TEXT("") : *FString::Printf(TEXT(" = %s"), *T(text)));
			simulated |= Simulated(key);
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Settings: %d setting(s) %s in %s"), keys.Count(), verb, *T(GameSettingsHost::ShippedDefaultsPath()));
		if (simulated)
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Settings: a simulation setting's default moved, so the bench digests move with it: re-record Bench/expected-digests.json."));
	}

	void List()
	{
		auto file = GameSettingsHost::ReadShippedDefaults();
		auto keys = file.Keys();
		std::sort(keys.Raw().begin(), keys.Raw().end(), [](const cs::String& a, const cs::String& b) { return cs::Strings::CompareOrdinal(a, b) < 0; });
		for (const auto& key : keys)
		{
			auto s = GameSettingsHost::Registry()->Find(key);
			FString where = s == nullptr ? FString(TEXT("no setting declares it"))
				: GameSettingsHost::Registry()->IsLockedIn(key) ? FString::Printf(TEXT("%s, now %s"), *T(s->Group), *T(s->ValueText()))
				: FString::Printf(TEXT("%s, refused"), *T(s->Group));
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Settings: %s = %s (%s)"), *T(key), *T(file[key]), *where);
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Settings: %d setting(s) locked in by %s"), keys.Count(), *T(GameSettingsHost::ShippedDefaultsPath()));
	}

	void Run(const TArray<FString>& args)
	{
		FString verb = args.Num() > 0 ? args[0] : FString(TEXT("list"));
		TArray<FString> rest(args);
		if (rest.Num() > 0) rest.RemoveAt(0);
		FString name = FString::Join(rest, TEXT(" "));
		bool all = name.IsEmpty() || name.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		cs::String target = all ? cs::String() : Resolve(name);
		if (verb.Equals(TEXT("list"), ESearchCase::IgnoreCase)) List();
		else if (verb.Equals(TEXT("lockin"), ESearchCase::IgnoreCase)) Report(TEXT("locked in"), GameSettingsHost::LockIn(target));
		else if (verb.Equals(TEXT("unlock"), ESearchCase::IgnoreCase) && !name.IsEmpty()) Report(TEXT("unlocked"), GameSettingsHost::Unlock(target));
		else UE_LOG(LogSpaceGod, Warning, TEXT("sg.Settings: usage: sg.Settings lockin [group|key] | unlock <group|key|all> | list"));
	}

	FAutoConsoleCommand GSettingsCommand(
		TEXT("sg.Settings"),
		TEXT("US-T512: sg.Settings lockin [group|key] makes each setting there not at its default (every setting when none is named) a shipped default "
			"in Content/Settings/shipped-defaults.json; unlock <group|key|all> takes them out again, back to the factory default; list shows the file."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
		{
			try { Run(args); }
			catch (const std::exception& e) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Settings: %s"), UTF8_TO_TCHAR(e.what())); }
		}));
}
