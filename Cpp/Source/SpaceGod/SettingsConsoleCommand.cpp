// US-T624: any setting by key from the console, through the same path as the Options menu's rows: a change is applied
// live (each setting's Changed), remembered in the profile (GameSetting's own Remember), and a co-op shared one follows
// CoopSettings' host rule.
//   sg.Get <key>              value, default, range or choices, group, and whether a shipped default locks it in
//   sg.Set <key> <value...>   the text read as the profile reads it (a key binding as "Shift+K", "Pad:A")
//   sg.Reset <key|group>      back to the default
//   sg.Toggle <key>           flips an on/off setting
//   sg.Find <text>            the settings whose key, label, group, description or keywords match, with their values

#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

#include "Coop/CoopPeer.h"
#include "Coop/CoopSettings.h"
#include "GameSettingsHost.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

#include <set>

using SpaceGod::GameSettingsHost;
using namespace SpaceGod::Core;

namespace
{
	FString T(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	cs::String C(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

	/// The setting under <paramref name="key"/>, as typed or in any case.
	cs::Ref<GameSetting> FindSetting(const FString& key)
	{
		const auto& registry = GameSettingsHost::Registry();
		if (auto s = registry->Find(C(key))) return s;
		for (const auto& s : registry->All())
			if (key.Equals(T(s->Key), ESearchCase::IgnoreCase)) return s;
		return nullptr;
	}

	/// The group named <paramref name="name"/> in any case, as the registry spells it; empty when there is none.
	cs::String FindGroup(const FString& name)
	{
		for (const auto& g : GameSettingsHost::Registry()->Groups())
			if (name.Equals(T(g), ESearchCase::IgnoreCase)) return g;
		return cs::String();
	}

	/// Why this machine may not change <paramref name="s"/> (a co-op client and a setting the host's values decide); empty when it may.
	FString Refusal(const cs::Ref<GameSetting>& s)
	{
		SpaceGod::Coop::CoopPeer* peer = SpaceGod::Coop::CoopPeer::Current();
		if (peer == nullptr || peer->IsHost() || !SpaceGod::Coop::CoopSettings::IsShared(s)) return FString();
		return FString::Printf(TEXT("%s is shared in co-op (%s): only the host changes it"), *T(s->Key), *T(s->Group));
	}

	FString Line(const cs::Ref<GameSetting>& s)
	{
		return FString::Printf(TEXT("%s = %s  (%s, %s)%s"), *T(s->Key), *T(s->ValueText()), *T(s->Label), *T(s->Group),
			s->IsDefault() ? TEXT("") : TEXT(" changed"));
	}

	void Get(const TArray<FString>& args, FOutputDevice& out)
	{
		if (args.Num() != 1) { out.Log(TEXT("sg.Get: usage: sg.Get <key>")); return; }
		auto s = FindSetting(args[0]);
		if (s == nullptr) { out.Logf(TEXT("sg.Get: no setting '%s' (sg.Find finds one)"), *args[0]); return; }
		out.Logf(TEXT("%s = %s  (profile text: %s)"), *T(s->Key), *T(s->ValueText()), *T(s->Format()));
		out.Logf(TEXT("  %s, in %s%s"), *T(s->Label), *T(s->Group), GameSettingsHost::Registry()->IsLockedIn(s->Key) ? TEXT(", locked in (shipped default)") : TEXT(""));
		if (auto t = cs::As<ToggleSetting>(s))
			out.Logf(TEXT("  default %s; on or off"), t->Default ? TEXT("On") : TEXT("Off"));
		else if (auto r = cs::As<RangeSetting>(s))
			out.Logf(TEXT("  default %s; %s to %s%s"), *T(cs::Fmt(r->Default, "R")), *T(cs::Fmt(r->Min, "R")), *T(cs::Fmt(r->Max, "R")),
				r->Step > 0 ? *FString::Printf(TEXT(" in steps of %s"), *T(cs::Fmt(r->Step, "R"))) : TEXT(""));
		else if (auto c = cs::As<ChoiceSetting>(s))
		{
			FString options;
			for (const auto& o : c->Options) options += (options.IsEmpty() ? TEXT("") : TEXT(", ")) + T(o);
			out.Logf(TEXT("  default %s; one of %s"), *T(c->Options[c->Default]), *options);
		}
		else if (auto x = cs::As<TextSetting>(s))
			out.Logf(TEXT("  default \"%s\"; at most %d characters"), *T(x->Default), x->MaxLength);
		else
			out.Logf(TEXT("  %s"), s->IsDefault() ? TEXT("at its default") : TEXT("changed from its default"));
		if (!s->Description.empty()) out.Logf(TEXT("  %s"), *T(s->Description));
	}

	/// Takes <paramref name="text"/> for <paramref name="s"/>; the reason when it is refused, empty when taken.
	FString Take(const cs::Ref<GameSetting>& s, const FString& text)
	{
		const cs::String value = C(text);
		if (auto t = cs::As<ToggleSetting>(s))
		{
			for (const TCHAR* on : { TEXT("on"), TEXT("true"), TEXT("1"), TEXT("yes") })
				if (text.Equals(on, ESearchCase::IgnoreCase)) { t->Set(true); return FString(); }
			for (const TCHAR* off : { TEXT("off"), TEXT("false"), TEXT("0"), TEXT("no") })
				if (text.Equals(off, ESearchCase::IgnoreCase)) { t->Set(false); return FString(); }
			return TEXT("a toggle takes on or off");
		}
		if (auto r = cs::As<RangeSetting>(s))
		{
			double v;
			if (!cs::Double::TryParse(value, v) || cs::Double::IsNaN(v) || cs::Double::IsInfinity(v)) return TEXT("not a number");
			if (v < r->Min || v > r->Max)
				return FString::Printf(TEXT("outside %s to %s"), *T(cs::Fmt(r->Min, "R")), *T(cs::Fmt(r->Max, "R")));
			r->Set(v);
			return FString();
		}
		if (auto c = cs::As<ChoiceSetting>(s))
		{
			FString options;
			for (int i = 0; i < c->Options.Count(); i++)
			{
				if (text.Equals(T(c->Options[i]), ESearchCase::IgnoreCase)) { c->Set(i); return FString(); }
				options += (i == 0 ? TEXT("") : TEXT(", ")) + T(c->Options[i]);
			}
			return FString::Printf(TEXT("not one of %s"), *options);
		}
		if (auto x = cs::As<TextSetting>(s))
		{
			if (x->Set(value)) return FString();
			int chars = 0; // UTF-8 code points, as TextSetting counts them
			for (char ch : value)
				if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) chars++;
			return chars > x->MaxLength ? FString::Printf(TEXT("at most %d characters"), x->MaxLength) : FString(TEXT("that value is refused"));
		}
		// Any other kind (a key binding): read as the profile reads it, then remembered and applied as its own change would be.
		const cs::String before = s->Format();
		if (!s->TryLoad(value)) return TEXT("the profile does not read that text as a value here");
		if (s->Format() != before)
		{
			s->RaiseChanged();
			if (s->Registry) s->Registry->Remember(*s);
		}
		return FString();
	}

	void Set(const TArray<FString>& args, FOutputDevice& out)
	{
		if (args.Num() < 2) { out.Log(TEXT("sg.Set: usage: sg.Set <key> <value...>")); return; }
		auto s = FindSetting(args[0]);
		if (s == nullptr) { out.Logf(TEXT("sg.Set: no setting '%s' (sg.Find finds one)"), *args[0]); return; }
		FString refusal = Refusal(s);
		if (refusal.IsEmpty())
		{
			TArray<FString> rest(args);
			rest.RemoveAt(0);
			refusal = Take(s, FString::Join(rest, TEXT(" ")));
		}
		if (!refusal.IsEmpty()) out.Logf(TEXT("sg.Set: refused: %s"), *refusal);
		else out.Log(Line(s));
	}

	void Toggle(const TArray<FString>& args, FOutputDevice& out)
	{
		if (args.Num() != 1) { out.Log(TEXT("sg.Toggle: usage: sg.Toggle <key>")); return; }
		auto s = FindSetting(args[0]);
		if (s == nullptr) { out.Logf(TEXT("sg.Toggle: no setting '%s' (sg.Find finds one)"), *args[0]); return; }
		auto t = cs::As<ToggleSetting>(s);
		if (t == nullptr) { out.Logf(TEXT("sg.Toggle: %s is not a toggle (sg.Set sets it)"), *T(s->Key)); return; }
		FString refusal = Refusal(s);
		if (!refusal.IsEmpty()) { out.Logf(TEXT("sg.Toggle: refused: %s"), *refusal); return; }
		t->Set(!t->Value);
		out.Log(Line(s));
	}

	void Reset(const TArray<FString>& args, FOutputDevice& out)
	{
		if (args.Num() < 1) { out.Log(TEXT("sg.Reset: usage: sg.Reset <key|group>")); return; }
		const auto& registry = GameSettingsHost::Registry();
		if (args.Num() == 1)
			if (auto s = FindSetting(args[0]))
			{
				FString refusal = Refusal(s);
				if (!refusal.IsEmpty()) { out.Logf(TEXT("sg.Reset: refused: %s"), *refusal); return; }
				registry->Reset(s);
				out.Log(Line(s));
				return;
			}
		const cs::String group = FindGroup(FString::Join(args, TEXT(" ")));
		if (group.empty()) { out.Logf(TEXT("sg.Reset: no setting or group '%s'"), *FString::Join(args, TEXT(" "))); return; }
		int reset = 0, refused = 0;
		for (const auto& s : registry->InGroup(group))
		{
			if (s->IsDefault()) continue;
			if (!Refusal(s).IsEmpty()) { refused++; continue; }
			registry->Reset(s);
			reset++;
		}
		out.Logf(TEXT("sg.Reset: %d setting(s) in %s back to their defaults"), reset, *T(group));
		if (refused > 0) out.Logf(TEXT("sg.Reset: refused %d shared in co-op: only the host changes them"), refused);
	}

	void Find(const TArray<FString>& args, FOutputDevice& out)
	{
		if (args.Num() < 1) { out.Log(TEXT("sg.Find: usage: sg.Find <text>")); return; }
		const FString text = FString::Join(args, TEXT(" "));
		const auto& registry = GameSettingsHost::Registry();
		std::set<cs::String> seen;
		int found = 0;
		for (const auto& s : registry->All())
			if (T(s->Key).Contains(text, ESearchCase::IgnoreCase) && seen.insert(s->Key).second) { out.Log(Line(s)); found++; }
		for (const auto& s : registry->Search(C(text)))
			if (seen.insert(s->Key).second) { out.Log(Line(s)); found++; }
		out.Logf(TEXT("sg.Find: %d setting(s) match '%s'"), found, *text);
	}

	using Verb = void (*)(const TArray<FString>&, FOutputDevice&);

	FConsoleCommandWithArgsAndOutputDeviceDelegate Run(const TCHAR* name, Verb verb)
	{
		return FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([name, verb](const TArray<FString>& args, FOutputDevice& out)
		{
			try { verb(args, out); }
			catch (const std::exception& e) { out.Logf(TEXT("%s: %s"), name, UTF8_TO_TCHAR(e.what())); }
		});
	}

	FAutoConsoleCommand GGetCommand(TEXT("sg.Get"),
		TEXT("US-T624: sg.Get <key> shows a setting's value, default, range or choices, group, and whether a shipped default locks it in."),
		Run(TEXT("sg.Get"), &Get));

	FAutoConsoleCommand GSetCommand(TEXT("sg.Set"),
		TEXT("US-T624: sg.Set <key> <value...> sets any setting live, as the Options menu does (tune.*, dev.*, player options; a key binding as Shift+K or Pad:A)."),
		Run(TEXT("sg.Set"), &Set));

	FAutoConsoleCommand GResetCommand(TEXT("sg.Reset"),
		TEXT("US-T624: sg.Reset <key|group> puts a setting, or every setting in a group, back to its default."),
		Run(TEXT("sg.Reset"), &Reset));

	FAutoConsoleCommand GToggleCommand(TEXT("sg.Toggle"),
		TEXT("US-T624: sg.Toggle <key> flips an on/off setting."),
		Run(TEXT("sg.Toggle"), &Toggle));

	FAutoConsoleCommand GFindCommand(TEXT("sg.Find"),
		TEXT("US-T624: sg.Find <text> lists the settings whose key, label, group, description or keywords match, with their values."),
		Run(TEXT("sg.Find"), &Find));
}
