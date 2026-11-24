#pragma once
// US-T674: what the local listener has heard on the radio, as lines to show (the HUD's radio log, the Logs screen's Radio
// tab). A sink on the app's RadioHost, held by the app after the host so it is gone first; the HUD only reads it. Each
// arrival is realised once, when heard: "[<channel> <MHz>] <sender>: <text>  - sent <light time> ago, <range>".
// View-local like the host: nothing here enters the simulation or saves.

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioHost.h"

#include <deque>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Radio
{
	class RadioHeard final : public IRadioSink
	{
	public:
		/// <summary>Lines kept, oldest dropped first.</summary>
		static constexpr int Capacity = 200;

		/// <summary>How a line is coloured: PlayerOptions' Hostile, Wary, Friendly or Neutral colour.</summary>
		enum class Tone : uint8_t { Neutral, Hostile, Wary, Friendly };

		struct HeardLine
		{
			cs::String Text;
			Tone Kind = Tone::Neutral;
			double ArrivalJd = 0;
			/// <summary>Time::unscaledTime when it was heard.</summary>
			float Heard = 0;
		};

		explicit RadioHeard(UnitySpaceApp& app);
		~RadioHeard() override;
		RadioHeard(const RadioHeard&) = delete;
		RadioHeard& operator=(const RadioHeard&) = delete;

		void OnRadioArrival(const RadioHost& host, const Core::RadioArrival& arrival, const Core::RadioReceptionResult& reception) override;

		/// <summary>Oldest first.</summary>
		const std::deque<HeardLine>& Lines() const { return lines; }
		/// <summary>The clock went back (a rewind or a load): drops the lines that arrived after <paramref name="jd"/>.</summary>
		void DropAfter(double jd);
		void Clear() { lines.clear(); }

	private:
		UnitySpaceApp& app;
		Core::RadioChannels channels;
		std::deque<HeardLine> lines;
	};
}
