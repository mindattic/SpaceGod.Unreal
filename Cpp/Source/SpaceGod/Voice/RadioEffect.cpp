#include "Voice/RadioEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

namespace SpaceGod::Voice::RadioEffect
{
	namespace
	{
		/// The line's own noise (xorshift64*), never the simulation's random stream.
		struct Noise
		{
			std::uint64_t State;

			explicit Noise(std::uint64_t seed) : State(seed ? seed : 0x9E3779B97F4A7C15ull) {}

			/// Uniform in -1..1.
			float Next()
			{
				State ^= State >> 12;
				State ^= State << 25;
				State ^= State >> 27;
				const std::uint64_t r = State * 0x2545F4914F6CDD1Dull;
				return static_cast<float>(r >> 40) / static_cast<float>(1ull << 23) - 1.0f;
			}
		};

		/// An RBJ cookbook biquad (direct form I), Q = 1/sqrt(2).
		struct Biquad
		{
			float B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0;
			float X1 = 0, X2 = 0, Y1 = 0, Y2 = 0;

			static Biquad Pass(float hz, int rate, bool high)
			{
				const float w = 2.0f * 3.14159265f * hz / static_cast<float>(rate);
				const float c = std::cos(w);
				const float alpha = std::sin(w) / (2.0f * 0.70710678f);
				const float a0 = 1.0f + alpha;
				Biquad q;
				q.B0 = (high ? (1.0f + c) : (1.0f - c)) * 0.5f / a0;
				q.B1 = (high ? -(1.0f + c) : (1.0f - c)) / a0;
				q.B2 = q.B0;
				q.A1 = -2.0f * c / a0;
				q.A2 = (1.0f - alpha) / a0;
				return q;
			}

			float Run(float x)
			{
				const float y = B0 * x + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2;
				X2 = X1;
				X1 = x;
				Y2 = Y1;
				Y1 = y;
				return y;
			}
		};

		/// A squelch click: a sharp edge, then a burst of noise dying away over the 40 ms.
		void Squelch(std::vector<float>& into, std::size_t at, std::size_t length, int rate, Noise& noise)
		{
			const float tau = 0.012f * static_cast<float>(rate);
			for (std::size_t i = 0; i < length && at + i < into.size(); ++i)
			{
				const float t = static_cast<float>(i);
				const float edge = i < 3 ? 0.6f * (1.0f - t / 3.0f) : 0.0f;
				into[at + i] += edge + 0.35f * std::exp(-t / tau) * noise.Next();
			}
		}
	}

	void Apply(Audio::SoundClip& clip, std::uint64_t seed)
	{
		const int rate = clip.SampleRate;
		if (!clip.IsValid() || rate < 8000) return;
		const std::vector<std::int16_t>& in = *clip.Samples;
		const std::size_t pad = static_cast<std::size_t>(SquelchSeconds * static_cast<float>(rate));

		int peak = 1;
		for (const std::int16_t s : in) peak = std::max(peak, std::abs(static_cast<int>(s)));
		const float level = 0.7f / static_cast<float>(peak);

		std::vector<float> buffer(in.size() + 2 * pad, 0.0f);
		for (std::size_t i = 0; i < in.size(); ++i) buffer[pad + i] = static_cast<float>(in[i]) * level;

		Noise noise(seed);
		// Uniform noise's RMS is 1/sqrt(3): scaled so the bed's RMS sits at StaticDb.
		const float bed = std::pow(10.0f, StaticDb / 20.0f) * 1.7320508f;
		for (float& s : buffer) s += bed * noise.Next();
		Squelch(buffer, 0, pad, rate, noise);
		Squelch(buffer, pad + in.size(), pad, rate, noise);

		Biquad high = Biquad::Pass(LowHz, rate, true);
		Biquad low = Biquad::Pass(std::min(HighHz, 0.45f * static_cast<float>(rate)), rate, false);
		constexpr float drive = 1.8f;
		const float norm = 1.0f / std::tanh(drive);
		auto out = std::make_shared<std::vector<std::int16_t>>(buffer.size());
		for (std::size_t i = 0; i < buffer.size(); ++i)
		{
			const float y = std::tanh(drive * low.Run(high.Run(buffer[i]))) * norm;
			(*out)[i] = static_cast<std::int16_t>(std::clamp(y * 0.9f, -1.0f, 1.0f) * 32767.0f);
		}
		clip.Samples = std::move(out);
	}

	const Audio::SoundClip& SquelchClip()
	{
		static const Audio::SoundClip clip = []
		{
			constexpr int rate = 22050;
			const std::size_t length = static_cast<std::size_t>(SquelchSeconds * static_cast<float>(rate));
			std::vector<float> buffer(length, 0.0f);
			Noise noise(0x5175E1C4ull);
			Squelch(buffer, 0, length, rate, noise);
			Biquad high = Biquad::Pass(LowHz, rate, true);
			Biquad low = Biquad::Pass(std::min(HighHz, 0.45f * static_cast<float>(rate)), rate, false);
			constexpr float drive = 1.8f;
			const float norm = 1.0f / std::tanh(drive);
			auto out = std::make_shared<std::vector<std::int16_t>>(length);
			for (std::size_t i = 0; i < length; ++i)
			{
				const float y = std::tanh(drive * low.Run(high.Run(buffer[i]))) * norm;
				(*out)[i] = static_cast<std::int16_t>(std::clamp(y * 0.9f, -1.0f, 1.0f) * 32767.0f);
			}
			Audio::SoundClip c;
			c.Samples = std::move(out);
			c.SampleRate = rate;
			c.Name = "radio.squelch";
			return c;
		}();
		return clip;
	}
}
