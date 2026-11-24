#include "Voice/VoiceArbiter.h"

#include "Audio/GameAudio.h"
#include "InterfaceOptions.h"
#include "Voice/RadioEffect.h"
#include "Voice/TtsService.h"

#include "HAL/PlatformTime.h"

#include <array>
#include <utility>

namespace SpaceGod::Voice
{
	namespace
	{
		struct Slot
		{
			RadioLine Line;
			/// FPlatformTime seconds it was heard.
			double Heard = 0;
			bool Prefetched = false;
		};

		/// US-T704: an extra radio lane beside CommsVoice (Max voices above 1): one line synthesized, then played.
		struct Lane
		{
			TtsId Id = 0;
			/// When the line was requested (0: the lane holds none).
			double Since = 0;
			bool Answered = false;
			Audio::SoundClip Clip;
			float Volume = 1.0f;
			bool Urgent = false;
			Audio::LoopId Playing = 0;
			/// When the line playing ends, the gap after it included.
			double BusyUntil = 0;
		};

		/// Game thread only.
		struct Arbiter
		{
			std::array<Lane, VoiceArbiter::MaxLanes - 1> Lanes;
			/// Best first; the first Count are in use.
			std::array<Slot, VoiceArbiter::Capacity> Slots;
			int Count = 0;
			/// Radio lines handed to CommsVoice and not yet over (CommsVoice idle again).
			int RadioPlaying = 0;
			/// Whether the radio line playing is urgent (an urgent line does not cut another).
			bool PlayingUrgent = false;
			std::function<void(const RadioLine&)> Digest;
		};

		Arbiter& A()
		{
			static Arbiter s;
			return s;
		}

		/// Whether a plays before b.
		bool Before(const RadioLine& a, const RadioLine& b)
		{
			if (a.Urgent != b.Urgent) return a.Urgent;
			if (a.Score != b.Score) return a.Score > b.Score;
			return a.Priority > b.Priority;
		}

		void RemoveAt(Arbiter& s, int i)
		{
			for (int j = i; j + 1 < s.Count; ++j) s.Slots[j] = std::move(s.Slots[j + 1]);
			s.Slots[--s.Count] = Slot();
		}

		SpokenLine Spoken(const RadioLine& line)
		{
			SpokenLine spoken = line.Line;
			spoken.Priority = line.Priority;
			return spoken;
		}

		/// Ends an extra lane's line (requested or playing); <paramref name="urgentToo"/> false keeps an urgent one.
		void CutLanes(Arbiter& s, bool urgentToo)
		{
			for (Lane& lane : s.Lanes)
			{
				if (!lane.Since && !lane.Playing) continue;
				if (lane.Urgent && !urgentToo) continue;
				if (lane.Id) TtsService::Cancel(lane.Id);
				if (lane.Playing) Audio::GameAudio::StopLoop(lane.Playing, 0.02f);
				lane = Lane();
			}
		}

		/// Plays the extra lanes' answered lines; true when a lane is free for a new one.
		bool TickLanes(Arbiter& s, double now, int lanes, Lane*& free)
		{
			free = nullptr;
			for (int i = 0; i < static_cast<int>(s.Lanes.size()); ++i)
			{
				Lane& lane = s.Lanes[i];
				if (lane.Since && (lane.Answered || now - lane.Since > CommsVoice::ReadyWithin))
				{
					if (!lane.Answered) TtsService::Cancel(lane.Id);
					const Audio::SoundClip clip = lane.Answered ? lane.Clip : Audio::SoundClip();
					lane.Id = 0;
					lane.Since = 0;
					lane.Answered = false;
					lane.Clip = Audio::SoundClip();
					if (clip.IsValid())
					{
						lane.Playing = Audio::GameAudio::Play2DStoppable(clip, Audio::AudioBus::Voice, lane.Volume);
						lane.BusyUntil = now + clip.Seconds() + CommsVoice::Gap;
					}
				}
				if (!lane.Since && now >= lane.BusyUntil) lane.Playing = 0;
				if (i + 1 < lanes && !free && !lane.Since && !lane.Playing) free = &lane;
			}
			return free != nullptr;
		}

		void StartLane(Lane& lane, const RadioLine& line)
		{
			SpokenLine spoken = Spoken(line);
			lane = Lane();
			lane.Since = FPlatformTime::Seconds();
			lane.Volume = spoken.Volume;
			lane.Urgent = line.Urgent;
			TtsRequest request { MoveTemp(spoken.Text), MoveTemp(spoken.Voice), spoken.Priority, spoken.Radio && Audio::GameAudio::RadioEffectOn() };
			lane.Id = TtsService::Request(MoveTemp(request), [](TtsId id, const Audio::SoundClip& clip)
			{
				for (Lane& l : A().Lanes)
					if (l.Id == id)
					{
						l.Answered = true;
						l.Clip = clip;
					}
			});
			if (!lane.Id) lane.Answered = true;
		}
	}

