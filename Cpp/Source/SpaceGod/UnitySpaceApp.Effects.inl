// UnitySpaceApp.Effects.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Effects.cpp.
//
// <summary>
// US-T447: the world's effects beyond the guns' fire (Views::WorldFx): every thrusting hull's exhaust as clouds left behind
// in world space, boost puffs, RCS vapour, damaged hulls and wrecks venting from their breaches, a fight's sparks, debris,
// shockwaves and smoke (BattleFx hands them over), a hull folding into or out of a gate, and the helm's cruise and FTL
// drives engaging. Read off the views and the simulation, never written back: view only, per peer. Tunables in
// Views::EffectsTune; docs/Effects.md.
// </summary>

public:
	/// <summary>The world's effects for this system (null before one is shown): BattleFx hands it a fight's hits and kills.</summary>
	Views::WorldFx* Effects() const { return worldFx.get(); }
	/// <summary>US-T880: the sparks of a hull hit on the held craft (null before a system is shown): the HUD hands it each hit.</summary>
	Views::HullSparks* Sparks() const { return hullSparks.get(); }

private:
	cs::Ref<Views::WorldFx> worldFx;
	cs::Ref<Transform> worldFxRoot;
	/// <summary>US-T729: the space dust round a held seat, made with the effects for this system.</summary>
	cs::Ref<Views::DustField> dustField;
	/// <summary>US-T880: hull-hit sparks off the camera, made with the effects for this system.</summary>
	cs::Ref<Views::HullSparks> hullSparks;

	/// <summary>Each ship as last drawn (by its index in the fleet): where a gate took it from, and whether it is new here.</summary>
	struct FxShipSeen
	{
		bool Live = false;
		Core::DVec3 Scene;
		Vector3 Forward;
		float Length = 0;
	};
	cs::List<FxShipSeen> fxShipsSeen;
	int fxShipCount = -1;
	bool fxCruiseWas = false, fxFtlWas = false;

	/// <summary>The effects' clock for the frame (before BattleFx hands over anything).</summary>
	void BeginWorldFx(float dt);

	/// <summary>Every hull's exhaust, vapour and venting, the craft's in battles, gate transits and drive flashes; then draws.</summary>
	void UpdateWorldFx();
