// US-T981: sg.LightSociety, the populace's opinion model (Light Society, docs/LightSociety.md) at the console.
//   sg.LightSociety [summary]                    the society: agents, day, routing, distillation, readout hash
//   sg.LightSociety topics                       each topic across the worlds: mean, shares, change rate, most for and against
//   sg.LightSociety world <name>                 one world's reading on every topic
//   sg.LightSociety router teacher|surrogate|mix <w>|schedule   who decides the changes, from the next batch on
//   sg.LightSociety distill                      distil the surrogate afresh in the next batch
//   sg.LightSociety scenario diffusion|continuous|trust|dictator|repeated|ultimatum [n] [days] [seed]
//                                                a sandbox run of a paper scenario off the game thread, its curves printed
//   sg.LightSociety live [days]                  an ordinary game warped until the society has read that many days (40), checked
//   sg.LightSociety selftest [seed]              sandbox determinism (same seed, 1 vs N threads) and the teacher-vs-surrogate gap
//   sg.LightSociety sway <topic> <world> <push>  the civ hand (sg.Civ sway): through CivHandAsk, so it travels in co-op
// A trailing "quit" exits once the command is done (a sandbox run: once it finishes), e.g. sg.LightSociety selftest quit.
// Router and distill change the game's society: single-player only, like sg.Society's hand.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "GameLaunch.h"
#include "SimClock.h"

