// Port of Unity.SpaceGod Runtime/Camera/SpaceCamera.cs (US-T29).

#include "Camera/SpaceCamera.h"
#include "Input/InputState.h"
#include "PlayerOptions.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/FlightModels.h"
#include "ScaleModel.h"
#include "Settings/GameSetting.h"
#include "Settings/Tuning.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Cameras
{
	const char* ToString(SpaceCamera::CameraMode v)
	{
		switch (v)
		{
			case SpaceCamera::CameraMode::Free: return "Free";
			case SpaceCamera::CameraMode::Travel: return "Travel";
			case SpaceCamera::CameraMode::Focus: return "Focus";
		}
		return "";
	}

	const cs::Ref<RangeSetting>& SpaceCamera::LookSensitivitySetting()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.lookSensitivity", SettingGroups::Controls,
			"Mouse look sensitivity", 1.0, 0.25, 4.0, 0.05,
			"How far the view (and a fighter's aim) turns for a mouse movement.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.00"), "x"); }), "mouse camera speed aim");
		return value;
	}

	const cs::Ref<ToggleSetting>& SpaceCamera::InvertLook()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("controls.invertLook", SettingGroups::Controls,
			"Invert vertical look", false, "Pushing the mouse forward looks (and pitches) down instead of up.", "mouse y pitch flight");
		return value;
	}

	namespace
	{
		cs::Ref<RangeSetting> AxisScale(const char* key, const char* label, const char* axis, const char* tags)
		{
			return cs::New<RangeSetting>(key, SettingGroups::Controls, label, 1.0, 0.25, 4.0, 0.05,
				cs::Cat("Scales the mouse look sensitivity ", axis, " only."),
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.00"), "x"); }), tags);
		}
	}

	const cs::Ref<RangeSetting>& SpaceCamera::LookHorizontal()
	{
		static const cs::Ref<RangeSetting> value = AxisScale("controls.lookHorizontal", "Horizontal look sensitivity",
			"left and right", "mouse camera speed aim yaw x axis");
		return value;
	}

	const cs::Ref<RangeSetting>& SpaceCamera::LookVertical()
	{
		static const cs::Ref<RangeSetting> value = AxisScale("controls.lookVertical", "Vertical look sensitivity",
			"up and down", "mouse camera speed aim pitch y axis");
		return value;
	}

	const cs::Ref<RangeSetting>& SpaceCamera::MouseSmoothing()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.mouseSmoothing", SettingGroups::Controls,
			"Mouse smoothing", 0.0, 0.0, 0.9, 0.05,
			"Evens out a jittery mouse by blending each movement with the last few; higher is smoother but lags behind the hand.",
			cs::Func<cs::String, double>([](double v) { return v <= 0 ? cs::String("Off") : cs::Cat(cs::ToInt(cs::Math::Round(v * 100)), "%"); }),
			"mouse camera smooth filter jitter lag");
		return value;
	}

	Vector2 SpaceCamera::ShapeMouse(Vector2 raw, float dt)
	{
		static Vector2 smoothed = Vector2::zero();
		if (PlayerOptions::ScriptedRun()) return raw;
		Vector2 scaled(raw.x * static_cast<float>(LookHorizontal()->Value), raw.y * static_cast<float>(LookVertical()->Value));
		float s = static_cast<float>(MouseSmoothing()->Value);
		if (s <= 0) return smoothed = scaled;
		// The blend is per 60 Hz frame, so the same setting feels the same at any frame rate.
		float keep = Mathf::Pow(s, Mathf::Max(0.0f, dt) * 60.0f);
		smoothed = smoothed * keep + scaled * (1.0f - keep);
		return smoothed;
	}

	float SpaceCamera::LookSensitivity() { return BaseLookSensitivity() * static_cast<float>(LookSensitivitySetting()->Value); }

	float SpaceCamera::LookY(float dy) { return InvertLook()->Value ? -dy : dy; }

	double SpaceCamera::PilotViewLift()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "pilotViewLift", "Pilot view lift", 0.22, 0, 1, 0.01, "",
			TuningScope::View, "How far above the line of sight a free-aim chase sits, in chase distances.");
		return value->Value;
	}

	float SpaceCamera::PilotCamResponseHz(cs::Nullable<FlightFamily> family)
	{
		static const cs::Ref<RangeSetting> fighter = Tuning::Knob("camera", "pilotResponse", "Pilot chase response", 20, 1, 60, 0.5, "Hz",
			TuningScope::View, "How fast the pilot chase turns after a fighter's facing (light fighter, interceptor, runner): higher is tighter, lower a looser chase.");
		static const cs::Ref<RangeSetting> heavy = Tuning::Knob("camera", "pilotResponseHeavy", "Heavy chase response", 12, 1, 60, 0.5, "Hz",
			TuningScope::View, "How fast the pilot chase turns after a heavy fighter's or a bomber's facing.");
		static const cs::Ref<RangeSetting> hauler = Tuning::Knob("camera", "pilotResponseHauler", "Hauler chase response", 12, 1, 60, 0.5, "Hz",
			TuningScope::View, "How fast the pilot chase turns after a hauler's facing.");
		static const cs::Ref<RangeSetting> capital = Tuning::Knob("camera", "pilotResponseCapital", "Capital chase response", 6, 1, 60, 0.5, "Hz",
			TuningScope::View, "How fast the pilot chase turns after a capital ship's facing.");
		if (!family.has_value()) return static_cast<float>(fighter->Value);
		switch (*family)
		{
		case FlightFamily::HeavyFighter: case FlightFamily::Bomber: return static_cast<float>(heavy->Value);
		case FlightFamily::Hauler: return static_cast<float>(hauler->Value);
		case FlightFamily::Capital: return static_cast<float>(capital->Value);
		default: return static_cast<float>(fighter->Value);
		}
	}

	float SpaceCamera::PilotLockTrackStrength()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "lockTrack", "Lock track strength", 0.6, 0, 1, 0.05, "",
			TuningScope::View, "How far the pilot chase leans from the hull's facing toward a locked target (0 ignores it, 1 looks straight at it).");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::TravelSeconds()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "travelSeconds", "Travel time", 1.6, 0.1, 6, 0.1, "s",
			TuningScope::View, "How long the camera's glide to a focused body or ship takes.");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::QuickTravelSeconds()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "quickTravelSeconds", "Quick travel time", 0.3, 0.05, 2, 0.05, "s",
			TuningScope::View, "How long a hurried glide (a seat switched to another mid-fight) takes.");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::LevelSeconds()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "levelSeconds", "Level time", 1.4, 0.1, 6, 0.1, "s",
			TuningScope::View, "How long the view takes to swing round to a new level, or ease a chase's roll out when let go.");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::BaseLookSensitivity()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "lookSensitivity", "Base look sensitivity", 0.12, 0.01, 1, 0.01, "deg",
			TuningScope::View, "Degrees the view turns per mouse count at the player's 1x sensitivity, before the Controls scales.");
		return static_cast<float>(value->Value);
	}

	double SpaceCamera::ChaseDistance()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "chaseDistance", "Chase distance", 1, 0.25, 4, 0.05, "x",
			TuningScope::View, "The pilot chase's distance behind the hull, times the framing each ship's chase is entered with.");
		return value->Value;
	}

	float SpaceCamera::ChaseLagHz()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "chaseLagHz", "Chase lag spring", 2.5, 0.5, 10, 0.1, "Hz",
			TuningScope::View, "How fast the pilot chase's spring lag swings after the hull's felt acceleration: higher is stiffer.");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::ChaseLagDamping()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "chaseLagDamping", "Chase lag damping", 0.7, 0.1, 1.5, 0.05, "",
			TuningScope::View, "How the chase's spring lag settles: 1 without overshoot, lower bounces past and back.");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::ChaseLagPerG()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "chaseLagPerG", "Chase lag per g", 0.015, 0, 0.1, 0.005, "x",
			TuningScope::View, "How far the pilot chase lags against a g of felt acceleration, in chase distances (0 is off).");
		return static_cast<float>(value->Value);
	}

	float SpaceCamera::ChaseLagMax()
	{
		static const cs::Ref<RangeSetting> value = Tuning::Knob("camera", "chaseLagMax", "Chase lag limit", 0.12, 0, 0.5, 0.01, "x",
			TuningScope::View, "The most the pilot chase's spring lag ever moves the eye, in chase distances.");
		return static_cast<float>(value->Value);
	}

	void SpaceCamera::DeclareTuning()
	{
		PilotViewLift(); PilotCamResponseHz(cs::Nullable<FlightFamily>()); PilotLockTrackStrength(); TravelSeconds(); QuickTravelSeconds(); LevelSeconds();
		BaseLookSensitivity(); ChaseDistance(); ChaseLagHz(); ChaseLagDamping(); ChaseLagPerG(); ChaseLagMax();
	}

	SpaceCamera::SpaceCamera(ICameraWorld* inWorld)
		: world(inWorld)
	{
	}

	// ---------------------------------------------------------------- commands

	void SpaceCamera::Level(Vector3 planeNormal)
	{
		if (planeNormal.sqrMagnitude() < 1e-12f) planeNormal = Vector3::up();
		auto before = Rotation;
		UpFrame = Quaternion::FromToRotation(Vector3::up(), planeNormal.normalized());
		RederiveAngles();
		Rotation = ComposedRotation();
		levelOffset = before * Quaternion::Inverse(Rotation);
		levelT = 0.0f;
	}

	void SpaceCamera::SetUpFrame(Quaternion upFrame)
	{
		UpFrame = upFrame;
		levelT = 1.0f;
		RederiveAngles();
	}

	void SpaceCamera::RederiveAngles()
	{
		auto forward = ToLocal(Rotation * Vector3::forward());
		yaw = Mathf::Atan2(forward.x, forward.z) * Mathf::Rad2Deg;
		pitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(forward.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -89.0f, 89.0f);
		if (Mode == CameraMode::Travel)
		{
			// Mid-flight: re-express where the flight ends, not where the camera happens to be.
			auto look = -ToLocal(ToVector(travelEndDirection));
			orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
			orbitPitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -88.0f, 88.0f);
		}
		else if (Mode == CameraMode::Focus && !FocusTarget.IsNone())
		{
			auto offset = ScenePosition - OrbitCentre(FocusTarget);
			if (offset.LengthSquared() > 0)
			{
				auto look = -ToLocal(ToVector(offset.Normalized()));
				orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
				orbitPitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -88.0f, 88.0f);
			}
		}
	}

	Quaternion SpaceCamera::ComposedRotation() const
	{
		return Mode == CameraMode::Focus ? UpFrame * Quaternion::Euler(orbitPitch, orbitYaw, 0) : UpFrame * Quaternion::Euler(pitch, yaw, 0);
	}

	void SpaceCamera::PlaceFree(DVec3 scenePosition, float yawDeg, float pitchDeg)
	{
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		SetRotation(Quaternion::Euler(Mathf::Clamp(pitchDeg, -89.0f, 89.0f), yawDeg, 0));
		velocity = Vector3::zero();
		SetScenePosition(scenePosition);
	}

	void SpaceCamera::SetPose(DVec3 scenePosition, Quaternion rotation)
	{
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		SetRotation(rotation);
		velocity = Vector3::zero();
		SetScenePosition(scenePosition);
	}

	void SpaceCamera::SetRotation(Quaternion rotation)
	{
		Rotation = rotation;
		auto forward = ToLocal(rotation * Vector3::forward());
		yaw = Mathf::Atan2(forward.x, forward.z) * Mathf::Rad2Deg;
		pitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(forward.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -89.0f, 89.0f);
	}

	void SpaceCamera::FocusOn(const Target& target, double radii, cs::Nullable<float> pitchOverride)
	{
		if (target.IsNone() || !world->TargetValid(target)) return; // US-T185: nothing real to fly to
		auto targetScene = OrbitCentre(target);
		double r = cs::Math::Max(world->TargetRadius(target), 1e-9);
		auto offset = ScenePosition - targetScene;
		double distance = offset.Length();

		double framing = !cs::Double::IsNaN(radii) ? radii : DefaultFraming(target);
		double currentRadii = distance / r;
		if (cs::Double::IsNaN(radii) && Mode != CameraMode::Free && FocusTarget == target) framing = zoomTarget;
		else if (cs::Double::IsNaN(radii) && currentRadii > MinZoom(target) * 1.05 && currentRadii < framing) framing = currentRadii;

		// Work in the level frame: elevation is measured from its horizon.
		auto dir = distance > 0 ? ToLocal(ToVector(offset / distance)) : Vector3(0, 0.3f, -1).normalized();
		// Approach from the side we are already on, but never from straight over a pole.
		auto flat = Vector3(dir.x, 0, dir.z);
		float elevation = Mathf::Asin(Mathf::Clamp(dir.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
		elevation = pitchOverride.has_value() ? *pitchOverride : Mathf::Clamp(elevation, -55.0f, 55.0f);
		if (flat.sqrMagnitude() < 1e-8f) flat = Vector3::back();
		flat.Normalize();
		float e = elevation * Mathf::Deg2Rad;
		auto endLocal = Vector3(flat.x * Mathf::Cos(e), Mathf::Sin(e), flat.z * Mathf::Cos(e));
		auto endWorld = UpFrame * endLocal;

		travelStartOffset = distance > 0 ? offset : DVec3(endWorld.x, endWorld.y, endWorld.z) * r * framing * 3;
		travelStartRotation = Rotation;
		travelEndDirection = DVec3(endWorld.x, endWorld.y, endWorld.z);
		travelEndZoom = framing;
		travelT = 0;
		hurried = false;
		ghostSeconds = 0;
		travelToPose = false;

		// The orbit angles that reproduce endDir: offset = Euler(pitch, yaw) * back, in the level frame.
		auto look = -endLocal;
		orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
		orbitPitch = -Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
		zoom = zoomTarget = framing;

		FocusTarget = target;
		Mode = CameraMode::Travel;
		velocity = Vector3::zero();
	}

	void SpaceCamera::ExitFocus()
	{
		if (Mode == CameraMode::Free) return;
		auto rotation = Rotation;
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		travelToPose = false;
		ghostSeconds = 0;
		PilotLocked = false;
		PilotView = std::nullopt;
		PilotEye = nullptr;
		ResetChaseLag();
		SetRotation(rotation);
		// US-T463: a chase or a seat's view may be rolled (the hull banked under the helm); the free camera has no roll, so
		// letting go once snapped it level in one frame. The roll now eases out over LevelSeconds, as Level does.
		auto level = ComposedRotation();
		if (Quaternion::Angle(rotation, level) > 0.05f)
		{
			levelOffset = rotation * Quaternion::Inverse(level);
			levelT = 0.0f;
		}
		Rotation = level;
		velocity = Vector3::zero();
		SetScenePosition(ScenePosition);
	}

	void SpaceCamera::EnterPilotChase(const Target& target, double radii)
	{
		FocusOn(target, radii);
		PilotLocked = true;
		PilotEye = nullptr;
		ResetChaseLag();
		chaseLagTarget = target;
	}

	void SpaceCamera::Ghost(float seconds, float sway)
	{
		if (Mode != CameraMode::Travel || !(seconds > 0)) return;
		ghostSeconds = seconds;
		ghostSway = Mathf::Clamp01(sway);
	}

	bool SpaceCamera::GlideToPose(const Target& anchor, DVec3 endScene, Quaternion endRotation)
	{
		if (anchor.IsNone() || !world->TargetValid(anchor)) return false;
		double r = world->TargetRadius(anchor);
		if (!(r > 0)) return false;
		auto centre = world->FocusScene(anchor);
		auto end = endScene - centre;
		double endDist = end.Length();
		if (!(endDist > r * 1e-6)) return false;
		auto start = ScenePosition - centre;
		PilotLocked = false;
		PilotView = std::nullopt;
		PilotEye = nullptr;
		ResetChaseLag();
		travelStartOffset = start.LengthSquared() > 0 ? start : end;
		travelStartRotation = Rotation;
		travelEndDirection = end / endDist;
		travelEndZoom = endDist / r;
		travelEndRotation = endRotation;
		travelToPose = true;
		travelT = 0;
		hurried = false;
		ghostSeconds = 0;
		zoom = zoomTarget = travelEndZoom;
		FocusTarget = anchor;
		Mode = CameraMode::Travel;
		velocity = Vector3::zero();
		return true;
	}

	double SpaceCamera::DefaultFraming(const Target& target)
	{
		return target.IsShip() || target.IsUnit() ? 5.0 : 3.4;
	}

	// ---------------------------------------------------------------- per frame

	void SpaceCamera::Update(float dt, const Input::InputState& input)
	{
		bool moving = input.Move.sqrMagnitude() > 0.01f;
		if (Mode != CameraMode::Free && moving && !PilotLocked)
		{
			// US-T479: let go from where this frame's follow would put the camera, not last frame's: the target has moved since
			// (at real scale a planet's orbit carries the hulls ~8 fighter radii a frame), and the free camera left behind by
			// that step read as an 8-radius pop on the first swim frame (sg.Feel, 'god swim').
			if (!world->TargetValid(FocusTarget)) {}
			else if (Mode == CameraMode::Travel) UpdateTravel(0.0f);
			else
			{
				auto back = Rotation * Vector3::back();
				ScenePosition = OrbitCentre(FocusTarget) + DVec3(back.x, back.y, back.z) * (zoom * world->TargetRadius(FocusTarget));
			}
			ExitFocus();
		}
		// US-T185: the focused thing went (despawned, left the system): let go rather than orbit the origin or a stale slot.
		if (Mode != CameraMode::Free && !PilotLocked && !world->TargetValid(FocusTarget)) ExitFocus();

		switch (Mode)
		{
			case CameraMode::Travel:
				UpdateTravel(dt);
				break;
			case CameraMode::Focus:
				UpdateFocus(dt, input);
				break;
			default:
				UpdateFree(dt, input);
				break;
		}

		// Levelling: the old orientation's difference from the new one eases away.
		if (levelT < 1.0f)
		{
			levelT = Mathf::Min(1.0f, levelT + dt / LevelSeconds());
			Rotation = Quaternion::Slerp(levelOffset, Quaternion::identity(), SmootherStep(levelT)) * Rotation;
		}
	}

	void SpaceCamera::UpdateFree(float dt, const Input::InputState& input)
	{
		if (input.LookHeld)
		{
			yaw += input.LookDelta.x * LookSensitivity();
			pitch = Mathf::Clamp(pitch - LookY(input.LookDelta.y) * LookSensitivity(), -89.0f, 89.0f);
		}
		Rotation = UpFrame * Quaternion::Euler(pitch, yaw, 0);

		if (cs::Math::Abs(input.Scroll) > 0)
			SpeedMultiplier = cs::Math::Max(MinSpeedMultiplier, cs::Math::Min(MaxSpeedMultiplier, SpeedMultiplier * cs::Math::Pow(1.3, input.Scroll)));

		// Re-derive the position from the frame (the frame body may have moved or the scale changed).
		ScenePosition = FramePosition();

		// US-T442: the god camera goes inside bodies and hulls; the gap either side of the nearest surface sets its pace.
		double surface = cs::Math::Max(world->SwimDistance(ScenePosition), 1e-6);
		double speed = surface * 0.9 * SpeedMultiplier * (input.Boost ? 6 : 1) * (input.Slow ? 0.15 : 1);
		CurrentSpeed = speed;

		auto desired = Rotation * Vector3::ClampMagnitude(input.Move, 1.0f) * static_cast<float>(speed);
		float response = 1.0f - Mathf::Exp(-dt * 7.0f);
		velocity = Vector3::Lerp(velocity, desired, response);
		if (input.Move.sqrMagnitude() < 0.01f && velocity.magnitude() < speed * 0.002) velocity = Vector3::zero();

		auto delta = DVec3(velocity.x, velocity.y, velocity.z) * dt;
		if (delta.LengthSquared() > 0) ApplySceneDelta(delta);

		// US-T442: no collision. The god camera swims through planets, stars and hulls (it once was pushed back out onto
		// each body's skin); from inside, the body or hull is drawn see-through with its inside as a
		// wireframe (UnitySpaceApp::UpdateGodInterior).
		UpdateFrame();
		ScenePosition = FramePosition();
	}

	void SpaceCamera::UpdateChaseLag(float dt)
	{
		if (!(FocusTarget == chaseLagTarget))
		{
			ResetChaseLag();
			chaseLagTarget = FocusTarget;
		}
		float feel = Mathf::Clamp(ChaseFeel, 0.0f, 2.0f);
		float limit = ChaseLagMax() * feel;
		if (!(limit > 0))
		{
			ResetChaseLag();
			return;
		}
		if (!(dt > 0)) return;
		auto want = ChaseAcceleration * (-ChaseLagPerG() * feel);
		if (!(want.sqrMagnitude() < 1e12f)) want = Vector3::zero();
		want = Vector3::ClampMagnitude(want, limit);
		// Semi-implicit spring, substepped so omega * step stays under a half whatever the frame time (at most 8 steps;
		// a hitch past a tenth of a second is stepped as one).
		float omega = 2.0f * Mathf::Pi * ChaseLagHz();
		float span = Mathf::Min(dt, 0.1f);
		int steps = Mathf::Clamp(Mathf::CeilToInt(span * omega / 0.5f), 1, 8);
		float h = span / steps;
		float pull = omega * omega * h, drag = 2.0f * ChaseLagDamping() * omega * h;
		for (int i = 0; i < steps; i++)
		{
			chaseLagVelocity += (want - chaseLag) * pull - chaseLagVelocity * drag;
			chaseLag += chaseLagVelocity * h;
		}
		chaseLag = Vector3::ClampMagnitude(chaseLag, limit);
	}

	void SpaceCamera::UpdateFocus(float dt, const Input::InputState& input)
	{
		if (PilotLocked)
		{
			auto shipRotation = world->TargetRotation(FocusTarget);
			auto wantRotation = PilotView.value_or(shipRotation);
			if (!PilotView.has_value() && !PilotLockTarget.IsNone())
			{
				auto toTarget = world->TargetScene(PilotLockTarget) - world->TargetScene(FocusTarget);
				if (toTarget.LengthSquared() > 1e-12)
				{
					auto lookAt = Quaternion::LookRotation(ToVector(toTarget.Normalized()), shipRotation * Vector3::up());
					wantRotation = Quaternion::Slerp(shipRotation, lookAt, PilotLockTrackStrength());
				}
			}
			// Eases toward the hull's facing (and any lock-on blend) rather than welding to it
			// instantly, so sharp turns read as a chase rather than a rigid 1:1 attachment.
			// A view the mouse sets (free aim, a fighter's seat, a gun, a capital's look-around) is the mouse's own: it
			// turns with it at once and the hull catches up after it (US-T129).
			if (PilotView.has_value()) Rotation = *PilotView;
			else Rotation = Quaternion::Slerp(Rotation, wantRotation, 1.0f - Mathf::Exp(-dt * PilotCamResponseHz(world->TargetFamily(FocusTarget))));
			if (cs::Math::Abs(input.Scroll) > 0)
				zoomTarget = cs::Math::Max(MinZoom(FocusTarget), cs::Math::Min(20.0, zoomTarget * cs::Math::Pow(0.82, input.Scroll)));
			zoom = cs::Math::Exp(cs::Math::Log(zoom) + (cs::Math::Log(zoomTarget) - cs::Math::Log(zoom)) * (1 - cs::Math::Exp(-dt * 10)));
			auto chaseBack = Rotation * Vector3::back();
			double chaseR = world->TargetRadius(FocusTarget) * ChaseScale();
			ScenePosition = world->TargetScene(FocusTarget) + DVec3(chaseBack.x, chaseBack.y, chaseBack.z) * (zoom * chaseR);
			if (PilotView.has_value())
			{
				auto lift = Rotation * Vector3::up();
				ScenePosition += DVec3(lift.x, lift.y, lift.z) * (zoom * chaseR * PilotViewLift());
			}
			// At a gun mount the eye is the mount's own, not a chase (U.SPC-T445).
			if (PilotEye)
			{
				ResetChaseLag();
				ScenePosition = PilotEye();
			}
			else
			{
				// US-T841: the eye lags the hull on a spring; Rotation is untouched, so the aim stays on the nose.
				UpdateChaseLag(dt);
				ScenePosition += DVec3(chaseLag.x, chaseLag.y, chaseLag.z) * (zoom * chaseR);
			}
			CurrentSpeed = 0;
			return;
		}
		if (input.LookHeld)
		{
			orbitYaw += input.LookDelta.x * LookSensitivity();
			orbitPitch = Mathf::Clamp(orbitPitch - LookY(input.LookDelta.y) * LookSensitivity(), -88.0f, 88.0f);
		}
		if (cs::Math::Abs(input.Scroll) > 0)
			zoomTarget = cs::Math::Max(MinZoom(FocusTarget), cs::Math::Min(5e7, zoomTarget * cs::Math::Pow(0.82, input.Scroll)));
		zoom = cs::Math::Exp(cs::Math::Log(zoom) + (cs::Math::Log(zoomTarget) - cs::Math::Log(zoom)) * (1 - cs::Math::Exp(-dt * 10)));

		Rotation = UpFrame * Quaternion::Euler(orbitPitch, orbitYaw, 0);
		auto back = Rotation * Vector3::back();
		double r = world->TargetRadius(FocusTarget);
		ScenePosition = OrbitCentre(FocusTarget) + DVec3(back.x, back.y, back.z) * (zoom * r);
		CurrentSpeed = 0;
	}

	void SpaceCamera::UpdateTravel(float dt)
	{
		// US-T472: a frame's step is capped (MaxTravelStep), so a hitch (a 100 ms frame) slows the glide for a frame rather
		// than skipping a tenth of it: sg.Feel read those as 20-30 degree look snaps mid fly-in.
		travelT = Mathf::Min(1.0f, travelT + Mathf::Min(dt, MaxTravelStep) / GlideSeconds());
		float e = SmootherStep(travelT);

		auto target = OrbitCentre(FocusTarget);
		double r = world->TargetRadius(FocusTarget);
		double startDist = cs::Math::Max(travelStartOffset.Length(), 1e-9);
		double endDist = travelEndZoom * r * ChaseScale();
		double dist = cs::Math::Exp(cs::Math::Log(startDist) + (cs::Math::Log(endDist) - cs::Math::Log(startDist)) * e);

		auto startDir = (travelStartOffset / startDist);
		Vector3 dir;
		Quaternion lookAt;
		if (PilotLocked)
		{
			// Settles in behind the hull's own facing from the start (U.SPC-T370), not round whatever side
			// the camera already happened to be on: a chase has no "approach from the side we're on", only
			// the hull's own forward, which the player may already be turning under their own input while
			// this plays out. Read live (not just at the moment the chase began) so a turn mid-transition
			// is followed rather than fought.
			// US-T463: behind the view the seat will hold (the mouse's, at a fighter's string or a capital's look-around), not
			// the bare hull's facing: the chase (UpdateFocus) sits there, so the glide once ended a view's turn off it and
			// popped across on its last frame.
			auto shipRotation = world->TargetRotation(FocusTarget);
			auto viewRotation = PilotView.value_or(shipRotation);
			auto endDirV = viewRotation * Vector3::back();
			dir = Vector3::Slerp(ToVector(startDir), endDirV, e).normalized();
			lookAt = viewRotation;
		}
		else
		{
			dir = Vector3::Slerp(ToVector(startDir), ToVector(travelEndDirection), e).normalized();
			lookAt = travelToPose ? travelEndRotation : Quaternion::LookRotation(-dir, Up());
		}
		ScenePosition = target + DVec3(dir.x, dir.y, dir.z) * dist;
		// US-T463: the chase over a seat's view rides PilotViewLift above it (UpdateFocus); the glide rises into that lift
		// as it lands, where it once jumped the whole lift (a fifth of the chase distance) on the frame the chase took over.
		if (PilotLocked && PilotView.has_value() && !PilotEye)
		{
			auto lift = *PilotView * Vector3::up();
			ScenePosition += DVec3(lift.x, lift.y, lift.z) * (dist * PilotViewLift() * e);
		}
		// Gliding to a gun mount (U.SPC-T445): the glide ends at the mount's eye, not behind the hull.
		if (PilotLocked && PilotEye)
		{
			auto eye = PilotEye();
			ScenePosition = ScenePosition + (eye - ScenePosition) * e;
		}
		// The turn lands ahead of the glide (x1.8), except on a hurried seat switch: its 0.3 s glide turned the view 90-120
		// degrees in under 0.17 s, 15-20 degrees a frame (sg.Feel, 'fighter again'); there the turn spans the whole glide.
		// US-T855: a ghost's drift turns over its whole length too, as the consciousness looks round on its way.
		float turnRate = hurried || ghostSeconds > 0 ? 1.0f : 1.8f;
		Rotation = Quaternion::Slerp(travelStartRotation, lookAt, SmootherStep(Mathf::Min(1.0f, travelT * turnRate)));
		// US-T855: the ghost floats as it drifts: a slow sway across the view and a lean of a few degrees, nothing at either eye.
		if (ghostSeconds > 0 && ghostSway > 0)
		{
			float swell = Mathf::Sin(Mathf::Pi * e) * ghostSway;
			float tau = travelT * ghostSeconds;
			auto across = Rotation * Vector3::right();
			auto rise = Rotation * Vector3::up();
			double amp = dist * 0.04 * swell;
			ScenePosition += DVec3(across.x, across.y, across.z) * (amp * Mathf::Sin(tau * 2.1f))
				+ DVec3(rise.x, rise.y, rise.z) * (amp * 0.7 * Mathf::Sin(tau * 1.37f + 1.0f));
			Rotation = Rotation * Quaternion::AngleAxis(3.0f * swell * Mathf::Sin(tau * 0.9f), Vector3::forward());
		}

		if (travelT >= 1.0f)
		{
			if (travelToPose)
			{
				Rotation = travelEndRotation;
				ExitFocus();
				return;
			}
			Mode = CameraMode::Focus;
			zoom = zoomTarget = travelEndZoom;
		}
	}

	// ---------------------------------------------------------------- reference frames

	DVec3 SpaceCamera::FramePosition()
	{
		auto body = (*world->Catalog())[frame];
		return world->BodyScene(frame) + world->Scale()->LocalOffset(body, trueOffset);
	}

	void SpaceCamera::ApplySceneDelta(DVec3 delta)
	{
		auto body = (*world->Catalog())[frame];
		const auto& scale = world->Scale();
		double d = trueOffset.Length();
		if (d <= 0)
		{
			trueOffset = delta.SwapYZ() * (body->RadiusKm / cs::Math::Max(scale->DisplayRadius(body), 1e-12));
			return;
		}
		double sceneDist = scale->LocalDistance(body, d);
		auto dirScene = trueOffset.SwapYZ() / d;
		double radial = DVec3::Dot(delta, dirScene);
		auto tangential = delta - dirScene * radial;
		double derivative = cs::Math::Max(scale->LocalDistanceDerivative(body, d), 1e-15);
		auto trueDeltaScene = dirScene * (radial / derivative) + tangential * (d / sceneDist);
		trueOffset += trueDeltaScene.SwapYZ();
		// US-T442: no keep-out above the frame body's surface: inside it the scale mapping is linear, so the true offset
		// stays exact down to its centre.
	}

	void SpaceCamera::SetScenePosition(DVec3 scenePosition)
	{
		int f = FrameAt(scenePosition);
		frame = f;
		trueOffset = world->Scale()->InverseLocalOffset((*world->Catalog())[f], scenePosition - world->BodyScene(f));
		ScenePosition = scenePosition;
	}

	int SpaceCamera::FrameAt(DVec3 scenePosition)
	{
		int f = 0;
		while (true)
		{
			int best = -1;
			double bestRatio = 1.0;
			for (int child : world->Catalog()->ChildrenOf(f))
			{
				double soi = world->Scale()->SceneSoi((*world->Catalog())[child]);
				double ratio = (scenePosition - world->BodyScene(child)).Length() / soi;
				if (ratio < bestRatio)
				{
					bestRatio = ratio;
					best = child;
				}
			}
			if (best < 0) break;
			f = best;
		}
		return f;
	}

	void SpaceCamera::UpdateFrame()
	{
		auto pos = FramePosition();
		if (frame != 0)
		{
			auto body = (*world->Catalog())[frame];
			if ((pos - world->BodyScene(frame)).Length() > world->Scale()->SceneSoi(body) * 1.08)
			{
				int parent = body->ParentIndex;
				frame = parent;
				trueOffset = world->Scale()->InverseLocalOffset((*world->Catalog())[parent], pos - world->BodyScene(parent));
				return;
			}
		}
		for (int child : world->Catalog()->ChildrenOf(frame))
		{
			auto c = (*world->Catalog())[child];
			if ((pos - world->BodyScene(child)).Length() < world->Scale()->SceneSoi(c) * 0.92)
			{
				frame = child;
				trueOffset = world->Scale()->InverseLocalOffset(c, pos - world->BodyScene(child));
				return;
			}
		}
	}
}
