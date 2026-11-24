#include "UI/DisplayOptions.h"

#include "ActiveProfile.h"
#include "Bootstrap.h"
#include "Persistence/Profiles.h"

#include <exception>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void DisplayOptions::ApplySaved()
	{
		Current = FromScreen().WithVSync(true);
		DisplaySettings saved;
		if (DisplaySettings::TryParse(GameSettings::Setting(ActiveProfile::Current(), GameSettings::Display), saved))
		{
			if (ScreenFixedByCommandLine()) Current = Current.WithVSync(saved.VSync);
			else
			{
				Apply(saved);
				return;
			}
		}
		DisplayBackend::SetVSyncCount(Current.VSync ? 1 : 0);
	}

	void DisplayOptions::Apply(DisplaySettings settings)
	{
		DisplayBackend::SetVSyncCount(settings.VSync ? 1 : 0);
		// A monitor no longer plugged in (or headless) means the primary one.
		if (settings.Monitor >= DisplayBackend::Monitors().Count()) settings = settings.WithMonitor(0);
		DisplayBackend::MoveToMonitor(settings.Monitor, settings.Width, settings.Height);
		auto mode = ToUnity(settings.Mode);
		DisplayBackend::Resolution best;
		if (settings.Mode == DisplayMode::Exclusive && TryFindMode(settings.Monitor, settings.Width, settings.Height, settings.RefreshHz, best))
			DisplayBackend::SetResolution(settings.Width, settings.Height, mode, best.refreshRateHz);
		else
			DisplayBackend::SetResolution(settings.Width, settings.Height, mode);
		Current = settings;
	}

	void DisplayOptions::Save()
	{
		try { GameSettings::SetSetting(ActiveProfile::Current(), GameSettings::Display, Current.Format()); }
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[Unity.SpaceGod] Saving the display settings failed: ", e.what())); }
	}

	cs::List<DisplayResolution> DisplayOptions::Resolutions(int monitor)
	{
		cs::List<DisplayResolution> modes;
		int maxW = Current.Width, maxH = Current.Height;
		for (const auto& r : DisplayBackend::Resolutions(monitor))
		{
			modes.Add(DisplayResolution(r.width, r.height, r.refreshRateHz));
			maxW = Mathf::Max(maxW, r.width);
			maxH = Mathf::Max(maxH, r.height);
		}
		auto native = DisplayBackend::CurrentResolution(monitor);
		maxW = Mathf::Max(maxW, native.width);
		maxH = Mathf::Max(maxH, native.height);
		auto list = DisplaySettings::WithStandardSizes(DisplaySettings::Distinct(modes), maxW, maxH);
		if (list.Count() == 0) list.Add(DisplayResolution(Current.Width, Current.Height, 0));
		return list;
	}

	cs::List<cs::String> DisplayOptions::Monitors() { return DisplayBackend::Monitors(); }

	cs::List<int> DisplayOptions::RefreshRates(int monitor, int width, int height)
	{
		cs::List<int> rates;
		for (const auto& r : DisplayBackend::Resolutions(monitor))
		{
			int hz = Mathf::RoundToInt(static_cast<float>(r.refreshRateHz));
			if (r.width == width && r.height == height && hz > 0 && !rates.Contains(hz)) rates.Add(hz);
		}
		rates.Sort([](int a, int b) { return a > b ? -1 : (a < b ? 1 : 0); });
		return rates;
	}

	bool DisplayOptions::TryFindMode(int monitor, int width, int height, int refreshHz, DisplayBackend::Resolution& best)
	{
		// The size's best mode at the chosen rate (59.94 and 60 both count as 60), else its best mode at any rate.
		best = DisplayBackend::Resolution();
		bool found = false, foundRate = false;
		for (const auto& r : DisplayBackend::Resolutions(monitor))
		{
			if (r.width != width || r.height != height) continue;
			bool atRate = refreshHz > 0 && Mathf::RoundToInt(static_cast<float>(r.refreshRateHz)) == refreshHz;
			if (!found || (atRate && !foundRate) || (atRate == foundRate && r.refreshRateHz > best.refreshRateHz))
			{
				best = r;
				found = true;
				foundRate = atRate;
			}
		}
		return found;
	}

	DisplaySettings DisplayOptions::FromScreen()
	{
		return DisplaySettings(Screen::width, Screen::height, FromUnity(DisplayBackend::CurrentMode()), DisplayBackend::VSyncCount() > 0,
			DisplayBackend::CurrentMonitor());
	}

	DisplayBackend::FullScreenMode DisplayOptions::ToUnity(DisplayMode mode)
	{
		using DisplayBackend::FullScreenMode;
		return mode == DisplayMode::Exclusive ? FullScreenMode::ExclusiveFullScreen
			: mode == DisplayMode::Borderless ? FullScreenMode::FullScreenWindow
			: FullScreenMode::Windowed;
	}

	DisplayMode DisplayOptions::FromUnity(DisplayBackend::FullScreenMode mode)
	{
		using DisplayBackend::FullScreenMode;
		return mode == FullScreenMode::ExclusiveFullScreen ? DisplayMode::Exclusive
			: mode == FullScreenMode::FullScreenWindow ? DisplayMode::Borderless
			: DisplayMode::Windowed; // MaximizedWindow is macOS's own windowed look
	}

	const cs::List<cs::String>& DisplayOptions::ScreenFlags()
	{
		static const cs::List<cs::String> flags
		{
			"-screen-width", "-screen-height", "-screen-fullscreen", "-window-mode", "-popupwindow", "-capture", "-demo",
		};
		return flags;
	}

	bool DisplayOptions::ScreenFixedByCommandLine()
	{
		for (const auto& flag : ScreenFlags())
			if (Bootstrap::HasCommandLineFlag(flag)) return true;
		return false;
	}
}
