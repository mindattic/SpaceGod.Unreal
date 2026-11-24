#include "DisplayExtras.h"
#include "UnitySpaceApp.h"

#include "PlayerOptions.h"
#include "UI/MainMenu.h"
#include "UI/PauseMenu.h"
#include "UnityCompat/UnityTypes.h"
#include "UnitySpaceAppActor.h"
#include "Settings/GameSetting.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

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
		int menuFps = 0;
		bool throttled = false;
		FTSTicker::FDelegateHandle ticker;
		bool lowLatency = false;
		// The engine's own r.GTSyncType and r.OneFrameThreadLag, kept while Low latency holds them.
		int savedSyncType = 0;
		int savedThreadLag = 1;

		const cs::List<int>& BackgroundRates()
		{
			static const cs::List<int> value{ 0, 15, 30, 60 };
			return value;
		}

		const cs::List<int>& MenuRates()
		{
			static const cs::List<int> value{ 0, 30, 60, 120 };
			return value;
		}

		/// Low latency syncs the game thread to the RHI thread and drops the render thread's frame of lag; off puts back
		/// what the engine had.
		void ApplyLowLatency(bool on)
		{
			IConsoleVariable* sync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GTSyncType"));
			IConsoleVariable* lag = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"));
			if (on == lowLatency) return;
			if (on)
			{
				if (sync != nullptr) { savedSyncType = sync->GetInt(); sync->Set(1, ECVF_SetByGameSetting); }
				if (lag != nullptr) { savedThreadLag = lag->GetInt(); lag->Set(0, ECVF_SetByGameSetting); }
			}
			else
			{
				if (sync != nullptr) sync->Set(savedSyncType, ECVF_SetByGameSetting);
				if (lag != nullptr) lag->Set(savedThreadLag, ECVF_SetByGameSetting);
			}
			lowLatency = on;
		}

		/// Whether the pause menu or the title menu covers the screen.
		bool MenuUp()
		{
			if (UI::MainMenu::Showing()) return true;
			if (!GEngine) return false;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
			{
				if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
				auto* actor = AUnitySpaceAppActor::Find(c.World());
				if (actor == nullptr || actor->GetApp() == nullptr) continue;
				const cs::Ref<UI::PauseMenu>& pause = actor->GetApp()->PauseMenuForChecks();
				if (pause != nullptr && pause->IsOpen) return true;
			}
			return false;
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

		/// While the window is in the background, t.MaxFPS is held at the background cap, and while a full-screen menu is
		/// up at the menu cap (either way the frame-rate cap, if lower); after, the frame-rate cap goes back.
		bool Tick(float)
		{
			IConsoleVariable* maxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
			if (maxFps == nullptr) return true;
			int cap = backgroundFps > 0 && !FApp::HasFocus() ? backgroundFps : menuFps > 0 && MenuUp() ? menuFps : 0;
			if (cap > 0)
			{
				int target = UnityCompat::Application::targetFrameRate();
				float capped = static_cast<float>(target > 0 ? cs::Math::Min(target, cap) : cap);
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

		void StartTicker()
		{
			if ((backgroundFps > 0 || menuFps > 0) && !ticker.IsValid())
				ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.25f);
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
				StartTicker();
			});
		return value;
	}

	const cs::Ref<ToggleSetting>& DisplayExtras::LowLatency()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("display.lowLatency", Group,
			"Low latency", false,
			"Trims the queue of frames waiting to be drawn, so the ship answers the controls sooner; can cost a few frames a second.",
			"low latency input lag render queue responsiveness display")
			->Applying([](bool on) { ApplyLowLatency(on && !PlayerOptions::ScriptedRun()); });
		return value;
	}

	const cs::Ref<ChoiceSetting>& DisplayExtras::MenuFrameCap()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("display.menuFrameCap", Group,
			"Menu frame cap", cs::List<cs::String>{ "Off", "30 fps", "60 fps", "120 fps" }, 0,
			"The most frames drawn a second while the pause or title menu covers the screen, to spare the GPU and the fans.",
			"menu pause title fps framerate power cap")
			->Applying([](int i)
			{
				menuFps = PlayerOptions::ScriptedRun() ? 0 : MenuRates()[i];
				StartTicker();
			});
		return value;
	}

	void DisplayExtras::Declare()
	{
		Brightness(); HdrOutput(); HdrPeakNits(); BackgroundFrameCap(); LowLatency(); MenuFrameCap();
	}

	bool DisplayExtras::HdrSupported()
	{
		UGameUserSettings* s = UserSettings();
		return s != nullptr && s->SupportsHDRDisplayOutput();
	}
}
