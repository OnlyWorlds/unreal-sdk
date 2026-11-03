#pragma once
// A packaged game's proof that the plugin works outside the editor: started with -OWWorld=<a world folder or an
// .owworld file>, the game opens the world through the plugin's Blueprint libraries, writes what it read to its log
// (lines "OWBoot: ..."), and quits by itself: exit 0 when every reading held, 2 when the world is refused or missing,
// 3 when a reading did not hold. Given -OWGraph too (bare: the host's own /Game/OW/BP_OWBootGraph; =<a soft class
// path>: that class), a Blueprint graph cooked into the pak reads the same world folder, and its answers must be the C++
// reading's (3 when they are not); 4 when that class, or its ReadWorld function, is not there. Given -OWFrame=<a .png
// path> too, the world's root Map is drawn after the readings, as the plugin's frame test draws it (OWFrame.h), with
// the material cooked from /Game/OW/M_OWFrame, and the frame written there; 3 when its markers are not where their pins
// put them, 5, and no file written, when it cannot be drawn (no PNG path, no renderer under -nullrhi, no root Map with
// a size, the material or the engine's meshes not in the pak, pixels that cannot be read, a write that fails).
// Without -OWWorld the subsystem is never made, so the editor and its tests never see it.

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "OWModels.h"

#include "OWBoot.generated.h"

class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTexture2D;
class UWorld;

UCLASS()
class UOWBootSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// Runs on the engine loop's first tick (the game is up when the world is opened), and with -OWFrame on each tick
	// after while the frame waits for its meshes (Frame).
	bool Run(float DeltaTime);

	// With -OWFrame, once the scene is laid out: on each tick, until every mesh in the frame's world has its scene proxy
	// (a cooked game makes one only once the mesh's pipeline states have precached) or the wait runs out; then the frame
	// shot, written and checked, and the game quit. True while it waits.
	bool Frame();

	FTSTicker::FDelegateHandle Tick;

	// The frame's scene and what it was made from, held across the ticks it waits.
	UPROPERTY()
	TObjectPtr<UWorld> FrameWorld;
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> FrameCapture;
	UPROPERTY()
	TObjectPtr<UTexture2D> FrameImage;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FrameMaterials;
	FOWMap FrameMap;
	TArray<FOWPin> FramePins;
	FString FramePath;
	FString FrameImageFile;
	// Whether every reading before the frame held (exit 3 when not, once the frame is written).
	bool bReadingsHeld = true;
	double FrameSince = 0.0;
	int32 FrameTicks = 0;
};
