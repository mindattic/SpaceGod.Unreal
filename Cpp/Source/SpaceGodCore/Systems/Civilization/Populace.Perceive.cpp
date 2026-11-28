// US-T975: Populace's perception (docs/research/LightSociety-design.md, "Perception"): what the people hear of, as events
// for the next batch. A world hears what happens in its own system at once and what happens elsewhere when the relays
// bring it (Newswire::RelayDays), weaker. Lists are walked in their own order only: the society is lockstep-deterministic.

#include "Systems/Civilization/Populace.h"

#include "Economy/Settlement.h"
#include "Information/News.h"
#include "Law/Outlaws.h"
#include "LightSociety/Types.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/Society.h"
#include "World.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	namespace LS = MindAttic::Society;

	namespace
	{
		/// <summary>What a world far from it makes of a happening, against the world where it happened.</summary>
		constexpr float RemoteShare = 0.35f;

		/// <summary>Host event kinds past the SocialKinds, for what Perceive finds outside the civilization's events.</summary>
		constexpr uint16_t BountyKillKind = 0x8001;
		constexpr uint16_t RobberyKind = 0x8002;

		struct Push
		{
			Topic topic;
			float push;
			float magnitude;
		};

		/// <summary>How a civilization event moves the people who hear of it (none for kinds they shrug at).</summary>
		int PushesOf(SocialKind kind, Push* out)
		{
			switch (kind)
			{
			case SocialKind::PirateRaid:
				out[0] = {Topic::PirateFear, 1, 0.5f};
				out[1] = {Topic::PolitySupport, -1, 0.3f};
				return 2;
			case SocialKind::PirateHold:
				out[0] = {Topic::PirateFear, 1, 0.7f};
				out[1] = {Topic::PolitySupport, -1, 0.4f};
				return 2;
			case SocialKind::HavenStormed:
				out[0] = {Topic::PirateFear, -1, 0.6f};
				out[1] = {Topic::PolitySupport, 1, 0.4f};
				return 2;
			case SocialKind::BattleFought:
				out[0] = {Topic::PolitySupport, -1, 0.3f};
				return 1;
			case SocialKind::WorldConquered:
				out[0] = {Topic::PolitySupport, -1, 0.7f};
				return 1;
			case SocialKind::Scandal:
				out[0] = {Topic::PolitySupport, -1, 0.3f};
				return 1;
			default:
				return 0;
			}
		}

		/// <summary>Populace's own publications (the poll and every kind appended after the Witness's): never heard back.</summary>
		bool Echo(SocialKind kind) { return kind == SocialKind::PollPublished || static_cast<int>(kind) > static_cast<int>(SocialKind::Embargo); }
	}

	void Populace::Perceive(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (state.impl == nullptr || cs::Double::IsNaN(OriginJd)) return;

		// Each group's system, as an index into the distinct systems the groups stand in.
		std::vector<cs::Ref<StarSystem>> systems;
		std::vector<int> systemOf(static_cast<size_t>(Groups.Count()), -1);
		for (int g = 0; g < Groups.Count(); g++)
		{
			auto sys = Civilization::SystemOf(galaxy, Groups[g]);
			if (sys == nullptr) continue;
			int i = 0;
			while (i < static_cast<int>(systems.size()) && systems[static_cast<size_t>(i)] != sys) i++;
			if (i == static_cast<int>(systems.size())) systems.push_back(sys);
			systemOf[static_cast<size_t>(g)] = i;
		}

		// Relay lags from each system something happened in to each of the groups' systems, worked out once a call.
		auto news = galaxy.Traffic != nullptr ? galaxy.Traffic->News : nullptr;
		std::vector<cs::Ref<StarSystem>> lagFrom;
		std::vector<std::vector<double>> lags;
		auto lagsFrom = [&](const cs::Ref<StarSystem>& from) -> const std::vector<double>& {
			for (size_t i = 0; i < lagFrom.size(); i++)
				if (lagFrom[i] == from) return lags[i];
			std::vector<double> row(systems.size(), cs::Double::PositiveInfinity);
			for (size_t i = 0; i < systems.size(); i++)
				if (systems[i] == from) row[i] = 0;
				else if (news != nullptr) row[i] = news->RelayDays(from, systems[i], jd);
			lagFrom.push_back(from);
			lags.push_back(std::move(row));
			return lags.back();
		};

		// One happening in a system, heard by every group whose relays reach it.
		auto hear = [&](const cs::Ref<StarSystem>& from, double when, uint16_t kind, uint32_t source, const Push* pushes, int n) {
			if (from == nullptr || n == 0) return;
			const std::vector<double>& row = lagsFrom(from);
			for (int g = 0; g < Groups.Count(); g++)
			{
				const int s = systemOf[static_cast<size_t>(g)];
				if (s < 0 || cs::Double::IsInfinity(row[static_cast<size_t>(s)])) continue;
				const bool local = row[static_cast<size_t>(s)] == 0 && systems[static_cast<size_t>(s)] == from;
				for (int k = 0; k < n; k++)
				{
					LS::EnvEvent e;
					e.time = cs::Math::Max(when, OriginJd) + row[static_cast<size_t>(s)] - OriginJd;
					e.kind = kind;
					e.topic = static_cast<uint8_t>(pushes[k].topic);
					e.push = pushes[k].push;
					e.magnitude = local ? pushes[k].magnitude : pushes[k].magnitude * RemoteShare;
					e.scope = LS::Scope::Group;
					e.target = static_cast<uint32_t>(g);
					e.source = source;
					Queue(e);
				}
			}
		};

		// The civilization's events since the last look, in the order they were published.
		int seen = SeenEventId;
		for (const auto& e : civ.Events)
		{
			if (e->Id <= SeenEventId) continue;
			if (e->Id > seen) seen = e->Id;
			if (Echo(e->Kind) || e->Jd < OriginJd || e->Unverified) continue;
			Push pushes[3];
			int n = PushesOf(e->Kind, pushes);
			if (e->Steered) pushes[n++] = {Topic::GodFaith, 1, 0.5f};
			auto from = e->System != nullptr ? e->System : Civilization::SystemOf(galaxy, e->Where);
			hear(from, e->Jd, static_cast<uint16_t>(e->Kind), static_cast<uint32_t>(e->Id), pushes, n);
		}
		SeenEventId = seen;

		// Bounty kills: outlaws at large at the last look whose careers someone has since ended.
		cs::List<cs::Ref<Outlaw>> wanted;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr || sys->World->Outlaws == nullptr) continue;
			for (const auto& o : sys->World->Outlaws->All)
			{
				if (o->AtLarge())
				{
					wanted.Add(o);
					continue;
				}
				if (!Wanted.Contains(o) || o->FateBy.empty() || o->Fate == OutlawFate::Vanished) continue;
				const Push pushes[2] = {{Topic::PirateFear, -1, o->Feared ? 0.6f : 0.3f}, {Topic::PolitySupport, 1, 0.2f}};
				hear(sys, o->FateJd, BountyKillKind, 0, pushes, 2);
			}
		}
		Wanted = wanted;

		// Lane robberies: liners boarded on the way since the last look, heard from the system they were bound for.
		if (auto society = Society::Find(galaxy); society != nullptr && society->Robberies != SeenRobberies)
		{
			const double since = cs::Double::IsNaN(SeenRobberyJd) ? OriginJd : SeenRobberyJd;
			double latest = since;
			for (const auto& t : society->Trips)
			{
				if (t->ArriveJd <= since || t->ArriveJd > jd || !t->Note.starts_with("robbed of ")) continue;
				latest = cs::Math::Max(latest, t->ArriveJd);
				const Push pushes[2] = {{Topic::PirateFear, 1, 0.3f}, {Topic::TradeTrust, -1, 0.2f}};
				hear(t->ToSystem, t->ArriveJd, RobberyKind, static_cast<uint32_t>(t->Id), pushes, 2);
			}
			SeenRobberies = society->Robberies;
			SeenRobberyJd = latest;
		}
	}
}
