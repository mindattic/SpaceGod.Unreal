// US-T981: sg.LightSociety's sandbox (docs/research/LightSociety-design.md): the library's paper scenarios and a
// distillation, each on a society of its own apart from the game's, for the console to run off the game thread.

#include "Systems/Civilization/Populace.h"

#include "LightSociety/Codec.h"
#include "LightSociety/Distill.h"
#include "LightSociety/Router.h"
#include "LightSociety/Scenarios/Scenario.h"
#include "LightSociety/Teacher.h"
#include "LightSociety/Worker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

namespace SpaceGod::Core
{
	namespace LS = MindAttic::Society;

	namespace
	{
		/// <summary>Agents per work chunk in a sandbox: small, so even a small run splits into several chunks.</summary>
		constexpr uint32_t SandboxChunk = 256;
		/// <summary>About how many rows of a long curve are shown.</summary>
		constexpr uint32_t ShownRows = 24;
		/// <summary>The diffusion columns shown: round, fired, rows, agree, neutral, disagree (the moves are left out).</summary>
		constexpr uint32_t DiffusionColumns = 6;
		// The games scenario's columns (Scenario.h, MakeGames).
		constexpr uint32_t GameColumn = 1;
		constexpr uint32_t IterationColumn = 2;
		constexpr uint32_t BandKindColumn = 3;

		uint64_t Fnv(const std::vector<uint8_t>& bytes)
		{
			uint64_t h = 1469598103934665603ull;
			for (uint8_t b : bytes)
			{
				h ^= b;
				h *= 1099511628211ull;
			}
			return h;
		}

		cs::String Cell(double v)
		{
			if (std::isnan(v)) return "-";
			char buf[32];
			if (v == std::floor(v) && std::fabs(v) < 1e9) std::snprintf(buf, sizeof buf, "%.0f", v);
			else std::snprintf(buf, sizeof buf, "%.3f", v);
			return buf;
		}

		void Print(const LS::Series& s, const std::vector<uint32_t>& rows, const std::vector<uint32_t>& columns, cs::List<cs::String>& lines)
		{
			cs::String head;
			for (uint32_t c : columns)
			{
				if (!head.empty()) head += ' ';
				head += s.columns[c];
			}
			lines.Add(head);
			for (uint32_t r : rows)
			{
				cs::String line;
				for (uint32_t c : columns)
				{
					if (!line.empty()) line += ' ';
					line += Cell(s.At(r, c));
				}
				lines.Add(line);
			}
		}

		/// <summary>Every row of a short curve; about <see cref="ShownRows"/> evenly spread, and the last, of a long one.</summary>
		std::vector<uint32_t> Spread(uint32_t count)
		{
			std::vector<uint32_t> rows;
			const uint32_t stride = std::max<uint32_t>(1, (count + ShownRows - 1) / ShownRows);
			for (uint32_t r = 0; r < count; r++)
				if (r % stride == 0 || r + 1 == count) rows.push_back(r);
			return rows;
		}
	}

	Populace::SandboxRun Populace::Sandbox(const cs::String& scenario, int n, int days, uint64_t seed, int threads)
	{
		SandboxRun run;
		const uint32_t agents = static_cast<uint32_t>(std::clamp(n, 0, static_cast<int>(LS::MaxAgents)));
		const uint32_t rounds = static_cast<uint32_t>(std::max(days, 0));
		// The games scenario's rows of one game (two for the ultimatum: once, then iterated); -1 for another scenario.
		int gameFirst = -1, gameLast = -1;
		std::unique_ptr<LS::Scenario> sc;
		if (scenario == "diffusion")
		{
			LS::DiffusionSpec spec;
			if (agents > 0) spec.agents = agents;
			if (rounds > 0) spec.rounds = rounds;
			sc = LS::MakeDiffusion(spec);
		}
		else if (scenario == "continuous")
		{
			LS::ContinuousSpec spec;
			if (agents > 0) spec.agents = agents;
			if (rounds > 0) spec.days = rounds;
			sc = LS::MakeContinuous(spec);
		}
		else
		{
			if (scenario == "trust") gameFirst = gameLast = 0;
			else if (scenario == "dictator") gameFirst = gameLast = 1;
			else if (scenario == "repeated") gameFirst = gameLast = 2;
			else if (scenario == "ultimatum") gameFirst = 3, gameLast = 4;
			else
			{
				run.Error = cs::Cat("no scenario called '", scenario, "' (diffusion, continuous, trust, dictator, repeated, ultimatum)");
				return run;
			}
			LS::GamesSpec spec;
			if (agents > 0) spec.pairs = agents;
			if (rounds > 0)
			{
				spec.repeatedTrustRounds = rounds;
				spec.ultimatumRounds = rounds;
			}
			sc = LS::MakeGames(spec);
		}

		LS::Config config;
		config.seed = seed;
		config.chunk = SandboxChunk;
		LS::Router router;
		LS::ChunkPool pool(static_cast<uint32_t>(std::max(threads, 1)));
		try
		{
			sc->Setup(config, router, &pool);
			sc->Run();
		}
		catch (const std::exception& e)
		{
			run.Error = e.what();
			return run;
		}
		catch (...)
		{
			run.Error = "the scenario failed";
			return run;
		}

		const LS::Series& s = sc->Output();
		LS::Writer w;
		w.U32(s.Width());
		w.U32(s.Rows());
		for (double v : s.values) w.F64(v);
		run.Hash = Fnv(w.Bytes());

		std::vector<uint32_t> rows, columns;
		if (gameFirst < 0)
		{
			rows = Spread(s.Rows());
			const uint32_t width = scenario == "diffusion" ? std::min(DiffusionColumns, s.Width()) : s.Width();
			for (uint32_t c = 0; c < width; c++) columns.push_back(c);
		}
		else
		{
			// Every iteration's all-agents row, and the last iteration's bands by class, education and age.
			double lastIteration = 0;
			for (uint32_t r = 0; r < s.Rows(); r++)
			{
				const double g = s.At(r, GameColumn);
				if (g >= gameFirst && g <= gameLast) lastIteration = std::max(lastIteration, s.At(r, IterationColumn));
			}
			for (uint32_t r = 0; r < s.Rows(); r++)
			{
				const double g = s.At(r, GameColumn);
				if (g < gameFirst || g > gameLast) continue;
				if (s.At(r, BandKindColumn) == 0 || s.At(r, IterationColumn) == lastIteration) rows.push_back(r);
			}
			// Columns this game has (a game's missing measures are NaN), past the round.
			for (uint32_t c = 1; c < s.Width(); c++)
			{
				bool any = false;
				for (uint32_t r : rows) any = any || !std::isnan(s.At(r, c));
				if (any) columns.push_back(c);
			}
		}
		Print(s, rows, columns, run.Lines);
		run.Ok = true;
		return run;
	}

	bool Populace::SandboxDistill(uint64_t seed, float& macroF1, float& changeRateGap, int& samples)
	{
		macroF1 = changeRateGap = 0;
		samples = 0;
		LS::Teacher teacher;
		LS::CaptureConfig capture;
		capture.seed = seed;
		LS::TrainSet set = LS::CaptureSynthetic(teacher, LS::Op::Shift, capture);
		LS::DistillConfig distill;
		distill.seed = seed;
		LS::DistillResult result = LS::Distill(teacher, set, distill);
		if (!result.ok) return false;
		macroF1 = result.metrics.macroF1;
		changeRateGap = result.metrics.changeRateGap;
		samples = static_cast<int>(result.metrics.samples);
		return true;
	}
}
