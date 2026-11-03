#pragma once
// Writes the Blueprint graphs the packaged game runs with -OWGraph (OWBoot.cpp), as saved assets in the host's
// Content/OW/, built the way the plugin's graph tests build theirs (OWGraphTests.cpp) but in real packages:
//   /Game/OW/BP_OWBootGraph       ReadWorld(Path, Id) -> ElementCount, ElementName, Error, through UOWLibrary's
//                                 OpenWorldFolder and the opened world's GetElementCount and GetElementName
//   /Game/OW/BP_OWBootGraphWrong  the same with GetWorldName wired into ElementName: it compiles, cooks and runs, and its
//                                 answer is not the C++ reading's (the red arm, exit 3)
// Run headless: UnrealEditor-Cmd.exe Host/OWHost.uproject -run=OWMakeGraph. Exits 0 when both are compiled and saved.

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OWMakeGraphCommandlet.generated.h"

UCLASS()
class UOWMakeGraphCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
