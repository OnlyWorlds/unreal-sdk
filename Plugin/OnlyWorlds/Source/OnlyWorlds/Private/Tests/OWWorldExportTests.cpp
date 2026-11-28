// The one-file world export onlyworlds.com hands an owner, through the Blueprint door: UOWLibrary::OpenWorldExport on
// keel's own fixture (schema-dist's example/world-export.json, vendored), its elements read as the structs a graph
// reads (UOWElements), the refusals by their reason, the world written as a folder and opened again, and the node a
// graph places. The core's own tests (tests/test_world_export.cpp) hold every reader rule.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWLibrary.h"
#include "OWModels.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* const WorldId = TEXT("01999999-0000-7000-8000-000000000420");
	const TCHAR* const Keep = TEXT("01999999-0000-7000-8000-000000000001");
	const TCHAR* const Mill = TEXT("01999999-0000-7000-8000-000000000002");
	const TCHAR* const Pip = TEXT("01999999-0000-7000-8000-000000000003");
	const TCHAR* const Mara = TEXT("01999999-0000-7000-8000-000000000004");
	const TCHAR* const Brave = TEXT("01999999-0000-7000-8000-000000000005");

	FString FixturePath() { return OWTestPaths::InRepo(TEXT("codegen/schema-dist/example/world-export.json")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldExportFixtureTest, "OnlyWorlds.WorldExport.Fixture", OWTestFlags)

bool FOWWorldExportFixtureTest::RunTest(const FString& Parameters)
{
	FOWExportVerdict Verdict;
	UOWWorld* World = UOWLibrary::OpenWorldExport(FixturePath(), Verdict);
	if (!TestNotNull(TEXT("the download opens: ") + Verdict.Message, World)) return false;

	TestTrue(TEXT("accepted"), Verdict.bAccepted);
	TestEqual(TEXT("no reason"), Verdict.Reason, FString());
	TestEqual(TEXT("the verdict's world id"), Verdict.WorldId, FString(WorldId));
	TestEqual(TEXT("the standard's version, as dist.149 sends it"), Verdict.SchemaVersion, FString(TEXT("00.34.01")));
	TestEqual(TEXT("when it was downloaded"), Verdict.ExportedAt, FString(TEXT("2026-10-19T12:30:00+00:00")));
	TestEqual(TEXT("the world's id"), World->GetId(), FString(WorldId));
	TestEqual(TEXT("the world's name"), World->GetWorldName(), FString(TEXT("Fixture World")));
	TestEqual(TEXT("no folder"), World->GetRoot(), FString());
	TestEqual(TEXT("five elements"), World->GetElementCount(), 5);
	TestEqual(TEXT("nothing skipped"), World->GetErrors().Num(), 0);
	TestEqual(TEXT("two characters"), World->GetElementIds(TEXT("character")).Num(), 2);
	TestEqual(TEXT("two locations"), World->GetElementIds(TEXT("location")).Num(), 2);
	TestEqual(TEXT("one trait"), World->GetElementIds(TEXT("trait")).Num(), 1);

	// As a graph reads a world folder: a single link, a multi-link, and a link between two of one type.
	FOWCharacter PipC, MaraC;
	FOWLocation MillL;
	if (TestTrue(TEXT("Pip as a struct"), UOWElements::FindCharacter(World, Pip, PipC)))
	{
		TestEqual(TEXT("Pip is at the Mill"), PipC.Location, FString(Mill));
		TestTrue(TEXT("Pip's traits are [Brave]"), PipC.Traits == TArray<FString>{ FString(Brave) });
		TestEqual(TEXT("Pip's name"), PipC.Name, FString(TEXT("Pip")));
	}
	if (TestTrue(TEXT("the Mill as a struct"), UOWElements::FindLocation(World, Mill, MillL)))
	{
		TestEqual(TEXT("the Mill's parent is the Keep"), MillL.ParentLocation, FString(Keep));
	}
	if (TestTrue(TEXT("Mara as a struct"), UOWElements::FindCharacter(World, Mara, MaraC)))
	{
		TestEqual(TEXT("Mara is at the Keep"), MaraC.Location, FString(Keep));
	}
	TestEqual(TEXT("every Character"), UOWElements::GetAllCharacters(World).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldExportRefusedTest, "OnlyWorlds.WorldExport.Refused", OWTestFlags)

bool FOWWorldExportRefusedTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FString Path;
		const TCHAR* Reason;
	};
	const FString Scratch = OWTestPaths::Scratch(TEXT("WorldExport"));
	IFileManager::Get().DeleteDirectory(*Scratch, false, true);
	const FString Folder = Scratch / TEXT("another-format.json");
	FFileHelper::SaveStringToFile(FString(TEXT("{\"format\":\"onlyworlds-world\",\"world\":{\"id\":\"w1\"}}")), *Folder,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const TArray<FCase> Cases = {
		// The older shape, the World at the root beside capitalised type keys: not this format (keel's spec, 8c).
		{ OWTestPaths::Fixtures() / TEXT("fail-OW-1-json-export.owworld"), TEXT("not_an_export") },
		// An .owworld is a zip: OpenWorldFile's, not this.
		{ OWTestPaths::Fixtures() / TEXT("pass-OW-10-api.owworld"), TEXT("not_json") },
		{ Folder, TEXT("not_an_export") },
		{ Scratch / TEXT("no-such-export.json"), TEXT("no_file") },
	};
	for (const FCase& C : Cases)
	{
		FOWExportVerdict Verdict;
		UOWWorld* World = UOWLibrary::OpenWorldExport(C.Path, Verdict);
		TestNull(TEXT("refused: ") + C.Path, World);
		TestFalse(TEXT("not accepted: ") + C.Path, Verdict.bAccepted);
		TestEqual(TEXT("the reason (") + Verdict.Message + TEXT(")"), Verdict.Reason, FString(C.Reason));
	}
	IFileManager::Get().DeleteDirectory(*Scratch, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldExportWriteFolderTest, "OnlyWorlds.WorldExport.WriteFolder", OWTestFlags)

bool FOWWorldExportWriteFolderTest::RunTest(const FString& Parameters)
{
	FOWExportVerdict Verdict;
	UOWWorld* World = UOWLibrary::OpenWorldExport(FixturePath(), Verdict);
	if (!TestNotNull(TEXT("the download opens: ") + Verdict.Message, World)) return false;

	const FString Root = OWTestPaths::Scratch(TEXT("WorldExportFolder"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	FString Error;
	TestTrue(TEXT("written as a world folder: ") + Error, World->WriteFolder(Root, false, Error));

	FString OpenError;
	UOWWorld* Back = UOWLibrary::OpenWorldFolder(Root, OpenError);
	if (TestNotNull(TEXT("the folder opens: ") + OpenError, Back))
	{
		TestEqual(TEXT("the world's id"), Back->GetId(), FString(WorldId));
		TestEqual(TEXT("five elements"), Back->GetElementCount(), 5);
		TestEqual(TEXT("nothing the folder reader could not read"), Back->GetErrors().Num(), 0);
		TestTrue(TEXT("each element at section 3.2's path"),
			FPaths::FileExists(Root / TEXT("elements/character/pip--00000003.json")));
		for (const FString& Id : World->GetElementIds(FString()))
		{
			TestEqual(Id + TEXT(": its body read back"), Back->GetElementJson(Id), World->GetElementJson(Id));
		}
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldExportNodeTest, "OnlyWorlds.WorldExport.Node",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOWWorldExportNodeTest::RunTest(const FString& Parameters)
{
	const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(TEXT("OWWorldExportProbe")));
	TStrongObjectPtr<UBlueprint> BP(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Unique,
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None));
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP.Get()) : nullptr;
	if (!TestNotNull(TEXT("a Blueprint with an event graph"), Graph)) return false;

	UFunction* Open = UOWLibrary::StaticClass()->FindFunctionByName(TEXT("OpenWorldExport"));
	if (!TestNotNull(TEXT("UFUNCTION OpenWorldExport"), Open)) return false;
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
	UK2Node_CallFunction* Node = Creator.CreateNode(false);
	Node->SetFromFunction(Open);
	Creator.Finalize();

	// A call's full title is the friendly name (K2Node_CallFunction asks for it on FullTitle): the README names it so.
	TestEqual(TEXT("its title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), FString(TEXT("Open World Export")));
	TestNotNull(TEXT("an exec pin: a call, not pure"), Node->FindPin(UEdGraphSchema_K2::PN_Execute));
	const UEdGraphPin* Path = Node->FindPin(FName(TEXT("Path")), EGPD_Input);
	TestTrue(TEXT("in Path, a string"), Path && Path->PinType.PinCategory == UEdGraphSchema_K2::PC_String);
	const UEdGraphPin* Verdict = Node->FindPin(FName(TEXT("Verdict")), EGPD_Output);
	TestTrue(TEXT("out Verdict, an FOWExportVerdict"), Verdict && Verdict->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
		&& Verdict->PinType.PinSubCategoryObject.Get() == FOWExportVerdict::StaticStruct());
	const UEdGraphPin* Out = Node->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
	TestTrue(TEXT("out ReturnValue, a UOWWorld"), Out && Out->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
		&& Out->PinType.PinSubCategoryObject.Get() == UOWWorld::StaticClass());
	return true;
}

#endif  // WITH_EDITOR

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
