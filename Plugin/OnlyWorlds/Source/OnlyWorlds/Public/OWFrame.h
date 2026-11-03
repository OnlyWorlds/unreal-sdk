#pragma once
// A world's Map laid out in a level and shot from above, for C++: the map as a plane carrying its image, each of its
// Pins with an x and a y a marker on it, a scene capture straight above into a render target, the frame's pixels read
// back and written as a PNG. The frame test (Private/Tests/OWFrameTests.cpp) shoots it in the editor; a packaged game
// shoots it the same way (the host's -OWFrame, Host/Source/OWHost/OWBoot.cpp). The plugin ships no content, so the
// caller gives the two materials: the editor can make one in code, a cooked game needs one cooked into its pak. Needs a
// renderer (FApp::CanEverRender: not under -nullrhi).

#include "CoreMinimal.h"

#include "OWModels.h"

class UMaterialInterface;
class UOWWorld;
class USceneCaptureComponent2D;
class UStaticMesh;
class UTexture2D;
class UWorld;

namespace OWFrame
{
	// The frame's size in pixels.
	inline constexpr int32 FrameW = 1800;
	inline constexpr int32 FrameH = 1200;

	// The engine's meshes the frame is made of: a plane 100 units square centred on its origin (its texture's U along
	// its local X, V along its local Y), and a sphere 100 units across. A cooked game has them only when its cook takes
	// /Engine/BasicShapes (nothing else references them).
	inline constexpr const TCHAR* PlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	inline constexpr const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	// The world's root Map (one with no parent_map; the last in read order when there are several) and its Pins that
	// have both an x and a y, in read order. False, Map left empty and Pins none, when the world holds no root Map.
	ONLYWORLDS_API bool RootMap(const UOWWorld* World, FOWMap& Map, TArray<FOWPin>& Pins);

	// The map's image as a transient texture: the first .png, in path order, under media/<map id>/base-image/ among
	// the world's files as read (a folder's media/, or an .owworld file's), File its path inside the world. nullptr,
	// File "", when there is none or it does not decode.
	ONLYWORLDS_API UTexture2D* MapImage(const UOWWorld* World, const FString& MapId, FString& File);

	// A new game world of its own, with an engine world context, to lay a frame out in; and its end, context and all.
	ONLYWORLDS_API UWorld* NewFrameWorld(FName Name);
	ONLYWORLDS_API void EndFrameWorld(UWorld* World);

	// The map laid out at the world's origin, one unit a map pixel: map x to the right of the frame (world +Y), map y
	// up from its bottom (world +X). The plane, Width by Height, carries PlaneMaterial; each pin is a sphere of radius
	// Width / 80 carrying MarkerMaterial, its centre on the plane at the pin's point, so it projects to that point from
	// any height. Every mesh is movable.
	ONLYWORLDS_API void LayOut(UWorld* World, const FOWMap& Map, const TArray<FOWPin>& Pins, UStaticMesh* Plane,
		UStaticMesh* Sphere, UMaterialInterface* PlaneMaterial, UMaterialInterface* MarkerMaterial);

	// A render target FrameW by FrameH (RGBA8, cleared black) and a scene capture straight above the map's centre,
	// half the map's width up, so its 90-degree view is exactly as wide as the map at the plane: the final colour, with
	// a fixed exposure and no vignette or bloom, so an unlit colour comes out as itself (bar the tonemapper's curve).
	// It captures only when asked (Shoot).
	ONLYWORLDS_API USceneCaptureComponent2D* PlaceCamera(UWorld* World, const FOWMap& Map);

	// How many static meshes in the world have no scene proxy yet. 0 in the editor once its shaders are compiled; in a
	// game, a mesh's proxy waits for its pipeline states to precache (r.PSOPrecache.ProxyCreationStrategy), and the
	// precache ends between frames, so a game asks again on each tick until this is 0.
	ONLYWORLDS_API int32 MeshesWaiting(UWorld* World);

	// The frame: every pending render update sent, two shots (the second once every resource the first touched is in
	// place), the pixels read back with their alpha set to 255. False when the read gives other than FrameW by FrameH
	// pixels.
	ONLYWORLDS_API bool Shoot(USceneCaptureComponent2D* Capture, TArray<FColor>& Pixels);

	// The frame written as a PNG at Path (an absolute path; its folder made). False when the write fails.
	ONLYWORLDS_API bool SavePng(const TArray<FColor>& Pixels, const FString& Path);
}
