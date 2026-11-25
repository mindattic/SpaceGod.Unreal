// US-T812: the mission board (Core's Combat/PilotCareer.h): the pilot's career across the top, the missions on offer
// (made from what the A-life is doing) with the briefing of the one picked, and the newest debriefing, FreeSpace-style.
// Accept takes a mission up through UnitySpaceApp::AcceptMission, which drops the player into its fight through "Fly this".

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "Combat/PilotCareer.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::UI
{
	void Hud::DrawMissions()
	{
		auto career = Core::PilotCareer::Find(app->World());
		constexpr float w = 760, rowH = 22, textH = 230;
		int rows = career != nullptr ? Mathf::Min(career->Missions.Count(), Core::PilotCareer::MaxOpen) : 0;
		float h = 74 + Mathf::Max(1, rows) * rowH + 8 + textH;
		auto r = Place(w, h); // the mission board's drawer (floating, centred)
		if (!missionText)
		{
			missionText = cs::New<GUIStyle>(*small);
			missionText->wordWrap = true;
		}
		auto old = GUI::contentColor;
		if (career == nullptr)
		{
			GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22), cs::String("MISSIONS"), title);
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x + 12, r.y + 30, w - 24, 18), cs::String("No orders yet: the board fills as the war goes on around you."), small);
			GUI::contentColor = old;
			return;
		}
		career->DebriefPending = false;

		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22),
			cs::Cat("MISSIONS  ·  ", Core::PilotCareer::RankName(career->Rank), " \"", career->Callsign, "\""), title);
		GUI::contentColor = Dim;
		cs::String medals;
		for (const auto& m : career->Medals) medals += cs::Cat(medals.empty() ? "" : ", ", m->Name);
		GUI::Label(Rect(r.x + 12, r.y + 30, w - 24, 18),
			cs::Cat("Merit ", cs::Fmt(career->Merit, "0"), "   Sorties ", career->Sorties, " (", career->Victories, " won)   Kills ", career->Kills,
				" + ", career->CraftKills, " craft   Capitals ", career->CapitalsCrippled, "   Missions ", career->MissionsWon, "/",
				career->MissionsWon + career->MissionsFailed), small);
		GUI::Label(Rect(r.x + 12, r.y + 48, w - 24, 18), medals.empty() ? cs::String("Medals: none yet") : cs::Cat("Medals: ", medals), small);

		float y = r.y + 70;
		if (rows == 0)
		{
			GUI::Label(Rect(r.x + 12, y, w - 24, rowH), cs::String("No missions on offer: they come from the fights and the lanes around you."), small);
		}
		for (int i = 0; i < rows; i++)
		{
			auto m = career->Missions[i];
			bool taken = m->State == Core::MissionState::Accepted;
			GUI::contentColor = taken ? UiStyle::Warning : m->Id == missionShown ? Color::white() : Dim;
			cs::String patron = m->Patron != nullptr ? m->Patron->Name : cs::String("-");
			if (GUI::Button(Rect(r.x + 12, y, w - 220, rowH - 2), cs::Cat(m->Title, "   ", Core::ToString(m->Kind), "   for ", patron, taken ? "   (accepted)" : ""), button))
				missionShown = m->Id;
			if (GUI::Button(Rect(r.x + w - 200, y, 88, rowH - 2), "Brief", button)) missionShown = m->Id;
			if (GUI::Button(Rect(r.x + w - 104, y, 92, rowH - 2), taken ? "Launch" : "Accept", button)) app->AcceptMission(m->Id);
			y += rowH;
		}
		if (rows == 0) y += rowH;

		// The briefing of the mission picked, else the newest debriefing.
		auto shown = missionShown != 0 ? career->OpenMission(missionShown) : nullptr;
		cs::String missionBody = shown != nullptr ? shown->Briefing
			: career->History.Count() > 0 ? career->History[career->History.Count() - 1]->Debriefing
			: cs::String("Pick a mission to read its briefing.");
		GUI::contentColor = Color::white();
		auto view = Rect(r.x + 12, y + 8, w - 24, textH);
		float contentH = Mathf::Max(textH, missionText->CalcHeight(GUIContent(missionBody), view.width - 18));
		auto content = Rect(0, 0, view.width - 18, contentH);
		missionScroll = GUI::BeginScrollView(view, missionScroll, content);
		GUI::Label(content, missionBody, missionText);
		GUI::EndScrollView();
		GUI::contentColor = old;
	}
}
