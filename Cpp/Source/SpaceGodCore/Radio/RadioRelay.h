#pragma once
// US-T645: interstellar relay bulletins (US-T577). Nothing crosses between the stars as a wavefront: a report the Newswire
// delivers to the listener's system (Newswire::Received) is rebroadcast there on the system's Relay channel, at most
// Newswire::HeadlinesPerReport headlines a report and each headline once, from the market the relay beams it in to (the
// one Newswire::InnerLightDays measures), else from the system's gate. The message's SentJd is when the report reached that
// market; its Args carry how old the report already was then, so the label says the report's real age (the gate route
// included): "relayed, sent 3.2 days ago".
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests, and nothing draws from
// the simulation's random stream.

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"

#include <unordered_map>
#include <vector>

namespace SpaceGod::Core
{
	class StarSystem;
	class SystemReport;

	class SPACEGODCORE_API RadioRelay final
	{
	public:
		/// <summary>The Args slot holding the report's age when it was rebroadcast, seconds.</summary>
		static constexpr int ArgAgeSeconds = 0;

		struct Bulletin
		{
			RadioMessage Message;
			cs::String Text;
		};

		/// <summary>
		/// The bulletins for <paramref name="report"/>, just delivered to <paramref name="at"/> (galaxy index
		/// <paramref name="systemId"/>): its newest headlines not relayed here before, at most HeadlinesPerReport, oldest
		/// first, appended to <paramref name="out"/> with the text "relayed from &lt;system&gt;: &lt;headline&gt;". A report
		/// from the charts (what the system knew at the start) is not news on the air. O(headlines + markets).
		/// </summary>
		void Bulletins(const cs::Ref<StarSystem>& at, int systemId, const SystemReport& report, std::vector<Bulletin>& out);

		/// <summary>How old the relayed report is at <paramref name="nowJd"/>, days: its age when rebroadcast plus the time since.</summary>
		static double ReportAgeDays(const RadioMessage& message, double nowJd);

		/// <summary>"relayed, sent X ago", X the report's age at <paramref name="nowJd"/>.</summary>
		static cs::String Label(const RadioMessage& message, double nowJd);

		/// <summary>Forgets which headlines were relayed (a load, a new listener).</summary>
		void Clear()
		{
			lastHeadlineJd.clear();
			channels.Clear();
		}

	private:
		RadioChannels channels;
		/// <summary>The newest headline relayed from each source system.</summary>
		std::unordered_map<const StarSystem*, double> lastHeadlineJd;
	};
}
