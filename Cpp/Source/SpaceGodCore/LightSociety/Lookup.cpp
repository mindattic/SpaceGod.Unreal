// US-T972: Light Society's lookup tier.

#include "LightSociety/Lookup.h"

#include "LightSociety/Distill.h"
#include "LightSociety/Readout.h"
#include "LightSociety/Rng.h"

#include <algorithm>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t LookupTag = FourCC('L', 'K', 'U', 'P');
		constexpr uint32_t LookupSalt = 0x900u;
		/// <summary>Self buckets quantize four features, other buckets three.</summary>
		constexpr uint32_t SelfDigits = 4;
		constexpr uint32_t OtherDigits = 3;

		uint32_t PowOf(uint32_t base, uint32_t exponent)
		{
			uint32_t v = 1;
			for (uint32_t i = 0; i < exponent; i++) v *= base;
			return v;
		}

		/// <summary>The level of a feature in [0, 1].</summary>
		uint32_t LevelOf(float x, uint32_t levels)
		{
			const float u = std::clamp(x, 0.0f, 1.0f);
			return std::min(levels - 1, static_cast<uint32_t>(u * static_cast<float>(levels)));
		}

		/// <summary>Digit <paramref name="place"/> of a bucket, the most significant first.</summary>
		uint32_t DigitOf(uint32_t bucket, uint32_t place, uint32_t digits, uint32_t levels)
		{
			return bucket / PowOf(levels, digits - 1 - place) % levels;
		}

		/// <summary>A value inside a level, at <paramref name="u"/> of the way across it.</summary>
		float InLevel(uint32_t level, float u, uint32_t levels)
		{
			return (static_cast<float>(level) + u) / static_cast<float>(levels);
		}

		/// <summary>A stance inside a stance class, at <paramref name="u"/> of the way across it.</summary>
		float InClass(uint32_t stanceClass, float u)
		{
			switch (stanceClass)
			{
			case 0: return -1.0f + u * (1.0f - SupporterStance);
			case 1: return -SupporterStance + u * 2.0f * SupporterStance;
			default: return SupporterStance + u * (1.0f - SupporterStance);
			}
		}
	}

	uint32_t Lookup::SelfBuckets(uint32_t levels)
	{
		return PowOf(levels, SelfDigits);
	}

	uint32_t Lookup::OtherBuckets(uint32_t levels)
	{
		return PowOf(levels, OtherDigits) + 1;
	}

	uint32_t Lookup::SelfBucket(std::span<const float> row, uint32_t levels)
	{
		uint32_t b = LevelOf(row[Feature::SelfEducation], levels);
		b = b * levels + LevelOf(row[Feature::SelfClass], levels);
		b = b * levels + LevelOf(row[Feature::SelfAge], levels);
		return b * levels + LevelOf(row[Feature::SelfOpenness], levels);
	}

	uint32_t Lookup::OtherBucket(std::span<const float> row, uint32_t levels)
	{
		// An event of the world has no speaker: the last bucket.
		if (row[Feature::Speech] <= 0.5f) return PowOf(levels, OtherDigits);
		uint32_t b = LevelOf(row[Feature::OtherEducation], levels);
		b = b * levels + LevelOf(row[Feature::OtherClass], levels);
		return b * levels + LevelOf(row[Feature::OtherAge], levels);
	}

	size_t Lookup::Cell(uint32_t self, uint32_t other, uint32_t selfClass, uint32_t otherClass) const
	{
		const size_t pair = static_cast<size_t>(self) * OtherBuckets(levelCount) + other;
		return (pair * StanceClasses + selfClass) * StanceClasses + otherClass;
	}

	void Lookup::Run(const OpBatch& batch, OpOut& out) const
	{
		const Op op = batch.op;
		const uint32_t rows = batch.Rows();
		out.values.assign(static_cast<size_t>(rows) * OutputWidth(op), 0.0f);
		if (!Supports(op)) return;
		for (uint32_t r = 0; r < rows; r++)
		{
			const std::span<const float> row = batch.Row(r);
			const size_t cell = Cell(SelfBucket(row, levelCount), OtherBucket(row, levelCount), StanceClassOf(row[Feature::SelfStance]),
				StanceClassOf(row[Feature::OtherStance]));
			const float* p = probs.data() + cell * StanceClasses;
			std::copy(p, p + StanceClasses, out.Row(op, r).begin());
		}
	}

	bool Lookup::Build(const IBackend& source, const LookupConfig& config)
	{
		const uint32_t levels = config.levels;
		const uint32_t samples = config.samplesPerCell;
		if (!source.Supports(Op::Stance) || levels == 0 || levels > MaxLevels || samples == 0) return false;

		const uint32_t selfBuckets = SelfBuckets(levels);
		const uint32_t otherBuckets = OtherBuckets(levels);
		const uint32_t world = otherBuckets - 1;
		constexpr uint32_t pairs = StanceClasses * StanceClasses;
		const uint32_t perSelf = otherBuckets * pairs * samples;
		std::vector<float> table(static_cast<size_t>(selfBuckets) * otherBuckets * pairs * StanceClasses, 0.0f);
		std::vector<float> rows(static_cast<size_t>(perSelf) * FeatureCount);
		OpBatch batch;
		batch.op = Op::Stance;
		batch.features = rows;
		OpOut out;
		const float share = 1.0f / static_cast<float>(samples);

		// One source call per self bucket, so the rows in flight stay bounded whatever the levels.
		for (uint32_t sb = 0; sb < selfBuckets; sb++)
		{
			std::fill(rows.begin(), rows.end(), 0.0f);
			for (uint32_t i = 0; i < perSelf; i++)
			{
				const uint32_t s = i % samples;
				const uint32_t cell = i / samples;
				const uint32_t ob = cell / pairs;
				const uint32_t sc = cell % pairs / StanceClasses;
				const uint32_t oc = cell % StanceClasses;
				const uint32_t key = (sb * otherBuckets + ob) * pairs * samples + cell % pairs * samples + s;
				const auto u = [&](uint32_t j) { return static_cast<float>(Rng::Unit(Rng::Hash(config.seed, key, j, LookupSalt))); };
				float* row = rows.data() + static_cast<size_t>(i) * FeatureCount;
				row[Feature::SelfEducation] = InLevel(DigitOf(sb, 0, SelfDigits, levels), u(0), levels);
				row[Feature::SelfClass] = InLevel(DigitOf(sb, 1, SelfDigits, levels), u(1), levels);
				row[Feature::SelfAge] = InLevel(DigitOf(sb, 2, SelfDigits, levels), u(2), levels);
				row[Feature::SelfOpenness] = InLevel(DigitOf(sb, 3, SelfDigits, levels), u(3), levels);
				row[Feature::SelfStance] = InClass(sc, u(4));
				row[Feature::OtherStance] = InClass(oc, u(5));
				row[Feature::SelfConfidence] = u(6);
				row[Feature::SelfAgreeableness] = u(7);
				row[Feature::SelfNeuroticism] = u(8);
				row[Feature::SelfTrust] = u(9);
				row[Feature::Reach] = u(10);
				row[Feature::Stubbornness] = u(11);
				row[Feature::TopicPrior] = 0.5f * (2.0f * u(12) - 1.0f);
				if (ob != world)
				{
					row[Feature::Speech] = 1.0f;
					row[Feature::OtherEducation] = InLevel(DigitOf(ob, 0, OtherDigits, levels), u(13), levels);
					row[Feature::OtherClass] = InLevel(DigitOf(ob, 1, OtherDigits, levels), u(14), levels);
					row[Feature::OtherAge] = InLevel(DigitOf(ob, 2, OtherDigits, levels), u(15), levels);
				}
				for (uint32_t f = 0; f < FeatureCount; f++) row[f] = QuantizeFeature(row[f]);
			}
			source.Run(batch, out);
			float* base = table.data() + static_cast<size_t>(sb) * otherBuckets * pairs * StanceClasses;
			for (uint32_t i = 0; i < perSelf; i++)
			{
				const std::span<const float> p = std::as_const(out).Row(Op::Stance, i);
				float* cell = base + static_cast<size_t>(i / samples) * StanceClasses;
				for (uint32_t k = 0; k < StanceClasses; k++) cell[k] += p[k] * share;
			}
		}
		levelCount = levels;
		probs = std::move(table);
		return true;
	}

	void Lookup::Write(Writer& w) const
	{
		w.U32(LookupTag);
		w.U8(static_cast<uint8_t>(probs.empty() ? 0 : levelCount));
		w.U32(static_cast<uint32_t>(probs.size()));
		for (const float v : probs) w.F32(v);
	}

	bool Lookup::Read(Reader& r)
	{
		if (!r.Expect(LookupTag)) return false;
		const uint8_t levels = r.U8();
		if (!r.Ok() || levels > MaxLevels)
		{
			r.Fail();
			return false;
		}
		const size_t expected = levels == 0 ? 0
			: static_cast<size_t>(SelfBuckets(levels)) * OtherBuckets(levels) * StanceClasses * StanceClasses * StanceClasses;
		const uint32_t n = r.Count(sizeof(float), static_cast<uint32_t>(expected));
		if (!r.Ok() || n != expected)
		{
			r.Fail();
			return false;
		}
		std::vector<float> read(n);
		for (float& v : read) v = r.F32();
		if (!r.Ok()) return false;
		levelCount = levels;
		probs = std::move(read);
		return true;
	}
}
