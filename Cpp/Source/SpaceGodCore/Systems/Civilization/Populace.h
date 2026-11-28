#pragma once
// US-T974: Populace, the Civilization layer's opinion model (docs/research/LightSociety-design.md, "Populace (adapter)").
// It maps the galaxy's people onto a Light Society (LightSociety/): every living citizen of a standing world is an anchor
// agent, each world is topped up with synthetic agents shaped by its Civics, tied by family, business, the citizens' own
// ties, a scale-free web within each world and a few links along business branches. The society steps on a worker
// thread a day at a time: each Civilization::Update waits for the last batch, takes its readout, gathers the days now
// due and launches the next batch. A galaxy without it (the Bench, older saves until their first day) reads neutral.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <cstdint>
#include <memory>

namespace MindAttic::Society
{
	struct EnvEvent;
}

namespace SpaceGod::Core
{
	class Civilization;
	class Galaxy;
	class Outlaw;
	class RangeSetting;
	class Settlement;

	/// <summary>What the populace holds opinions on (US-T974): a stance in -1..1 per world.</summary>
	enum class Topic : int
	{
		/// <summary>For (+) or against (-) the flag that rules them.</summary>
		PolitySupport,
		/// <summary>Afraid of pirates (+) or not (-).</summary>
		PirateFear,
		/// <summary>Faith in the SpaceGod (+) or doubt (-).</summary>
		GodFaith,
		/// <summary>Trust in trade and traders (+) or suspicion (-).</summary>
		TradeTrust,
		/// <summary>For war (+) or peace (-).</summary>
		WarHawk,
		Count_,
	};
	SPACEGODCORE_API const char* ToString(Topic v);

	/// <summary>
	/// The Light Society engine with its threads and the game thread's copy of its last readout. Saved as one base64 string
	/// (LightStateCodec in Populace.cpp); destroying it joins its worker.
	/// </summary>
	class SPACEGODCORE_API LightState final
	{
	public:
		struct Impl;

		LightState();
		~LightState();
		LightState(const LightState&) = delete;
		LightState& operator=(const LightState&) = delete;

		/// <summary>Null until the society is built (or after a load of a save without one).</summary>
		std::unique_ptr<Impl> impl;

		/// <summary>Waits for the worker, then the whole engine as a base64 blob ("" when none).</summary>
		cs::String Encode();
		/// <summary>Replaces the engine with a blob <see cref="Encode"/> made; "" or a bad blob leaves none.</summary>
		void Decode(const cs::String& text);
	};

	class SPACEGODCORE_API Populace final
	{
	public:
		/// <summary>Batches of more days than this are stepped this many days a launch.</summary>
		static constexpr int MaxStrideDays = 7;

		/// <summary>The world of each group of agents: a group's id is its index here. Only ever appended to.</summary>
		cs::List<cs::Ref<Settlement>> Groups = cs::List<cs::Ref<Settlement>>();
		/// <summary>The day the society was built (its day 0); NaN until then.</summary>
		double OriginJd = cs::Double::NaN;

		static const cs::Ref<RangeSetting>& AgentsPerSettlement();
		static const cs::Ref<RangeSetting>& MaxAgents();
		static const cs::Ref<RangeSetting>& DistillDays();

		/// <summary>
		/// Run at the top of every <see cref="Civilization.Update"/>: waits for the last batch, takes its readout, feeds it
		/// back, gathers the days now due and launches the next batch (builds the society first when there is none).
		/// </summary>
		void Pump(Civilization& civ, Galaxy& galaxy, double jd);

		/// <summary>A world's mean stance on a topic (-1..1); 0, neutral, before the society has read it.</summary>
		double Mood(const cs::Ref<Settlement>& s, Topic topic) const;
		/// <summary>The galaxy's populace's <see cref="Mood"/>, or neutral when the galaxy has none (the Bench).</summary>
		static double MoodOf(const Galaxy& galaxy, const cs::Ref<Settlement>& s, Topic topic);
		/// <summary>US-T978: a world's opinion on a topic as last read: its mean stance, the shares of supporters, neutrals
		/// and sceptics, and the share whose side changed since the reading before.</summary>
		struct Reading
		{
			double Mean = 0, Supporter = 0, Neutral = 0, Skeptic = 0, ChangeRate = 0;
		};
		/// <summary>US-T978: the world's <see cref="Reading"/> on a topic; false before the society has read it.</summary>
		bool Read(const cs::Ref<Settlement>& s, Topic topic, Reading& out) const;

