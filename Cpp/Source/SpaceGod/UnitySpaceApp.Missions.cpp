// US-T812: missions taken up from the board (Core's Combat/PilotCareer.h) and flown through "Fly this" (US-T806), and
// sg.Missions: sg.Missions lists the pilot's career and the board, sg.Missions accept <n> takes mission n up (dropping into
// its fight when one is under way), sg.Missions callsign <name> renames the pilot, sg.Missions show opens the board.
// Taking a mission up changes the world, so like launching from the god view it is single player only for now.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

#include "Combat/Battle.h"
#include "Combat/PilotCareer.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace SpaceGod
{
	void UnitySpaceApp::AcceptMission(int id)
	{
		auto career = Core::PilotCareer::Find(World());
		auto m = career != nullptr ? career->OpenMission(id) : nullptr;
		if (m == nullptr)
		{
			ShowToast("That mission is no longer on the board");
			return;
		}
		if (Coop::CoopPeer::Current() != nullptr)
		{
			ShowToast("Missions are taken up in single player only for now");
			return;
		}
		if (!career->Accept(m)) return;
		if (m->Battle != nullptr && !m->Battle->Over)
		{
			// Beside a unit of the mission's side still in it (else anywhere in the fight): "Fly this" seats the player there.
			cs::Ref<Core::BattleUnit> at;
			for (const auto& u : m->Battle->Units)
				if (u->Side == m->Side && u->Present())
				{
					at = u;
					break;
				}
			ShowToast(cs::Cat(m->Title, ": launch"));
			ShowMissions = false;
			FlyThis(at != nullptr ? Target::OfUnit(m->Battle, at) : Target::OfBody(m->Battle->Body));
			return;
		}
		ShowToast(cs::Cat(m->Title, " accepted: fly to ", m->Ship != nullptr ? m->Ship->Name : cs::String("the marker"), " (its marker is on the HUD)"));
	}
}

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* MissionsApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	FString MissionsU(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	void MissionsCommand(const TArray<FString>& args)
	{
		auto* app = MissionsApp();
		if (app == nullptr || app->Galaxy == nullptr || app->World() == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Missions: no game running"));
			return;
		}
		if (args.Num() == 0)
		{
			auto c = Core::PilotCareer::Find(app->World());
			if (c == nullptr)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Missions: no career yet (the board opens once you are in the game)"));
				return;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Missions: %s \"%s\", merit %.0f, %d sortie(s), %d kill(s) + %d craft, %d medal(s), missions %d won / %d failed"),
				UTF8_TO_TCHAR(Core::PilotCareer::RankName(c->Rank)), *MissionsU(c->Callsign), c->Merit, c->Sorties, c->Kills, c->CraftKills,
				c->Medals.Count(), c->MissionsWon, c->MissionsFailed);
			for (const auto& m : c->Missions)
				UE_LOG(LogSpaceGod, Display, TEXT("  %d [%s] %s (%s)"), m->Id, UTF8_TO_TCHAR(Core::ToString(m->State)), *MissionsU(m->Title),
					UTF8_TO_TCHAR(Core::ToString(m->Kind)));
			return;
		}
		if (args[0].Equals(TEXT("show"), ESearchCase::IgnoreCase))
		{
			app->ShowMissions = true;
			return;
		}
		if (args.Num() >= 2 && args[0].Equals(TEXT("accept"), ESearchCase::IgnoreCase))
		{
			app->AcceptMission(FCString::Atoi(*args[1]));
			return;
		}
		if (args.Num() >= 2 && args[0].Equals(TEXT("callsign"), ESearchCase::IgnoreCase))
		{
			if (Coop::CoopPeer::Current() != nullptr)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Missions: the callsign is set in single player only for now"));
				return;
			}
			auto c = Core::PilotCareer::Of(app->World());
			if (c == nullptr) return;
			FString name = FString::Join(TArray<FString>(args.GetData() + 1, args.Num() - 1), TEXT(" "));
			c->Callsign = cs::String(TCHAR_TO_UTF8(*name));
			app->ShowToast(cs::Cat("Callsign: ", c->Callsign));
			return;
		}
		UE_LOG(LogSpaceGod, Warning, TEXT("sg.Missions: usage: sg.Missions [show | accept <n> | callsign <name>]"));
	}

	FAutoConsoleCommand GMissionsCommand(
		TEXT("sg.Missions"),
		TEXT("US-T812: the pilot's career and the mission board: sg.Missions [show | accept <n> | callsign <name>]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { MissionsCommand(args); }));
}
