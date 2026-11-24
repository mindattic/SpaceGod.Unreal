#pragma once
// US-T680: ambient radio chatter (US-T644). Between the world's notable events the air carries routine talk: the
// settlements' traffic control within station reach of the listener, and the hulls within ship reach (a raider clan's
// taunts on its Pirate net, a navy's check-ins on its Fleet net, a trader's docking request on its destination's Traffic
// channel, else idle talk on Civil). Talk is sampled in windows of WindowSeconds game time, each group drawing a Poisson
// count at its role's rate per game hour times Density; a window keeps at most MaxPerWindow messages.
// Fast and bounded: a Tick samples at most MaxWindowsPerTick windows and then skips ahead, and each window scans at most
// MaxShipsPerWindow hulls (a round-robin cursor over the fleet), so nothing scales with the size of the galaxy.
// BackFill seeds a fresh listener with what is already in flight: the talk sent over each class's light-delay span.
// Its own SplitMix64 stream (Seed), never the simulation's random stream; read-only and view-local: it never changes the
// world, and nothing here enters the simulation, the lockstep frames, saves or the bench digests.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	class StarSystem;
	class World;

	class SPACEGODCORE_API ChatterSampler final
	{
	public:
		/// <summary>One sampling window, game seconds.</summary>
		static constexpr double WindowSeconds = 60.0;
		/// <summary>The most windows one Tick samples; a longer gap (a time warp) is skipped, not replayed.</summary>
		static constexpr int MaxWindowsPerTick = 3;
		/// <summary>The most messages one window keeps (a random subset of what it drew).</summary>
		static constexpr int MaxPerWindow = 32;
		/// <summary>The most hulls one window looks at.</summary>
		static constexpr int MaxShipsPerWindow = 256;
		/// <summary>The most messages BackFill gives.</summary>
		static constexpr int MaxBackFill = 128;
		/// <summary>BackFill's density, relative to Density.</summary>
		static constexpr double BackFillDensity = 0.25;

		/// <summary>Messages a game hour from each group, before Density.</summary>
		static constexpr double StationPerHour = 6.0;
		static constexpr double PiratePerHour = 1.0;
		static constexpr double PatrolPerHour = 1.5;
		static constexpr double TraderPerHour = 1.0;
		static constexpr double CivilPerHour = 0.5;

		/// <summary>How busy the air is: 1 is the rates above, 0 silences ambient chatter.</summary>
		double Density = 1.0;

		/// <summary>Restarts the sampler's own random stream.</summary>
		void Seed(uint64_t seed) { rng = seed; }

		/// <summary>
		/// The chatter sent in <paramref name="system"/> (galaxy index <paramref name="systemId"/>) since the last Tick, by
		/// emitters whose reach plus <paramref name="travelMarginKm"/> covers the listener at <paramref name="listenerKm"/>,
		/// appended to <paramref name="out"/> sorted by SentJd. The first Tick on a world, or after the clock went back,
		/// starts from now. At most MaxWindowsPerTick windows of at most MaxPerWindow messages.
		/// </summary>
		void Tick(const cs::Ref<StarSystem>& system, int systemId, DVec3 listenerKm, double travelMarginKm, double nowJd, std::vector<RadioMessage>& out);

		/// <summary>
		/// What is already in flight for a fresh listener: talk at BackFillDensity sent over each class's light-delay span
		/// (its reach plus the margin) up to <paramref name="nowJd"/>, from where the emitters are now; at most MaxBackFill,
		/// appended to <paramref name="out"/> sorted by SentJd. Ticks then start from now, so their messages come after these.
		/// </summary>
		void BackFill(const cs::Ref<StarSystem>& system, int systemId, DVec3 listenerKm, double travelMarginKm, double nowJd, std::vector<RadioMessage>& out);

		/// <summary>Forgets the clock and the hull cursor (a load, a new listener).</summary>
		void Clear()
		{
			world = nullptr;
			shipCursor = 0;
			channels.Clear();
		}

	private:
		/// <summary>Draws each group's messages sent in [fromJd, toJd) at <paramref name="density"/> into <c>drawn</c>.</summary>
		void Sample(const StarSystem& system, const RadioChannelSet& set, int systemId, DVec3 listenerKm, double travelMarginKm, double fromJd, double toJd, double density);

		/// <summary>Keeps a random <paramref name="cap"/> of <c>drawn</c>, sorts them by SentJd and appends them to <paramref name="out"/>.</summary>
		void Flush(int cap, std::vector<RadioMessage>& out);

		uint64_t Next();
		double Unit();
		int Poisson(double mean);

		RadioChannels channels;
		std::vector<RadioMessage> drawn;
		uint64_t rng = 0x9E3779B97F4A7C15ull;
		/// <summary>The world the clock runs on (compared only, never dereferenced).</summary>
		const World* world = nullptr;
		/// <summary>The end of the next window to sample.</summary>
		double nextJd = 0;
		int shipCursor = 0;
	};
}
