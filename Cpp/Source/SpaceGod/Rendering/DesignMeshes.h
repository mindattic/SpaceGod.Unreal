#pragma once
// US-T839: an assembled design (ModularAssembler) merged into one mesh, built off the game thread.

#include "Assembly/ModularAssembler.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityRenderingTypes.h"

#include <functional>
#include <vector>

namespace SpaceGod::Rendering
{
	/// <summary>
	/// The kit parts' meshes as a design draws them, and a whole design merged into one static mesh (US-T839): one draw
	/// per material (USC atlas, Star Sparrow livery, grey box) instead of one per part. The merge is gathered on the game
	/// thread, built into a mesh description on the thread pool and made a static mesh (the fast non-Nanite build) back on
	/// the game thread; every request for the same design shares the one mesh.
	/// </summary>
	class DesignMeshes
	{
	public:
		/// <summary>A part's imported mesh, fitted to the catalogue's box in the model frame: turned from the import's frame
		/// (USC's nose -Z, Star Sparrow's +X; whichever turn makes the mesh's box the catalogue's) and scaled to its size.</summary>
		struct Fitted
		{
			cs::Ref<Mesh> Drawn;
			Quaternion Turn;
			float Scale = 1;
			bool Ok = false;
		};

		/// <summary>The fit of <paramref name="part"/>'s imported mesh (cached per asset; Ok false when it has none).</summary>
		static const Fitted& FitOf(const Core::KitPart& part);
		/// <summary>The shared material an imported part draws with.</summary>
		static cs::Ref<Material> MaterialOf(const Core::KitPart& part);

		/// <summary>FNV-1a over every placement's part, pose, scale and mirroring: equal designs hash alike.</summary>
		static uint64_t Hash(const Core::ModularDesign& design);

		/// <summary>The merged mesh (Unity units, the design frame) and one material per section.</summary>
		using Done = std::function<void(const cs::Ref<Mesh>& merged, const std::vector<cs::Ref<Material>>& materials)>;

		/// <summary>Calls <paramref name="done"/> on the game thread with <paramref name="design"/> merged: at once when it
		/// is built already, else once the build finishes. Never called when the merge has nothing to draw.</summary>
		static void Request(const Core::ModularDesign& design, const Core::KitCatalogue& catalogue, Done done);
	};
}
