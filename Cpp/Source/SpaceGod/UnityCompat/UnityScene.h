#pragma once

// The bridge from the Unity scene (UnityRendering.h's GameObjects, renderers, meshes and materials) to Unreal (US-T30).
// Once a frame, after every view has placed what it draws, Sync() gives each live renderer an Unreal
// UDynamicMeshComponent on the host actor (full-precision UVs, which the line and asteroid layouts need), uploads a mesh
// again only when it changed, keeps one UMaterialInstanceDynamic per material (and one per renderer with a property
// block), and sets each component's transform from its Unity world matrix through UnrealSpace.h. Everything is drawn
// camera-relative as the original does: the Unreal camera sits at the origin (the floating origin).
//
// The host (US-T29's app actor) calls Attach once, BeginFrame at the start of its tick and Sync at its end.

#include "CoreMinimal.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityObjects.h"
#include "UnityCompat/UnityRuntime.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

namespace SpaceGod::UnityCompat
{
	class UnityScene
	{
	public:
		/// What the last Sync pushed (the smoke logs it, as the C# track's UnityBridge.Last): the scene's renderers, the
		/// Unreal primitives backing them, those drawn this frame and their triangles, meshes uploaded this frame,
		/// material instances made so far.
		struct Stats
		{
			int Renderers = 0, Primitives = 0, Visible = 0, MeshUploads = 0, MaterialInstances = 0;
			int64_t Triangles = 0;
			/// US-T53 (not in ToString, which the smoke compares with the C# track's): renderers left out by the screen
			/// camera's culling mask, the layers (a bit each) of the renderers drawn (the backdrop aside), whether its
			/// SolidColor backdrop was drawn and whether a Light lit a material (US-T57).
			int Masked = 0, VisibleLayers = 0;
			bool Backdrop = false, KeyLit = false;
			cs::String ToString() const;
		};
		static const Stats& Last();

		/// The camera that draws the screen, as Unity picks it (the C# track's UnityBridge.ScreenCamera, US-T53): of the
		/// active, enabled cameras, the one with the highest depth (the model viewer's, depth 1000, over the game's;
		/// Camera.main on a tie); Camera.main if none is enabled.
		static Camera* ScreenCamera();

		/// A light on a GameObject (RenderBackend::AddKeyLight), kept while the transform it sits under lives.
		static void AddLight(const cs::Ref<Light>& light);

		/// The enabled directional light under `camera` whose culling mask overlaps the camera's (the model viewer's key
		/// light), or null: the C# track's UnityBridge.KeyLightOf.
		static Light* KeyLightOf(Camera* camera);

		/// The actor whose components draw the scene (and whose world it is in).
		static void Attach(AActor* host);
		/// Lets go of every Unreal component and material instance (the game scene is going).
		static void Detach();
		static AActor* Host();
		static UWorld* World();

		/// Start of a frame: finishes background loads (Resources::Pump) and steps the particle systems.
		static void BeginFrame(float dt);

		/// End of a frame: every renderer to its Unreal component, the shader globals to MPC_SpaceGod.
		static void Sync();

		/// Sync without ending the frame: brings Unreal up to date now (a scene capture about to render, ShipThumbnails).
		static void SyncNow();

		/// The number of the frame being built (Renderer::OnlyFrame), counted by Sync.
		static int64_t CurrentFrame();

		/// The Unreal components drawing the renderers under root (a scene capture's show-only list).
		static TArray<UPrimitiveComponent*> ComponentsUnder(const cs::Ref<Transform>& root);

		/// The Unreal component drawing one renderer (null before its first Sync, or while it draws nothing).
		static UPrimitiveComponent* ComponentOf(const Renderer& renderer);

		/// US-T172: the components casting shadows: a renderer whose shadowCastingMode is not Off drawn with a lit (not
		/// Unlit) material, the prebuilt hulls' PBR ones. Every self-lit SpaceGodRender material is Unlit, so nothing else
		/// casts. Entries whose component is gone are left for the reader to skip.
		static const TArray<TWeakObjectPtr<UPrimitiveComponent>>& ShadowCasters();
	};

	/// <summary>
	/// US-T721: many copies of one mesh in one material, drawn by one Unreal UInstancedStaticMeshComponent on the host
	/// actor instead of a GameObject each: the far forms (impostors, icons) of crowds that would otherwise cost a draw
	/// call apiece. Each frame <see cref="Clear"/>, an <see cref="Add"/> per copy in <c>parent</c>'s frame, then
	/// <see cref="Flush"/>, which writes every transform in one batch update. A plain ISM rather than a HISM: every
	/// instance moves every frame, which would rebuild a HISM's tree each time. The instance buffer only grows (copies
	/// past the frame's count are collapsed to nothing), so a steady crowd allocates nothing per frame. Drawn while
	/// <c>parent</c> is active and its layer is in the screen camera's culling mask, as a renderer would be.
	/// </summary>
	class InstancedBatch final
	{
	public:
		InstancedBatch(const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& material);
		~InstancedBatch();
		InstancedBatch(const InstancedBatch&) = delete;
		InstancedBatch& operator=(const InstancedBatch&) = delete;

		/// Starts a frame: no copies.
		void Clear() { count = 0; }

		/// A copy at <paramref name="position"/>, turned by <paramref name="rotation"/> and scaled by <paramref name="scale"/>
		/// (uniformly), all in the parent's frame (a child's localPosition, localRotation and localScale).
		void Add(Vector3 position, Quaternion rotation, float scale) { Add(position, rotation, Vector3(scale, scale, scale)); }

		/// US-T744: a copy scaled per axis (its own x, y and z before the turn), for a glow stretched into a streak.
		void Add(Vector3 position, Quaternion rotation, Vector3 scale);

		/// Writes the frame's copies to Unreal in one batch update.
		void Flush();

		/// The copies added since the last Clear.
		int Count() const { return count; }

		/// US-T851: whether Lumen treats the copies' emissive as a light source, so they light what is near them (Unreal's
		/// bEmissiveLightSource). Taken at the next Flush.
		bool EmissiveLightSource = false;

	private:
		struct Item
		{
			Vector3 Position;
			Quaternion Rotation;
			Vector3 Scale;
		};
		struct Engine;

		std::weak_ptr<Transform> parent;
		cs::Ref<Mesh> mesh;
		cs::Ref<Material> material;
		std::vector<Item> items;
		int count = 0;
		std::unique_ptr<Engine> engine;
	};
}
