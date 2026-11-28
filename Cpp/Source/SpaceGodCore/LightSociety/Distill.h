#pragma once
// US-T972: Light Society's knowledge distillation, after the paper: capture the teacher's answers over a sweep of feature
// rows, train candidate surrogates (every epoch of every architecture is a checkpoint), shortlist those whose macro-F1 on
// held-out rows is within eps of the best, and keep the one whose change rate on a held-out rollout is closest to the
// teacher's.

#include "LightSociety/AgentStore.h"
#include "LightSociety/Backend.h"
#include "LightSociety/Readout.h"
#include "LightSociety/Surrogate.h"
#include "LightSociety/Types.h"

#include <cstdint>
#include <span>

namespace MindAttic::Society
{
	/// <summary>0 skeptic, 1 neutral, 2 supporter.</summary>
	inline uint32_t StanceClassOf(float stance)
	{
		return stance > SupporterStance ? 2u : (stance < -SupporterStance ? 0u : 1u);
	}

	/// <summary>
	/// Classes an op's output is scored in: its own classes for a class op; the stance class it lands the agent in for
	/// <see cref="Op::Shift"/>; quarters of the share for a game share.
	/// </summary>
	uint32_t OutputClasses(Op op);
	uint32_t OutputClass(Op op, std::span<const float> row, std::span<const float> out);

	/// <summary>
	/// Macro-F1 of predicted against true outputs (OutputWidth(op) floats per row) over the rows' output classes,
	/// averaged over the classes that occur in either.
	/// </summary>
	float MacroF1(Op op, std::span<const float> features, std::span<const float> predicted, std::span<const float> truth);

	/// <summary>
	/// The share of rounds in which an agent's output class changes, rolling a backend forward from each of the first
	/// <paramref name="agents"/> rows for <paramref name="rounds"/> rounds: a persuasion op feeds its answer back into the
	/// agent's stance (a class op's class drawn by the counter hash, so two backends rolled with the same seed share
	/// their draws); a game op steps its run's progress.
	/// </summary>
	float ChangeRate(const IBackend& backend, Op op, std::span<const float> starts, uint32_t agents, uint32_t rounds, uint64_t seed);

	/// <summary>The sweep of feature rows the teacher is asked: profiles x stances x topics.</summary>
	struct CaptureConfig
	{
		uint64_t seed = 0;
		uint32_t profiles = 64;
		/// <summary>Steps of the agent's stance over [-1, 1] (of the share at stake, over [0, 1], for a game op).</summary>
		uint32_t stances = 9;
		uint32_t topics = 4;
	};

	/// <summary>Captures the teacher's answers over synthetic profiles and topics drawn from the seed.</summary>
	TrainSet CaptureSynthetic(const IBackend& teacher, Op op, const CaptureConfig& config);
	/// <summary>
	/// Captures the teacher's answers over profiles of the store's active agents (up to config.profiles, spread evenly
	/// over ascending ids) and the given topics (up to config.topics); falls back to synthetic topics when none are given.
	/// </summary>
	TrainSet CaptureFromStore(const IBackend& teacher, Op op, const AgentStore& store, std::span<const TopicSpec> topics,
		const CaptureConfig& config);

	struct DistillConfig
	{
		/// <summary>Seeds the held-out split, the weights, the training order and the rollout's draws.</summary>
		uint64_t seed = 0;
		/// <summary>Epochs per architecture; each one ends in a candidate checkpoint.</summary>
		uint32_t epochs = 12;
		float learningRate = 0.02f;
		float dropout = 0.1f;
		/// <summary>Candidates whose macro-F1 is within this of the best make the shortlist.</summary>
		float f1Eps = 0.01f;
		/// <summary>Share of the samples held out of training, for scoring.</summary>
		float heldOutShare = 0.2f;
		uint32_t rolloutAgents = 256;
		uint32_t rolloutRounds = 16;
	};

	/// <summary>How the chosen surrogate measured, for the readout and the selftest.</summary>
	struct DistillMetrics
	{
		/// <summary>Held-out macro-F1 against the teacher.</summary>
		float macroF1 = 0;
		/// <summary>|surrogate change rate - teacher change rate| on the held-out rollout.</summary>
		float changeRateGap = 0;
		/// <summary>Teacher samples captured (training and held out).</summary>
		uint32_t samples = 0;
		uint32_t heldOut = 0;
		SurrogateArch arch = SurrogateArch::Softmax;
		/// <summary>The checkpoint's epoch, from 0.</summary>
		uint32_t epoch = 0;
		uint32_t candidates = 0;
		uint32_t shortlisted = 0;
	};

	struct DistillResult
	{
		/// <summary>False when the teacher does not answer the op or the samples left nothing to train or score on.</summary>
		bool ok = false;
		/// <summary>The chosen surrogate, with a head for the samples' op only (<see cref="Surrogate::Adopt"/> merges ops).</summary>
		Surrogate backend;
		DistillMetrics metrics;
	};

	DistillResult Distill(const IBackend& teacher, const TrainSet& samples, const DistillConfig& config);
}
