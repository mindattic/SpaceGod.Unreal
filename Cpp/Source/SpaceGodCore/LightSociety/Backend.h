#pragma once
// US-T971: Light Society's backends, the paper's mixture of models: every operation request is a batch of feature rows
// that a registered backend (the teacher, a distilled surrogate, a lookup table) turns into class probabilities or
// continuous deltas. The feature row and output layouts are defined here once, for the teacher and the surrogates and
// lookup tables distilled from it (US-T972).

#include "LightSociety/Types.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	/// <summary>An operation a backend answers.</summary>
	enum class Op : uint8_t
	{
		/// <summary>Persuasion as a class: probabilities of the agent's next stance class {skeptic, neutral, supporter}.</summary>
		Stance = 0,
		/// <summary>Persuasion as a step: {dStance, dConfidence}.</summary>
		Shift = 1,
		/// <summary>Trust game, trustor: the share of the endowment sent (it is tripled on the way), {share}.</summary>
		TrustSend = 2,
		/// <summary>Trust game, trustee: the share of what arrived that is sent back, {share}.</summary>
		TrustReturn = 3,
		/// <summary>Dictator game: the share of the endowment given away, {share}.</summary>
		DictatorGive = 4,
		/// <summary>Ultimatum game, proposer: the share offered, {share}.</summary>
		UltimatumOffer = 5,
		/// <summary>Ultimatum game, responder: probabilities {reject, accept}.</summary>
		UltimatumAnswer = 6,
		Count_ = 7,
	};

	inline constexpr uint32_t OpCount = static_cast<uint32_t>(Op::Count_);

	/// <summary>
	/// One sample's feature row: Feature::Count floats, each quantized by <see cref="QuantizeFeature"/>. "Self" is the
	/// agent deciding; "Other" is who it faces (the speaker of a persuasion, the partner in a game). Fields an op does not
	/// use are 0.
	/// </summary>
	namespace Feature
	{
		enum : uint32_t
		{
			/// <summary>The agent's stance on the topic, in [-1, 1].</summary>
			SelfStance,
			/// <summary>Its confidence in that stance, in [0, 1].</summary>
			SelfConfidence,
			SelfEducation,
			SelfClass,
			/// <summary>Age in years / 100, at most 1.</summary>
			SelfAge,
			SelfOpenness,
			SelfAgreeableness,
			SelfNeuroticism,
			/// <summary>The agent's general trust, in [0, 1].</summary>
			SelfTrust,
			/// <summary>The stance pushed at the agent (an event's push, a speaker's stance), in [-1, 1].</summary>
			OtherStance,
			OtherEducation,
			OtherClass,
			OtherAge,
			/// <summary>How strongly it reaches the agent: magnitude times edge weight, in [0, 1].</summary>
			Reach,
			/// <summary>The topic's stubbornness, in [0, 1].</summary>
			Stubbornness,
			/// <summary>The topic's prior mean, in [-1, 1].</summary>
			TopicPrior,
			/// <summary>1 when another agent speaks (persuasion by a peer), 0 for an event of the world.</summary>
			Speech,
			/// <summary>Games: the share at stake (sent by the trustor, offered by the proposer), in [0, 1].</summary>
			Amount,
			/// <summary>Repeated games: the partner's last return share, 0.5 when there is none, in [0, 1].</summary>
			Reciprocity,
			/// <summary>Repeated games: how far through the run, in [0, 1].</summary>
			Progress,
			Count,
		};
	}

	inline constexpr uint32_t FeatureCount = Feature::Count;
	/// <summary>Feature quantization: steps per unit. A quantized feature is an exact int16 times 1 / FeatureScale.</summary>
	inline constexpr float FeatureScale = 1024.0f;

	/// <summary>A feature rounded to the grid every backend sees, so equal cache keys mean equal inputs.</summary>
	inline int16_t FeatureStep(float x)
	{
		const float v = std::isfinite(x) ? x * FeatureScale : 0.0f;
		const float c = v < -32767.0f ? -32767.0f : (v > 32767.0f ? 32767.0f : v);
		return static_cast<int16_t>(std::lround(c));
	}

	inline float QuantizeFeature(float x)
	{
		return static_cast<float>(FeatureStep(x)) / FeatureScale;
	}

	/// <summary>Stance classes of <see cref="Op::Stance"/>'s output.</summary>
	enum class StanceClass : uint8_t
	{
		Skeptic = 0,
		Neutral = 1,
		Supporter = 2,
	};

	/// <summary>Answers of <see cref="Op::UltimatumAnswer"/>'s output.</summary>
	enum class Answer : uint8_t
	{
		Reject = 0,
		Accept = 1,
	};

	/// <summary>Most floats an op writes per row.</summary>
	inline constexpr uint32_t MaxOutputs = 3;

	/// <summary>Floats per output row of an op.</summary>
	constexpr uint32_t OutputWidth(Op op)
	{
		switch (op)
		{
		case Op::Stance: return 3;
		case Op::Shift: return 2;
		case Op::UltimatumAnswer: return 2;
		default: return 1;
		}
	}

	/// <summary>True when an op's output is class probabilities (summing to 1) rather than continuous values.</summary>
	constexpr bool IsClassOp(Op op)
	{
		return op == Op::Stance || op == Op::UltimatumAnswer;
	}

	/// <summary>One call to a backend: Rows() feature rows of one op, row-major.</summary>
	struct OpBatch
	{
		Op op = Op::Shift;
		std::span<const float> features;

		uint32_t Rows() const { return static_cast<uint32_t>(features.size() / FeatureCount); }
		std::span<const float> Row(uint32_t i) const { return features.subspan(static_cast<size_t>(i) * FeatureCount, FeatureCount); }
	};

	/// <summary>A backend's answer: OutputWidth(op) floats per row, in row order.</summary>
	struct OpOut
	{
		std::vector<float> values;

		std::span<float> Row(Op op, uint32_t i)
		{
			const uint32_t w = OutputWidth(op);
			return std::span<float>(values).subspan(static_cast<size_t>(i) * w, w);
		}
		std::span<const float> Row(Op op, uint32_t i) const
		{
			const uint32_t w = OutputWidth(op);
			return std::span<const float>(values).subspan(static_cast<size_t>(i) * w, w);
		}
	};

	/// <summary>
	/// A model that answers operations. Run must be a pure function of the batch (the same rows give the same values,
	/// bit for bit, whatever came before), since the router caches answers by row; and it must be safe to call from
	/// several threads at once. It sizes out.values to Rows() * OutputWidth(op).
	/// </summary>
	class IBackend
	{
	public:
		virtual ~IBackend() = default;
		virtual bool Supports(Op op) const = 0;
		virtual void Run(const OpBatch& batch, OpOut& out) const = 0;
	};

	/// <summary>The tiers of the backend table, from the most faithful to the cheapest.</summary>
	enum class BackendKind : uint8_t
	{
		Teacher = 0,
		Surrogate = 1,
		Lookup = 2,
		Count_ = 3,
	};

	inline constexpr uint32_t BackendKindCount = static_cast<uint32_t>(BackendKind::Count_);

	/// <summary>The backend table: per op and tier, the backend registered for it (not owned), or null.</summary>
	class BackendTable
	{
	public:
		/// <summary>Registers a backend for one op and tier (null unregisters); false when it does not support the op.</summary>
		bool Register(BackendKind kind, Op op, const IBackend* backend)
		{
			if (kind >= BackendKind::Count_ || op >= Op::Count_) return false;
			if (backend && !backend->Supports(op)) return false;
			slots[static_cast<uint32_t>(kind) * OpCount + static_cast<uint32_t>(op)] = backend;
			return true;
		}

		/// <summary>Registers a backend for every op it supports.</summary>
		void RegisterAll(BackendKind kind, const IBackend* backend)
		{
			for (uint32_t o = 0; o < OpCount; o++)
			{
				if (backend && backend->Supports(static_cast<Op>(o))) Register(kind, static_cast<Op>(o), backend);
			}
		}

		const IBackend* Find(BackendKind kind, Op op) const
		{
			if (kind >= BackendKind::Count_ || op >= Op::Count_) return nullptr;
			return slots[static_cast<uint32_t>(kind) * OpCount + static_cast<uint32_t>(op)];
		}

	private:
		const IBackend* slots[BackendKindCount * OpCount] = {};
	};
}
