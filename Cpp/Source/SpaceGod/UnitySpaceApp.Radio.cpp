// UnitySpaceApp.Radio.cpp -- US-T656: the radio host's per-frame tick and its resets (see UnitySpaceApp.Radio.inl).

#include "UnitySpaceApp.h"

#include "Audio/GameAudio.h"
#include "Information/News.h"
#include "InterfaceOptions.h"
#include "PlayerOptions.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "UnityCompat/UnityRuntime.h"
#include "Radio/RadioStats.h" // US-T650
#include "Voice/VoiceArbiter.h" // US-T685

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace SpaceGod
{
	using namespace SpaceGod::Core;

	void UnitySpaceApp::RadioTick()
	{
		if (Clock == nullptr) return;
		// US-T651: the frame budget counts this frame's TtsService::Tick (run before this, still in Cycles) and the rest of
		// this function; the previous frame's host tick is reserved before the sampler and event pump.
		const uint64_t budgetStart = FPlatformTime::Cycles64();
		const uint64_t ttsCycles = Radio::RadioStats::Get().Cycles[static_cast<size_t>(Radio::RadioStage::TtsTick)];
		// US-T650: every stage below is timed (stat SpaceGodRadio, Insights, sg.Radio stats); the counters go out on return.
		Radio::RadioStats::Get().BeginFrame();
		const double reserveMs = Radio::RadioStats::Get().LastMs(Radio::RadioStage::HostTick);
		auto spentMs = [&]() { return FPlatformTime::ToMilliseconds64(ttsCycles + (FPlatformTime::Cycles64() - budgetStart)); };
		enum : int { StageSampler, StageEvents, StageDigest, StageCount };
		bool deferred[StageCount] = {};
		// Whether a deferrable stage runs now: its turn always does, the others while the budget (less the reserve) lasts.
		auto mayRun = [&](int stage, double reserve)
		{
			if (stage == radioBudgetTurn || spentMs() + reserve < RadioBudgetMs) return true;
			deferred[stage] = true;
			return false;
		};
		struct NextTurn
		{
			int& Turn;
			const bool* Deferred;
			~NextTurn()
			{
				// Round-robin: the first stage deferred after this frame's turn takes the next one.
				for (int i = 1; i <= StageCount; i++)
					if (Deferred[(Turn + i) % StageCount])
					{
						Turn = (Turn + i) % StageCount;
						return;
					}
			}
		} nextTurn { radioBudgetTurn, deferred };
		const double warp = Clock->Paused() ? 0.0 : Clock->Warp().SimSecondsPerSecond * Clock->Speed;
		struct PublishOnReturn
		{
			const Radio::RadioHost& Host;
			~PublishOnReturn()
			{
				Radio::RadioStats& stats = Radio::RadioStats::Get();
				stats.LogSize = Host.Log() != nullptr ? Host.Log()->Count() : 0;
				stats.HeapSize = Host.Receiver().Count();
				stats.Digested = Host.Digest().Pending();
				stats.Publish();
			}
		} publishOnReturn { radio };
		SG_RADIO_SCOPE(Tick);
		{
			// US-T685: the spoken radio lines take their turns on the one voice; under high warp only urgent ones are kept.
			SG_RADIO_SCOPE(Arbiter);
			Voice::VoiceArbiter::Tick(static_cast<float>(warp));
		}
		radioHeard.DropAfter(Clock->Jd); // a rewind or a load
		cs::Ref<Newswire> news = Galaxy != nullptr && Galaxy->Traffic != nullptr ? Galaxy->Traffic->News : nullptr;
		if (news != radioNews)
		{
			if (radioNews != nullptr) radioNews->Received -= radioNewsToken;
			radioNews = news;
			radioNewsToken = news != nullptr ? (news->Received += [this](cs::Ref<StarSystem> at, cs::Ref<SystemReport> report) { RadioRelayReceived(at, report); }) : 0;
		}
		if (ActiveSystem != radioSystem)
		{
			radioSystem = ActiveSystem;
			radioSystemIndex = Galaxy != nullptr && ActiveSystem != nullptr ? Galaxy->Systems.IndexOf(ActiveSystem) : -1;
		}
		// US-T679: the tuner's scan dwells in real time; the host routes each arrival through it and the active system's
		// channels (none between the stars), and records it as channel activity.
		radioTuner.Tick(UnityCompat::Time::unscaledDeltaTime, Clock->Jd);
		RadioListener listener;
		listener.Channels = GameRadioChannels();
		listener.Tuner = &radioTuner;
		listener.MinPriority = InterfaceOptions::RadioMinPriority(); // US-T704
		radio.SetListener(listener, &radioTuner);
		radioAmbient.Density = InterfaceOptions::ChatterDensity(); // US-T704: Settings > Interface > Chatter density
		// US-T703: after the host's tick, the digest's period summaries go to the log (one may be spoken); scripted runs keep
		// none.
		auto takeDigest = [this]()
		{
			SG_RADIO_SCOPE(Digest);
			if (PlayerOptions::ScriptedRun())
			{
				radio.Digest().Clear();
				return;
			}
			const bool warped = !Clock->Paused() && Clock->Warp().SimSecondsPerSecond * Clock->Speed > 1.0;
			std::vector<RadioDigestLine> digest;
			radio.Digest().Take(Clock->Jd, static_cast<double>(UnityCompat::Time::unscaledTime), warped, GameRadioChannels(), digest);
			radioHeard.AddDigest(digest, Clock->Jd);
		};
		if (ActiveSystem == nullptr)
		{
			radio.Tick(-1, DVec3(), Clock->Jd);
			if (mayRun(StageDigest, 0.0)) takeDigest();
			return;
		}
		if (radioSystemIndex >= 0 && radio.SystemId() == radioSystemIndex && !PlayerOptions::ScriptedRun() && SpaceGod::Audio::GameAudio::Enabled()
			&& warp <= RadioAmbientPauseWarp && mayRun(StageSampler, reserveMs)) // US-T651: paused above 1000x, else budgeted
		{
			// US-T681: ambient talk (read-only, view-local, its own random stream). A new system or a rebase back-fills what is
			// already in flight first: the log clamps a SentJd earlier than its newest, so nothing newer may go out before it.
			SG_RADIO_SCOPE(Sampler);
			if (!radioAmbientSeeded)
			{
				std::random_device device;
				const uint64_t clock = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
				radioAmbient.Seed(((static_cast<uint64_t>(device()) << 32) ^ device()) ^ clock);
				radioAmbientSeeded = true;
			}
			const DVec3 hereKm = CommsHere().HelioKm;
			const double marginKm = radio.Receiver().TravelMarginKm();
			radioAmbientOut.clear();
			if (radio.SystemId() != radioAmbientSystemId || radio.RebaseCount() != radioAmbientRebases)
			{
				radioAmbientSystemId = radio.SystemId();
				radioAmbientRebases = radio.RebaseCount();
				radioAmbient.Clear();
				radioAmbient.BackFill(ActiveSystem, radioSystemIndex, hereKm, marginKm, Clock->Jd, radioAmbientOut);
			}
			radioAmbient.Tick(ActiveSystem, radioSystemIndex, hereKm, marginKm, Clock->Jd, radioAmbientOut);
			for (const auto& m : radioAmbientOut) radio.Emit(m);
		}
		if (radioSystemIndex >= 0 && !PlayerOptions::ScriptedRun() && mayRun(StageEvents, reserveMs)) // US-T651: budgeted
		{
			// US-T682: the world's events in this system go out as chatter (read-only, view-local).
			SG_RADIO_SCOPE(Events);
			radioChatterOut.clear();
			radioChatter.Pump(ActiveSystem, radioSystemIndex, Clock->Jd, radioChatterOut);
			for (const auto& m : radioChatterOut)
			{
				if (m.Text.empty()) radio.Emit(m.Message);
				else radio.EmitText(m.Message, m.Text);
			}
		}
		radio.Tick(radioSystemIndex, CommsHere().HelioKm, Clock->Jd); // always: Advance pops at most RadioReceiver::MaxPopsPerFrame
		if (mayRun(StageDigest, 0.0)) takeDigest();
	}

	void UnitySpaceApp::RadioReset()
	{
		radio.Reset();
		radio.SetListener(RadioListener());
		radioTuner.Reset();
		radioChannels.Clear();
		radioHeard.Clear();
		radioRelay.Clear();
		radioChatter.Clear();
		radioChatterOut.clear();
		radioAmbient.Clear();
		radioAmbientOut.clear();
		radioAmbientSystemId = -1;
		radioAmbientRebases = ~0ull;
		radioSystem = nullptr;
		radioSystemIndex = -1;
	}

	void UnitySpaceApp::RadioRelayReceived(const cs::Ref<StarSystem>& at, const cs::Ref<SystemReport>& report)
	{
		if (at == nullptr || at != ActiveSystem || radioSystemIndex < 0 || report == nullptr) return;
		std::vector<RadioRelay::Bulletin> bulletins;
		radioRelay.Bulletins(at, radioSystemIndex, *report, bulletins);
		for (const auto& b : bulletins) radio.EmitText(b.Message, b.Text);
	}
}
