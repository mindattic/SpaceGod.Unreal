#include "Radio/RadioTuner.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <vector>

namespace SpaceGod::Core
{
	namespace
	{
		constexpr std::string_view Header = "RT1";
		constexpr int64_t NoBucket = std::numeric_limits<int64_t>::min();

		void AppendHex(cs::String& out, uint32_t v)
		{
			char buf[8];
			auto r = std::to_chars(buf, buf + sizeof(buf), v, 16);
			out.append(buf, r.ptr);
		}

		template <typename Each>
		bool ParseHexList(std::string_view text, const Each& each)
		{
			while (!text.empty())
			{
				size_t comma = text.find(',');
				std::string_view item = text.substr(0, comma);
				uint32_t v = 0;
				auto r = std::from_chars(item.data(), item.data() + item.size(), v, 16);
				if (item.empty() || r.ec != std::errc() || r.ptr != item.data() + item.size()) return false;
				each(v);
				if (comma == std::string_view::npos) break;
				text.remove_prefix(comma + 1);
			}
			return true;
		}
	}

	const char* ToString(RadioAcceptance v)
	{
		switch (v)
		{
		case RadioAcceptance::Ignored: return "Ignored";
		case RadioAcceptance::CountedOnly: return "CountedOnly";
		case RadioAcceptance::Heard: return "Heard";
		default: return "Unknown";
		}
	}

	void RadioTuner::Tune(uint32_t channelId)
	{
		tuned_ = channelId;
		scanning_ = false;
		scanChannel_ = 0;
	}

	bool RadioTuner::Monitor(uint32_t channelId)
	{
		if (channelId == 0) return false;
		if (IsMonitored(channelId)) return true;
		for (uint32_t& slot : monitored_)
			if (slot == 0)
			{
				slot = channelId;
				return true;
			}
		return false;
	}

	void RadioTuner::Unmonitor(uint32_t channelId)
	{
		for (uint32_t& slot : monitored_)
			if (slot == channelId) slot = 0;
	}

	bool RadioTuner::IsMonitored(uint32_t channelId) const
	{
		return channelId != 0 && std::find(monitored_.begin(), monitored_.end(), channelId) != monitored_.end();
	}

	void RadioTuner::SetMuted(uint32_t channelId, bool muted)
	{
		if (channelId == 0) return;
		if (muted)
			muted_.insert(channelId);
		else
			muted_.erase(channelId);
	}

	void RadioTuner::SetPreset(int slot, uint32_t channelId)
	{
		if (slot >= 1 && slot <= PresetCount) presets_[slot - 1] = channelId;
	}

	uint32_t RadioTuner::Preset(int slot) const { return slot >= 1 && slot <= PresetCount ? presets_[slot - 1] : 0; }

	bool RadioTuner::RecallPreset(int slot)
	{
		uint32_t id = Preset(slot);
		if (id == 0) return false;
		Tune(id);
		return true;
	}

	void RadioTuner::SetScanning(bool scanning)
	{
		if (scanning == scanning_) return;
		scanning_ = scanning;
		scanChannel_ = 0;
		dwellLeft_ = 0;
	}

	uint32_t RadioTuner::NextActiveAfter(uint32_t channelId, double nowJd) const
	{
		const double since = nowJd - ActivityWindowDays;
		uint32_t next = 0, lowest = 0;
		for (const auto& [id, a] : activity_)
		{
			if (a.LastJd < since || a.LastJd > nowJd) continue;
			if (lowest == 0 || id < lowest) lowest = id;
			if (id > channelId && (next == 0 || id < next)) next = id;
		}
		return next != 0 ? next : lowest;
	}

	void RadioTuner::Tick(double realDt, double nowJd)
	{
		if (!scanning_) return;
		dwellLeft_ -= realDt;
		if (dwellLeft_ > 0) return;
		dwellLeft_ = ScanDwellSeconds;
		scanChannel_ = NextActiveAfter(scanChannel_, nowJd);
	}

	bool RadioTuner::IsListening(const RadioChannel& channel) const
	{
		if (IsMuted(channel.Id)) return false;
		return channel.Kind == RadioChannelKind::Guard || channel.Id == Current() || IsMonitored(channel.Id);
	}

	RadioAcceptance RadioTuner::Accepts(const RadioMessage& message, const RadioChannelSet& channels, const cs::Ref<Faction>& listenerFaction) const
	{
		const RadioChannel* channel = channels.Find(message.ChannelId);
		if (channel == nullptr) return RadioAcceptance::Ignored;
		if (!IsListening(*channel)) return RadioAcceptance::CountedOnly;
		if (channel->Encrypted)
		{
			const RadioChannel* own = listenerFaction != nullptr ? channels.FleetOf(listenerFaction) : nullptr;
			if (own == nullptr || own->Id != channel->Id) return RadioAcceptance::CountedOnly;
		}
		return RadioAcceptance::Heard;
	}

