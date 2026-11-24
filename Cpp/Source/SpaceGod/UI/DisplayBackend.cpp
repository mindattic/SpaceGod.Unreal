// DisplayBackend (declared in DisplayOptions.h): Unity's Screen display modes and QualitySettings.vSyncCount on
// Unreal's UGameUserSettings (US-T31), as the C# track's Screen facade does (US-T28, UnityCompat/Imgui/Screen.cs). The
// modes and their refresh rates are the monitor's own, from DXGI (SpaceGodRender's SpaceGodDisplayModes.h, US-T56), and
// an exclusive-fullscreen SetResolution switches the monitor to the rate asked for, as Unity's does.

#include "UI/DisplayOptions.h"
#include "SpaceGodLog.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "SpaceGodDisplayModes.h"

namespace SpaceGod::UI::DisplayBackend
{
	namespace
	{
		int vSync = 1;
		Resolution requested;

		UGameUserSettings* Settings() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

		EWindowMode::Type ToUnreal(FullScreenMode mode)
		{
			return mode == FullScreenMode::ExclusiveFullScreen ? EWindowMode::Fullscreen
				: mode == FullScreenMode::FullScreenWindow ? EWindowMode::WindowedFullscreen
				: EWindowMode::Windowed;
		}

		/// The game window's HWND (null headless: the primary monitor is asked then).
		void* GameWindow()
		{
			UGameViewportClient* client = GEngine ? GEngine->GameViewport : nullptr;
			TSharedPtr<SWindow> window = client ? client->GetWindow() : nullptr;
			TSharedPtr<FGenericWindow> native = window.IsValid() ? window->GetNativeWindow() : nullptr;
			return native.IsValid() ? native->GetOSWindowHandle() : nullptr;
		}

		/// The game window's RHI viewport (its swap chain), which Slate's renderer owns.
		FRHIViewport* GameViewportRHI()
		{
			UGameViewportClient* client = GEngine ? GEngine->GameViewport : nullptr;
			TSharedPtr<SWindow> window = client ? client->GetWindow() : nullptr;
			if (!window.IsValid() || !FSlateApplication::IsInitialized()) return nullptr;
			FSlateRenderer* renderer = FSlateApplication::Get().GetRenderer();
			return renderer ? static_cast<FRHIViewport*>(renderer->GetViewportResource(*window)) : nullptr;
		}

		// The rate an exclusive-fullscreen SetResolution asked for, set once the window is fullscreen at that size
		// (Unreal goes fullscreen at "any" rate, a frame or more after ApplyResolutionSettings). The C# track's primary
		// module does the same for sg.SetFullscreenRefresh.
		TOptional<FSpaceGodDisplayMode> pending;
		int pendingFrames = 0;
		FTSTicker::FDelegateHandle ticker;

		bool TickFullscreenRefresh(float)
		{
			if (!pending.IsSet()) return true;
			const FSpaceGodDisplayMode mode = pending.GetValue();
			UGameViewportClient* client = GEngine ? GEngine->GameViewport : nullptr;
			FViewport* viewport = client ? client->Viewport : nullptr;
			const bool ready = viewport && viewport->GetWindowMode() == EWindowMode::Fullscreen
				&& viewport->GetSizeXY() == FIntPoint(mode.Width, mode.Height) && GameViewportRHI() != nullptr;
			if (!ready)
			{
				if (++pendingFrames > 600)
				{
					UE_LOG(LogSpaceGod, Display, TEXT("Display: %dx%d at %.3f Hz not applied: the window never went exclusive fullscreen at that size"),
						mode.Width, mode.Height, mode.RefreshHz());
					pending.Reset();
				}
				return true;
			}
			pending.Reset();
			const ERHIInterfaceType rhi = RHIGetInterfaceType();
			if (rhi != ERHIInterfaceType::D3D12 && rhi != ERHIInterfaceType::D3D11) return true; // no DXGI swap chain to ask
			FlushRenderingCommands();
			const bool ok = SpaceGodDisplay::SetFullscreenRefresh(GameViewportRHI()->GetNativeSwapChain(), mode);
			FSpaceGodDisplayMode now;
			const bool known = SpaceGodDisplay::CurrentMode(GameWindow(), now);
			UE_LOG(LogSpaceGod, Display, TEXT("Display: exclusive fullscreen %dx%d at %u/%u Hz (%.3f): %s; the monitor now runs %dx%d at %u Hz"),
				mode.Width, mode.Height, mode.RefreshNumerator, mode.RefreshDenominator, mode.RefreshHz(), ok ? TEXT("set") : TEXT("refused"),
				known ? now.Width : 0, known ? now.Height : 0, known ? now.RefreshNumerator : 0);
			return true;
		}

