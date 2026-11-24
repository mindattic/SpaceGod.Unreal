#pragma once
// US-T631: the game's radio host (US-T575). Owns the RadioLog of the listener's current star system (made lazily, dropped
// on leaving it) and the local RadioReceiver; Emit is the single entry point for every source, Tick runs once a frame and
// hands the messages heard to the sinks (the HUD, reception, voice). A discontinuity rebases the receiver: a system change
// (new log), the clock going back (a rewind or a load: DropAfter plus Rebase), or the listener jumping faster than half of
// light (FTL arrival, the tour, a teleport); Rebase and Reset are there for callers that know of one first.
// View-local: each co-op peer runs its own; nothing here enters the simulation, the lockstep frames, saves or the bench
// digests, and nothing draws from the simulation RNG.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Radio/RadioLog.h"
#include "Radio/RadioMessage.h"
#include "Radio/RadioReceiver.h"
#include "Radio/RadioReception.h"

#include <array>
#include <cstdint>
#include <vector>

namespace SpaceGod::Radio
{
	class RadioHost;

	/// <summary>Told of every message the local listener hears, with its route (never Ignored).</summary>
	class IRadioSink
	{
	public:
		virtual ~IRadioSink() = default;
		virtual void OnRadioArrival(const RadioHost& host, const Core::RadioArrival& arrival, const Core::RadioReceptionResult& reception) = 0;
	};

	class RadioHost
	{
	public:
		static constexpr uint64_t NoIndex = ~0ull;
		/// <summary>Arrivals kept for sg.Radio.</summary>
		static constexpr int RecentCapacity = 8;
		/// <summary>A listener moving faster than this fraction of light between frames has jumped (FTL, tour, teleport).</summary>
		static constexpr double JumpFractionOfLight = 0.5;
		/// <summary>The travel margin's floor, km: how far a slow listener may drift while a front is in flight.</summary>
		static constexpr double MinTravelMarginKm = 1e5;

		/// <summary>
		/// Once a frame: the listener (the held unit, else the camera's focus) is at <paramref name="listenerKm"/>
		/// (heliocentric) in system <paramref name="systemId"/> (-1: none, between the stars) at <paramref name="jd"/>.
		/// Retires the log, rebases on a discontinuity, pops what has arrived and tells the sinks. O(k log n).
		/// </summary>
		void Tick(int systemId, const Core::DVec3& listenerKm, double jd);

		/// <summary>
		/// Logs <paramref name="message"/> (sent in the listener's system) and schedules it; returns its log index, or NoIndex
		/// when it was sent in another system or there is no listener yet.
		/// </summary>
		uint64_t Emit(const Core::RadioMessage& message);
		uint64_t EmitText(const Core::RadioMessage& message, const cs::String& text);

		/// <summary>Rebuilds the schedule from the log at the next Tick.</summary>
		void Rebase() { rebasePending = true; }
		/// <summary>Drops the log and the schedule (a load: the history restarts empty).</summary>
		void Reset();

		void AddSink(IRadioSink* sink);
		void RemoveSink(IRadioSink* sink);

		/// <summary>
		/// Who routes the arrivals (RadioReception::Route); the channels and tuner it points at must outlive it. US-T679: with
		/// <paramref name="receiverTuner"/> (the listener's own tuner) and channels set, each arrival is first received by it (counted
		/// as channel activity) and a CountedOnly one (muted, not listened to, encrypted) goes no further; one on no channel of
		/// the set (Ignored, ChannelId 0) is still routed as if there were no tuner.
		/// </summary>
		void SetListener(const Core::RadioListener& value, Core::RadioTuner* receiverTuner = nullptr)
		{
			listener = value;
			tuner = receiverTuner;
		}
		const Core::RadioListener& Listener() const { return listener; }
		/// <summary>Arrivals routed each way since the start (the digest's counter hook).</summary>
		uint64_t RoutedCount(Core::RadioRoute route) const { return routed[static_cast<size_t>(route)]; }

		int SystemId() const { return systemId; }
		const Core::RadioLog* Log() const { return log.get(); }
		const Core::RadioReceiver& Receiver() const { return receiver; }
		/// <summary>The latest Subtitled and Spoken arrivals, oldest first (their Message pointers are not kept: see RecentEntry).</summary>
		struct RecentEntry
		{
			Core::RadioMessage Message;
			/// <summary>As shown: garbled when Noisy.</summary>
			cs::String Text;
			Core::RadioReceptionResult Reception;
			double ArrivalJd = 0;
			double DistanceKm = 0;
			double AgeDays = 0;
		};
		const std::vector<RecentEntry>& Recent() const { return recent; }
		uint64_t HeardCount() const { return heard; }
		uint64_t RebaseCount() const { return rebases; }
		const Core::DVec3& ListenerKm() const { return listenerKm; }
		double Jd() const { return jd; }

	private:
		uint64_t Schedule(uint64_t index);
		void DoRebase();
		/// <summary>The listener's path, extrapolated from its measured velocity.</summary>
		Core::DVec3 ListenerAt(double atJd) const { return listenerKm + velocityKmPerDay * (atJd - jd); }

		cs::Ref<Core::RadioLog> log;
		Core::RadioReceiver receiver;
		std::vector<IRadioSink*> sinks;
		std::vector<Core::RadioArrival> arrivals;
		std::vector<RecentEntry> recent;
		Core::RadioListener listener;
		/// <summary>US-T679: the tuner that receives each arrival (SetListener); null for none.</summary>
		Core::RadioTuner* tuner = nullptr;
		std::array<uint64_t, static_cast<size_t>(Core::RadioRoute::Spoken) + 1> routed{};
		int systemId = -1;
		bool started = false;
		bool rebasePending = false;
		Core::DVec3 listenerKm;
		Core::DVec3 velocityKmPerDay;
		double jd = 0;
		uint64_t heard = 0;
		uint64_t rebases = 0;
	};
}
