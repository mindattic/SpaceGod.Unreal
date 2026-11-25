#pragma once
// US-T729: space dust round the camera, the scale cue a seat needs: motes fixed in space at true spacing, so they stream
// past with the hull's real speed and a 3 km hull reads against them as enormous. View only, per peer.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::UnityCompat
{
	class InstancedBatch;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Motes on a lattice of cells fixed in the scene (true metres, through the scale model), a few per cell at spots
	/// hashed from the cell, drawn in the cube of cells round the camera as one instanced draw of camera-facing glows a few
	/// pixels wide that shrink away toward the field's edge. Nothing is pooled per mote or integrated: each frame is one
	/// pass over a fixed number of cells (at most 686 motes), no allocations past the first. Drawn only while a seat is
	/// held (UnitySpaceApp::InhabitedSizeKm), at Settings > Graphics > Space dust's density, and hidden for a frame the
	/// camera jumps (cruise, FTL, a time warp).
	/// </summary>
	class DustField final
	{
	public:
		DustField(UnitySpaceApp* app, const cs::Ref<Transform>& root);

		/// <summary>Places the frame's motes round the camera and draws them.</summary>
		void Update();

		/// <summary>The motes drawn this frame.</summary>
		int Count() const;

	private:
		UnitySpaceApp* app;
		cs::Ref<Transform> root;
		cs::Ref<UnityCompat::InstancedBatch> motes;
		Core::DVec3 lastEye;
		bool hasEye = false, shown = false;
	};
}
