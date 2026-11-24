#include "Voice/CommsVoice.h"

#include "Audio/GameAudio.h"
#include "Voice/TtsService.h"

#include "HAL/PlatformTime.h"

namespace SpaceGod::Voice
{
	namespace
	{
		struct QueuedLine
		{
			TtsId Id = 0;
			/// Answered by TtsService (an invalid clip when it could not be spoken).
			bool Answered = false;
			Audio::SoundClip Clip;
		};

		/// Game thread only.
		struct Playback
		{
			/// In the order spoken.
			TArray<QueuedLine> Lines;
			/// When the line at the front got its turn (0: not yet).
			double TurnSince = 0;
			/// When the line playing ends, the gap after it included.
			double BusyUntil = 0;
		};

		Playback& P()
		{
			static Playback s;
			return s;
		}

		QueuedLine* FindLine(TtsId id)
		{
			for (QueuedLine& line : P().Lines)
				if (line.Id == id) return &line;
			return nullptr;
		}
	}

	void CommsVoice::Speak(SpokenLine line)
	{
		if (!Audio::GameAudio::Enabled() || line.Text.empty()) return;
		Playback& s = P();
		if (s.Lines.Num() >= Capacity)
		{
			TtsService::Cancel(s.Lines[0].Id);
			s.Lines.RemoveAt(0);
			s.TurnSince = 0;
		}
		TtsRequest request { MoveTemp(line.Text), MoveTemp(line.Voice), line.Priority, line.Radio };
		QueuedLine& waiting = s.Lines.AddDefaulted_GetRef();
		waiting.Id = TtsService::Request(MoveTemp(request), [](TtsId id, const Audio::SoundClip& clip)
		{
			if (QueuedLine* answered = FindLine(id))
			{
				answered->Answered = true;
				answered->Clip = clip;
			}
		});
		if (!waiting.Id) waiting.Answered = true;
	}

	void CommsVoice::Tick()
	{
		Playback& s = P();
		if (s.Lines.IsEmpty()) return;
		if (!Audio::GameAudio::Enabled())
		{
			Stop();
			return;
		}
		const double now = FPlatformTime::Seconds();
		if (now < s.BusyUntil) return;
		while (!s.Lines.IsEmpty())
		{
			QueuedLine& front = s.Lines[0];
			if (!s.TurnSince) s.TurnSince = now;
			if (!front.Answered && now - s.TurnSince < ReadyWithin) return;
			if (!front.Answered) TtsService::Cancel(front.Id);
			const Audio::SoundClip clip = front.Answered ? front.Clip : Audio::SoundClip();
			s.Lines.RemoveAt(0);
			s.TurnSince = 0;
			if (clip.IsValid())
			{
				Audio::GameAudio::Play2D(clip, Audio::AudioBus::Voice);
				s.BusyUntil = now + clip.Seconds() + Gap;
				return;
			}
		}
	}

	void CommsVoice::Stop()
	{
		Playback& s = P();
		for (const QueuedLine& line : s.Lines) TtsService::Cancel(line.Id);
		s.Lines.Reset();
		s.TurnSince = 0;
	}
}
