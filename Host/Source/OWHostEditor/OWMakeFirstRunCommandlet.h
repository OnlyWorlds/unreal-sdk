#pragma once
// Writes the README's Blueprint first run as a saved asset, /Game/OW/BP_OWFirstRun: an Actor Blueprint whose event
// graph is the README's five nodes (OWFirstRunGraph.h), Open World Folder's Path `Worlds/hyperion`, compiled by the
// Kismet compiler, then saved into the host's Content/OW/ (generated, never committed). Its nodes are logged as the
// editor titles them, with their pins.
//   -Play      after the save, the class loaded back by its path and run: placed in a new game world, BeginPlay
//              dispatched; Print String's lines are in the log under LogBlueprintUserMessages
//   -PlayOnly  the run alone, of the asset already on disk (nothing built or saved)
// Run headless: UnrealEditor-Cmd.exe Host/OWHost.uproject -run=OWMakeFirstRun [-Play | -PlayOnly]. Exits 0 when the
// graph is compiled and saved and, with a run, when its BeginPlay ran; what it printed is the log's to read.

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OWMakeFirstRunCommandlet.generated.h"

UCLASS()
class UOWMakeFirstRunCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
