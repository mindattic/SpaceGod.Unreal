#pragma once
// US-T623: one radio transmission (US-T575). A message is a wavefront leaving OriginKm at SentJd; it is heard wherever and
// whenever that front reaches a listener (Wavefront::ArrivalJd), and only within the reach of its power class. Its text is
// realised only when it is heard, from TemplateId and Args, unless it carries literal text in its log's string table.
// View-local: nothing here enters the simulation, the lockstep frames, saves or the bench digests.

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>

namespace SpaceGod::Core
{
	/// <summary>How loud a transmitter is (its EIRP), so how far it can be heard.</summary>
	enum class RadioPower : uint8_t
	{
		Suit,
		Ship,
		Station,
		FleetCommand,
		GuardBeacon,
		Count
	};

	/// <summary>
	/// The power-class table. A front from a class is heard while its flux EIRP / (4 pi r^2) stays above the receiver
	/// noise floor, so out to R_max = sqrt(EIRP / (4 pi N_floor)). Tuned so a ship is heard to about 0.5 AU and a
	/// station across the inner system (about 3 AU).
	/// </summary>
	class SPACEGODCORE_API RadioPowerTable
	{
	public:
		/// <summary>The receiver noise floor, watts per square kilometre.</summary>
		static constexpr double NoiseFloorWPerKm2 = 1e-13;

		/// <summary>The class's effective isotropic radiated power, watts.</summary>
		static double EirpW(RadioPower power);

		/// <summary>The farthest a front of this class can be heard, km (computed once per class).</summary>
		static double MaxRangeKm(RadioPower power);

		/// <summary>The farthest any class can be heard, km.</summary>
		static double LongestRangeKm();
	};

	/// <summary>One transmission, as logged. Plain data: copied into the log's ring as is.</summary>
	struct RadioMessage
	{
		/// <summary>TextSlot when the message is realised from TemplateId and Args.</summary>
		static constexpr uint32_t NoText = 0xFFFFFFFFu;
		static constexpr int ArgCount = 4;

		uint32_t Id = 0;
		uint32_t ChannelId = 0;
		/// <summary>Who sent it (a ship, station or pilot key); 0 when anonymous.</summary>
		uint64_t EmitterKey = 0;
		/// <summary>Who it is addressed to (a ship, station or pilot key); 0 when to all.</summary>
		uint64_t AddresseeKey = 0;
		int32_t FactionId = -1;
		int32_t SystemId = -1;
		/// <summary>Where the emitter was when it sent, heliocentric km.</summary>
		DVec3 OriginKm;
		double SentJd = 0;
		RadioPower Power = RadioPower::Ship;
		/// <summary>0 (chatter) to 3 (distress).</summary>
		uint8_t Priority = 0;
		uint16_t Situation = 0;
		uint16_t TemplateId = 0;
		int32_t Args[ArgCount] = {};
		/// <summary>Literal text's slot in the owning RadioLog's string table, or NoText. Set by RadioLog::AppendText.</summary>
		uint32_t TextSlot = NoText;

		double MaxRangeKm() const { return RadioPowerTable::MaxRangeKm(Power); }
	};
}
