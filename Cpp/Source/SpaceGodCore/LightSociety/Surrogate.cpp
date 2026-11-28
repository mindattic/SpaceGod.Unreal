// US-T972: Light Society's surrogates.

#include "LightSociety/Surrogate.h"

#include "LightSociety/Rng.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MindAttic::Society
{
	namespace
	{
		constexpr uint32_t SurrogateTag = FourCC('S', 'U', 'R', 'R');
		/// <summary>Salts of the counter hash's op field, so each use draws its own stream.</summary>
		constexpr uint32_t InitSalt = 0x100u;
		constexpr uint32_t OrderSalt = 0x200u;
		constexpr uint32_t DropSalt = 0x300u;

		constexpr uint32_t MaxLayers = 3;
		constexpr uint32_t MaxWidth = 64;
		static_assert(FeatureCount <= MaxWidth && Surrogate::Hidden1 <= MaxWidth && Surrogate::Hidden2 <= MaxWidth
			&& MaxOutputs <= MaxWidth);

		struct Shape
		{
			uint32_t layers = 0;
			uint32_t in[MaxLayers] = {};
			uint32_t out[MaxLayers] = {};
			/// <summary>Where each layer's weights start in the head's packed weights.</summary>
			size_t offset[MaxLayers] = {};
			size_t size = 0;
		};

		Shape ShapeOf(SurrogateArch arch, Op op)
		{
			Shape s;
			const uint32_t w = OutputWidth(op);
			if (arch == SurrogateArch::Mlp)
			{
				s.layers = 3;
				s.in[0] = FeatureCount;
				s.out[0] = Surrogate::Hidden1;
				s.in[1] = Surrogate::Hidden1;
				s.out[1] = Surrogate::Hidden2;
				s.in[2] = Surrogate::Hidden2;
				s.out[2] = w;
			}
			else
			{
				s.layers = 1;
				s.in[0] = FeatureCount;
				s.out[0] = w;
			}
			for (uint32_t l = 0; l < s.layers; l++)
			{
				s.offset[l] = s.size;
				s.size += static_cast<size_t>(s.out[l]) * s.in[l] + s.out[l];
			}
			return s;
		}

		/// <summary>Each layer's input (a[0] is the feature row, a[layers] the output's logits) and the dropout masks.</summary>
		struct Activations
		{
			float a[MaxLayers + 1][MaxWidth] = {};
			float mask[MaxLayers][MaxWidth] = {};
		};

		/// <summary>Runs the head on act.a[0]. With dropout above 0, hidden units are dropped by draws keyed by dropKey.</summary>
		void Forward(const Shape& s, const float* params, Activations& act, float dropout, uint64_t dropKey)
		{
			const float keep = dropout > 0.0f ? 1.0f / (1.0f - dropout) : 1.0f;
			for (uint32_t l = 0; l < s.layers; l++)
			{
				const uint32_t in = s.in[l];
				const uint32_t out = s.out[l];
				const float* w = params + s.offset[l];
				const float* b = w + static_cast<size_t>(out) * in;
				const bool hidden = l + 1 < s.layers;
				for (uint32_t o = 0; o < out; o++)
				{
					const float* wr = w + static_cast<size_t>(o) * in;
					float z = b[o];
					for (uint32_t i = 0; i < in; i++) z += wr[i] * act.a[l][i];
					if (hidden)
					{
						z = z > 0.0f ? z : 0.0f;
						float m = 1.0f;
						if (dropout > 0.0f) m = Rng::Roll(Rng::Mix(dropKey ^ (static_cast<uint64_t>(l) * MaxWidth + o)), dropout) ? 0.0f : keep;
						act.mask[l][o] = m;
						z *= m;
					}
					act.a[l + 1][o] = z;
				}
			}
		}

		/// <summary>Logits to outputs: a softmax for a class op, as they are for a continuous one.</summary>
		void Finish(Op op, const float* z, float* y)
		{
			const uint32_t w = OutputWidth(op);
			if (!IsClassOp(op))
			{
				for (uint32_t k = 0; k < w; k++) y[k] = z[k];
				return;
			}
			float top = z[0];
			for (uint32_t k = 1; k < w; k++) top = std::max(top, z[k]);
			float sum = 0.0f;
			for (uint32_t k = 0; k < w; k++)
			{
				y[k] = std::exp(z[k] - top);
				sum += y[k];
			}
			for (uint32_t k = 0; k < w; k++) y[k] /= sum;
		}
	}

	bool Surrogate::Supports(Op op) const
	{
		return op < Op::Count_ && heads[static_cast<uint32_t>(op)].ready;
	}

	size_t Surrogate::Parameters(Op op) const
	{
		return Supports(op) ? heads[static_cast<uint32_t>(op)].weights.size() : 0;
	}

	void Surrogate::Run(const OpBatch& batch, OpOut& out) const
	{
		const Op op = batch.op;
		const uint32_t rows = batch.Rows();
		out.values.assign(static_cast<size_t>(rows) * OutputWidth(op), 0.0f);
		if (!Supports(op)) return;
		const Head& head = heads[static_cast<uint32_t>(op)];
		const Shape s = ShapeOf(head.arch, op);
		Activations act;
		for (uint32_t r = 0; r < rows; r++)
		{
			const std::span<const float> row = batch.Row(r);
			std::copy(row.begin(), row.end(), act.a[0]);
			Forward(s, head.weights.data(), act, 0.0f, 0);
			Finish(op, act.a[s.layers], out.Row(op, r).data());
		}
	}

	void Surrogate::Init(Op op, SurrogateArch arch, uint64_t seed)
	{
		if (op >= Op::Count_ || arch >= SurrogateArch::Count_) return;
		Head& head = heads[static_cast<uint32_t>(op)];
		const Shape s = ShapeOf(arch, op);
		head.arch = arch;
		head.weights.assign(s.size, 0.0f);
		for (uint32_t l = 0; l < s.layers; l++)
		{
			// Glorot-uniform weights, zero biases.
			const float bound = std::sqrt(6.0f / static_cast<float>(s.in[l] + s.out[l]));
			const uint32_t n = s.out[l] * s.in[l];
			float* w = head.weights.data() + s.offset[l];
			for (uint32_t i = 0; i < n; i++)
			{
				const double u = Rng::Unit(Rng::Hash(seed, i, l, InitSalt | static_cast<uint32_t>(op)));
				w[i] = static_cast<float>((2.0 * u - 1.0) * bound);
			}
		}
		head.ready = true;
	}

	void Surrogate::Epoch(const TrainSet& set, std::span<const uint32_t> rows, const TrainConfig& config, uint32_t epoch)
	{
		const Op op = set.op;
		if (!Supports(op)) return;
		Head& head = heads[static_cast<uint32_t>(op)];
		const Shape s = ShapeOf(head.arch, op);
		const uint32_t opBits = static_cast<uint32_t>(op);
		const uint32_t w = OutputWidth(op);
		const uint32_t count = set.Rows();
		const float dropout = head.arch == SurrogateArch::Mlp ? std::clamp(config.dropout, 0.0f, 0.9f) : 0.0f;
		const float rate = config.learningRate;

		// The epoch's order: the rows sorted by their hash, ties by row.
		std::vector<std::pair<uint64_t, uint32_t>> order;
		order.reserve(rows.size());
		for (const uint32_t r : rows)
		{
			if (r < count) order.emplace_back(Rng::Hash(config.seed, r, epoch, OrderSalt | opBits), r);
		}
		std::sort(order.begin(), order.end());

		Activations act;
		float y[MaxOutputs] = {};
		float delta[MaxWidth] = {};
		float below[MaxWidth] = {};
		float* params = head.weights.data();
		for (const auto& entry : order)
		{
			const uint32_t r = entry.second;
			const std::span<const float> row = set.Row(r);
			const std::span<const float> target = set.Target(r);
			std::copy(row.begin(), row.end(), act.a[0]);
			Forward(s, params, act, dropout, Rng::Hash(config.seed, r, epoch, DropSalt | opBits));
			Finish(op, act.a[s.layers], y);

			// Softmax with cross-entropy and identity with half squared error share one output gradient: y - t.
			for (uint32_t k = 0; k < w; k++) delta[k] = y[k] - target[k];
			for (uint32_t l = s.layers; l-- > 0;)
			{
				const uint32_t in = s.in[l];
				const uint32_t out = s.out[l];
				float* wl = params + s.offset[l];
				float* bl = wl + static_cast<size_t>(out) * in;
				if (l > 0)
				{
					// Back through the weights before they move, then through the ReLU and the dropout mask.
					for (uint32_t i = 0; i < in; i++)
					{
						float g = 0.0f;
						for (uint32_t o = 0; o < out; o++) g += wl[static_cast<size_t>(o) * in + i] * delta[o];
						below[i] = act.a[l][i] > 0.0f ? g * act.mask[l - 1][i] : 0.0f;
					}
				}
				for (uint32_t o = 0; o < out; o++)
				{
					const float step = rate * delta[o];
					float* wr = wl + static_cast<size_t>(o) * in;
					for (uint32_t i = 0; i < in; i++) wr[i] -= step * act.a[l][i];
					bl[o] -= step;
				}
				if (l > 0) std::copy(below, below + in, delta);
			}
		}
	}

	void Surrogate::Adopt(Op op, const Surrogate& from)
	{
		if (op >= Op::Count_) return;
		heads[static_cast<uint32_t>(op)] = from.heads[static_cast<uint32_t>(op)];
	}

	void Surrogate::Write(Writer& w) const
	{
		w.U32(SurrogateTag);
		for (const Head& head : heads)
		{
			w.Bool(head.ready);
			if (!head.ready) continue;
			w.U8(static_cast<uint8_t>(head.arch));
			w.U32(static_cast<uint32_t>(head.weights.size()));
			for (const float v : head.weights) w.F32(v);
		}
	}

	bool Surrogate::Read(Reader& r)
	{
		if (!r.Expect(SurrogateTag)) return false;
		std::array<Head, OpCount> read;
		for (uint32_t o = 0; o < OpCount; o++)
		{
			Head& head = read[o];
			if (!r.Bool()) continue;
			const uint8_t arch = r.U8();
			if (arch >= static_cast<uint8_t>(SurrogateArch::Count_))
			{
				r.Fail();
				return false;
			}
			head.arch = static_cast<SurrogateArch>(arch);
			const size_t size = ShapeOf(head.arch, static_cast<Op>(o)).size;
			const uint32_t n = r.Count(sizeof(float), static_cast<uint32_t>(size));
			if (!r.Ok() || n != size)
			{
				r.Fail();
				return false;
			}
			head.weights.resize(n);
			for (float& v : head.weights) v = r.F32();
			head.ready = true;
		}
		if (!r.Ok()) return false;
		heads = std::move(read);
		return true;
	}
}
