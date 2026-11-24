// US-T677: the radio tuner panel (declared in Hud.Radio.inl), modelled on the battle plot (Hud.Plot.cpp). Shift+C
// (KeyAction::RadioTuner) shows or hides it. It shows a dial with the tuned frequency (the scan's channel while scanning)
// over the band of the active system's channels, presets 1-8 (a click recalls one, Shift+click stores the tuned channel
// in it, Shift+click with nothing tuned clears it), a Scan toggle, and the channel list: each row's activity in the last
// game hour (RadioTuner::Activity) as a meter, a lock mark on an encrypted net, the name and frequency, and Tune /
// Monitor / Mute buttons. The list scrolls with the wheel over it.
//
// It drives only the app's session tuner (UnitySpaceApp::GameRadioTuner, US-T679), which is view-local: nothing here
// enters the simulation, the lockstep frames, saves or the bench digests.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"
#include "Input/InputDevice.h"
#include "Input/KeyBindings.h"

#include "Radio/RadioChannels.h"
#include "Radio/RadioTuner.h"
#include "SimClock.h"

#include <algorithm>
#include <cstdio>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float TunerW = 460, TunerPad = 8, HeadH = 20, DialH = 46, PresetH = 24, RowH = 22, ButtonW = 58, MeterW = 54;
		constexpr int ShownRows = 12;
		/// The activity that fills a row's meter (arrivals in the game hour).
		constexpr int MeterFull = 20;

		cs::String Mhz(double mhz)
		{
			char buf[32];
			std::snprintf(buf, sizeof buf, "%.3f MHz", mhz);
			return cs::String(buf);
		}

		void Fill(const Rect& r, Color c)
		{
			auto old = GUI::color;
			GUI::color = c;
			GUI::DrawTexture(r, Icons::White());
			GUI::color = old;
		}

		Color Faded(Color c, float alpha) { return Color(c.r, c.g, c.b, alpha); }
	}

	bool Hud::UpdateRadioTunerKey(bool keysFree)
	{
		if (!keysFree) return false;
		const Input::InputDevice& kb = Input::InputDevice::current();
		if (!Input::KeyBindings::Pressed(Input::KeyAction::RadioTuner, kb)) return false;
		radioTunerShown = !radioTunerShown;
		return true; // spent (C alone still sinks: a held action is never pre-empted)
	}

	void Hud::DrawRadioTuner()
	{
		if (!radioTunerShown) return;
		auto& tuner = app->GameRadioTuner();
		const RadioChannelSet* set = app->GameRadioChannels();
		const auto* all = set != nullptr ? &set->All() : nullptr;
		int count = all != nullptr ? static_cast<int>(all->size()) : 0;
		double jd = app->Clock != nullptr ? app->Clock->Jd : 0;

		int rows = std::min(count, ShownRows);
		radioTunerScroll = std::clamp(radioTunerScroll, 0, std::max(0, count - rows));
		float listH = count > 0 ? rows * RowH : RowH;
		float h = HeadH + DialH + PresetH + TunerPad + listH + TunerPad;
		float x = (SafeLeft() + SafeRight()) * 0.5f - TunerW * 0.5f;
		float y = HudLayout::TopBarBottom + HudLayout::Margin;
		auto r = Rect(x, y, TunerW, h);
		RegisterUi(r);

		const Event& ev = Event::current();
		bool repaint = ev.type == EventType::Repaint;
		if (repaint) GUI::Box(r, GUIContent::none(), panel);

		// The head: the title and the key that hides it, the Scan toggle on the right.
		float inW = TunerW - 2 * TunerPad;
		auto oldContent = GUI::contentColor;
		GUI::contentColor = Accent;
		GUI::Label(Rect(r.x + TunerPad, r.y + 3, inW - ButtonW - 4, 16),
			cs::Cat("RADIO TUNER  \xC2\xB7  ", Input::KeyBindings::Keys(Input::KeyAction::RadioTuner), " hides"), small);
		GUI::contentColor = oldContent;
		if (GUI::Button(Rect(r.x + TunerW - TunerPad - ButtonW, r.y + 2, ButtonW, HeadH - 3), "Scan", tuner.Scanning() ? buttonActive : button))
			tuner.SetScanning(!tuner.Scanning());

		// The dial: the frequency listened to, its channel, and the band with every channel's tick and the needle.
		float dy = r.y + HeadH;
		uint32_t current = tuner.Current();
		const RadioChannel* on = set != nullptr && current != 0 ? set->Find(current) : nullptr;
		GUI::contentColor = on != nullptr ? Accent : Dim;
		GUI::Label(Rect(r.x + TunerPad, dy, 150, 20), on != nullptr ? Mhz(on->FrequencyMhz) : cs::String("---.--- MHz"), title);
		GUI::contentColor = Dim;
		cs::String what = on != nullptr ? on->Name : cs::String(set == nullptr ? "No channels between the stars" : "Not tuned");
		if (tuner.Scanning()) what = cs::Cat("Scanning  \xC2\xB7  ", what);
		GUI::Label(Rect(r.x + TunerPad + 156, dy + 3, inW - 156, 16), what, small);
		GUI::contentColor = oldContent;
		if (repaint && count > 0)
		{
			double lo = (*all)[0].FrequencyMhz, hi = lo;
			for (const auto& c : *all)
			{
				lo = std::min(lo, c.FrequencyMhz);
				hi = std::max(hi, c.FrequencyMhz);
			}
			double span = std::max(hi - lo, 1.0);
			float bx = r.x + TunerPad, bw = inW, by = dy + 30;
			Fill(Rect(bx, by, bw, 1), Faded(Dim, 0.6f));
			auto at = [&](double mhz) { return bx + static_cast<float>((mhz - lo) / span) * bw; };
			for (const auto& c : *all)
			{
				bool hot = tuner.Activity(c.Id, jd) > 0;
				Fill(Rect(at(c.FrequencyMhz) - 0.5f, by - (hot ? 6 : 3), 1, hot ? 6 : 3), Faded(c.Encrypted ? HostileColor : Dim, hot ? 0.9f : 0.5f));
			}
			if (on != nullptr) Fill(Rect(at(on->FrequencyMhz) - 1, by - 10, 2, 14), LockColor);
		}

		// Presets 1-8: a click recalls, Shift+click stores the tuned channel (clears the slot when nothing is tuned).
		float py = dy + DialH;
		float pw = (inW - (RadioTuner::PresetCount - 1) * 4) / RadioTuner::PresetCount;
		for (int i = 1; i <= RadioTuner::PresetCount; i++)
		{
			uint32_t ch = tuner.Preset(i);
			bool here = ch != 0 && ch == tuner.Tuned();
			auto pr = Rect(r.x + TunerPad + (i - 1) * (pw + 4), py, pw, PresetH - 4);
			const RadioChannel* pc = set != nullptr && ch != 0 ? set->Find(ch) : nullptr;
			cs::String presetText = ch == 0 ? cs::Cat(i) : cs::Cat(i, pc != nullptr ? "\xE2\x80\xA2" : "?");
			if (GUI::Button(pr, presetText, here ? buttonActive : button))
			{
				if (ev.shift) tuner.SetPreset(i, tuner.Tuned());
				else tuner.RecallPreset(i);
			}
		}

		// The channel list.
		float ly = py + PresetH + TunerPad;
		auto list = Rect(r.x + TunerPad, ly, inW, listH);
		if (count == 0)
		{
			GUI::contentColor = Dim;
			GUI::Label(list, set == nullptr ? "Nothing on the air out here." : "No channels in this system.", small);
			GUI::contentColor = oldContent;
			return;
		}
		if (ev.type == EventType::ScrollWheel && list.Contains(ev.mousePosition))
		{
			radioTunerScroll = std::clamp(radioTunerScroll + (ev.delta.y > 0 ? 1 : -1), 0, std::max(0, count - rows));
			Event::current().Use();
		}
		float nameX = TunerPad + MeterW + 6 + 16, nameW = inW - nameX + TunerPad - 3 * (ButtonW + 3) - 76;
		for (int k = 0; k < rows; k++)
		{
			int i = radioTunerScroll + k;
			if (i >= count) break;
			const RadioChannel& c = (*all)[i];
			float ry = ly + k * RowH;
			bool tuned = c.Id == tuner.Tuned(), monitored = tuner.IsMonitored(c.Id), muted = tuner.IsMuted(c.Id);
			if (repaint)
			{
				if (c.Id == current) Fill(Rect(r.x + 2, ry, TunerW - 4, RowH - 1), Faded(Accent, 0.12f));
				int activity = tuner.Activity(c.Id, jd);
				auto meter = Rect(r.x + TunerPad, ry + 7, MeterW, 7);
				Fill(meter, Faded(Dim, 0.18f));
				float f = std::min(1.0f, activity / static_cast<float>(MeterFull));
				if (f > 0) Fill(Rect(meter.x, meter.y, Mathf::Max(2.0f, meter.width * f), meter.height), Faded(muted ? Dim : Accent, 0.85f));
			}
			if (c.Encrypted)
			{
				GUI::contentColor = HostileColor;
				GUI::Label(Rect(r.x + TunerPad + MeterW + 6, ry + 3, 16, 16), "\xE2\x9A\xBF", small); // the lock mark: outsiders hear only that there is traffic
			}
			GUI::contentColor = muted ? Faded(Dim, 0.6f) : tuned ? Accent : oldContent;
			GUI::Label(Rect(r.x + nameX, ry + 3, nameW, 16), c.Name, small);
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x + nameX + nameW + 2, ry + 3, 74, 16), Mhz(c.FrequencyMhz), small);
			GUI::contentColor = oldContent;

			float bx = r.x + TunerW - TunerPad - 3 * ButtonW - 6;
			if (GUI::Button(Rect(bx, ry + 1, ButtonW, RowH - 3), "Tune", tuned ? buttonActive : button)) tuner.Tune(tuned ? 0 : c.Id);
			if (GUI::Button(Rect(bx + ButtonW + 3, ry + 1, ButtonW, RowH - 3), "Monitor", monitored ? buttonActive : button))
			{
				if (monitored) tuner.Unmonitor(c.Id);
				else tuner.Monitor(c.Id); // false when all MaxMonitored slots are taken: the button stays off
			}
			if (GUI::Button(Rect(bx + 2 * (ButtonW + 3), ry + 1, ButtonW, RowH - 3), "Mute", muted ? buttonActive : button)) tuner.SetMuted(c.Id, !muted);
		}
		if (count > rows && repaint)
		{
			// A thin scroll thumb on the list's right edge.
			float th = listH * rows / count, ty = ly + listH * radioTunerScroll / count;
			Fill(Rect(r.x + TunerW - 4, ty, 2, th), Faded(Dim, 0.6f));
		}
	}
}
