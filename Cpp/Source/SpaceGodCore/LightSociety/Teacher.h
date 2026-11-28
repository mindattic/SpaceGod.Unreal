#pragma once
// US-T971: Light Society's teacher, the in-engine stand-in for the paper's LLM: rule and utility policies grounded in the
// paper's findings, answering every op of Backend.h. The surrogates and lookup tables (US-T972) are distilled from it.

#include "LightSociety/Backend.h"

#include <span>

namespace MindAttic::Society
{
	/// <summary>
	/// Persuasion: bounded confidence (a push from within the agent's reach pulls it along, scaled by reach, the topic's
	/// give, its leaning with the topic's prior, the speaker's education and class, and the agent's confidence, education
	/// and class); a push from too far off meets reactance, which only shakes the agent toward neutral; and no single step
	/// carries a committed agent past neutral, so change runs through neutral (the paper's U-shaped agree curve).
	/// Games: trust rises with class and education and falls with an age gap; trustees reciprocate with inequity aversion;
	/// dictators give about a fifth; proposers offer about 40%, less as a run goes on; responders reject low offers, most
	/// readily mid-run. Pure and thread-safe.
	/// </summary>
	class Teacher final : public IBackend
	{
	public:
		bool Supports(Op op) const override { return op < Op::Count_; }
		void Run(const OpBatch& batch, OpOut& out) const override;

		/// <summary>One row of <see cref="Op::Shift"/>: writes {dStance, dConfidence}.</summary>
		static void Shift(std::span<const float> row, std::span<float> out);
		/// <summary>One row of <see cref="Op::Stance"/>: writes the probabilities of the next stance class.</summary>
		static void Stance(std::span<const float> row, std::span<float> out);
		/// <summary>One row of a game op: writes its share or its {reject, accept} probabilities.</summary>
		static void Game(Op op, std::span<const float> row, std::span<float> out);
	};
}
