#pragma once
// US-T724: the sim tier scheduler.

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>
#include <unordered_map>

namespace SpaceGod::Core
{
	/// <summary>
	/// How closely a fleet (a ship and the escorts flying on it) is simulated, by how far it is from anyone watching.
	/// Every tier keeps its real schedule, routes and cargo (the fleet's event queue runs for all of them); the tiers
	/// differ only in the per-frame work done on top of it.
	/// </summary>
	enum class SimTier : uint8_t
	{
		/// <summary>Near an observer: stepped every frame.</summary>
		Near = 0,
		/// <summary>Further off: stepped about once a second (<see cref="SimTiers::MidPeriodS"/>), a batch a frame.</summary>
		Mid = 1,
		/// <summary>Far from every observer: no per-frame work, only its scheduled events (aggregate, event-driven).</summary>
		Far = 2,
	};

	static constexpr int SimTierCount = 3;

	SPACEGODCORE_API const char* SimTierName(SimTier tier);

	/// <summary>
	/// Assigns each fleet a <see cref="SimTier"/> from its distance to the nearest observer (the player's seat or camera),
	/// hysteretic the way <see cref="Streaming"/> loads and unloads: a fleet leaves a tier only once it is
	/// <see cref="Hysteresis"/> past the edge it entered at, so nothing flickers between tiers at a boundary.
	///
	/// With no observer (the Bench, a headless run) every fleet is <see cref="SimTier::Near"/>, and the scheduler does
	/// nothing at all: the simulation is exactly what it was before tiers existed.
	///
	/// Bounded per frame: at most <see cref="RetierPerFrame"/> fleets are re-measured a frame (round robin; one never
	/// measured is Near), and the Mid batch admitted each frame shrinks while the Mid tier runs over its budget. Time spent
	/// in each tier is charged with <see cref="Charge"/> and checked against <see cref="BudgetMs"/> (see <see cref="Stats"/>).
	/// </summary>
	class SPACEGODCORE_API SimTiers final
	{
	public:
		/// <summary>Within this (km) of an observer a fleet is Near.</summary>
		double NearKm = 100000;

		/// <summary>Beyond this (km) of every observer a fleet is Far; between the two, Mid.</summary>
		double FarKm = 20000000;

		/// <summary>The share past an edge a fleet must go before it leaves the tier it is in.</summary>
		double Hysteresis = 0.25;

		/// <summary>Real seconds between a Mid fleet's steps.</summary>
		double MidPeriodS = 1.0;

		/// <summary>Fleets re-measured against the observers each frame.</summary>
		int RetierPerFrame = 128;

		/// <summary>An observer not reported again within this many real seconds stops counting (the scheduler goes idle).</summary>
		double ObserverStaleS = 2.0;

		/// <summary>The game-thread milliseconds a frame may spend on each tier (Near, Mid, Far).</summary>
		double BudgetMs[SimTierCount] = { 2.0, 0.5, 1.0 };

		/// <summary>The observers this frame (heliocentric km) and the real time (seconds) they were reported at.</summary>
		void Observe(const cs::List<DVec3>& helioKm, double nowS);

		/// <summary>No observers: every fleet goes back to Near and the scheduler is idle.</summary>
		void Clear();

		/// <summary>Whether tiers are in force: an observer reported within <see cref="ObserverStaleS"/> of <paramref name="nowS"/>.</summary>
		bool Active() const { return active; }

		/// <summary>The tier something <paramref name="distanceKm"/> from the nearest observer goes to, given the tier it is
		/// in: it goes outward only past an edge plus the hysteresis, inward only once within it.</summary>
		static SimTier TierFor(double distanceKm, SimTier current, double nearKm, double farKm, double hysteresis);

		/// <summary>The distance (km) from <paramref name="helioKm"/> to the nearest observer (infinite with none).</summary>
		double NearestObserverKm(DVec3 helioKm) const;

		/// <summary>The tier of fleet <paramref name="key"/> (Near when idle or never measured).</summary>
		SimTier TierOf(int key) const;

		/// <summary>Starts a frame: the real time now, and the Mid batch for it (sized from the last frame's Mid cost).</summary>
		void BeginFrame(double nowS);

		/// <summary>Whether the fleet at <paramref name="index"/> of the caller's <paramref name="count"/> is due to be re-measured
		/// this frame (round robin, <see cref="RetierPerFrame"/> a frame).</summary>
		bool DueRetier(int index, int count) const;

		/// <summary>Measures fleet <paramref name="key"/> at <paramref name="helioKm"/> and moves it between tiers.</summary>
		void Retier(int key, DVec3 helioKm);

		/// <summary>Whether fleet <paramref name="key"/> is stepped this frame: always if Near, never if Far, and if Mid once
		/// <see cref="MidPeriodS"/> has passed since its last step and there is room in this frame's batch (and again for the
		/// rest of the fleet once it is let through this frame).</summary>
		bool Steps(int key);

		/// <summary>Adds <paramref name="ms"/> of game-thread time spent on <paramref name="tier"/> this frame.</summary>
		void Charge(SimTier tier, double ms) { frameMs[static_cast<int>(tier)] += ms; }

		/// <summary>US-T725: the worker-thread milliseconds the last batch of <paramref name="tier"/> took off the game thread
		/// (reported beside the tier's game-thread time, never counted against its budget).</summary>
		void ReportWorker(SimTier tier, double ms) { stats.WorkerMs[static_cast<int>(tier)] = ms; }

		/// <summary>Closes a frame: its per-tier time is checked against <see cref="BudgetMs"/> and folded into the figures.</summary>
		void EndFrame();

		/// <summary>Real seconds on a steady clock, for <see cref="Observe"/> and <see cref="BeginFrame"/>.</summary>
		static double ClockS();

		/// <summary>The figures <c>sg.Sim</c> reports.</summary>
		struct Stats
		{
			int Fleets[SimTierCount] = {};
			int Stepped[SimTierCount] = {};
			double LastMs[SimTierCount] = {};
			double AvgMs[SimTierCount] = {};
			double WorstMs[SimTierCount] = {};
			double WorkerMs[SimTierCount] = {};
			int64_t OverBudget[SimTierCount] = {};
			int64_t Frames = 0;
			int Promotions = 0, Demotions = 0;
			int MidBatch = 0;
			int Observers = 0;
		};
		const Stats& Statistics() const { return stats; }
		void ResetStatistics();

	private:
		struct Entry
		{
			SimTier Tier = SimTier::Near;
			double LastStepS = -1e300;
		};
		std::unordered_map<int, Entry> entries;
		cs::List<DVec3> observers;
		double observedS = -1e300, nowS = 0;
		bool active = false;
		int retierCursor = 0;
		int midBatch = 64, midAdmitted = 0;
		double frameMs[SimTierCount] = {};
		int stepped[SimTierCount] = {};
		Stats stats;
	};
}
