#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Core { class TextSetting; }

namespace SpaceGod::Audio
{
	/// Settings > Audio > Output device (US-T612): the system default, or one of the devices the audio engine lists,
	/// switched live. A device that goes away plays on the system default until it comes back; the saved choice is kept.
	///
	/// A view setting: each machine's own, never shared in co-op. A scripted run, or one with no sound, stays on the
	/// default, so shots and digests stay alike.
	class AudioOutput
	{
	public:
		/// audio.outputDevice: the chosen device's id, empty for the system default. Registered by GameSettingsHost
		/// after GameAudio's.
		static void Declare();
		static const cs::Ref<Core::TextSetting>& Device();

		/// What the row shows: "System default", the chosen device's name, or that it is not connected.
		static cs::String Shown();
		/// Steps the choice over System default and each listed device.
		static void Cycle(int step);
	};
}
