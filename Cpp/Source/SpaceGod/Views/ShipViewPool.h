#pragma once
// US-T720: ship models pooled by the model they draw, taken when the render budget puts a ship in a drawn tier and given
// back when it leaves one.

#include "UnityCompat/UnityRenderingTypes.h"

#include <vector>

namespace SpaceGod::Core
{
	class Ship;
}

namespace SpaceGod::Views
{
	class ShipView;

	/// <summary>
	/// The 3D view's ship models (<see cref="ShipView"/>), pooled by the model they draw: a ship the render budget puts
	/// in a drawn tier takes one (its own if it is still parked here, else one parked for a hull of the same model, handed
	/// over by <see cref="ShipView.Rebind"/>, else a new one is built), and gives it back when it leaves the drawn tiers or
	/// streams out. A given-back view is parked (hidden and inactive) rather than destroyed, so a battle's ships swapping
	/// in and out of the caps cost no building or tearing down; the parked are held to the drawn tiers' caps together,
	/// the longest parked destroyed first past that. Game thread only.
	/// </summary>
	class ShipViewPool final
	{
	public:
		/// <summary>The 3D view's pool, which sg.Lod reports.</summary>
		static ShipViewPool& Ships();

		/// <summary>A parked view for <paramref name="ship"/>, shown again: its own, else one of
		/// the same model handed over to it; null when none is parked (<see cref="Spawn"/> then builds one).</summary>
		cs::Ref<ShipView> Acquire(const cs::Ref<Core::Ship>& ship);

		/// <summary>Builds a new view for <paramref name="ship"/> under <paramref name="parent"/> (a spawn, counted).</summary>
		cs::Ref<ShipView> Spawn(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent);

		/// <summary><see cref="Acquire"/>, else <see cref="Spawn"/>.</summary>
		cs::Ref<ShipView> Take(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent);

		/// <summary>Gives <paramref name="view"/> back: parked for reuse, or destroyed when it shows a wreck or the pool is full.</summary>
		void Release(const cs::Ref<ShipView>& view);

		/// <summary>Forgets every view (a system's scene rebuilt), destroying the parked.</summary>
		void Reset();

		int Parked() const { return static_cast<int>(parked.size()); }
		int InUse() const { return inUse; }

		/// <summary>One line for sg.Lod: parked, in use, and spawns, reuses and destroys over the last second.</summary>
		cs::String Describe();

	private:
		struct Key
		{
			int Class = 0, Civil = 0, Parity = 0, Pods = 0, Spheres = 0;
			const void* Faction = nullptr;
			const void* Spec = nullptr;
			cs::String Kenney;
			bool operator==(const Key& o) const
			{
				return Class == o.Class && Civil == o.Civil && Parity == o.Parity && Pods == o.Pods && Spheres == o.Spheres &&
				       Faction == o.Faction && Spec == o.Spec && Kenney == o.Kenney;
			}
		};

		struct Entry
		{
			cs::Ref<ShipView> View;
			Key Of;
		};

		static Key KeyOf(const cs::Ref<Core::Ship>& ship);
		static void Destroy(const cs::Ref<ShipView>& view);
		void Tick();

		std::vector<Entry> parked; // oldest first
		int inUse = 0;
		// Counts in the second under way and in the last whole one.
		double secondStart = -1;
		int spawns = 0, reuses = 0, destroys = 0;
		int lastSpawns = 0, lastReuses = 0, lastDestroys = 0;
	};
}
