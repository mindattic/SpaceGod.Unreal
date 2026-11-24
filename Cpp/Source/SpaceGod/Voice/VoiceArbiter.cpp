#include "Voice/VoiceArbiter.h"

#include "Audio/GameAudio.h"
#include "Voice/RadioEffect.h"

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

		/// Game thread only.
		struct Arbiter
		{
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
	}

	void VoiceArbiter::Comms(SpokenLine line)
	{
		if (!Audio::GameAudio::Enabled() || line.Text.empty()) return;
		Arbiter& s = A();
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
		if (!Audio::GameAudio::Enabled() || line.Line.Text.empty()) return;
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
		}
	}

	void VoiceArbiter::Tick(float warpRate)
	{
		Arbiter& s = A();
		if (!Audio::GameAudio::Enabled())
		{
			while (s.Count > 0) RemoveAt(s, s.Count - 1);
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

		if (!CommsVoice::Idle()) return;
		s.RadioPlaying = 0;
		s.PlayingUrgent = false;
		if (s.Count == 0 || s.RadioPlaying >= MaxPlaying) return;
		Slot& top = s.Slots[0];
		s.PlayingUrgent = top.Line.Urgent;
		CommsVoice::Speak(Spoken(top.Line));
		RemoveAt(s, 0);
		++s.RadioPlaying;
	}

	void VoiceArbiter::Stop()
	{
		Arbiter& s = A();
		while (s.Count > 0) RemoveAt(s, s.Count - 1);
		if (s.RadioPlaying > 0) CommsVoice::Cut();
		s.RadioPlaying = 0;
		s.PlayingUrgent = false;
	}

	void VoiceArbiter::SetDigest(std::function<void(const RadioLine&)> digest) { A().Digest = std::move(digest); }

	int VoiceArbiter::Waiting() { return A().Count; }
}
