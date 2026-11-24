#pragma once
// US-T533 (not a C# source): the one way a system declares a live-tuning knob for the Developer screen (US-T501).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;

	/// <summary>
	/// Who a knob's value matters to. A Simulation knob changes what happens (a thrust, a damage multiplier), so in
	/// co-op the host's value is everyone's; a View knob changes only how this player sees it (a camera's lag) and
	/// stays each player's own.
	/// </summary>
	enum class TuningScope : int
	{
		Simulation,
		View,
	};

	/// <summary>
	/// Live-tuning knobs: a number a system reads every time it uses it, set from the Developer screen. A knob is a
	/// <see cref="RangeSetting"/> keyed "tune.&lt;area&gt;.&lt;name&gt;" in the group "Tuning: &lt;area&gt;", found by the
	/// words "tuning developer" and its area. Tuning groups are never the player's own options. Its default is the value
	/// the system used before it had a knob (on a step, so it stays exact), so nothing changes until someone turns it.
	///
	/// A system keeps each knob in a function-local static, as any setting (see DevFlags):
	///   static const cs::Ref&lt;RangeSetting&gt; value = Tuning::Knob("flight", "thrust", "Thrust", 1, 0.25, 4, 0.05, "x",
	///       TuningScope::Simulation, "Every ship's main drive thrust, times this.");
	/// </summary>
	class SPACEGODCORE_API Tuning
	{
	public:
		/// <summary>What every Tuning group's name starts with.</summary>
		static constexpr const char* GroupPrefix = "Tuning: ";

		/// <summary>
		/// Declares the knob <paramref name="area"/>.<paramref name="name"/>. <paramref name="unit"/> is how the menu
		/// writes its value: "x" for a multiplier ("1.25x"), any other word after the number ("40 m/s"), or nothing
		/// for plain digits.
		/// </summary>
		static cs::Ref<RangeSetting> Knob(const cs::String& area, const cs::String& name, const cs::String& label, double defaultValue, double min,
			double max, double step, const cs::String& unit, TuningScope scope, const cs::String& description);

		/// <summary>The group a knob of <paramref name="area"/> is listed under: "Tuning: &lt;area&gt;".</summary>
		static cs::String Group(const cs::String& area);

		/// <summary>Whether <paramref name="group"/> is a Tuning group.</summary>
		static bool IsTuningGroup(const cs::String& group);

		/// <summary>Whether <paramref name="key"/> is a knob declared with <see cref="TuningScope::Simulation"/> (co-op shares it).</summary>
		static bool IsSimulationScope(const cs::String& key);
	};
}
