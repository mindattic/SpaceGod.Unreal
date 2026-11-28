#pragma once
// US-T972: Light Society's surrogates, the paper's distilled students: small networks trained on the teacher's answers
// (Teacher.h) that answer the same ops far more cheaply. Training is deterministic: fixed epochs, a fixed sample order
// per epoch from a hash permutation, dropout drawn from the counter hash, float32, one thread, so the same samples and
// seed give the same weights bit for bit.

#include "LightSociety/Backend.h"
#include "LightSociety/Codec.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>A surrogate's network.</summary>
	enum class SurrogateArch : uint8_t
	{
		/// <summary>One linear layer: softmax regression for a class op, linear regression for a continuous one.</summary>
		Softmax = 0,
		/// <summary>Two hidden ReLU layers (<see cref="Surrogate::Hidden1"/>, <see cref="Surrogate::Hidden2"/>) with dropout.</summary>
		Mlp = 1,
		Count_ = 2,
	};

	/// <summary>The teacher's answers for one op: feature rows and the outputs it gave for them.</summary>
	struct TrainSet
	{
		Op op = Op::Stance;
		/// <summary>FeatureCount floats per row, row-major.</summary>
		std::vector<float> features;
		/// <summary>OutputWidth(op) floats per row, in row order.</summary>
		std::vector<float> targets;

		uint32_t Rows() const { return static_cast<uint32_t>(features.size() / FeatureCount); }
		std::span<const float> Row(uint32_t i) const
		{
			return std::span<const float>(features).subspan(static_cast<size_t>(i) * FeatureCount, FeatureCount);
		}
		std::span<const float> Target(uint32_t i) const
		{
			const uint32_t w = OutputWidth(op);
			return std::span<const float>(targets).subspan(static_cast<size_t>(i) * w, w);
		}
	};

	struct TrainConfig
	{
		/// <summary>Seeds the sample order and the dropout draws.</summary>
		uint64_t seed = 0;
		float learningRate = 0.02f;
		/// <summary>Share of hidden units dropped per sample (MLP only).</summary>
		float dropout = 0.1f;
	};

	/// <summary>
	/// A distilled backend: per op, a head of one <see cref="SurrogateArch"/>. A class op's head ends in a softmax trained
	/// by cross-entropy against the teacher's probabilities; a continuous op's head is a regression trained by squared
	/// error. Run is pure and thread-safe; Init and Epoch are not.
	/// </summary>
	class Surrogate final : public IBackend
	{
	public:
		static constexpr uint32_t Hidden1 = 64;
		static constexpr uint32_t Hidden2 = 32;

		bool Supports(Op op) const override;
		void Run(const OpBatch& batch, OpOut& out) const override;

		/// <summary>Starts a fresh head for an op, its weights drawn from <paramref name="seed"/>.</summary>
		void Init(Op op, SurrogateArch arch, uint64_t seed);
		/// <summary>
		/// One epoch of per-sample SGD on the listed rows of the set's op, in an order fixed by (seed, epoch). Does nothing
		/// when the op has no head.
		/// </summary>
		void Epoch(const TrainSet& set, std::span<const uint32_t> rows, const TrainConfig& config, uint32_t epoch);
		/// <summary>Copies another surrogate's head for an op into this one (or drops this one's, when it has none).</summary>
		void Adopt(Op op, const Surrogate& from);

		SurrogateArch Arch(Op op) const { return heads[static_cast<uint32_t>(op)].arch; }
		/// <summary>Weights and biases in an op's head, 0 when it has none.</summary>
		size_t Parameters(Op op) const;

		void Write(Writer& w) const;
		/// <summary>Reads what <see cref="Write"/> wrote; leaves the surrogate unchanged and returns false on bad input.</summary>
		bool Read(Reader& r);

	private:
		struct Head
		{
			bool ready = false;
			SurrogateArch arch = SurrogateArch::Softmax;
			/// <summary>Per layer: weights (out x in, row-major), then biases.</summary>
			std::vector<float> weights;
		};

		std::array<Head, OpCount> heads;
	};
}
