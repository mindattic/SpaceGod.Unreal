#pragma once
// US-T972: Light Society's lookup tier, the cheapest backend: a table of stance-class probabilities precomputed from the
// selected surrogate (Distill.h) by profile bucket and stance class, answered by one index per row.

#include "LightSociety/Backend.h"
#include "LightSociety/Codec.h"

#include <cstdint>
#include <span>
#include <vector>

namespace MindAttic::Society
{
	struct LookupConfig
	{
		/// <summary>Quantization levels per profile feature, 1 to <see cref="Lookup::MaxLevels"/>.</summary>
		uint32_t levels = 2;
		/// <summary>Rows drawn inside each cell and averaged into its probabilities.</summary>
		uint32_t samplesPerCell = 8;
		uint64_t seed = 0;
	};

	/// <summary>
	/// Answers <see cref="Op::Stance"/> from a table [self bucket x other bucket x self stance class x other stance class]
	/// of class probabilities. The self bucket quantizes the agent's education, class, age and openness; the other bucket
	/// the speaker's education, class and age (the row carries no openness for it), plus one bucket for an event of the
	/// world, which has no speaker. The other stance class is that of the push. Pure and thread-safe once built.
	/// </summary>
	class Lookup final : public IBackend
	{
	public:
		static constexpr uint32_t MaxLevels = 4;
		static constexpr uint32_t StanceClasses = 3;

		bool Supports(Op op) const override { return op == Op::Stance && !probs.empty(); }
		void Run(const OpBatch& batch, OpOut& out) const override;

		/// <summary>
		/// Fills the table by averaging the source's <see cref="Op::Stance"/> answers over rows drawn inside each cell; false
		/// (and the table left as it was) when the source does not answer it or the config is out of range.
		/// </summary>
		bool Build(const IBackend& source, const LookupConfig& config);

		uint32_t Levels() const { return levelCount; }
		size_t Cells() const { return probs.size() / StanceClasses; }

		static uint32_t SelfBuckets(uint32_t levels);
		static uint32_t OtherBuckets(uint32_t levels);
		static uint32_t SelfBucket(std::span<const float> row, uint32_t levels);
		static uint32_t OtherBucket(std::span<const float> row, uint32_t levels);

		void Write(Writer& w) const;
		/// <summary>Reads what <see cref="Write"/> wrote; leaves the table unchanged and returns false on bad input.</summary>
		bool Read(Reader& r);

	private:
		size_t Cell(uint32_t self, uint32_t other, uint32_t selfClass, uint32_t otherClass) const;

		uint32_t levelCount = 0;
		/// <summary><see cref="StanceClasses"/> probabilities per cell.</summary>
		std::vector<float> probs;
	};
}
