// US-T648: the radio's digest (see RadioDigest.h).

#include "Radio/RadioDigest.h"

#include "Information/LightSpeed.h"

namespace SpaceGod::Core
{
	const char* RadioDigest::SituationPlural(int bucket)
	{
		switch (static_cast<ChatterSituation>(bucket))
		{
		case ChatterSituation::Idle: return "routine chatter";
		case ChatterSituation::DockingRequest: return "docking requests";
		case ChatterSituation::Clearance: return "docking clearances";
		case ChatterSituation::Departure: return "departures";
		case ChatterSituation::PatrolCheckIn: return "patrol check-ins";
		case ChatterSituation::ContactReport: return "contact reports";
		case ChatterSituation::TradeQuote: return "trade quotes";
		case ChatterSituation::ConvoyCall: return "convoy calls";
		case ChatterSituation::Distress: return "distress calls";
		case ChatterSituation::BattleCallout: return "battle calls";
		case ChatterSituation::BattleOver: return "after-action reports";
		case ChatterSituation::PirateTaunt: return "pirate taunts";
		case ChatterSituation::BountyNotice: return "bounty notices";
		case ChatterSituation::RelayBulletin: return "relay bulletins";
		default: return "other traffic";
		}
	}

	void RadioDigest::Count(uint32_t channelId, uint16_t situation, RadioDigestKind kind, bool encrypted)
	{
		auto [it, added] = byChannel.try_emplace(channelId, entries.size());
		if (added)
		{
			entries.emplace_back();
			entries.back().ChannelId = channelId;
		}
		Entry& e = entries[it->second];
		int bucket = situation < static_cast<uint16_t>(ChatterSituation::Count) ? situation : SituationBuckets - 1;
		if (encrypted) e.Encrypted++; // an encrypted burst's situation cannot be read
		else e.Situations[bucket]++;
		if (kind < RadioDigestKind::Count) e.Kinds[static_cast<size_t>(kind)]++;
		e.Total++;
		pending++;
	}

	void RadioDigest::Take(double jd, double realSeconds, bool warped, const RadioChannelSet* channels, std::vector<RadioDigestLine>& out)
	{
		if (!started || jd < periodJd)
		{
			// First call, or the clock went back: what was counted after it never was heard.
			Clear();
			started = true;
			periodJd = jd;
			periodReal = realSeconds;
			return;
		}
		bool due = jd - periodJd >= PeriodDays || (warped && realSeconds - periodReal >= WarpedPeriodSeconds);
		if (!due) return;
		double periodDays = jd - periodJd;
		periodJd = jd;
		periodReal = realSeconds;
		if (pending == 0) return;
		for (Entry& e : entries)
		{
			if (e.Total == 0) continue;
			const RadioChannel* channel = channels != nullptr ? channels->Find(e.ChannelId) : nullptr;
			RadioDigestLine line;
			line.ChannelId = e.ChannelId;
			line.Total = e.Total;
			line.Distress = e.Situations[static_cast<size_t>(ChatterSituation::Distress)];
			line.Text = Summary(e, channel != nullptr ? channel->Name : cs::Cat("ch ", e.ChannelId), periodDays);
			out.push_back(std::move(line));
			uint32_t id = e.ChannelId;
			e = Entry();
			e.ChannelId = id;
		}
		pending = 0;
	}

	void RadioDigest::Clear()
	{
		for (Entry& e : entries)
		{
			uint32_t id = e.ChannelId;
			e = Entry();
			e.ChannelId = id;
		}
		pending = 0;
		started = false;
	}

	cs::String RadioDigest::Summary(const Entry& e, const cs::String& name, double periodDays)
	{
		uint32_t readable = e.Total - e.Encrypted;
		if (readable == 0)
			return cs::Cat(name, ": encrypted traffic, ", e.Total, e.Total == 1 ? " burst" : " bursts");

		// The most common situation among the readable ones (encrypted bursts carry no readable situation).
		int top = 0;
		for (int i = 1; i < SituationBuckets; i++)
			if (e.Situations[i] > e.Situations[top]) top = i;
		const size_t distressBucket = static_cast<size_t>(ChatterSituation::Distress);
		uint32_t distress = e.Situations[distressBucket];

		cs::String text = cs::Cat(name, ": ", e.Total, e.Total == 1 ? " message" : " messages", " in the last ",
			LightSpeed::Describe(periodDays));
		if (e.Situations[top] > 0)
			text = cs::Cat(text, e.Situations[top] * 2 > e.Total ? ", mostly " : ", including ", SituationPlural(top));
		if (distress > 0 && static_cast<size_t>(top) != distressBucket)
			text = cs::Cat(text, "; ", distress, distress == 1 ? " distress call" : " distress calls");
		if (e.Encrypted > 0)
			text = cs::Cat(text, "; ", e.Encrypted, e.Encrypted == 1 ? " encrypted burst" : " encrypted bursts");
		uint32_t faint = e.Kinds[static_cast<size_t>(RadioDigestKind::Unreadable)];
		if (faint > 0) text = cs::Cat(text, "; ", faint, " too faint to read");
		return text;
	}
}
