#pragma once
// A packaged game's proof that the plugin works outside the editor: started with -OWWorld=<a world folder or an
// .owworld file>, the game opens the world through the plugin's Blueprint libraries, writes what it read to its log
// (lines "OWBoot: ..."), and quits by itself: exit 0 when every reading held, 2 when the world is refused or missing,
// 3 when a reading did not hold. Given -OWGraph too (bare: the host's own /Game/OW/BP_OWBootGraph; =<a soft class
// path>: that class), a Blueprint graph cooked into the pak reads the same world folder, and its answers must be the C++
// reading's (3 when they are not); 4 when that class, or its ReadWorld function, is not there. Given -OWFrame=<a .png
// path> too, the world's root Map is drawn after the readings and its frame written there; 5, and no file written,
// when it cannot be drawn (no PNG path, no renderer under -nullrhi, no root Map with a size). Without -OWWorld the
// subsystem is never made, so the editor and its tests never see it.

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "OWBoot.generated.h"

UCLASS()
class UOWBootSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// Runs once, on the engine loop's first tick: the game is up when the world is opened.
	bool Run(float DeltaTime);

	FTSTicker::FDelegateHandle Tick;
};
