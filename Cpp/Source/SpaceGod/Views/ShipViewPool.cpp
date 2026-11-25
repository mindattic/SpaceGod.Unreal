// US-T720: ship models pooled by the model they draw (see ShipViewPool.h).

#include "Views/ShipViewPool.h"
#include "Views/ShipView.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"

#include "Combat/BulkFreighters.h"
#include "Combat/CombatState.h"
#include "RenderBudget.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

#include <chrono>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	ShipViewPool& ShipViewPool::Ships()
	{
		static ShipViewPool pool;
		return pool;
	}

	ShipViewPool::Key ShipViewPool::KeyOf(const cs::Ref<Ship>& ship)
	{
		// What ShipModels::Build and the view's hardpoints are built from: a hull of the same class, craft, livery and
		// fitting draws the same model (a courier alternates between two by its id; a truss carries its pods).
		Key k;
		k.Class = static_cast<int>(ship->Spec->Class);
		k.Civil = static_cast<int>(ship->Civil);
		k.Parity = ship->Spec->Class == ShipClass::Courier ? ship->Id % 2 : 0;
		if (BulkFreighters::IsTruss(ship->Spec->Class))
		{
			k.Pods = BulkFreighters::Pods(ship);
			k.Spheres = BulkFreighters::Spheres(ship);
		}
		k.Faction = ship->Faction.get();
		k.Spec = ship->Combat ? ship->Combat->Spec.get() : nullptr;
		k.Kenney = Rendering::ShipModels::KenneyModelFor(ship);
		return k;
	}

	void ShipViewPool::Destroy(const cs::Ref<ShipView>& view)
	{
		if (!Alive(view->Root)) return;
		Rendering::Materials::DestroyOwned(view->Root->transform);
		GameObject::Destroy(view->Root);
	}

	void ShipViewPool::Tick()
	{
		double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (secondStart < 0) secondStart = now;
		if (now - secondStart < 1) return;
		bool gap = now - secondStart >= 2; // nothing happened through a whole second: it counted none
		lastSpawns = gap ? 0 : spawns;
		lastReuses = gap ? 0 : reuses;
		lastDestroys = gap ? 0 : destroys;
		spawns = reuses = destroys = 0;
		secondStart = now;
	}

	cs::Ref<ShipView> ShipViewPool::Acquire(const cs::Ref<Ship>& ship)
	{
		Tick();
		if (parked.empty()) return nullptr;
		int own = -1, same = -1;
		Key key = KeyOf(ship);
		for (int i = static_cast<int>(parked.size()) - 1; i >= 0; i--)
		{
			const Entry& e = parked[static_cast<size_t>(i)];
			if (!(e.Of == key)) continue;
			if (e.View->Ship == ship) { own = i; break; }
			if (same < 0) same = i;
		}
		int at = own;
		if (at < 0 && same >= 0 && parked[static_cast<size_t>(same)].View->Rebind(ship)) at = same;
		if (at < 0) return nullptr;
		cs::Ref<ShipView> view = parked[static_cast<size_t>(at)].View;
		parked.erase(parked.begin() + at);
		if (!Alive(view->Root)) return nullptr;
		view->Root->SetActive(true);
		inUse++;
		reuses++;
		return view;
	}

	cs::Ref<ShipView> ShipViewPool::Spawn(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& parent)
	{
		Tick();
		inUse++;
		spawns++;
		return cs::New<ShipView>(ship, parent);
	}

	cs::Ref<ShipView> ShipViewPool::Take(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& parent)
	{
		cs::Ref<ShipView> view = Acquire(ship);
		return view != nullptr ? view : Spawn(ship, parent);
	}

	void ShipViewPool::Release(const cs::Ref<ShipView>& view)
	{
		if (view == nullptr) return;
		Tick();
		if (inUse > 0) inUse--;
		if (!Alive(view->Root)) return;
		// A wreck's view shows what only its own ship went through; it is not handed on.
		if (view->Ship == nullptr || view->Ship->Destroyed)
		{
			Destroy(view);
			destroys++;
			return;
		}
		view->SetVisible(false);
		view->Root->SetActive(false);
		parked.push_back({ view, KeyOf(view->Ship) });
		// Held to the drawn tiers' caps together: past that, the longest parked go.
		int cap = static_cast<int>(RenderBudget::FullCap()->Value + RenderBudget::SimplifiedCap()->Value);
		int over = static_cast<int>(parked.size()) - cs::Math::Max(cap, 0);
		if (over <= 0) return;
		for (int i = 0; i < over; i++) Destroy(parked[static_cast<size_t>(i)].View);
		destroys += over;
		parked.erase(parked.begin(), parked.begin() + over);
	}

	void ShipViewPool::Reset()
	{
		for (const Entry& e : parked) Destroy(e.View);
		parked.clear();
		inUse = 0;
	}

	cs::String ShipViewPool::Describe()
	{
		Tick();
		return cs::Cat(inUse, " in use, ", static_cast<int>(parked.size()), " parked (pool ", inUse + static_cast<int>(parked.size()), "); last second: ",
			lastSpawns, " spawned, ", lastReuses, " reused, ", lastDestroys, " destroyed");
	}
}
