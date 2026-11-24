#include "Audio/Captions.h"
#include "AccessibilityOptions.h"
#include "UnitySpaceApp.h"

#include "Comms/Comms.h"
#include "World.h"

#include "HAL/PlatformTime.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Audio::Captions
{
	namespace
	{
		cs::List<Caption> showing;

		// What Poll compares frame to frame.
		const UnitySpaceApp* seenApp = nullptr;
		const Core::World* seenWorld = nullptr;
		int seenCount = 0;
		CommsLine seenLast;
		double polledAt = 0;

		bool Takes(CaptionKind kind)
		{
			auto level = AccessibilityOptions::CaptionsShown();
			if (level == AccessibilityOptions::CaptionLevel::Off) return false;
			return kind == CaptionKind::Comms || level == AccessibilityOptions::CaptionLevel::CommsAndSounds;
		}

		/// Long enough to read: 2 s plus a beat per word-ish, 2.5..8 s; a sound's tag 2.5 s.
		float SecondsFor(CaptionKind kind, const cs::String& text)
		{
			if (kind == CaptionKind::Sound) return 2.5f;
			return cs::Math::Max(2.5f, cs::Math::Min(8.0f, 2.0f + 0.06f * static_cast<float>(text.size())));
		}

		bool Same(const CommsLine& a, const CommsLine& b) { return a.Who == b.Who && a.Text == b.Text && a.FromSpeaker == b.FromSpeaker; }

		void Remember(const cs::List<CommsLine>& log)
		{
			seenCount = log.Count();
			seenLast = log.Count() > 0 ? log[log.Count() - 1] : CommsLine();
		}
	}

	void Raise(CaptionKind kind, const cs::String& speaker, const cs::String& text)
	{
		if (text.empty() || !Takes(kind)) return;
		const float seconds = SecondsFor(kind, text);
		if (showing.Count() > 0)
		{
			auto& newest = showing[showing.Count() - 1];
			if (newest.Kind == kind && newest.Speaker == speaker && newest.Text == text)
			{
				newest.SecondsLeft = seconds;
				return;
			}
		}
		showing.Add(Caption{ speaker, text, kind, seconds });
		while (showing.Count() > Kept) showing.RemoveAt(0);
	}

	void Poll(UnitySpaceApp* app)
	{
		const double now = FPlatformTime::Seconds();
		const float dt = polledAt > 0 ? static_cast<float>(cs::Math::Min(1.0, now - polledAt)) : 0.0f;
		polledAt = now;
		for (int i = showing.Count() - 1; i >= 0; i--)
		{
			showing[i].SecondsLeft -= dt;
			if (showing[i].SecondsLeft <= 0) showing.RemoveAt(i);
		}

		if (app == nullptr || app->Galaxy == nullptr || app->ActiveSystem == nullptr)
		{
			seenApp = nullptr;
			showing.Clear();
			return;
		}
		const auto& log = app->CommsLog();
		const Core::World* world = app->World().get();
		// A new world (a load, a new game, a co-op sync) starts afresh, and with captions off nothing piles up to show later.
		if (seenApp != app || seenWorld != world || AccessibilityOptions::CaptionsShown() == AccessibilityOptions::CaptionLevel::Off)
		{
			seenApp = app;
			seenWorld = world;
			if (AccessibilityOptions::CaptionsShown() == AccessibilityOptions::CaptionLevel::Off) showing.Clear();
			Remember(log);
			return;
		}
		if (log.Count() == seenCount && (log.Count() == 0 || Same(log[log.Count() - 1], seenLast))) return;

		// The lines after the last one seen (the log drops its oldest past its cap, so find that line from the end); a log
		// cleared or rewritten past recognition captions only its newest lines.
		int first = cs::Math::Max(0, log.Count() - Kept);
		if (seenCount > 0)
			for (int i = log.Count() - 1; i >= 0; i--)
				if (Same(log[i], seenLast))
				{
					first = cs::Math::Max(i + 1, log.Count() - Kept);
					break;
				}
		for (int i = first; i < log.Count(); i++)
		{
			const auto& line = log[i];
			// The other party's words, not the player's own nor the link's system lines.
			if (!line.FromSpeaker && line.Who != "comms") Raise(CaptionKind::Comms, line.Who, line.Text);
		}
		Remember(log);
	}

	const cs::List<Caption>& Showing() { return showing; }
}
