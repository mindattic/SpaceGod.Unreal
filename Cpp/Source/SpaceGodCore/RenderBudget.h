#pragma once
// US-T719: the render budget's hard caps per tier, over RenderLod's per-size tiers. Not a C# port.

#include "RenderLod.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core
{
	class RangeSetting;

	/// <summary>Why a thing matters to the player beyond its size on screen; a candidate with any of these is ranked
	/// ahead of every one without, and is never culled for being small or past the horizon.</summary>
	namespace LodRelevance
	{
		constexpr uint8_t None = 0;
		/// <summary>The hull the player flies or sits in.</summary>
		constexpr uint8_t Piloted = 1 << 0;
		/// <summary>The player's selected target.</summary>
		constexpr uint8_t Target = 1 << 1;
		/// <summary>Flies in the player's group: its leader, or an escort of the same leader.</summary>
		constexpr uint8_t Wingman = 1 << 2;
		/// <summary>A hostile whose battle target is the player's hull.</summary>
		constexpr uint8_t Attacker = 1 << 3;
	}

	/// <summary>What one frame's <see cref="RenderBudget.Assign"/> did, for sg.Lod and the stats.</summary>
	struct LodBudgetStats
	{
		/// <summary>How many candidates got each tier, by <see cref="LodTier"/> (Aggregate is never assigned here).</summary>
		int Count[5] = {};
		int Candidates = 0;
		/// <summary>Candidates whose size earned a better tier than a cap let them have.</summary>
		int Demoted = 0;
		/// <summary>Of those culled, how many for being under the cull size or past the horizon (the rest by the icon cap).</summary>
		int CulledSmallOrFar = 0;
		/// <summary>The caps the frame ran under.</summary>
		int FullCap = 0, SimplifiedCap = 0, IconCap = 0;
		/// <summary>Game-thread time the frame's Assign took, milliseconds.</summary>
		double AssignMs = 0;
	};

	/// <summary>
	/// The render budget's hard caps (US-T719): each frame its candidates (things that could be drawn: ships in view, say)
	/// are ranked by gameplay relevance first (<see cref="LodRelevance"/>: the piloted hull, the target, wingmen,
	/// attackers) and then by apparent size, and walked in that order taking the tier their size earns
	/// (<see cref="RenderLod.Tier"/>) while that tier has room under its cap (<see cref="FullCap"/>,
	/// <see cref="SimplifiedCap"/>, <see cref="IconCap"/>). A full model past its cap falls to its simplified form if it
	/// has one, else to its far form; a simplified one past its cap to its far form (an icon or an impostor, both
	/// <see cref="LodTier.Icon"/>); an icon past its cap, or anything under the cull size or past the visual horizon,
	/// is <see cref="LodTier.Culled"/>: no actor, no instance (the HUD's sensor contacts are drawn apart from this).
	/// </summary>
	/// <remarks>
	/// Hysteresis keeps a thing from flickering between tiers at a threshold or a cap's edge: a thing changes tier on
	/// size only once it is <see cref="Hysteresis"/> past the swap point, and one already in a capped tier outranks a
	/// newcomer up to that much bigger. Per-frame cost is one sort of the candidates and two linear passes, with every
	/// buffer kept between frames (no allocation once they have grown to the scene's size). Game thread only.
	/// Usage per frame: <see cref="Begin"/>, optional <see cref="Mark"/>s, an <see cref="Add"/> per candidate,
	/// <see cref="Assign"/>, then <see cref="TierOf"/> by key until the next Begin.
	/// </remarks>
	class SPACEGODCORE_API RenderBudget final
	{
	public:
		/// <summary>At most this many things are drawn as full models at once.</summary>
		static const cs::Ref<RangeSetting>& FullCap();

		/// <summary>At most this many things are drawn as simplified models at once.</summary>
		static const cs::Ref<RangeSetting>& SimplifiedCap();

		/// <summary>At most this many things are drawn as icons or impostors at once.</summary>
		static const cs::Ref<RangeSetting>& IconCap();

		/// <summary>How far past a swap point, as a fraction of it, a thing must go before it changes tier.</summary>
		static const cs::Ref<RangeSetting>& Hysteresis();

		/// <summary>The 3D view's budget for ships (keys are ship ids), which sg.Lod reports.</summary>
		static RenderBudget& Ships();

		/// <summary>Starts a frame: forgets the last frame's candidates (keeping their tiers for hysteresis).</summary>
		void Begin();

		/// <summary>Adds <paramref name="flags"/> to the relevance of <paramref name="key"/> for this frame, before or after its <see cref="Add"/>
		/// (marking attackers from a battle's side is cheaper than asking per candidate).</summary>
		void Mark(int key, uint8_t flags);

		/// <summary>
		/// Adds a candidate this frame: <paramref name="key"/> a small non-negative id stable across frames (a ship's id),
		/// <paramref name="px"/> its apparent size, <paramref name="distance"/> from the camera in the units the horizon
		/// is given in, <paramref name="relevance"/> its <see cref="LodRelevance"/> flags, <paramref name="c"/> its render class.
		/// A key added twice in one frame keeps its first.
		/// </summary>
		void Add(int key, double px, double distance, uint8_t relevance, const LodClass& c);

		/// <summary>Assigns every candidate's tier by the current settings, with <paramref name="horizon"/> the visual
		/// horizon (the camera's far clip) in the candidates' distance units.</summary>
		void Assign(double horizon);

		/// <summary>Assigns every candidate's tier under the caps and swap points given (the settings-free core of <see cref="Assign(double)"/>).</summary>
		void Assign(double horizon, int fullCap, int simplifiedCap, int iconCap, double simplifyBelowPx, double cullBelowPx, double hysteresis);

		/// <summary>Whether <paramref name="key"/> was a candidate this frame (its <see cref="TierOf"/> is then the budget's).</summary>
		bool Has(int key) const;

		/// <summary>The tier <paramref name="key"/> was assigned this frame; <see cref="LodTier.Culled"/> for a key that was no candidate.</summary>
		LodTier TierOf(int key) const;

		/// <summary>The latest frame's counts, caps and cost.</summary>
		const LodBudgetStats& Stats() const { return stats; }

		/// <summary>One line for sg.Lod: the count in each tier against its cap, the demotions and the cost.</summary>
		cs::String Describe() const;

	private:
		struct Candidate
		{
			int Key;
			double Px, Distance;
			uint8_t Relevance;
			const LodClass* Class;
			LodTier Previous; // last frame's tier, Culled when it was no candidate then
			bool Seen;        // whether it was a candidate last frame
			LodTier Wanted;   // the tier its size earns, before the caps
			double Score;
		};

		struct State;
		static State& S();

		static LodTier Earned(const Candidate& c, double horizon, double simplifyBelowPx, double cullBelowPx, double band);

		std::vector<Candidate> candidates;
		std::vector<int> order;
		// By key: the tier assigned, the frame it was assigned in, and this frame's marks.
		std::vector<uint8_t> tierByKey;
		std::vector<uint32_t> frameByKey;
		std::vector<uint8_t> marks;
		std::vector<int> marked;
		uint32_t frame = 1;
		bool assigned = false;
		LodBudgetStats stats;
	};
}
