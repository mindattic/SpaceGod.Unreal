#pragma once

#include "Voice/CommsVoice.h"

#include <functional>

namespace SpaceGod::Voice
{
	/// One radio line overheard (chatter, distress, traffic) waiting for its turn to be read aloud.
	struct RadioLine
	{
		/// Read aloud through CommsVoice; its Priority is set from Priority below.
		SpokenLine Line;
		/// How much it matters to the player now; the higher plays first.
		float Score = 0;
		/// Breaks a tie in Score; also the synthesis priority.
		int Priority = 0;
		/// The channel it was heard on (the log, the digest).
		int ChannelId = 0;
		/// A distress call on Guard, or a line addressed to the player: kept under warp, and it cuts a non-urgent radio
		/// line playing (with a squelch click).
		bool Urgent = false;
	};

	/// Decides which spoken line gets the one voice (US-T684). Comms lines (the comms panel, encounters) always come
	/// first: they go straight to CommsVoice and cut a radio line playing. Radio lines wait in a fixed list of Capacity,
	/// best first (urgent, then Score, then Priority); a line arriving at a full list evicts the worst to the digest
	/// hook. Tick starts the best radio line only while CommsVoice is idle, so at most one plays at a time. Game thread
	/// only, view-local, silent while GameAudio is disabled (like CommsVoice).
	class VoiceArbiter
	{
	public:
		/// A comms line: spoken next, ahead of every radio line; a radio line playing is cut.
		static void Comms(SpokenLine line);
		/// A radio line into the waiting list; an urgent one cuts a non-urgent radio line playing with a squelch.
		static void Radio(RadioLine line);
		/// Game thread, every frame, before CommsVoice::Tick. <paramref name="warpRate"/> is the time warp (1: real
		/// time): above WarpQuiet only urgent lines are kept.
		static void Tick(float warpRate);
		/// Drops every waiting radio line and cuts the one playing (a scene change).
		static void Stop();

		/// Where an evicted radio line goes (a summary in the log, say); none set, it is dropped.
		static void SetDigest(std::function<void(const RadioLine&)> digest);

		/// How many radio lines wait (the audit).
		static int Waiting();

		static constexpr int Capacity = 8;
		/// Radio lines playing at once.
		static constexpr int MaxPlaying = 1;
		/// A radio line not started within this many real seconds is dropped.
		static constexpr double StaleSeconds = 20.0;
		/// Above this warp only urgent radio lines are kept.
		static constexpr float WarpQuiet = 10.0f;
		/// The best few radio lines are synthesized ahead of their turn, once each.
		static constexpr int PrefetchTop = 3;
	};
}
