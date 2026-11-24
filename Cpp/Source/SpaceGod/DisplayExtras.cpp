#include "DisplayExtras.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

#include "PlayerOptions.h"
#include "UnityCompat/UnityTypes.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// The rows' values as last put into effect (written by the Applying callbacks only, see GraphicsSettings.cpp).
		constexpr double DefaultGamma = 2.2; // the engine's own display gamma
		bool hdrOn = false;
		int hdrNits = 1000;
		int backgroundFps = 0;
		bool throttled = false;
		FTSTicker::FDelegateHandle ticker;

		const cs::List<int>& BackgroundRates()
		{
			static const cs::List<int> value{ 0, 15, 30, 60 };
			return value;
		}

		UGameUserSettings* UserSettings() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

		void ApplyHdr()
		{
			UGameUserSettings* s = UserSettings();
			if (s == nullptr) return;
			bool on = hdrOn && s->SupportsHDRDisplayOutput();
			if (!on && !s->IsHDREnabled()) return; // off and never turned on: the engine's own output, untouched
			if (on == s->IsHDREnabled() && (!on || s->GetCurrentHDRDisplayNits() == hdrNits)) return;
			s->EnableHDRDisplayOutput(on, hdrNits);
		}

		/// While the window is in the background, t.MaxFPS is held at the background cap (or the frame-rate cap, if
		/// lower); coming back puts the frame-rate cap back.
		bool Tick(float)
		{
			IConsoleVariable* maxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
			if (maxFps == nullptr) return true;
			if (backgroundFps > 0 && !FApp::HasFocus())
			{
				int target = UnityCompat::Application::targetFrameRate();
				float capped = static_cast<float>(target > 0 ? cs::Math::Min(target, backgroundFps) : backgroundFps);
				throttled = true;
				if (maxFps->GetFloat() != capped) maxFps->Set(capped, ECVF_SetByGameSetting);
			}
			else if (throttled)
			{
				throttled = false;
				UnityCompat::Application::SetTargetFrameRate(UnityCompat::Application::targetFrameRate());
			}
			return true;
		}
	}

	const cs::Ref<RangeSetting>& DisplayExtras::Brightness()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("display.brightness", Group,
			"Brightness", DefaultGamma, 1.6, 2.8, 0.1,
			"The display gamma: higher brightens the shadows and the dark of space, lower deepens them.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.0"); }), "brightness gamma dark light screen display")
			->Applying([](double v)
			{
				if (GEngine) GEngine->DisplayGamma = static_cast<float>(PlayerOptions::ScriptedRun() ? DefaultGamma : v);
			});
		return value;
	}

	const cs::Ref<ToggleSetting>& DisplayExtras::HdrOutput()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("display.hdr", Group,
			"HDR output", false,
			"Sends a high dynamic range picture to a screen that supports it, so suns and engine glow can outshine white.",
			"hdr high dynamic range display output nits screen")
			->Applying([](bool on)
			{
				hdrOn = on && !PlayerOptions::ScriptedRun();
				ApplyHdr();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& DisplayExtras::HdrPeakNits()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("display.hdrPeakNits", Group,
			"HDR peak brightness", 1000, 400, 2000, 100,
			"The brightest the screen can show, in nits; match it to the screen's own HDR rating. Used only with HDR output on.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " nits"); }), "hdr peak brightness nits luminance display")
			->Applying([](double v)
			{
				hdrNits = cs::ToInt(cs::Math::Round(v));
				ApplyHdr();
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& DisplayExtras::BackgroundFrameCap()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("display.backgroundFrameCap", Group,
			"Background frame cap", cs::List<cs::String>{ "Off", "15 fps", "30 fps", "60 fps" }, 0,
			"The most frames drawn a second while the game's window is in the background, to spare the GPU and the fans.",
			"background unfocused alt tab minimized fps framerate power")
			->Applying([](int i)
			{
				backgroundFps = PlayerOptions::ScriptedRun() ? 0 : BackgroundRates()[i];
				if (backgroundFps > 0 && !ticker.IsValid())
					ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.25f);
			});
		return value;
	}

	void DisplayExtras::Declare()
	{
		Brightness(); HdrOutput(); HdrPeakNits(); BackgroundFrameCap();
	}

	bool DisplayExtras::HdrSupported()
	{
		UGameUserSettings* s = UserSettings();
		return s != nullptr && s->SupportsHDRDisplayOutput();
	}
}
