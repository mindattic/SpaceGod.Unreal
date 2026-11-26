#pragma once
// US-T850: the hull master material at run time (US-T849 builds the assets: tools/hull_material.py).

#include "UnityCompat/UnityRendering.h"

namespace SpaceGod::Rendering
{
	/// <summary>
	/// US-T850: which hulls draw with M_HullMaster (procedural panel lines, wear, grime, window lights and per-section battle
	/// damage along the keel) and what the ship views feed it. A pack instance's MI_..._Hull is used in place of its _Lit
	/// one (HullLighting::LitVariant asks here first) when it has been imported and <c>sg.HullMaster</c> is on; otherwise
	/// the _Lit instance draws as before. Nothing here touches the simulation.
	/// </summary>
	class HullMaterial
	{
	public:
		/// <summary>sg.HullMaster, read when the first hull is built.</summary>
		static bool Enabled();

		/// <summary>The hull-master variant of a pack instance path (".../MI_USC_HyperFalcon_Grey" -> "..._Hull") when
		/// enabled and imported; otherwise the path as given.</summary>
		static cs::String Variant(const cs::String& path);

		/// <summary>The keel's span and panel size on a hull's block: HullSpan (stern, bow, keel sections, keel axis), the
		/// stern and bow as positions along the keel axis in the mesh's own (native Unreal) units and the axis as 1, 2 or 3
		/// for the mesh's x, y or z, negative when the nose runs down it; and panel cells per hull length from its length in
		/// metres.</summary>
		static void SetSpan(UnityCompat::MaterialPropertyBlock& block, float stern, float bow, int keelAxis, int sections, double lengthM);

		/// <summary>The 8 keel sections' damage (0 the stern, 0..1) on a hull's block.</summary>
		static void SetDamage(UnityCompat::MaterialPropertyBlock& block, const std::vector<float>& damage);
	};
}
