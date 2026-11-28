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

	private:
		LightState state;

		bool Build(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>US-T982: who was born, died or fell with their world since the last batch, as joins and leaves for the next.</summary>
		void Census(Civilization& civ, Galaxy& galaxy, double jd);
		int GroupOf(const cs::Ref<Settlement>& s) const;
		/// <summary>US-T975: an event for the next batch.</summary>
		void Queue(const MindAttic::Society::EnvEvent& e);

		SG_SERIALIZABLE(Populace);
	};
}
