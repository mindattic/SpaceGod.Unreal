// US-T971: Light Society's teacher.

#include "LightSociety/Teacher.h"

#include "LightSociety/Readout.h"

#include <algorithm>
#include <cmath>

namespace MindAttic::Society
{
	namespace
	{
		constexpr float Pi = 3.14159265f;

		float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		/// <summary>0 skeptic, 1 neutral, 2 supporter.</summary>
		uint32_t ClassOf(float s)
		{
			return s > SupporterStance ? 2u : (s < -SupporterStance ? 0u : 1u);
		}
	}

	void Teacher::Shift(std::span<const float> row, std::span<float> out)
	{
		const float s = row[Feature::SelfStance];
		const float c = row[Feature::SelfConfidence];
		const float edu = row[Feature::SelfEducation];
		const float cls = row[Feature::SelfClass];
		const float openness = row[Feature::SelfOpenness];
		const float push = row[Feature::OtherStance];
		const float reach = row[Feature::Reach];
		const float give = 1.0f - row[Feature::Stubbornness];
		const float prior = row[Feature::TopicPrior];
		const bool speech = row[Feature::Speech] > 0.5f;
		const float gap = push - s;

		// Bounded confidence: how far off a push may come from and still be heard out.
		const float bound = 1.0f + 0.4f * openness - 0.5f * c;
		float ds;
		float dc;
		if (std::fabs(gap) > bound)
		{
			// Reactance: not moved toward the push, only shaken loose toward neutral.
			ds = -s * 0.15f * reach * give;
			dc = -0.02f * reach;
		}
		else
		{
			// An educated, well-off speaker persuades more; an educated, well-off, confident listener yields less.
			const float persuade = speech ? 0.5f + 0.5f * (0.6f * row[Feature::OtherEducation] + 0.4f * row[Feature::OtherClass]) : 1.0f;
			const float yield = (1.0f - 0.5f * c) * (1.0f - 0.35f * edu - 0.25f * cls) * (0.8f + 0.4f * openness);
			// A push the topic's prior leans with lands more easily.
			const float lean = 1.0f + 0.5f * push * prior;
			ds = 0.6f * gap * reach * give * persuade * yield * lean;
			// A committed agent on the far side moves at most to neutral in one step.
			if (std::fabs(s) > SupporterStance && s * push < 0.0f) ds = s < 0.0f ? std::min(ds, -s) : std::max(ds, -s);
			dc = (push * s >= 0.0f ? 0.05f : -0.03f) * reach;
		}
		out[0] = Clamp(s + ds, -1.0f, 1.0f) - s;
		out[1] = Clamp(c + dc, 0.0f, 1.0f) - c;
	}

	void Teacher::Stance(std::span<const float> row, std::span<float> out)
	{
		float step[2];
		Shift(row, step);
		const float s = row[Feature::SelfStance];
		const float ds = step[0];
		const uint32_t from = ClassOf(s);
		out[0] = 0.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
		const int dir = ds > 0.0f ? 1 : (ds < 0.0f ? -1 : 0);
		const int to = std::clamp(static_cast<int>(from) + dir, 0, 2);
		if (to == static_cast<int>(from))
		{
			out[from] = 1.0f;
			return;
		}
		// One class at a time: the chance of the step is how far the shift carries toward the next class's edge.
		const float edge = dir > 0 ? (from == 0 ? -SupporterStance : SupporterStance) : (from == 2 ? SupporterStance : -SupporterStance);
		const float p = std::min(0.9f, std::fabs(ds) / std::max(std::fabs(edge - s), 0.05f));
		out[from] = 1.0f - p;
		out[static_cast<uint32_t>(to)] = p;
	}

	void Teacher::Game(Op op, std::span<const float> row, std::span<float> out)
	{
		const float edu = row[Feature::SelfEducation];
		const float cls = row[Feature::SelfClass];
		const float agree = row[Feature::SelfAgreeableness];
		const float neuro = row[Feature::SelfNeuroticism];
		const float trust = row[Feature::SelfTrust];
		const float ageGap = std::fabs(row[Feature::SelfAge] - row[Feature::OtherAge]);
		const float amount = row[Feature::Amount];
		const float reciprocity = row[Feature::Reciprocity];
		const float progress = row[Feature::Progress];

		switch (op)
		{
		case Op::TrustSend:
		{
			// Trust rises with class and education, with the partner's standing and its last return; falls with an age gap.
			const float partner = 0.05f * (row[Feature::OtherEducation] + row[Feature::OtherClass] - 1.0f);
			out[0] = Clamp(0.25f + 0.2f * cls + 0.15f * edu + 0.2f * (trust - 0.5f) + 0.1f * (agree - 0.5f) + partner
				- 0.2f * ageGap + 0.4f * (reciprocity - 0.5f), 0.0f, 1.0f);
			break;
		}
		case Op::TrustReturn:
		{
			// Inequity aversion: the share of the tripled amount that evens both payoffs, weighed by agreeableness and
			// education; reciprocity: the more trusted, the more returned.
			const float fair = amount > 0.25f ? (4.0f * amount - 1.0f) / (6.0f * amount) : 0.0f;
			const float aversion = 0.4f + 0.4f * agree + 0.2f * edu;
			out[0] = Clamp(aversion * fair + 0.25f * amount * (0.5f + 0.5f * trust), 0.0f, 0.9f);
			break;
		}
		case Op::DictatorGive:
			out[0] = Clamp(0.08f + 0.25f * agree + 0.07f * edu + 0.05f * (trust - 0.5f), 0.0f, 0.5f);
			break;
		case Op::UltimatumOffer:
			// About 40%, a little more from the agreeable and the anxious, declining as a run goes on.
			out[0] = Clamp(0.4f + 0.1f * (agree - 0.5f) + 0.05f * (neuro - 0.5f) - 0.1f * progress, 0.05f, 0.6f);
			break;
		case Op::UltimatumAnswer:
		{
			// Low offers are rejected; spite peaks mid-run.
			const float threshold = 0.25f + 0.1f * (neuro - 0.5f) + 0.05f * (cls - 0.5f) - 0.05f * (agree - 0.5f)
				+ 0.08f * std::sin(Pi * progress);
			const float accept = 1.0f / (1.0f + std::exp(-16.0f * (amount - threshold)));
			out[0] = 1.0f - accept;
			out[1] = accept;
			break;
		}
		default:
			for (float& v : out) v = 0.0f;
			break;
		}
	}

	void Teacher::Run(const OpBatch& batch, OpOut& out) const
	{
		const uint32_t rows = batch.Rows();
		out.values.assign(static_cast<size_t>(rows) * OutputWidth(batch.op), 0.0f);
		for (uint32_t i = 0; i < rows; i++)
		{
			const std::span<const float> row = batch.Row(i);
			const std::span<float> o = out.Row(batch.op, i);
			switch (batch.op)
			{
			case Op::Shift: Shift(row, o); break;
			case Op::Stance: Stance(row, o); break;
			default: Game(batch.op, row, o); break;
			}
		}
	}
}
