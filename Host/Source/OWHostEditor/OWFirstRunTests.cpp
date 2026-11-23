// The README's Blueprint first run, kept true: its graph built node for node (OWFirstRunGraph.h), placed in a game
// world, its BeginPlay dispatched, and what Print String wrote read from the log. On the example world this
// repository carries (codegen/schema-dist/example/hyperion, through a path relative to the project's folder, as the
// README's is), the three Characters' names in the order read; on a folder that is not there, nothing printed and the
// plugin's line saying why.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWFirstRunGraph.h"

namespace
{
	// From the host's folder (Host/, the project's) to the example world, two folders over: relative, so the plugin
	// takes it from the project's folder, as it takes the README's Worlds/hyperion.
	const TCHAR* const OWExampleWorld = TEXT("../codegen/schema-dist/example/hyperion");
	const TCHAR* const OWAbsentWorld = TEXT("Worlds/no-world-here");

	// The graph built in the transient package and played; false, with the test's error, when it does not compile.
	bool BuildAndPlay(FAutomationTestBase& Test, const FString& Path, OWFirstRunGraph::FPlayed& Played)
	{
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_OWFirstRun"));
		const OWFirstRunGraph::FMade Made = OWFirstRunGraph::Make(GetTransientPackage(), Name, Path);
		TStrongObjectPtr<UBlueprint> Held(Made.Blueprint);
		if (!Made.Why.IsEmpty() || !Made.Blueprint)
		{
			Test.AddError(FString::Printf(TEXT("the README's graph (Path %s) was not built: %s"), *Path, *Made.Why));
			for (const FString& Line : Made.Nodes) Test.AddInfo(Line);
			return false;
		}
		Test.TestEqual(TEXT("links made, every one through the K2 schema"), Made.Links, 7);
		Played = OWFirstRunGraph::Play(Made.Blueprint->GeneratedClass);
		Test.TestTrue(TEXT("the placed actor's BeginPlay ran"), Played.bBegunPlay);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFirstRunBlueprintTest, "OnlyWorlds.FirstRun.Blueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOWFirstRunBlueprintTest::RunTest(const FString& Parameters)
{
	OWFirstRunGraph::FPlayed Green;
	if (BuildAndPlay(*this, OWExampleWorld, Green))
	{
		TArray<FString> Names;
		for (const FString& Line : Green.Printed) Names.Add(OWFirstRunGraph::Text(Line));
		const TArray<FString> Wanted = { TEXT("Mr. Steinway"), TEXT("Rachmaninov"), TEXT("The Consul") };
		TestEqual(TEXT("Print String's lines on the example world"), FString::Join(Names, TEXT(" | ")), FString::Join(Wanted, TEXT(" | ")));
		TestEqual(TEXT("no OpenWorldFolder refusal on the example world"), Green.Opened.Num(), 0);
	}

	OWFirstRunGraph::FPlayed Red;
	if (BuildAndPlay(*this, OWAbsentWorld, Red))
	{
		TestEqual(TEXT("nothing printed when the folder is not there"), Red.Printed.Num(), 0);
		const FString Why = FString::Printf(TEXT("OpenWorldFolder: no world.json in %s"), OWAbsentWorld);
		TestTrue(FString::Printf(TEXT("the plugin's log says why (\"%s\")"), *Why),
			Red.Opened.Num() == 1 && Red.Opened[0].Contains(Why));
	}
	return true;
}

#endif
