// UnitySpaceApp.RideAlong.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.RideAlong.cpp.
//
// US-T954: riding along (docs/Possession.md). The god goes into a ship without taking its helm: the camera rides in its chase,
// the possession show plays and the view becomes what its sensors pick up, but its crew keeps flying its own schedule (lanes,
// parking, its trade), the god's warp keeps running and nothing in the simulation changes: no PilotedShip, no session seat,
// no co-op frame. Local view only. Ctrl+P (or "Ride along" on the selection card) starts it; P or Esc goes back to the god.

public:
	/// <summary>Whether this machine's player is riding along in a ship.</summary>
	bool IsRiding() const;

	/// <summary>The ship ridden in, or null.</summary>
	cs::Ref<Core::Ship> RiddenShip() const { return IsRiding() ? rideAlong.Ship : nullptr; }

	/// <summary>Whether a ride along in <paramref name="ship"/> can start now (the god's view, a ship still flying).</summary>
	bool CanRideAlong(const cs::Ref<Core::Ship>& ship);

	/// <summary>Rides along in <paramref name="ship"/>: its chase view and its sensors' picture, its job and the warp untouched.</summary>
	void StartRideAlong(const cs::Ref<Core::Ship>& ship);

	/// <summary>Back to the god's view the ride was started from; says <paramref name="why"/> if given.</summary>
	void StopRideAlong(const cs::String& why = cs::String());

private:
	Target rideAlong = Target::None();
	cs::Ref<GUIStyle> rideStyle;

	/// <summary>Ctrl+P on the selection (or what is under the mouse).</summary>
	void ToggleRideAlong();

	/// <summary>Each frame: the ride ends if the ship is gone or the camera was taken off it.</summary>
	void UpdateRideAlong();

	/// <summary>The caption over the top of the view while riding.</summary>
	void DrawRideAlongHud();
