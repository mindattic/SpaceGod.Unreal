#pragma once

// US-T672: Speaker mode Stereo speakers and Headphones on a device the engine opened with more than two channels
// (5.1, 7.1). Unreal 5.8 has no override for the output channel count, so this effect on the main submix folds the
// mix down to the front left and right channels (centre and each side or back channel at -3 dB into its side, the LFE
// dropped) and leaves the others silent. GameAudio puts it in the chain only while it is wanted.

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"
#include "StereoFold.generated.h"

UCLASS()
class USpaceGodStereoFoldPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	virtual FText GetAssetActionName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual USoundEffectPreset* CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const override;
	virtual FSoundEffectBase* CreateNewEffect() const override;
	virtual void Init() override {}
};
