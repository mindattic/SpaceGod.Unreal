// US-T625: sg.Help [filter], every console object whose name starts sg. with its help text, read from the console manager
// itself so a new command is listed the moment it is registered. A filter keeps those whose name or help contains it.

#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

namespace
{
	void Help(const TArray<FString>& args, FOutputDevice& out)
	{
		const FString filter = FString::Join(args, TEXT(" "));
		TArray<TPair<FString, FString>> found;
		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([&](const TCHAR* name, IConsoleObject* object)
		{
			const FString help(object->GetHelp());
			if (filter.IsEmpty() || FCString::Stristr(name, *filter) != nullptr || help.Contains(filter, ESearchCase::IgnoreCase))
				found.Emplace(name, help);
		}), TEXT("sg."));
		found.Sort([](const TPair<FString, FString>& a, const TPair<FString, FString>& b) { return a.Key < b.Key; });
		for (const auto& [name, help] : found)
			out.Logf(TEXT("%s  %s"), *name, *help);
		if (filter.IsEmpty()) out.Logf(TEXT("sg.Help: %d command(s)"), found.Num());
		else out.Logf(TEXT("sg.Help: %d command(s) match '%s'"), found.Num(), *filter);
	}

	FAutoConsoleCommand GHelpCommand(TEXT("sg.Help"),
		TEXT("US-T625: sg.Help [filter] lists every sg.* console command with its help; a filter keeps those whose name or help contains it."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&Help));
}
