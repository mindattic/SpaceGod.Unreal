#pragma once

// The one conversion between Unity.SpaceGod's space and Unreal's, used everywhere a value crosses into the engine
// (camera pose, view placement, lights, the mouse).
//
// Unity (and the Runtime's scene space, and Core's DVec3::SwapYZ() of the ecliptic) is left-handed, Y up, Z forward,
// X right, in scene units. Unreal is left-handed too, Z up, in centimetres. The tracks share the SpaceGodRender
// plugin's convention (Shared/Plugins/SpaceGodRender/Shaders/SpaceGodCommon.ush, and the C# track's UnrealSpace):
// Unity (x, y, z) == Unreal (x, -z, y), a proper rotation (a quarter turn about X), so rotations carry over with the
// same rotation of the quaternion's vector part and nothing is mirrored; the shared materials rely on it.
//
// Positions stay camera-relative as the original keeps them (floating origin: the camera sits at the origin and
// everything is drawn at scene - camera, in Core doubles until this point), and become Unreal's Large World
// Coordinates doubles (FVector is double) here, so Neptune 4.5 million units out stays exact and a hull beside the
// camera clean. Rebasing on top of LWC costs nothing and keeps the GPU's float positions small.

#include "CoreMinimal.h"
#include "UnityCompat/UnityMath.h"

namespace SpaceGod::UnrealSpace
{
	using UnityCompat::Vector2;
	using UnityCompat::Vector3;
	using UnityCompat::Quaternion;
	using UnityCompat::Color;

	namespace Detail
	{
		inline double CmPerUnit = 100.0;
		inline uint64 ScaleRevision = 0;
	}

	/// Unreal units (cm) per Unity scene unit. US-T716: it follows the seat (SetRenderScale, each frame the game runs) so
	/// that round the camera 1 UU is 1 true cm of hardware; 100 until a game sets it. Every camera-relative position and
	/// size is scaled by it about the eye, so changing it changes nothing on screen (docs/TrueScale.md section 5.2).
	inline double CmPerUnit() { return Detail::CmPerUnit; }

	/// US-T716: bumped each time CmPerUnit changes, for what keeps values converted with it (material positions).
	inline uint64 ScaleRevision() { return Detail::ScaleRevision; }

	/// US-T716: the render scale for a view drawing a true km of hardware as shipUnitsPerKm scene units
	/// (ScaleModel::ShipLength(1.0)): sg.Origin.CmPerTrueKm / shipUnitsPerKm (100 with that setting 0).
	void SetRenderScale(double shipUnitsPerKm);

	/// US-T716: the near plane (true cm) while a seat is held (sg.Origin.CockpitNearCm).
	double CockpitNearCm();

	/// US-T716: past this distance from the camera (cm) a drawn position is log-compressed (sg.Origin.FarFieldCm).
	double FarFieldCm();

	/// US-T716: a camera-relative position (cm) as drawn: unchanged within FarFieldCm, else log-compressed along its own
	/// direction, d' = F (1 + ln(d / F)), with shrink = d' / d, the factor its size takes so its angular size is kept.
	FVector FarField(const FVector& cm, double& shrink);

	/// A Unity-axes point or direction (scene units) as Unreal's (cm).
	inline FVector ToUnreal(Vector3 v) { return FVector(v.x * CmPerUnit(), -v.z * CmPerUnit(), v.y * CmPerUnit()); }

	/// A double-precision Unity-axes scene point (camera-relative) as Unreal's (cm, LWC doubles).
	inline FVector ToUnreal(const Core::DVec3& v) { return FVector(v.X * CmPerUnit(), -v.Z * CmPerUnit(), v.Y * CmPerUnit()); }

	/// A Unity-axes direction as Unreal's, unscaled.
	inline FVector DirectionToUnreal(Vector3 v) { return FVector(v.x, -v.z, v.y); }

	/// Back from Unreal (cm) to Unity axes (scene units).
	inline Core::DVec3 FromUnreal(const FVector& v) { return Core::DVec3(v.X / CmPerUnit(), v.Z / CmPerUnit(), -v.Y / CmPerUnit()); }

	/// A Unity rotation as Unreal's: the vector part turned as the axes are (w unchanged).
	inline FQuat ToUnreal(Quaternion q) { return FQuat(q.x, -q.z, q.y, q.w); }

	inline Quaternion FromUnreal(const FQuat& q)
	{
		return Quaternion(static_cast<float>(q.X), static_cast<float>(q.Z), static_cast<float>(-q.Y), static_cast<float>(q.W));
	}

	/// A Unity scale (x, y, z along its axes) as Unreal's (along X, Y, Z).
	inline FVector ScaleToUnreal(Vector3 s) { return FVector(s.x, s.z, s.y); }

	/// Unity's camera field of view is vertical, Unreal's horizontal: the horizontal one for an aspect (w/h).
	inline float HorizontalFov(float verticalDeg, float aspect)
	{
		return static_cast<float>(2.0 * FMath::Atan(FMath::Tan(verticalDeg * 0.5 * UE_DOUBLE_PI / 180.0) * aspect) * 180.0 / UE_DOUBLE_PI);
	}

	/// A clip distance in scene units as Unreal's (cm).
	inline float ClipToUnreal(float sceneUnits) { return static_cast<float>(sceneUnits * CmPerUnit()); }

	/// Unreal's mouse position (pixels, origin top-left) as Unity's (origin bottom-left).
	inline Vector2 MouseToUnity(float x, float y, int screenHeight) { return Vector2(x, screenHeight - y); }

	/// Unity's GUI space (origin top-left) is Unreal's canvas space: unchanged.
	inline FVector2D GuiToUnreal(Vector2 p) { return FVector2D(p.x, p.y); }

	inline FLinearColor ToUnreal(Color c) { return FLinearColor(c.r, c.g, c.b, c.a); }

	inline FString ToFString(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	inline std::string FromFString(const FString& s) { return std::string(TCHAR_TO_UTF8(*s)); }
}