	RadioAcceptance RadioTuner::Receive(const RadioMessage& message, const RadioChannelSet& channels, const cs::Ref<Faction>& listenerFaction, double arrivalJd)
	{
		RadioAcceptance result = Accepts(message, channels, listenerFaction);
		if (result != RadioAcceptance::Ignored) RecordActivity(message.ChannelId, arrivalJd);
		return result;
	}

	int64_t RadioTuner::BucketKey(double jd) { return static_cast<int64_t>(std::floor(jd / ActivityBucketDays)); }

	void RadioTuner::RecordActivity(uint32_t channelId, double jd)
	{
		auto [it, added] = activity_.try_emplace(channelId);
		ChannelActivity& a = it->second;
		if (added)
		{
			a.Keys.fill(NoBucket);
			a.LastJd = jd;
		}
		const int64_t key = BucketKey(jd);
		const int slot = static_cast<int>(((key % ActivityBuckets) + ActivityBuckets) % ActivityBuckets);
		if (a.Keys[slot] != key)
		{
			a.Keys[slot] = key;
			a.Counts[slot] = 0;
		}
		a.Counts[slot]++;
		a.LastJd = std::max(a.LastJd, jd);
	}

	int RadioTuner::Activity(uint32_t channelId, double nowJd) const
	{
		auto it = activity_.find(channelId);
		if (it == activity_.end()) return 0;
		const int64_t now = BucketKey(nowJd);
		int total = 0;
		for (int i = 0; i < ActivityBuckets; i++)
		{
			const int64_t key = it->second.Keys[i];
			if (key != NoBucket && key > now - ActivityBuckets && key <= now) total += static_cast<int>(it->second.Counts[i]);
		}
		return total;
	}

	double RadioTuner::LastActivityJd(uint32_t channelId) const
	{
		auto it = activity_.find(channelId);
		return it == activity_.end() ? -std::numeric_limits<double>::infinity() : it->second.LastJd;
	}

	cs::String RadioTuner::Serialize() const
	{
		cs::String out(Header);
		out += ";t=";
		AppendHex(out, tuned_);
		out += ";m=";
		for (int i = 0; i < MaxMonitored; i++)
		{
			if (i > 0) out += ',';
			AppendHex(out, monitored_[i]);
		}
		out += ";p=";
		for (int i = 0; i < PresetCount; i++)
		{
			if (i > 0) out += ',';
			AppendHex(out, presets_[i]);
		}
		out += ";x=";
		std::vector<uint32_t> muted(muted_.begin(), muted_.end());
		std::sort(muted.begin(), muted.end());
		for (size_t i = 0; i < muted.size(); i++)
		{
			if (i > 0) out += ',';
			AppendHex(out, muted[i]);
		}
		out += ";s=";
		out += scanning_ ? '1' : '0';
		return out;
	}

	bool RadioTuner::Deserialize(std::string_view text)
	{
		ResetTuning();
		if (text.substr(0, Header.size()) != Header) return false;
		text.remove_prefix(Header.size());

		bool ok = true;
		while (ok && !text.empty())
		{
			if (text.front() != ';')
			{
				ok = false;
				break;
			}
			text.remove_prefix(1);
			size_t end = text.find(';');
			std::string_view field = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end);
			if (field.size() < 2 || field[1] != '=')
			{
				ok = false;
				break;
			}
			const char name = field[0];
			field.remove_prefix(2);
			int i = 0;
			switch (name)
			{
			case 't':
				ok = ParseHexList(field, [&](uint32_t v) { if (i++ == 0) tuned_ = v; }) && i <= 1;
				break;
			case 'm':
				ok = ParseHexList(field, [&](uint32_t v) { if (i < MaxMonitored) monitored_[i] = v; i++; }) && i <= MaxMonitored;
				break;
			case 'p':
				ok = ParseHexList(field, [&](uint32_t v) { if (i < PresetCount) presets_[i] = v; i++; }) && i <= PresetCount;
				break;
			case 'x':
				ok = ParseHexList(field, [&](uint32_t v) { if (v != 0) muted_.insert(v); });
				break;
			case 's':
				ok = field == "0" || field == "1";
				scanning_ = field == "1";
				break;
			default:
				break;
			}
		}
		if (!ok) ResetTuning();
		return ok;
	}

	void RadioTuner::Reset()
	{
		ResetTuning();
		activity_.clear();
	}

	void RadioTuner::ResetTuning()
	{
		tuned_ = 0;
		monitored_.fill(0);
		presets_.fill(0);
		muted_.clear();
		scanning_ = false;
		scanChannel_ = 0;
		dwellLeft_ = 0;
	}
}
