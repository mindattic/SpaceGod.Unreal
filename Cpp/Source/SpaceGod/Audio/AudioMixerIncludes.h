#pragma once

// The engine's audio mixer and DSP headers use Unreal's legacy PI macro, which Cs.h takes away: bring it back for
// the engine includes only.
#ifdef PI
#pragma push_macro("PI")
#undef PI
#define SPACEGOD_RESTORE_PI 1
#endif
#define PI (3.1415926535897932f)

#include "AudioMixerBlueprintLibrary.h"
#include "AudioMixerDevice.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"

#undef PI
#ifdef SPACEGOD_RESTORE_PI
#pragma pop_macro("PI")
#undef SPACEGOD_RESTORE_PI
#endif
