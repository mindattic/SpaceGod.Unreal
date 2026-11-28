// US-T974: Populace, the Civilization layer's opinion model (see the header).

#include "Systems/Civilization/Populace.h"

#include "Economy/Settlement.h"
#include "Law/Outlaws.h"
#include "LightSociety/Distill.h"
#include "LightSociety/Engine.h"
#include "LightSociety/Graph.h"
#include "LightSociety/Router.h"
#include "LightSociety/Surrogate.h"
#include "LightSociety/Teacher.h"
#include "LightSociety/Worker.h"
#include "Persistence/ObjectGraph.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace SpaceGod::Core
{
	namespace LS = MindAttic::Society;
	using namespace Civ;

	const char* ToString(Topic v)
	{
		switch (v)
		{
		case Topic::PolitySupport: return "PolitySupport";
		case Topic::PirateFear: return "PirateFear";
		case Topic::GodFaith: return "GodFaith";
		case Topic::TradeTrust: return "TradeTrust";
		case Topic::WarHawk: return "WarHawk";
		default: return "Count_";
		}
	}

	/// <summary>The engine, its policy and threads, the events gathered for the next batch and the last readout taken.</summary>
	struct LightState::Impl
	{
		LS::Engine engine;
		LS::Router router;
		LS::ChunkPool pool{0};
		std::vector<LS::EnvEvent> inputs;
		LS::Readout readout;
		/// <summary>The rule teacher distillation asks, and the surrogate it distils (job-owned until the job is waited for).</summary>
		LS::Teacher teacher;
		LS::Surrogate surrogate;
		bool distilled = false;
		/// <summary>Whether the surrogate is registered on the router: from engine day <see cref="Populace.DistillDays"/> on.</summary>
		bool switched = false;
		/// <summary>The census's changes for the next job: leavers, joiners and new ties (an end at NewAgent + i names joins[i]).</summary>
		std::vector<LS::AgentId> leave;
		std::vector<LS::Profile> joins;
		std::vector<LS::Edge> links;
		/// <summary>Last, so it is destroyed (its job cancelled and its thread joined) before what the job touches.</summary>
		LS::JobRunner runner;

		Impl()
		{
			engine.SetPolicy(&router);
			engine.SetPool(&pool);
		}
	};

	namespace
	{
		constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		cs::String ToBase64(const std::vector<uint8_t>& bytes)
		{
			cs::String out;
			out.reserve((bytes.size() + 2) / 3 * 4);
			for (size_t i = 0; i < bytes.size(); i += 3)
			{
				const size_t n = std::min<size_t>(3, bytes.size() - i);
				uint32_t v = static_cast<uint32_t>(bytes[i]) << 16;
				if (n > 1) v |= static_cast<uint32_t>(bytes[i + 1]) << 8;
				if (n > 2) v |= bytes[i + 2];
				out += Alphabet[(v >> 18) & 63];
				out += Alphabet[(v >> 12) & 63];
				out += n > 1 ? Alphabet[(v >> 6) & 63] : '=';
				out += n > 2 ? Alphabet[v & 63] : '=';
			}
			return out;
		}

		bool FromBase64(const cs::String& text, std::vector<uint8_t>& out)
		{
			out.clear();
			if (text.size() % 4 != 0) return false;
			out.reserve(text.size() / 4 * 3);
			uint32_t v = 0;
			int bits = 0;
			for (size_t i = 0; i < text.size(); i++)
			{
				const char c = text[i];
				if (c == '=') return i + 2 >= text.size();
				int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
				if (d < 0) return false;
				v = (v << 6) | static_cast<uint32_t>(d);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back(static_cast<uint8_t>(v >> bits));
				}
			}
			return true;
		}

		/// <summary>The society saved as one base64 string node (not a byte array: a saved array spends a Value per element).</summary>
		struct LightStateCodec
		{
			static const Graph::NetType* Type() { return Graph::StringType(); }
			static void Write(Graph::Writer& w, LightState& v)
			{
				cs::String text = v.Encode();
				Graph::StringCodec<true>::Write(w, text);
			}
			static void Read(Graph::Reader& r, const Graph::Value& value, LightState& v)
			{
				cs::String text;
				Graph::StringCodec<true>::Read(r, value, text);
				v.Decode(text);
			}
			static void Walk(Graph::StaticWalker&, LightState&, const cs::String&, int) {}
		};

		uint8_t Byte(double v) { return static_cast<uint8_t>(cs::Math::Clamp(cs::Math::Round(v), 0.0, 255.0)); }

		/// <summary>Noise of -spread..spread from a hash.</summary>
		double Noise(std::uint32_t h, double spread) { return (Unit(h) * 2 - 1) * spread; }

		/// <summary>0 (poor) .. 4 (rich) by a house's credits.</summary>
		uint8_t WealthClass(const cs::Ref<Household>& h)
		{
			double w = h != nullptr ? h->Wealth + (h->Business != nullptr ? h->Business->Capital : 0) : 0;
			return w < 1e3 ? 0 : w < 1e4 ? 1 : w < 1e5 ? 2 : w < 1e6 ? 3 : 4;
		}

		// Random-draw ops of the build (CrossGroup and BarabasiAlbert take them with a group or business in the low bits).
		constexpr uint32_t OpWithin = 0x01000000u;
		constexpr uint32_t OpBranch = 0x02000000u;
		/// <summary>Ties each agent makes within its world (Barabasi-Albert m), and links one business branch makes.</summary>
		constexpr uint32_t WithinTies = 3;
		constexpr uint32_t BranchLinks = 2;
		/// <summary>An edge end at or past this names the census's joiner (end - NewAgent), seated by the next job.</summary>
		constexpr LS::AgentId NewAgent = 0x80000000u;

		std::uint32_t Seed32(const Galaxy& galaxy, double originJd) { return H(static_cast<std::uint32_t>(galaxy.Seed), DayOf(originJd), 0x1A9Fu); }

		/// <summary>A living citizen as an agent: their house, business, age and traits, their world's schooling.</summary>
		LS::Profile Anchor(const cs::Ref<Citizen>& c, const cs::Ref<Household>& h, int g, const cs::Ref<Civics>& cv, double jd, std::uint32_t seed32)
		{
			double learning = cv != nullptr ? cv->Learning : 0.3;
			std::uint32_t r = H(static_cast<std::uint32_t>(c->Id), seed32, 0xA4C7u);
			auto has = [&](Trait t) { return (c->Traits & static_cast<int>(t)) != 0; };
			LS::Profile p;
			p.externalId = static_cast<uint64_t>(c->Id);
			p.group = static_cast<LS::GroupId>(g);
			p.household = static_cast<uint32_t>(h->Id);
			p.business = h->Business != nullptr ? static_cast<uint32_t>(h->Business->Id) : LS::NoUnit;
			p.age = Byte(c->AgeYears(jd));
			p.cls = WealthClass(h);
			p.edu = Byte(learning * 4 + Unit(H(r, 1)));
			p.big5[0] = Byte(128 + (has(Trait::Curious) ? 60 : 0) + Noise(H(r, 2), 30));
			p.big5[1] = Byte(128 + (has(Trait::Honest) ? 40 : 0) + (has(Trait::Loyal) ? 40 : 0) + Noise(H(r, 3), 30));
			p.big5[2] = Byte(128 + (has(Trait::Charming) ? 60 : 0) + Noise(H(r, 4), 30));
			p.big5[3] = Byte(128 - (has(Trait::Cruel) ? 60 : 0) - (has(Trait::Vengeful) ? 30 : 0) + Noise(H(r, 5), 30));
			p.big5[4] = Byte(128 + (has(Trait::Reckless) ? 40 : 0) + (has(Trait::Vengeful) ? 30 : 0) + Noise(H(r, 6), 30));
			p.traits = static_cast<uint16_t>(c->Traits & 0xFFFF);
			return p;
		}

		/// <summary>A synthetic agent of world g drawn from hash r, shaped by its Civics, its class that of one of its houses.</summary>
		LS::Profile Synthetic(int g, std::uint32_t r, const cs::Ref<Civics>& cv, const std::vector<cs::Ref<Household>>& houses)
		{
			double liberty = cv != nullptr ? cv->Liberty : 0, hawk = cv != nullptr ? cv->Hawk : 0;
			double learning = cv != nullptr ? cv->Learning : 0.3, faith = cv != nullptr ? cv->FaithShare : 0;
			double weariness = cv != nullptr ? cv->Weariness : 0;
			LS::Profile p;
			p.group = static_cast<LS::GroupId>(g);
			p.age = Byte(16 + H(r, 1) % 60u);
			p.cls = houses.empty() ? 1 : WealthClass(houses[H(r, 2) % houses.size()]);
			p.edu = Byte(learning * 4 + Unit(H(r, 3)));
			p.big5[0] = Byte(128 + liberty * 60 + Noise(H(r, 4), 40));
			p.big5[1] = Byte(128 + Noise(H(r, 5), 40));
			p.big5[2] = Byte(128 + Noise(H(r, 6), 40));
			p.big5[3] = Byte(128 - hawk * 60 + Noise(H(r, 7), 40));
			p.big5[4] = Byte(128 + weariness * 60 + Noise(H(r, 8), 40));
			if (Roll(H(r, 9), faith)) p.traits = static_cast<uint16_t>(Trait::Pious);
			return p;
		}

		/// <summary>The teacher's persuasion distilled into a surrogate, from the society as it stands (run on the worker).</summary>
		void Distil(LightState::Impl& light)
		{
			const LS::Config& config = light.engine.GetConfig();
			LS::CaptureConfig capture;
			capture.seed = config.seed;
			capture.topics = config.topics;
			LS::TrainSet samples = LS::CaptureFromStore(light.teacher, LS::Op::Shift, light.engine.Store(), light.engine.Topics(), capture);
			LS::DistillConfig distill;
			distill.seed = config.seed;
			LS::DistillResult result = LS::Distill(light.teacher, samples, distill);
			if (result.ok) light.surrogate = std::move(result.backend);
			light.distilled = true;
		}

		/// <summary>Seats the census's joiners, retires its leavers and wires the new ties (run on the worker).</summary>
		void Seat(LS::Engine& engine, const std::vector<LS::AgentId>& leave, const std::vector<LS::Profile>& joins, std::vector<LS::Edge> links)
		{
			for (LS::AgentId a : leave) engine.Leave(a);
			if (joins.empty() && links.empty()) return;
			// Drop the leavers' ties first, so a joiner seated in a freed slot does not inherit them.
			if (!leave.empty() && !joins.empty()) engine.Link({});
			std::vector<LS::AgentId> seated;
			seated.reserve(joins.size());
			for (const LS::Profile& p : joins) seated.push_back(engine.Join(p));
			auto resolve = [&](LS::AgentId a) { return a >= NewAgent ? (a - NewAgent < seated.size() ? seated[a - NewAgent] : LS::NoAgent) : a; };
			for (LS::Edge& e : links)
			{
				e.a = resolve(e.a);
				e.b = resolve(e.b);
			}
			engine.Link(links);
		}
	}

	LightState::LightState() = default;
	LightState::~LightState() = default;

	cs::String LightState::Encode()
	{
		if (impl == nullptr) return cs::String();
		impl->runner.Wait();
		LS::Writer w;
		impl->engine.Write(w);
		// US-T982: the distilled surrogate follows the engine (a blob that ends there has none yet).
		w.Bool(impl->distilled);
		if (impl->distilled) impl->surrogate.Write(w);
		return ToBase64(w.Bytes());
	}

	void LightState::Decode(const cs::String& text)
	{
		impl.reset();
		std::vector<uint8_t> bytes;
		if (text.empty() || !FromBase64(text, bytes)) return;
		auto fresh = std::make_unique<Impl>();
		LS::Reader r(bytes);
		if (!r.Ok() || !fresh->engine.Read(r) || !r.Ok()) return;
		// A bad surrogate is distilled again by the next job rather than losing the society.
		if (!r.AtEnd()) fresh->distilled = r.Bool() && r.Ok() && fresh->surrogate.Read(r);
		fresh->readout = fresh->engine.Latest();
		impl = std::move(fresh);
	}

	// ---------------------------------------------------------------- knobs

	const cs::Ref<RangeSetting>& Populace::AgentsPerSettlement()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("light", "agentsPerSettlement", "Agents per world", 64, 0, 1024, 1, "",
			TuningScope::Simulation, "Each standing world's people in the opinion model are topped up with synthetic agents to this many.");
		return s;
	}

	const cs::Ref<RangeSetting>& Populace::MaxAgents()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("light", "maxAgents", "Most agents", 65536, 1024, 1048576, 1024, "",
			TuningScope::Simulation, "The most agents the opinion model holds, citizens and synthetic agents together (read when it is built).");
		return s;
	}

	const cs::Ref<RangeSetting>& Populace::DistillDays()
	{
		static const cs::Ref<RangeSetting> s = Tuning::Knob("light", "distillDays", "Teacher days", 30, 0, 365, 1, "days",
			TuningScope::Simulation, "Days after the opinion model is built that the rule teacher decides every change, before its distilled models take over.");
		return s;
	}

	// ---------------------------------------------------------------- the pipeline

	void Populace::Pump(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (state.impl == nullptr)
		{
			// Built this update, stepped from the next: f_I and the distillation run on the worker meanwhile.
			Build(civ, galaxy, jd);
			return;
		}
		LightState::Impl& light = *state.impl;
		try
		{
			light.runner.Wait();
		}
		catch (...)
		{
			// A batch that failed leaves the engine half stepped: drop it and build afresh on the next day.
			state.impl.reset();
			return;
		}
		light.readout = light.engine.Latest();
		Feedback(civ, galaxy, jd);

		const long long due = static_cast<long long>(cs::Math::Floor(jd) - OriginJd);
		const uint32_t now = light.engine.Day();
		if (due <= static_cast<long long>(now)) return;
		// The surrogate takes over on a sim day, never when a thread happens to finish: batches stop at the switch day.
		const long long switchDay = static_cast<long long>(cs::Math::Max(0.0, DistillDays()->Value));
		if (!light.switched && light.distilled && static_cast<long long>(now) >= switchDay)
		{
			light.router.Register(LS::BackendKind::Surrogate, LS::Op::Shift, &light.surrogate);
			light.switched = true;
		}
		Perceive(civ, galaxy, jd);
		Trade(civ, galaxy, jd); // US-T985
		Census(civ, galaxy, jd);
		long long last = std::min<long long>(due, static_cast<long long>(now) + MaxStrideDays);
		if (!light.switched && static_cast<long long>(now) < switchDay) last = std::min(last, switchDay);
		// A save without a surrogate distils in this batch, one day long so the switch after it lands on a fixed day.
		const bool distil = !light.distilled;
		if (distil) last = static_cast<long long>(now) + 1;
		const uint32_t to = static_cast<uint32_t>(last);
		light.runner.Submit([&light, events = std::exchange(light.inputs, {}), leave = std::exchange(light.leave, {}),
								joins = std::exchange(light.joins, {}), links = std::exchange(light.links, {}), to, distil](const std::atomic<bool>&) {
			Seat(light.engine, leave, joins, links);
			if (distil) Distil(light);
			light.engine.Inject(events);
			light.engine.StepTo(static_cast<double>(to));
		});
	}

	void Populace::Queue(const LS::EnvEvent& e)
	{
		if (state.impl != nullptr) state.impl->inputs.push_back(e);
	}

	double Populace::Mood(const cs::Ref<Settlement>& s, Topic topic) const
	{
		if (state.impl == nullptr) return 0;
		int g = GroupOf(s);
		if (g < 0) return 0;
		const LS::TopicReadout* cell = state.impl->readout.Find(static_cast<LS::GroupId>(g), static_cast<uint32_t>(topic));
		return cell != nullptr && cell->count > 0 ? cell->mean : 0;
	}

	double Populace::Share(const cs::Ref<Settlement>& s, Topic topic) const
	{
		if (state.impl == nullptr) return 0;
		int g = GroupOf(s);
		if (g < 0) return 0;
		const LS::TopicReadout* cell = state.impl->readout.Find(static_cast<LS::GroupId>(g), static_cast<uint32_t>(topic));
		return cell != nullptr && cell->count > 0 ? cell->supporter : 0;
	}

	void Populace::Hear(int group, Topic topic, float push, float magnitude, double jd)
	{
		if (state.impl == nullptr || group < 0 || group >= Groups.Count() || cs::Double::IsNaN(OriginJd)) return;
		LS::EnvEvent e;
		e.time = cs::Math::Max(jd, OriginJd) - OriginJd;
		e.topic = static_cast<uint8_t>(topic);
		e.push = push;
		e.magnitude = magnitude;
		e.scope = LS::Scope::Group;
		e.target = static_cast<uint32_t>(group);
		state.impl->inputs.push_back(e);
	}

	int Populace::ReadDay() const { return state.impl != nullptr ? static_cast<int>(state.impl->readout.day) : -1; }

	double Populace::MoodOf(const Galaxy& galaxy, const cs::Ref<Settlement>& s, Topic topic)
	{
		auto civ = Civilization::Find(galaxy);
		return civ != nullptr && civ->Populace != nullptr ? civ->Populace->Mood(s, topic) : 0;
	}

	int Populace::GroupOf(const cs::Ref<Settlement>& s) const
	{
		if (s == nullptr) return -1;
		for (int i = 0; i < Groups.Count(); i++)
			if (Groups[i] == s) return i;
		return -1;
	}

	// ---------------------------------------------------------------- f_I: the people as agents

	bool Populace::Build(Civilization& civ, Galaxy& galaxy, double jd)
	{
		auto society = Society::Find(galaxy);
		if (society == nullptr || civ.Worlds.Count() == 0) return false;

		OriginJd = cs::Math::Floor(jd);
		Groups.Clear();
		for (const auto& w : civ.Worlds)
			if (GroupOf(w->Where) < 0) Groups.Add(w->Where);

		const std::uint32_t seed32 = Seed32(galaxy, OriginJd);
		const uint64_t seed = (static_cast<uint64_t>(seed32) << 32) | H(seed32, 0x50C1u);
		const uint32_t cap = static_cast<uint32_t>(cs::Math::Clamp(MaxAgents()->Value, 1.0, static_cast<double>(LS::MaxAgents)));
		const int perWorld = static_cast<int>(cs::Math::Max(0.0, AgentsPerSettlement()->Value));

		const size_t G = static_cast<size_t>(Groups.Count());
		std::vector<LS::Profile> profiles;
		std::vector<LS::Edge> edges;
		std::vector<std::vector<LS::AgentId>> members(G);
		/// (citizen id, agent), sorted after the anchors are made: the citizens' ties are found by binary search.
		std::vector<std::pair<int, LS::AgentId>> anchors;

		auto civicsOf = [&](size_t g) { return civ.CivicsOf(Groups[static_cast<int>(g)]); };
		auto add = [&](const LS::Profile& p) {
			LS::AgentId a = static_cast<LS::AgentId>(profiles.size());
			profiles.push_back(p);
			members[p.group].push_back(a);
			return a;
		};

		// Anchors: every living citizen of a standing world, house by house, with the house's family and business ties.
		std::vector<std::vector<cs::Ref<Household>>> housesOf(G);
		for (const auto& h : society->Houses)
		{
			if (h->Scattered) continue;
			int g = GroupOf(h->Home);
			if (g < 0) continue;
			housesOf[static_cast<size_t>(g)].push_back(h);
			auto cv = civicsOf(static_cast<size_t>(g));
			std::vector<LS::AgentId> family;
			for (const auto& c : h->Members)
			{
				if (!c->Alive() || profiles.size() >= cap) continue;
				LS::AgentId a = add(Anchor(c, h, g, cv, jd, seed32));
				family.push_back(a);
				anchors.emplace_back(c->Id, a);
			}
			LS::Clique(family, LS::EdgeKind::Family, 255, edges);
		}
		std::sort(anchors.begin(), anchors.end());

		// Synthetic agents: each world topped up to the knob's count, shaped by its Civics, some working for its businesses.
		std::vector<std::vector<LS::AgentId>> staff(G);
		for (size_t g = 0; g < G; g++)
		{
			auto cv = civicsOf(g);
			const auto& houses = housesOf[g];
			std::vector<cs::Ref<Household>> firms;
			for (const auto& h : houses)
				if (h->Business != nullptr) firms.push_back(h);
			std::uint32_t world = H(HashText(Groups[static_cast<int>(g)]->Name), seed32, 0x5E7Bu);
			for (int k = static_cast<int>(members[g].size()); k < perWorld && profiles.size() < cap; k++)
			{
				std::uint32_t r = H(world, static_cast<std::uint32_t>(k));
				LS::Profile p = Synthetic(static_cast<int>(g), r, cv, houses);
				const cs::Ref<Household>* firm = !firms.empty() && Roll(H(r, 10), 0.5) ? &firms[H(r, 11) % firms.size()] : nullptr;
				if (firm != nullptr) p.business = static_cast<uint32_t>((*firm)->Business->Id);
				LS::AgentId a = add(p);
				if (firm != nullptr) staff[g].push_back(a);
			}
		}

		// Businesses: owners and staff together; branches link the home world to each branch's world.
		for (size_t g = 0; g < G; g++)
			for (const auto& h : housesOf[g])
			{
				if (h->Business == nullptr) continue;
				const uint32_t id = static_cast<uint32_t>(h->Business->Id);
				std::vector<LS::AgentId> firm;
				for (LS::AgentId a : members[g])
					if (profiles[a].business == id) firm.push_back(a);
				LS::Clique(firm, LS::EdgeKind::Business, 160, edges);
				for (const auto& b : h->Business->Branches)
				{
					int to = GroupOf(b);
					if (to < 0 || static_cast<size_t>(to) == g) continue;
					LS::CrossGroup(members[g], members[static_cast<size_t>(to)], BranchLinks, seed, OpBranch | (id & 0xFFFFFFu), edges, 96);
				}
			}

		// The citizens' own ties (friends, rivals, lovers, patrons, enemies, mentors), weighted by strength.
		for (const auto& h : society->Houses)
		{
			if (h->Scattered || GroupOf(h->Home) < 0) continue;
			for (const auto& c : h->Members)
			{
				if (!c->Alive()) continue;
				auto self = std::lower_bound(anchors.begin(), anchors.end(), std::make_pair(c->Id, LS::AgentId(0)));
				if (self == anchors.end() || self->first != c->Id) continue;
				for (const auto& t : c->Ties)
				{
					if (t->Other == nullptr || !t->Other->Alive() || t->Other->Id <= c->Id) continue;
					auto other = std::lower_bound(anchors.begin(), anchors.end(), std::make_pair(t->Other->Id, LS::AgentId(0)));
					if (other == anchors.end() || other->first != t->Other->Id) continue;
					edges.push_back({self->second, other->second, static_cast<uint8_t>(LS::EdgeKind::Social), Byte(t->Strength * 255)});
				}
			}
		}

		// A scale-free web within each world.
		for (size_t g = 0; g < G; g++)
			LS::BarabasiAlbert(members[g], WithinTies, seed, OpWithin | static_cast<uint32_t>(g), edges, LS::EdgeKind::Social, 128);

		LS::Config config;
		config.seed = seed;
		config.topics = static_cast<uint32_t>(Topic::Count_);
		// The router asks the teacher until the distilled surrogate is registered (Pump, from engine day DistillDays on).
		config.policy = LS::Policy::AllSurrogate;
		std::vector<LS::TopicSpec> topics;
		for (uint32_t t = 0; t < config.topics; t++) topics.push_back({static_cast<uint8_t>(t), 0.0f, 0.25f, 0.3f});

		state.impl = std::make_unique<LightState::Impl>();
		// f_I and the distillation run on the worker; the readout stays empty (every mood neutral) until the first day is stepped.
		state.impl->runner.Submit([&light = *state.impl, config, profiles = std::move(profiles), edges = std::move(edges),
									  topics = std::move(topics)](const std::atomic<bool>&) {
			light.engine.Init(config, profiles, edges, topics);
			Distil(light);
		});
		return true;
	}

	// ---------------------------------------------------------------- join and leave

	void Populace::Census(Civilization& civ, Galaxy& galaxy, double jd)
	{
		auto society = Society::Find(galaxy);
		if (society == nullptr || state.impl == nullptr) return;
		LightState::Impl& light = *state.impl;
		const LS::AgentStore& store = light.engine.Store();
		for (const auto& w : civ.Worlds)
			if (GroupOf(w->Where) < 0) Groups.Add(w->Where);
		const size_t G = static_cast<size_t>(Groups.Count());
		std::vector<char> standing(G);
		for (size_t g = 0; g < G; g++)
		{
			const auto& s = Groups[static_cast<int>(g)];
			standing[g] = Civilization::Stands(s) && civ.CivicsOf(s) != nullptr;
		}

		// Who is seated: a fallen world's agents leave with it; the rest are counted by world, the citizens by id.
		std::vector<std::vector<LS::AgentId>> members(G);
		std::vector<std::pair<int, LS::AgentId>> seated;
		store.ForEachActive([&](LS::AgentId a) {
			const LS::Profile& p = store.GetProfile(a);
			if (p.group >= G || !standing[p.group])
			{
				light.leave.push_back(a);
				return;
			}
			members[p.group].push_back(a);
			if (p.externalId != 0) seated.emplace_back(static_cast<int>(p.externalId), a);
		});
		std::sort(seated.begin(), seated.end());

		const std::uint32_t seed32 = Seed32(galaxy, OriginJd);
		const std::uint32_t day = light.engine.Day();
		const uint64_t seed = light.engine.GetConfig().seed ^ (static_cast<uint64_t>(H(day, seed32, 0xB127u)) << 32);
		const uint32_t cap = static_cast<uint32_t>(cs::Math::Clamp(MaxAgents()->Value, 1.0, static_cast<double>(LS::MaxAgents)));
		const int perWorld = static_cast<int>(cs::Math::Max(0.0, AgentsPerSettlement()->Value));
		size_t total = store.ActiveCount() - light.leave.size();
		auto join = [&](const LS::Profile& p) {
			LS::AgentId a = NewAgent + static_cast<LS::AgentId>(light.joins.size());
			light.joins.push_back(p);
			total++;
			return a;
		};

		// The living citizens of standing worlds, house by house: the unseated join, tied to their family.
		std::vector<int> living;
		std::vector<std::vector<LS::AgentId>> fresh(G);
		std::vector<std::vector<cs::Ref<Household>>> housesOf(G);
		for (const auto& h : society->Houses)
		{
			int g = h->Scattered ? -1 : GroupOf(h->Home);
			if (g < 0 || !standing[static_cast<size_t>(g)]) continue;
			housesOf[static_cast<size_t>(g)].push_back(h);
			std::vector<LS::AgentId> family;
			for (const auto& c : h->Members)
			{
				if (!c->Alive()) continue;
				living.push_back(c->Id);
				auto it = std::lower_bound(seated.begin(), seated.end(), std::make_pair(c->Id, LS::AgentId(0)));
				if (it != seated.end() && it->first == c->Id) family.push_back(it->second);
				else if (total < cap)
				{
					LS::AgentId a = join(Anchor(c, h, g, civ.CivicsOf(h->Home), jd, seed32));
					family.push_back(a);
					fresh[static_cast<size_t>(g)].push_back(a);
				}
			}
			for (size_t i = 0; i < family.size(); i++)
				for (size_t j = i + 1; j < family.size(); j++)
					if (family[i] >= NewAgent || family[j] >= NewAgent)
						light.links.push_back({family[i], family[j], static_cast<uint8_t>(LS::EdgeKind::Family), 255});
		}

		// The dead, and citizens whose house scattered or left the standing worlds, leave.
		std::sort(living.begin(), living.end());
		for (const auto& [id, a] : seated)
			if (!std::binary_search(living.begin(), living.end(), id)) light.leave.push_back(a);

		for (size_t g = 0; g < G; g++)
		{
			if (!standing[g]) continue;
			if (members[g].empty())
			{
				// A world new to the society: topped up like the first day's, and given its own scale-free web.
				auto cv = civ.CivicsOf(Groups[static_cast<int>(g)]);
				std::uint32_t world = H(HashText(Groups[static_cast<int>(g)]->Name), seed32, 0x5E7Bu);
				for (int k = static_cast<int>(fresh[g].size()); k < perWorld && total < cap; k++)
					fresh[g].push_back(join(Synthetic(static_cast<int>(g), H(world, static_cast<std::uint32_t>(k)), cv, housesOf[g])));
				LS::BarabasiAlbert(fresh[g], WithinTies, seed, OpWithin | static_cast<uint32_t>(g), light.links, LS::EdgeKind::Social, 128);
			}
			else if (!fresh[g].empty())
			{
				// The newborn meet their world as every agent did: a few ties each into it.
				LS::CrossGroup(fresh[g], members[g], WithinTies * static_cast<uint32_t>(fresh[g].size()), seed,
					OpWithin | static_cast<uint32_t>(g), light.links, 128);
			}
		}
	}

	SG_SERIALIZE(Populace, "SpaceGod.Core.Populace",
		SG_F(Groups), SG_F(OriginJd),
		b.FieldWith<LightStateCodec>("State", &Self::state),
		SG_OPT(SeenEventId), SG_OPT(SeenRobberies), SG_OPT(SeenRobberyJd), SG_OPT(Wanted), // US-T975
		SG_OPT(FedDay), SG_OPT(NextPollJd), SG_OPT(Bands), // US-T984
		SG_OPT(NextTradeJd)) // US-T985
}
