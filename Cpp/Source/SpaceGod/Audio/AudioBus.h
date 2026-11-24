#pragma once

#include <cstdint>

namespace SpaceGod::Audio
{
	/// The mix buses every sound plays on (US-T240). Master is everything (audio.masterVolume, the listener volume);
	/// each of the others has its own volume under Settings > Audio, a sound class and a submix.
	enum class AudioBus : std::uint8_t
	{
		Master,
		Music,
		Effects,
		Interface,
		Ambience,
		Voice,
	};

	inline constexpr int AudioBusCount = 6;

	inline const char* BusName(AudioBus bus)
	{
		switch (bus)
		{
		case AudioBus::Music: return "Music";
		case AudioBus::Effects: return "Effects";
		case AudioBus::Interface: return "Interface";
		case AudioBus::Ambience: return "Ambience";
		case AudioBus::Voice: return "Voice";
		default: return "Master";
		}
	}
}