		/// <summary>US-T975: turns what happened up to <paramref name="jd"/> into events for the next batch.</summary>
		void Perceive(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>US-T975: the last civilization event perceived, the lane robberies counted and the last robbed arrival
		/// perceived, and the outlaws at large at the last look (whose fates are diffed for bounty kills).</summary>
		int SeenEventId = 0;
		int SeenRobberies = 0;
		double SeenRobberyJd = cs::Double::NaN;
		cs::List<cs::Ref<Outlaw>> Wanted = cs::List<cs::Ref<Outlaw>>();
		/// <summary>US-T976: moves the worlds by the readout just taken.</summary>
		void Feedback(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>US-T984: the readout day last fed back, the day of the next poll (NaN until the first feedback), and each
		/// group's mood bands (group * <see cref="BandCount"/> + band): -1, 0 or 1.</summary>
		int FedDay = 0;
		double NextPollJd = cs::Double::NaN;
		cs::List<int> Bands = cs::List<int>();
		static constexpr int BandCount = 4;
		static const cs::Ref<RangeSetting>& FeedbackGain();
		static const cs::Ref<RangeSetting>& FeedbackCap();
		/// <summary>US-T984: a world's share of supporters on a topic (0..1); 0 before the society has read it.</summary>
		double Share(const cs::Ref<Settlement>& s, Topic topic) const;
		/// <summary>US-T984: pushes group <paramref name="group"/>'s stance on a topic in the next batch, as of day jd.</summary>
		void Hear(int group, Topic topic, float push, float magnitude, double jd);
		/// <summary>US-T984: the society's readout day, or -1 when there is no society.</summary>
		int ReadDay() const;
		/// <summary>US-T985: each month the trading houses' heads of each world deal with each other; the mean outcome is heard
		/// as trade trust. NextTradeJd is the day of the next round (NaN until the first).</summary>
		void Trade(Civilization& civ, Galaxy& galaxy, double jd);
		double NextTradeJd = cs::Double::NaN;
		/// <summary>US-T977: a world's group (its index in <see cref="Groups"/>), or -1 when the society holds none of it.</summary>
		int GroupOf(const cs::Ref<Settlement>& s) const;
		/// <summary>US-T977: a hash of the readout last taken, the same on every co-op peer in lockstep; 0 when there is no society.</summary>
		uint64_t ReadoutHash() const;
		/// <summary>US-T977: the galaxy's populace's <see cref="ReadoutHash"/>, or 0 when the galaxy has none (the Bench).</summary>
		static uint64_t ReadoutHashOf(const Galaxy& galaxy);

		// ---------------------------------------------------------------- US-T981: sg.LightSociety
		/// <summary>US-T981: who decides the society's changes: the schedule (the teacher, its distilled surrogate from
		/// <see cref="DistillDays"/> on), always the teacher, always the surrogate, or the surrogate for a share of them. Until
		/// a surrogate is distilled the teacher answers whatever the routing.</summary>
		enum class Routing : int
		{
			Scheduled,
			Teacher,
			Surrogate,
			Mix,
		};
		/// <summary>US-T981: the society as sg.LightSociety shows it. The distillation's figures are read only between batches
		/// (Stepping is true while one runs), and only for a surrogate distilled since the game was loaded (Samples 0 else).</summary>
		struct LightStatus
		{
			bool Built = false, Stepping = false, Distilled = false, Switched = false;
			Routing Route = Routing::Scheduled;
			float Weight = 0;
			int Agents = 0, Day = -1, Threads = 0;
			float MacroF1 = 0, ChangeRateGap = 0;
			int Samples = 0;
			uint64_t CacheHits = 0, CacheMisses = 0;
		};
		/// <summary>US-T981: the society's <see cref="LightStatus"/>; Built is false when there is none.</summary>
		LightStatus Status() const;
		/// <summary>US-T981: routes the society's changes so from the next batch on (weight: the surrogate's share, for Mix);
		/// false when there is no society.</summary>
		bool Steer(Routing routing, float weight);
		/// <summary>US-T981: distils the surrogate afresh from the society as it stands, in the next batch (the teacher decides
		/// until the schedule switches it back in); false when there is no society.</summary>
		bool Redistill();
		/// <summary>US-T981: a sandbox run of one of the Light Society's paper scenarios, on a society apart from the game's.</summary>
		struct SandboxRun
		{
			bool Ok = false;
			/// <summary>Why it did not run, when it did not.</summary>
			cs::String Error;
			/// <summary>Its curves: a header line, then a line per row shown.</summary>
			cs::List<cs::String> Lines = cs::List<cs::String>();
			/// <summary>FNV-1a of its whole output written as a Codec blob: the same for the same arguments on any thread count.</summary>
			uint64_t Hash = 0;
		};
		/// <summary>US-T981: runs scenario "diffusion", "continuous", "trust", "dictator", "repeated" or "ultimatum" (the last
		/// four show the games scenario's rows of that game) with n agents (pairs for a game; 0 the scenario's default), days
		/// rounds (0 the default) and the seed, its chunks on a pool of that many threads. It blocks: run it off the game thread.</summary>
		static SandboxRun Sandbox(const cs::String& scenario, int n, int days, uint64_t seed, int threads);
		/// <summary>US-T981: distils the teacher's persuasion from synthetic profiles drawn from the seed and measures the
		/// surrogate against it; false when it would not train. It blocks: run it off the game thread.</summary>
		static bool SandboxDistill(uint64_t seed, float& macroF1, float& changeRateGap, int& samples);

	private:
		LightState state;

		bool Build(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>US-T982: who was born, died or fell with their world since the last batch, as joins and leaves for the next.</summary>
		void Census(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>US-T975: an event for the next batch.</summary>
		void Queue(const MindAttic::Society::EnvEvent& e);

		SG_SERIALIZABLE(Populace);
	};
}
