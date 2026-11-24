#include "Radio/RadioReception.h"

#include <limits>

namespace SpaceGod::Core
{
	namespace
	{
		uint64_t Mix(uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		bool Dropped(uint32_t messageId, uint32_t word)
		{
			uint64_t h = Mix((static_cast<uint64_t>(messageId) << 32) | word);
			return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0) < RadioReception::NoisyDropout;
		}

		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
	}

	const char* ToString(RadioClarity v)
	{
		switch (v)
		{
		case RadioClarity::Clean: return "Clean";
		case RadioClarity::Noisy: return "Noisy";
		case RadioClarity::Unreadable: return "Unreadable";
		default: return "Unknown";
		}
	}

	const char* ToString(RadioRoute v)
	{
		switch (v)
		{
		case RadioRoute::Ignored: return "Ignored";
		case RadioRoute::Digest: return "Digest";
		case RadioRoute::Subtitled: return "Subtitled";
		case RadioRoute::Spoken: return "Spoken";
		default: return "Unknown";
		}
	}

	double RadioReception::SignalToNoise(const RadioMessage& message, double distanceKm)
	{
		double d2 = distanceKm * distanceKm;
		if (d2 <= 0) return std::numeric_limits<double>::infinity();
		return RadioPowerTable::EirpW(message.Power) / (4.0 * cs::Math::PI * d2) / RadioPowerTable::NoiseFloorWPerKm2;
	}

	RadioClarity RadioReception::Clarity(const RadioMessage& message, double distanceKm)
	{
		double snr = SignalToNoise(message, distanceKm);
		if (snr >= CleanSnr) return RadioClarity::Clean;
		if (snr >= NoisySnr) return RadioClarity::Noisy;
		return RadioClarity::Unreadable;
	}

	cs::String RadioReception::Garble(const cs::String& text, uint32_t messageId)
	{
		cs::String out;
		out.reserve(text.size());
		uint32_t word = 0;
		bool lastDropped = false;
		size_t i = 0;
		while (i < text.size())
		{
			while (i < text.size() && IsSpace(text[i])) i++;
			size_t start = i;
			while (i < text.size() && !IsSpace(text[i])) i++;
			if (i == start) break;
			bool drop = Dropped(messageId, word++);
			if (drop && lastDropped) continue;
			if (!out.empty()) out += ' ';
			if (drop)
				out += "...";
			else
				out.append(text, start, i - start);
			lastDropped = drop;
		}
		return out;
	}

	double RadioReception::Score(const RadioMessage& message, double distanceKm, const RadioListener& listener)
	{
		int priority = cs::Math::Clamp(static_cast<int>(message.Priority), 0, 3);
		double score = static_cast<double>(1 << priority);
		if (message.AddresseeKey != 0 && message.AddresseeKey == listener.UnitKey) score *= AddressedWeight;
		if (message.FactionId >= 0 && message.FactionId == listener.FactionId) score *= OwnFactionWeight;
		if (listener.Tuner != nullptr && message.ChannelId != 0 && message.ChannelId == listener.Tuner->Current()) score *= PrimaryWeight;
		double reach = message.MaxRangeKm();
		double closeness = reach > 0 ? 1.0 - cs::Math::Clamp(distanceKm / reach, 0.0, 1.0) : 0.0;
		return score * (0.5 + closeness);
	}

	RadioReceptionResult RadioReception::Route(const RadioMessage& message, double distanceKm, const RadioListener& listener)
	{
		RadioReceptionResult r;
		r.Clarity = Clarity(message, distanceKm);
		if (listener.Channels != nullptr && listener.Tuner != nullptr)
			r.Acceptance = listener.Tuner->Accepts(message, *listener.Channels, listener.ListenerFaction);
		if (r.Acceptance == RadioAcceptance::Ignored)
		{
			r.Route = RadioRoute::Ignored;
			return r;
		}
		r.Score = Score(message, distanceKm, listener);
		if (r.Acceptance != RadioAcceptance::Heard || r.Clarity == RadioClarity::Unreadable || message.Priority < MinPriority)
			r.Route = RadioRoute::Digest;
		else if (r.Score >= SpokenScore)
			r.Route = RadioRoute::Spoken;
		else if (r.Score >= SubtitledScore)
			r.Route = RadioRoute::Subtitled;
		else
			r.Route = RadioRoute::Digest;
		return r;
	}
}