		Resolution FromMode(const FSpaceGodDisplayMode& m)
		{
			Resolution r;
			r.width = m.Width;
			r.height = m.Height;
			r.refreshRateHz = m.RefreshHz();
			r.refreshNumerator = m.RefreshNumerator;
			r.refreshDenominator = m.RefreshDenominator;
			return r;
		}

		/// US-T546: the monitors, the primary one first; empty headless (no game window).
		TArray<FMonitorInfo> MonitorList()
		{
			TArray<FMonitorInfo> list;
			if (GameWindow() == nullptr) return list;
			FDisplayMetrics metrics;
			FDisplayMetrics::RebuildDisplayMetrics(metrics);
			for (const FMonitorInfo& m : metrics.MonitorInfo)
				if (m.bIsPrimary) list.Add(m);
			for (const FMonitorInfo& m : metrics.MonitorInfo)
				if (!m.bIsPrimary) list.Add(m);
			return list;
		}

		FIntPoint Centre(const FPlatformRect& rect) { return FIntPoint((rect.Left + rect.Right) / 2, (rect.Top + rect.Bottom) / 2); }

		/// The monitor asked for when it is not the one the game is on, else null (the game window's own is asked then).
		const FMonitorInfo* OtherMonitor(const TArray<FMonitorInfo>& list, int monitor)
		{
			return list.IsValidIndex(monitor) && monitor != CurrentMonitor() ? &list[monitor] : nullptr;
		}
	}

