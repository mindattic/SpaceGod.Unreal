#pragma once

#include "Cs/Cs.h"
#include "Audio/AudioBus.h"
#include "Audio/SynthSound.h"
#include "UnityCompat/UnityMath.h"

namespace SpaceGod::Core { class RangeSetting; class ToggleSetting; class ChoiceSetting; }

namespace SpaceGod::Audio
{
	/// A loop started by GameAudio::StartLoop: 0 is none.
	using LoopId = int;

	/// The game's audio layer (US-T240): plays one-shots (2D, or 3D at a camera-relative scene point) and loops through
	/// Unreal's audio engine, each on a bus with its own sound class, submix and volume (Settings > Audio: Music,
	/// Effects, Interface, Ambience, Voice under the master volume, which stays audio.masterVolume and the listener's).
	///
	/// Everything here is local to this machine: nothing enters the Core or the co-op lockstep frames, so each peer
	/// plays its own sounds and the bench digests are untouched. It is silent and does nothing at all, safely, when
	/// there is no audio device or the run is scripted (--headless, -RenderOffscreen, the smoke/shots/audits, -demo,
	/// -capture) unless -GameAudio asks for sound, and under the CLI mute (-muted, SessionAudio).
	class GameAudio
	{
	public:
		/// Whether sounds play this run (a device, not scripted or -GameAudio, not muted).
		static bool Enabled();

		/// The volume of a bus, 0..1, as its setting has it (Master: the master volume).
		static float BusVolume(AudioBus bus);

		/// A one-shot that is not placed in the world (the interface, music stings). <paramref name="volume"/> scales
		/// the bus's volume; <paramref name="pitch"/> changes speed and pitch together.
		static void Play2D(const SoundClip& clip, AudioBus bus, float volume = 1.0f, float pitch = 1.0f);
		/// Play2D that can be cut short (US-T684: a radio line an urgent one or a comms line interrupts): StopLoop ends
		/// it, LoopPlaying is false once it has ended by itself. Returns 0 when nothing plays.
		static LoopId Play2DStoppable(const SoundClip& clip, AudioBus bus, float volume = 1.0f, float pitch = 1.0f);

		/// A one-shot at a camera-relative scene point (scene units, Unity axes): heard from the camera, fading out
		/// between <paramref name="minDistance"/> and <paramref name="maxDistance"/> scene units.
		static void Play3D(const SoundClip& clip, AudioBus bus, UnityCompat::Vector3 position, float volume = 1.0f, float pitch = 1.0f,
			float minDistance = 50.0f, float maxDistance = 5000.0f);

		/// A loop until StopLoop: 2D, or (<paramref name="positional"/>) at <paramref name="position"/>, which
		/// SetLoopPosition moves. Returns 0 when nothing plays (disabled).
		static LoopId StartLoop(const SoundClip& clip, AudioBus bus, float volume = 1.0f, bool positional = false,
			UnityCompat::Vector3 position = UnityCompat::Vector3(), float minDistance = 50.0f, float maxDistance = 5000.0f);
		static void SetLoopVolume(LoopId loop, float volume);
		/// A loop's speed and pitch together (US-T242: an engine's note following its throttle), 1 as recorded.
		static void SetLoopPitch(LoopId loop, float pitch);
		static void SetLoopPosition(LoopId loop, UnityCompat::Vector3 position);
		static void StopLoop(LoopId loop, float fadeSeconds = 0.1f);
		/// Whether a loop is still playing (US-T243): false once stopped, or when its world went away under it.
		static bool LoopPlaying(LoopId loop);
		/// Stops every sound playing (a scene change).
		static void StopAll();

		/// Settings > Audio's bus volumes (0-100%), saved to the profile like audio.masterVolume; registered by
		/// GameSettingsHost right after it.
		static void Declare();
		static const cs::Ref<Core::RangeSetting>& MusicVolume();
		static const cs::Ref<Core::RangeSetting>& EffectsVolume();
		static const cs::Ref<Core::RangeSetting>& InterfaceVolume();
		static const cs::Ref<Core::RangeSetting>& AmbienceVolume();
		static const cs::Ref<Core::RangeSetting>& VoiceVolume();
		/// US-T649: Spoken comms (on): off, CommsVoice and VoiceArbiter stay silent; the text still shows.
		static const cs::Ref<Core::ToggleSetting>& SpokenCommsSetting();
		/// US-T649: Voice engine (Auto, Piper, Windows), handed to TtsService.
		static const cs::Ref<Core::ChoiceSetting>& VoiceEngineSetting();
		/// US-T649: Radio effect (on): whether radio lines get RadioEffect (part of the voice cache key).
		static const cs::Ref<Core::ToggleSetting>& RadioEffectSetting();
		/// The two toggles above as they apply now (a scripted run: their factory defaults).
		static bool SpokenComms();
		static bool RadioEffectOn();
		/// US-T552: silent while the window is in the background (on: Unreal's own default, as before).
		static const cs::Ref<Core::ToggleSetting>& MuteInBackground();
		/// US-T552: what the game's sound does while the pause menu is open (Play, Duck, Mute); menu sounds still play.
		static const cs::Ref<Core::ChoiceSetting>& SoundWhilePaused();
		/// US-T552: Full, or Night (a compressor on the main submix: quiet sounds up, loud ones down).
		static const cs::Ref<Core::ChoiceSetting>& DynamicRange();

		/// How many one-shots and loops are playing (the audit, the log).
		static int Playing();

		/// How 3D sounds are placed (US-T613, from Settings > Sound > Speaker mode): Panned, the engine's own; Headphones,
		/// near sounds narrowed toward the middle; Mono, nothing panned.
		enum class Placement { Panned, Headphones, Mono };
		/// Applies to every sound from now on and to those playing.
		static void SetPlacement(Placement placement);
		/// US-T672: folds the main mix down to the front left and right channels (Stereo speakers or Headphones on a
		/// surround device), on the next tick of a built mix; off, the fold is not in the chain.
		static void SetStereoFold(bool on);
		/// US-T673: whether 3D sounds go through the audio device's spatialization plugin (HRTF: Binaural or Windows
		/// spatial sound) instead of the engine's panning. Applies to every sound from now on and to those playing.
		static void SetSpatialPlugin(bool on);
	};
}
