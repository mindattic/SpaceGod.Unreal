#pragma once
// US-T640: the local player's radio tuner (US-T576). One tuned primary channel, up to MaxMonitored monitored channels,
// presets 1-8, a mute set and a scan mode that dwells about ScanDwellSeconds of real time on each channel with activity
// in the last game hour. Accepts routes a message to Heard, CountedOnly (muted, not listened to, or another faction's
// encrypted fleet traffic) or Ignored (not a channel of the set). Activity is counted per channel over a rolling game
// hour in ring buckets, O(1) per arrival. Serialize/Deserialize keep the state as a short string.
// View-local: not saved, not in the lockstep frames or the bench digests, never draws from the simulation RNG.

#include "Cs/Cs.h"
#include "Radio/RadioChannels.h"
#include "Radio/RadioMessage.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace SpaceGod::Core
{
	class Faction;

	enum class RadioAcceptance : uint8_t
	{
		/// <summary>Not a channel of this set: neither heard nor counted.</summary>
		Ignored,
		/// <summary>Counted as activity only: muted, not listened to, or encrypted to the listener.</summary>
		CountedOnly,
		Heard
	};
	SPACEGODCORE_API const char* ToString(RadioAcceptance v);

	class SPACEGODCORE_API RadioTuner
	{
	public:
		static constexpr int MaxMonitored = 4;
		static constexpr int PresetCount = 8;
		static constexpr double ScanDwellSeconds = 4.0;
		/// <summary>The rolling activity window, game days (one hour), and its ring buckets.</summary>
		static constexpr double ActivityWindowDays = 1.0 / 24.0;
		static constexpr int ActivityBuckets = 12;
		static constexpr double ActivityBucketDays = ActivityWindowDays / ActivityBuckets;

		/// <summary>Tunes the primary channel (0 for none); ends a scan.</summary>
		void Tune(uint32_t channelId);
		/// <summary>The primary channel, 0 when none.</summary>
		uint32_t Tuned() const { return tuned_; }
		/// <summary>The channel being listened to as primary: the scan's current channel while scanning, else Tuned.</summary>
		uint32_t Current() const { return scanning_ && scanChannel_ != 0 ? scanChannel_ : tuned_; }

		/// <summary>Adds a monitored channel; false when all MaxMonitored slots are taken (true if already monitored).</summary>
		bool Monitor(uint32_t channelId);
		void Unmonitor(uint32_t channelId);
		bool IsMonitored(uint32_t channelId) const;
		/// <summary>The monitored slots; 0 marks a free one.</summary>
		const std::array<uint32_t, MaxMonitored>& Monitored() const { return monitored_; }

		void SetMuted(uint32_t channelId, bool muted);
		bool IsMuted(uint32_t channelId) const { return muted_.count(channelId) != 0; }

		/// <summary>Stores a channel (0 clears) in preset 1-8; out-of-range slots are ignored.</summary>
		void SetPreset(int slot, uint32_t channelId);
		/// <summary>The channel in preset 1-8, 0 when empty.</summary>
		uint32_t Preset(int slot) const;
		/// <summary>Tunes the preset's channel; false when the slot is empty or out of range.</summary>
		bool RecallPreset(int slot);

		void SetScanning(bool scanning);
		bool Scanning() const { return scanning_; }

		/// <summary>
		/// Advances the scan by <paramref name="realDt"/> real seconds: after each dwell it moves to the next channel (by id,
		/// wrapping) with activity in the last game hour before <paramref name="nowJd"/>, or back to Tuned when none has any.
		/// Only walks the active channels once per dwell, never per frame.
		/// </summary>
		void Tick(double realDt, double nowJd);

		/// <summary>Whether the channel reaches the speaker: the current or a monitored channel, or Guard, and not muted.</summary>
		bool IsListening(const RadioChannel& channel) const;

		/// <summary>
		/// Routes a message for a listener flying for <paramref name="listenerFaction"/> (null for none). O(1); records nothing.
		/// </summary>
		RadioAcceptance Accepts(const RadioMessage& message, const RadioChannelSet& channels, const cs::Ref<Faction>& listenerFaction) const;

		/// <summary>Accepts, then records the arrival at <paramref name="arrivalJd"/> unless Ignored.</summary>
		RadioAcceptance Receive(const RadioMessage& message, const RadioChannelSet& channels, const cs::Ref<Faction>& listenerFaction, double arrivalJd);

		/// <summary>Counts one arrival on a channel at game time <paramref name="jd"/>. O(1).</summary>
		void RecordActivity(uint32_t channelId, double jd);
		/// <summary>Arrivals on the channel in the game hour up to <paramref name="nowJd"/>. O(ActivityBuckets).</summary>
		int Activity(uint32_t channelId, double nowJd) const;
		/// <summary>The latest arrival on the channel, game JD; -infinity when none.</summary>
		double LastActivityJd(uint32_t channelId) const;
		void ClearActivity() { activity_.clear(); }

		/// <summary>The tuning, monitors, presets, mutes and scan flag as a short string (activity is not kept).</summary>
		cs::String Serialize() const;
		/// <summary>Restores what Serialize wrote, keeping the activity; false (tuning left reset) when the text is not one.</summary>
		bool Deserialize(std::string_view text);

		/// <summary>Back to nothing tuned, monitored, preset or muted, not scanning, no activity.</summary>
		void Reset();

	private:
		struct ChannelActivity
		{
			std::array<int64_t, ActivityBuckets> Keys{};
			std::array<uint32_t, ActivityBuckets> Counts{};
			double LastJd = 0;
		};

		static int64_t BucketKey(double jd);
		void ResetTuning();
		uint32_t NextActiveAfter(uint32_t channelId, double nowJd) const;

		uint32_t tuned_ = 0;
		std::array<uint32_t, MaxMonitored> monitored_{};
		std::array<uint32_t, PresetCount> presets_{};
		std::unordered_set<uint32_t> muted_;
		bool scanning_ = false;
		uint32_t scanChannel_ = 0;
		double dwellLeft_ = 0;
		std::unordered_map<uint32_t, ChannelActivity> activity_;
	};
}
