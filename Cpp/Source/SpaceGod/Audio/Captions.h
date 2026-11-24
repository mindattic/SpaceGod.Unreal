#pragma once

#include "Cs/Cs.h"

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// US-T636: the caption queue: a few short lines of what was heard, newest last, each fading after its own seconds.
	/// Fed by comms lines (Poll watches the app's comms log) and by the interface's alert sounds (InterfaceSounds raises
	/// one per alert it plays), and later by spoken radio; Settings > Accessibility > Captions says which kinds it takes
	/// (none by default, and none in a scripted run). Local to this machine; the HUD draws it (US-T637).
	namespace Captions
	{
		enum class CaptionKind { Comms, Sound };

		struct Caption
		{
			cs::String Speaker; // who said it ("" for a sound)
			cs::String Text;
			CaptionKind Kind = CaptionKind::Comms;
			float SecondsLeft = 0;
		};

		/// At most this many captions at once; a new one past it drops the oldest.
		constexpr int Kept = 4;

		/// Adds a caption when Captions takes its kind (a repeat of the newest only restarts its time).
		void Raise(CaptionKind kind, const cs::String& speaker, const cs::String& text);

		/// Once a frame (from InterfaceSounds::Poll): ages the queue and captions comms lines new since the last call.
		void Poll(UnitySpaceApp* app);

		/// What is showing, oldest first.
		const cs::List<Caption>& Showing();
	}
}
