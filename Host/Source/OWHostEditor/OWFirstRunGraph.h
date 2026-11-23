#pragma once
// The README's Blueprint first run ("On Blueprints, from a world on disk"), built in code node for node: an Actor
// Blueprint whose event graph holds Event BeginPlay -> Open World Folder (Path) -> For Each Loop over the opened
// world's Get All Characters, a Break OWCharacter on each element, and a Print String of its Name from Loop Body. Every
// node is placed as the editor places one and every link made through the K2 schema, as a person drags one; nothing is
// forced. Used by the commandlet that saves it as /Game/OW/BP_OWFirstRun (OWMakeFirstRunCommandlet.cpp) and by the
// test that keeps it true (OWFirstRunTests.cpp, OnlyWorlds.FirstRun.Blueprint).

#include "CoreMinimal.h"

class UBlueprint;
class UClass;

namespace OWFirstRunGraph
{
	struct FMade
	{
		UBlueprint* Blueprint = nullptr;
		// Each node of the event graph as the editor titles it, with its pins (display name: type), one line each.
		TArray<FString> Nodes;
		int32 Links = 0;
		int32 Refused = 0;
		int32 CompileErrors = 0;
		int32 CompileWarnings = 0;
		// Empty when the graph was built and compiled up to date with no link refused; else why not.
		FString Why;
	};

	// The graph built in Outer as an Actor Blueprint named Name, Open World Folder's Path set to Path, and compiled.
	FMade Make(UObject* Outer, FName Name, const FString& Path);

	struct FPlayed
	{
		bool bBegunPlay = false;
		// What Print String wrote, as the log holds it under LogBlueprintUserMessages ("[<actor>] <text>").
		TArray<FString> Printed;
		// The plugin's OpenWorldFolder lines under LogOnlyWorlds (the folder's Error, when it opened nothing).
		TArray<FString> Opened;
	};

	// An instance of ActorClass placed in a new game world, then the world's begin-play dispatched as a level's start
	// dispatches it (AWorldSettings::NotifyBeginPlay, which every actor in the level gets its BeginPlay from), the log
	// heard meanwhile; the world is torn down after.
	FPlayed Play(UClass* ActorClass);

	// "Mr. Steinway" from "[BP_OWFirstRun_C_0] Mr. Steinway": a printed line without the source prefix the engine adds
	// (BaseEngine.ini, Kismet bLogPrintStringSource).
	FString Text(const FString& Printed);
}