	void SetResolution(int width, int height, FullScreenMode mode, double refreshHz)
	{
		UGameUserSettings* s = Settings();
		if (s == nullptr) return;
		s->SetScreenResolution(FIntPoint(width, height));
		s->SetFullscreenMode(ToUnreal(mode));
		s->ApplyResolutionSettings(false);

		// US-T56: as Unity's, exclusive fullscreen at a rate switches the monitor to it.
		pending.Reset();
		pendingFrames = 0;
		requested = Resolution();
		FSpaceGodDisplayMode m;
		if (mode != FullScreenMode::ExclusiveFullScreen || refreshHz <= 0 || !SpaceGodDisplay::FindMode(GameWindow(), width, height, refreshHz, m)) return;
		requested = FromMode(m);
		pending = m;
		if (!ticker.IsValid()) ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickFullscreenRefresh));
		UE_LOG(LogSpaceGod, Display, TEXT("Display: exclusive fullscreen %dx%d asked at %u/%u Hz (%.3f)"),
			m.Width, m.Height, m.RefreshNumerator, m.RefreshDenominator, m.RefreshHz());
	}

	Resolution RequestedRefresh() { return requested; }

	cs::List<Resolution> Resolutions(int monitor)
	{
		cs::List<Resolution> list;
		const TArray<FMonitorInfo> monitors = MonitorList();
		const FMonitorInfo* other = OtherMonitor(monitors, monitor);
		const FIntPoint at = other ? Centre(other->DisplayRect) : FIntPoint::ZeroValue;
		for (const FSpaceGodDisplayMode& m : other ? SpaceGodDisplay::ModesAt(at.X, at.Y) : SpaceGodDisplay::Modes(GameWindow())) list.Add(FromMode(m));
		if (list.Count() > 0) return list;
		// Where DXGI cannot be asked: Unreal's own list, which has no rates (each at 60 Hz, as before US-T56).
		TArray<FIntPoint> modes;
		if (UKismetSystemLibrary::GetSupportedFullscreenResolutions(modes))
			for (const FIntPoint& m : modes)
			{
				Resolution r;
				r.width = m.X;
				r.height = m.Y;
				r.refreshRateHz = 60;
				r.refreshNumerator = 60;
				r.refreshDenominator = 1;
				list.Add(r);
			}
		return list;
	}

	Resolution CurrentResolution(int monitor)
	{
		const TArray<FMonitorInfo> monitors = MonitorList();
		if (const FMonitorInfo* other = OtherMonitor(monitors, monitor))
		{
			Resolution r;
			r.width = other->DisplayRect.Right - other->DisplayRect.Left;
			r.height = other->DisplayRect.Bottom - other->DisplayRect.Top;
			r.refreshRateHz = 60;
			return r;
		}
		FSpaceGodDisplayMode now;
		if (SpaceGodDisplay::CurrentMode(GameWindow(), now)) return FromMode(now);
		Resolution r;
		r.refreshRateHz = 60;
		if (UGameUserSettings* s = Settings())
		{
			FIntPoint d = s->GetDesktopResolution();
			r.width = d.X;
			r.height = d.Y;
		}
		else
		{
			r.width = Screen::width;
			r.height = Screen::height;
		}
		return r;
	}

	FullScreenMode CurrentMode()
	{
		UGameUserSettings* s = Settings();
		if (s == nullptr) return FullScreenMode::Windowed;
		switch (s->GetFullscreenMode())
		{
			case EWindowMode::Fullscreen: return FullScreenMode::ExclusiveFullScreen;
			case EWindowMode::WindowedFullscreen: return FullScreenMode::FullScreenWindow;
			default: return FullScreenMode::Windowed;
		}
	}

	cs::List<cs::String> Monitors()
	{
		cs::List<cs::String> names;
		for (const FMonitorInfo& m : MonitorList())
			names.Add(cs::String(TCHAR_TO_UTF8(*m.Name)));
		return names;
	}

	int CurrentMonitor()
	{
		UGameViewportClient* client = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> window = client ? client->GetWindow() : nullptr;
		if (!window.IsValid()) return 0;
		const FVector2D position = window->GetPositionInScreen(), size = window->GetSizeInScreen();
		const FVector2D centre = position + size * 0.5;
		const TArray<FMonitorInfo> monitors = MonitorList();
		for (int i = 0; i < monitors.Num(); i++)
		{
			const FPlatformRect& d = monitors[i].DisplayRect;
			if (centre.X >= d.Left && centre.X < d.Right && centre.Y >= d.Top && centre.Y < d.Bottom) return i;
		}
		return 0;
	}

	void MoveToMonitor(int monitor, int width, int height)
	{
		UGameViewportClient* client = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> window = client ? client->GetWindow() : nullptr;
		const TArray<FMonitorInfo> monitors = MonitorList();
		if (!window.IsValid() || !monitors.IsValidIndex(monitor) || monitor == CurrentMonitor()) return;
		// A fullscreen window belongs to its monitor: go windowed, move, and let the caller's SetResolution take the mode.
		if (CurrentMode() != FullScreenMode::Windowed) SetResolution(width, height, FullScreenMode::Windowed);
		const FPlatformRect& area = monitors[monitor].WorkArea;
		const int32 areaW = area.Right - area.Left, areaH = area.Bottom - area.Top;
		const FVector2D at(area.Left + FMath::Max(0, (areaW - width) / 2), area.Top + FMath::Max(0, (areaH - height) / 2));
		window->MoveWindowTo(at);
		UE_LOG(LogSpaceGod, Display, TEXT("Display: moved to monitor %d (%s) at %.0f,%.0f"), monitor, *monitors[monitor].Name, at.X, at.Y);
	}

	int VSyncCount() { return vSync; }

	void SetVSyncCount(int count)
	{
		vSync = count;
		if (UGameUserSettings* s = Settings())
		{
			s->SetVSyncEnabled(count > 0);
			s->ApplyNonResolutionSettings();
		}
	}
}
