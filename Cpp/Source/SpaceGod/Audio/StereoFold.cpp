#include "Audio/StereoFold.h"

namespace
{
	/// -3 dB: the centre and each surround channel into its side.
	constexpr float Fold = 0.70710678f;

	class FStereoFold : public FSoundEffectSubmix
	{
	public:
		virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override
		{
			const int32 channels = InData.NumChannels;
			const int32 frames = InData.NumFrames;
			const float* in = InData.AudioBuffer->GetData();
			float* out = OutData.AudioBuffer->GetData();
			if (channels <= 2)
			{
				FMemory::Memcpy(out, in, sizeof(float) * frames * channels);
				return;
			}
			for (int32 f = 0; f < frames; f++, in += channels, out += channels)
			{
				// Front left, front right, centre, LFE, then left/right pairs (side, back).
				float left = in[0] + Fold * in[2];
				float right = in[1] + Fold * in[2];
				for (int32 c = 4; c + 1 < channels; c += 2)
				{
					left += Fold * in[c];
					right += Fold * in[c + 1];
				}
				out[0] = left;
				out[1] = right;
				for (int32 c = 2; c < channels; c++) out[c] = 0.0f;
			}
		}
	};
}

FText USpaceGodStereoFoldPreset::GetAssetActionName() const { return FText::FromString(TEXT("Stereo fold")); }

UClass* USpaceGodStereoFoldPreset::GetSupportedClass() const { return USpaceGodStereoFoldPreset::StaticClass(); }

USoundEffectPreset* USpaceGodStereoFoldPreset::CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const
{
	return NewObject<USpaceGodStereoFoldPreset>(InParent, GetSupportedClass(), Name, Flags);
}

FSoundEffectBase* USpaceGodStereoFoldPreset::CreateNewEffect() const { return new FStereoFold(); }