#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Populace.h"
#include "Systems/Galaxy.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace
{
	using namespace SpaceGod;
	using Core::Populace;

	UnitySpaceApp* LightSocietyApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	cs::String S(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

	void Say(const cs::String& line) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), *U(line)); }
	void Warn(const cs::String& line) { UE_LOG(LogSpaceGod, Warning, TEXT("%s"), *U(line)); }

	cs::String Hex(uint64_t v)
	{
		char buf[24];
		std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
		return buf;
	}

	cs::String Signed(double v) { return cs::Cat(v >= 0 ? "+" : "", cs::Fmt(v, "0.00")); }
	cs::String Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	cs::String RoutingName(const Populace::LightStatus& st)
	{
		switch (st.Route)
		{
		case Populace::Routing::Teacher: return "teacher";
		case Populace::Routing::Surrogate: return "surrogate";
		case Populace::Routing::Mix: return cs::Cat("mix ", cs::Fmt(st.Weight, "0.00"), " surrogate");
		default: return cs::Cat("schedule (teacher, surrogate from day ", static_cast<int>(Populace::DistillDays()->Value), ")");
		}
	}

	// ---------------------------------------------------------------- the live society

	void Summary(const Populace& pop)
	{
		auto st = pop.Status();
		if (!st.Built)
		{
			Say("sg.LightSociety: the populace has not woken yet (let a day pass)");
			return;
		}
		cs::String distilled = st.Stepping ? cs::String("a batch is stepping")
			: !st.Distilled ? cs::String("no surrogate distilled yet")
			: st.Samples > 0 ? cs::Cat("surrogate distilled (macro-F1 ", cs::Fmt(st.MacroF1, "0.000"), ", change-rate gap ", cs::Fmt(st.ChangeRateGap, "0.000"), ", ", st.Samples, " samples)")
			: cs::String("surrogate distilled (before this load)");
		Say(cs::Cat("sg.LightSociety: ", st.Agents, " agents in ", pop.Groups.Count(), " worlds, read on day ", st.Day, ", readout hash ", Hex(pop.ReadoutHash()),
			"; routing ", RoutingName(st), ", surrogate ", st.Switched ? "switched in" : "not switched in", "; ", distilled, "; ", st.Threads, " threads, cache ",
			st.CacheHits, " hits, ", st.CacheMisses, " misses"));
	}

	void Topics(const Populace& pop)
	{
		for (int t = 0; t < static_cast<int>(Core::Topic::Count_); t++)
		{
			auto topic = static_cast<Core::Topic>(t);
			int worlds = 0;
			Populace::Reading sum;
			double hi = -2, lo = 2;
			cs::String hiName, loName;
			for (const auto& s : pop.Groups)
			{
				Populace::Reading r;
				if (s == nullptr || !pop.Read(s, topic, r)) continue;
				worlds++;
				sum.Mean += r.Mean;
				sum.Supporter += r.Supporter;
				sum.Neutral += r.Neutral;
				sum.Skeptic += r.Skeptic;
				sum.ChangeRate += r.ChangeRate;
				if (r.Mean > hi) { hi = r.Mean; hiName = s->Name; }
				if (r.Mean < lo) { lo = r.Mean; loName = s->Name; }
			}
			if (worlds == 0)
			{
				Say(cs::Cat("  ", Core::ToString(topic), ": not read yet"));
				continue;
			}
			Say(cs::Cat("  ", Core::ToString(topic), ": mean ", Signed(sum.Mean / worlds), " over ", worlds, " worlds; for ", Percent(sum.Supporter / worlds),
				", neutral ", Percent(sum.Neutral / worlds), ", against ", Percent(sum.Skeptic / worlds), "; changing ", cs::Fmt(sum.ChangeRate / worlds * 100, "0.0"),
				"% a day; most for ", hiName, " (", Signed(hi), "), most against ", loName, " (", Signed(lo), ")"));
		}
	}

	void WorldReading(const Populace& pop, const FString& name)
	{
		cs::Ref<Core::Settlement> where;
		for (const auto& s : pop.Groups)
			if (s != nullptr && U(s->Name).Equals(name, ESearchCase::IgnoreCase)) where = s;
		if (where == nullptr)
		{
			Warn(cs::Cat("sg.LightSociety: the populace has no world called '", S(name), "'"));
			return;
		}
		Say(cs::Cat("sg.LightSociety: ", where->Name, " (group ", pop.GroupOf(where), ")"));
		for (int t = 0; t < static_cast<int>(Core::Topic::Count_); t++)
		{
			auto topic = static_cast<Core::Topic>(t);
			Populace::Reading r;
			if (!pop.Read(where, topic, r))
			{
				Say(cs::Cat("  ", Core::ToString(topic), ": not read yet"));
				continue;
			}
			Say(cs::Cat("  ", Core::ToString(topic), ": mean ", Signed(r.Mean), "; for ", Percent(r.Supporter), ", neutral ", Percent(r.Neutral), ", against ",
				Percent(r.Skeptic), "; changing ", cs::Fmt(r.ChangeRate * 100, "0.0"), "% a day"));
		}
	}

	// ---------------------------------------------------------------- sandbox runs, off the game thread

	struct Line
	{
		bool warn = false;
		cs::String text;
	};
	using Lines = std::vector<Line>;

	/// <summary>True while a sandbox run is out (game thread only): one at a time.</summary>
	bool GSandboxBusy = false;

	Lines RunScenario(const cs::String& name, int n, int days, uint64_t seed)
	{
		Lines out;
		double started = FPlatformTime::Seconds();
		auto run = Populace::Sandbox(name, n, days, seed, 0);
		if (!run.Ok)
		{
			out.push_back({true, cs::Cat("sg.LightSociety scenario ", name, ": ", run.Error)});
			return out;
		}
		out.push_back({false, cs::Cat("sg.LightSociety scenario ", name, " (n ", n, ", days ", days, ", seed ", seed, "): blob hash ", Hex(run.Hash), ", ",
			static_cast<int>((FPlatformTime::Seconds() - started) * 1000), " ms")});
		for (const auto& l : run.Lines) out.push_back({false, cs::Cat("  ", l)});
		return out;
	}

	Lines SelfTest(uint64_t seed)
	{
		Lines out;
		int checks = 0, failed = 0;
		auto expect = [&](bool pass, const cs::String& what) {
			checks++;
			if (!pass) failed++;
			out.push_back({!pass, cs::Cat("sg.LightSociety selftest: ", pass ? "PASS " : "FAIL ", what)});
		};
		// One small run of each scenario (the trust run plays every game), their blob hashes combined.
		struct Combined
		{
			bool ok = true;
			uint64_t hash = 1469598103934665603ull;
			cs::String error;
		};
		auto all = [](uint64_t s, int threads) {
			Combined c;
			const struct { const char* name; int n, days; } runs[] = { {"diffusion", 3000, 40}, {"continuous", 1500, 8}, {"trust", 500, 0} };
			for (const auto& r : runs)
			{
				auto run = Populace::Sandbox(r.name, r.n, r.days, s, threads);
				if (!run.Ok)
				{
					c.ok = false;
					c.error = cs::Cat(r.name, ": ", run.Error);
					return c;
				}
				c.hash = (c.hash ^ run.Hash) * 1099511628211ull;
			}
			return c;
		};
		constexpr int ManyThreads = 4;
		Combined a = all(seed, 1), b = all(seed, 1), many = all(seed, ManyThreads), other = all(seed + 1, 1);
		if (!a.ok || !b.ok || !many.ok || !other.ok)
		{
			expect(false, cs::Cat("the sandbox runs: ", !a.ok ? a.error : !b.ok ? b.error : !many.ok ? many.error : other.error));
		}
		else
		{
			expect(a.hash == b.hash, cs::Cat("same seed, same blob hash (", Hex(a.hash), " and ", Hex(b.hash), ")"));
			expect(a.hash == many.hash, cs::Cat("1 thread and ", ManyThreads, " threads, same blob hash (", Hex(a.hash), " and ", Hex(many.hash), ")"));
			expect(a.hash != other.hash, cs::Cat("another seed, another blob hash (", Hex(other.hash), ")"));
		}
		float f1 = 0, gap = 0;
		int samples = 0;
		bool distilled = Populace::SandboxDistill(seed, f1, gap, samples);
		expect(distilled && std::isfinite(gap), distilled ? cs::Cat("teacher-vs-surrogate change-rate gap ", cs::Fmt(gap, "0.0000"), " (macro-F1 ", cs::Fmt(f1, "0.000"), ", ",
			samples, " samples)") : cs::String("teacher-vs-surrogate change-rate gap: the surrogate would not train"));
		out.push_back({failed > 0, cs::Cat("sg.LightSociety selftest: ", failed == 0 ? "PASS" : "FAIL", " (", checks, " checks, ", failed, " failed)")});
		return out;
	}

	/// <summary>Runs work on its own thread and prints its lines on the game thread when it is done.</summary>
	void Launch(TUniqueFunction<Lines()> work, bool quit)
	{
		if (GSandboxBusy)
		{
			Warn("sg.LightSociety: a sandbox run is still out; wait for it");
			if (quit) FPlatformMisc::RequestExit(false);
			return;
		}
		GSandboxBusy = true;
		auto future = std::make_shared<TFuture<Lines>>(Async(EAsyncExecution::Thread, [work = MoveTemp(work)]() mutable -> Lines {
			try
			{
				return work();
			}
			catch (const std::exception& e)
			{
				return Lines{{true, cs::Cat("sg.LightSociety: the sandbox run failed: ", e.what())}};
			}
			catch (...)
			{
				return Lines{{true, cs::String("sg.LightSociety: the sandbox run failed")}};
			}
		}));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([future, quit](float) {
			if (!future->IsReady()) return true;
			for (const Line& l : future->Get())
			{
				if (l.warn) Warn(l.text);
				else Say(l.text);
			}
			GSandboxBusy = false;
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}));
	}

	// ---------------------------------------------------------------- the command

	/// <summary>Everything but a sandbox run: done when it returns.</summary>
	void Live(const TArray<FString>& args, const FString& verb)
	{
		auto* app = LightSocietyApp();
		if (app == nullptr || app->Galaxy == nullptr)
		{
			Warn("sg.LightSociety: no game running");
			return;
		}
		if (verb == TEXT("sway"))
		{
			cs::String line = "sway";
			for (int i = 1; i < args.Num(); i++) line = cs::Cat(line, " ", S(args[i]));
			bool sent = false;
			cs::String done;
			cs::String why = app->CivHandAsk(line, sent, done);
			if (!why.empty()) Warn(cs::Cat("sg.LightSociety: ", why));
			else if (sent) Say(cs::Cat("sg.LightSociety: sent to the party: ", line));
			else Say(cs::Cat("sg.LightSociety: ", done));
			return;
		}
		auto civ = Core::Civilization::Find(*app->Galaxy);
		Populace* pop = civ != nullptr ? civ->Populace.get() : nullptr;
		if (pop == nullptr || !pop->Status().Built)
		{
			Warn("sg.LightSociety: the populace has not woken yet (let a day pass)");
			return;
		}
		if (verb.IsEmpty() || verb == TEXT("summary"))
		{
			Summary(*pop);
			return;
		}
		if (verb == TEXT("topics"))
		{
			Topics(*pop);
			return;
		}
		if (verb == TEXT("world"))
		{
			FString rest;
			for (int i = 1; i < args.Num(); i++) rest += (i > 1 ? TEXT(" ") : TEXT("")) + args[i];
			WorldReading(*pop, rest);
			return;
		}
		if (verb == TEXT("router") || verb == TEXT("distill"))
		{
			if (Coop::CoopPeer::Current() != nullptr)
			{
				Warn("sg.LightSociety: routing and distillation change the society, single-player only for now");
				return;
			}
			if (verb == TEXT("distill"))
			{
				pop->Redistill();
				Say("sg.LightSociety: the surrogate is distilled afresh in the next batch; the teacher decides until the schedule switches it back in");
				return;
			}
			FString how = args.Num() > 1 ? args[1].ToLower() : FString();
			Populace::Routing routing = Populace::Routing::Scheduled;
			float weight = pop->Status().Weight;
			if (how == TEXT("teacher")) routing = Populace::Routing::Teacher;
			else if (how == TEXT("surrogate")) routing = Populace::Routing::Surrogate;
			else if (how == TEXT("schedule")) routing = Populace::Routing::Scheduled;
			else if (how == TEXT("mix") && args.Num() > 2)
			{
				routing = Populace::Routing::Mix;
				weight = FMath::Clamp(FCString::Atof(*args[2]), 0.0f, 1.0f);
			}
			else
			{
				Warn("sg.LightSociety: usage: sg.LightSociety router teacher|surrogate|mix <w>|schedule");
				return;
			}
			pop->Steer(routing, weight);
			Say(cs::Cat("sg.LightSociety: routing ", RoutingName(pop->Status()), " from the next batch on"));
			return;
		}
		Warn("sg.LightSociety: usage: sg.LightSociety [summary | topics | world <name> | router teacher|surrogate|mix <w>|schedule | distill | "
			"scenario diffusion|continuous|trust|dictator|repeated|ultimatum [n] [days] [seed] | selftest [seed] | sway <topic> <world> <push>] [quit]");
	}

	/// <summary>sg.LightSociety live [days]: the society in an ordinary game. Starts a new game when none runs, warps a week
	/// a second, waits until the populace has read at least that many days (default 40, past the distillation switch),
	/// then prints the summary and topics and a PASS/FAIL line per check. Gives up after ten real minutes.</summary>
	void LiveCheck(int days, bool quit)
	{
		struct State { double started = FPlatformTime::Seconds(); bool askedNewGame = false, warped = false; };
		auto st = std::make_shared<State>();
		Say(cs::Cat("sg.LightSociety live: waiting for the society to read day ", days));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([st, days, quit](float) {
			double now = FPlatformTime::Seconds();
			auto finish = [quit](bool ok, const cs::String& line) {
				if (ok) Say(cs::Cat("sg.LightSociety live: ", line));
				else Warn(cs::Cat("sg.LightSociety live: ", line));
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			};
			if (now - st->started > 600.0) return finish(false, "FAIL timed out after 600 s");
			auto* app = LightSocietyApp();
			if (app == nullptr || app->Galaxy == nullptr || app->Clock == nullptr)
			{
				if (!st->askedNewGame && now - st->started > 2.0)
				{
					st->askedNewGame = true;
					Say("sg.LightSociety live: no game running; starting a new one");
					SpaceGod::GameLaunch::NewGame();
				}
				return true;
			}
			if (!st->warped)
			{
				const auto& levels = Core::SimClock::Levels();
				for (int i = 0; i < levels.Count(); i++)
					if (levels[i].Label == "1 week/s") app->Clock->SetWarp(i);
				app->Clock->SetPaused(false);
				st->warped = true;
				Say(cs::Cat("sg.LightSociety live: warp ", app->Clock->Warp().Label));
			}
			auto civ = Core::Civilization::Find(*app->Galaxy);
			Populace* pop = civ != nullptr ? civ->Populace.get() : nullptr;
			if (pop == nullptr) return true;
			auto status = pop->Status();
			if (!status.Built || status.Day < days) return true;
			Summary(*pop);
			Topics(*pop);
			int failed = 0;
			auto expect = [&failed](bool pass, const cs::String& what) {
				if (!pass) failed++;
				if (pass) Say(cs::Cat("sg.LightSociety live: PASS ", what));
				else Warn(cs::Cat("sg.LightSociety live: FAIL ", what));
			};
			expect(status.Agents > 0, cs::Cat(status.Agents, " agents"));
			expect(pop->Groups.Count() > 0, cs::Cat(pop->Groups.Count(), " worlds"));
			expect(pop->ReadoutHash() != 0, cs::Cat("readout hash ", Hex(pop->ReadoutHash())));
			expect(status.Distilled, "a surrogate is distilled");
			expect(status.Switched || status.Route != Populace::Routing::Scheduled, "the schedule switched to the surrogate");
			return finish(failed == 0, failed == 0 ? cs::String("PASS (5 checks, 0 failed)") : cs::Cat("FAIL (5 checks, ", failed, " failed)"));
		}));
	}

	void LightSociety(const TArray<FString>& raw)
	{
		TArray<FString> args;
		bool quit = false;
		for (const FString& a : raw)
		{
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else args.Add(a);
		}
		FString verb = args.Num() > 0 ? args[0].ToLower() : FString();
		auto num = [&args](int i, int fallback) { return args.Num() > i ? FCString::Atoi(*args[i]) : fallback; };

		if (verb == TEXT("scenario"))
		{
			cs::String name = args.Num() > 1 ? S(args[1].ToLower()) : cs::String();
			int n = FMath::Max(0, num(2, 0)), days = FMath::Max(0, num(3, 0));
			uint64_t seed = static_cast<uint64_t>(FMath::Max(0, num(4, 1)));
			Say(cs::Cat("sg.LightSociety scenario ", name, ": running off the game thread"));
			Launch([name, n, days, seed] { return RunScenario(name, n, days, seed); }, quit);
			return;
		}
		if (verb == TEXT("selftest"))
		{
			uint64_t seed = static_cast<uint64_t>(FMath::Max(0, num(1, 981)));
			Say("sg.LightSociety selftest: running off the game thread");
			Launch([seed] { return SelfTest(seed); }, quit);
			return;
		}
		if (verb == TEXT("live"))
		{
			LiveCheck(FMath::Max(1, num(1, 40)), quit);
			return;
		}
		Live(args, verb);
		if (quit) FPlatformMisc::RequestExit(false);
	}

	FAutoConsoleCommand GLightSocietyCommand(
		TEXT("sg.LightSociety"),
		TEXT("US-T981: the populace's opinion model: sg.LightSociety [summary | topics | world <name> | router teacher|surrogate|mix <w>|schedule | distill | scenario <name> [n] [days] [seed] | selftest [seed] | sway <topic> <world> <push>] [quit]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { LightSociety(args); }));
}