	int VoiceArbiter::MaxPlaying() { return InterfaceOptions::RadioVoices(); }

	void VoiceArbiter::Comms(SpokenLine line)
	{
		if (!Audio::GameAudio::Enabled() || !Audio::GameAudio::SpokenComms() || line.Text.empty()) return;
		Arbiter& s = A();
		CutLanes(s, true); // US-T704: a comms line cuts every radio lane
		if (s.RadioPlaying > 0)
		{
			CommsVoice::Cut();
			s.RadioPlaying = 0;
			s.PlayingUrgent = false;
		}
		CommsVoice::Speak(std::move(line));
	}

	void VoiceArbiter::Radio(RadioLine line)
	{
		if (!Audio::GameAudio::Enabled() || !Audio::GameAudio::SpokenComms() || line.Line.Text.empty()) return;
		Arbiter& s = A();
		if (s.Count >= Capacity)
		{
			// Full: the worst of the waiting lines and the new one goes to the digest.
			Slot& worst = s.Slots[s.Count - 1];
			if (!Before(line, worst.Line))
			{
				if (s.Digest) s.Digest(line);
				return;
			}
			RadioLine evicted = std::move(worst.Line);
			--s.Count;
			s.Slots[s.Count] = Slot();
			if (s.Digest) s.Digest(evicted);
		}
		int at = s.Count;
		while (at > 0 && Before(line, s.Slots[at - 1].Line))
		{
			s.Slots[at] = std::move(s.Slots[at - 1]);
			--at;
		}
		const bool urgent = line.Urgent;
		s.Slots[at] = Slot{ std::move(line), FPlatformTime::Seconds(), false };
		++s.Count;

		if (urgent && s.RadioPlaying > 0 && !s.PlayingUrgent)
		{
			const Audio::SoundClip& squelch = RadioEffect::SquelchClip();
			Audio::GameAudio::Play2D(squelch, Audio::AudioBus::Voice);
			CommsVoice::Cut(squelch.Seconds() + CommsVoice::Gap);
			s.RadioPlaying = 0;
			CutLanes(s, false); // US-T704: and the non-urgent extra lanes
		}
	}

	void VoiceArbiter::Tick(float warpRate)
	{
		Arbiter& s = A();
		if (!Audio::GameAudio::Enabled() || !Audio::GameAudio::SpokenComms())
		{
			while (s.Count > 0) RemoveAt(s, s.Count - 1);
			CutLanes(s, true);
			s.RadioPlaying = 0;
			s.PlayingUrgent = false;
			return;
		}

		const double now = FPlatformTime::Seconds();
		const bool quiet = warpRate > WarpQuiet;
		for (int i = s.Count - 1; i >= 0; --i)
		{
			const Slot& slot = s.Slots[i];
			if (now - slot.Heard > StaleSeconds || (quiet && !slot.Line.Urgent)) RemoveAt(s, i);
		}

		for (int i = 0; i < s.Count && i < PrefetchTop; ++i)
		{
			Slot& slot = s.Slots[i];
			if (slot.Prefetched) continue;
			slot.Prefetched = true;
			CommsVoice::Prefetch(Spoken(slot.Line));
		}

		// US-T704: the extra lanes (Max voices) play what is ready, and take the next best lines while no comms line speaks.
		const int lanes = MaxPlaying();
		Lane* free = nullptr;
		TickLanes(s, now, lanes, free);
		const bool idle = CommsVoice::Idle();
		if (idle)
		{
			s.RadioPlaying = 0;
			s.PlayingUrgent = false;
			if (s.Count > 0)
			{
				Slot& top = s.Slots[0];
				s.PlayingUrgent = top.Line.Urgent;
				CommsVoice::Speak(Spoken(top.Line));
				RemoveAt(s, 0);
				++s.RadioPlaying;
			}
		}
		if (!idle && s.RadioPlaying == 0) return; // a comms line has the voice
		while (s.Count > 0 && TickLanes(s, now, lanes, free))
		{
			StartLane(*free, s.Slots[0].Line);
			RemoveAt(s, 0);
		}
	}

	void VoiceArbiter::Stop()
	{
		Arbiter& s = A();
		while (s.Count > 0) RemoveAt(s, s.Count - 1);
		CutLanes(s, true);
		if (s.RadioPlaying > 0) CommsVoice::Cut();
		s.RadioPlaying = 0;
		s.PlayingUrgent = false;
	}

	void VoiceArbiter::SetDigest(std::function<void(const RadioLine&)> digest) { A().Digest = std::move(digest); }

	int VoiceArbiter::Waiting() { return A().Count; }
}
