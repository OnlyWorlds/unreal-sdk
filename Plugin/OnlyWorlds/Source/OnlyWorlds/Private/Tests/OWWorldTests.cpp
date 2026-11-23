// Real world folders through the plugin: opened with UOWLibrary, written unedited to a scratch folder with
// UOWWorld::WriteFolder, and compared file by file with the folder read, both sides read by Unreal's own file
// manager (never the core's). Then the generated structs on the same worlds, and one edit.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "OWConvert.h"
#include "OWLibrary.h"
#include "OWModels.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	FString WorldRoot(const FString& Name)
	{
		return Name == TEXT("Sikelia") ? OWTestPaths::Sikelia() : OWTestPaths::Moppetopia();
	}

	// Every file under a folder, by its path inside it ('/'-separated).
	TArray<FString> Tree(const FString& Root)
	{
		TArray<FString> Full;
		IFileManager::Get().FindFilesRecursive(Full, *Root, TEXT("*"), true, false);
		TArray<FString> Rel;
		Rel.Reserve(Full.Num());
		for (FString& F : Full)
		{
			FPaths::NormalizeFilename(F);
			FString R = F;
			FPaths::MakePathRelativeTo(R, *(Root / TEXT("")));
			Rel.Add(R);
		}
		return Rel;
	}

	bool IsElementFile(const FString& Rel)
	{
		if (!Rel.StartsWith(TEXT("elements/")) && !Rel.StartsWith(TEXT("spatial/"))) return false;
		return Rel.EndsWith(TEXT(".json")) && !FPaths::GetCleanFilename(Rel).StartsWith(TEXT("."));
	}

	UOWWorld* Open(FAutomationTestBase& Test, const FString& Name)
	{
		const FString Root = WorldRoot(Name);
		if (!FPaths::FileExists(Root / TEXT("world.json")))
		{
			Test.AddError(FString::Printf(TEXT("no world at %s (set OW_SIKELIA / OW_MOPPETOPIA): a missing world is a "
				"failure here, never a pass"), *Root));
			return nullptr;
		}
		FString Error;
		const double T0 = FPlatformTime::Seconds();
		UOWWorld* World = UOWLibrary::OpenWorldFolder(Root, Error);
		Test.AddInfo(FString::Printf(TEXT("%s: %d elements read in %.2f s"), *Name, World ? World->GetElementCount() : 0,
			FPlatformTime::Seconds() - T0));
		Test.TestNotNull(TEXT("the world opens (") + Error + TEXT(")"), World);
		return World;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FOWWorldWriteBackTest, "OnlyWorlds.World.WriteBack", OWTestFlags)

void FOWWorldWriteBackTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Name : { TEXT("Moppetopia"), TEXT("Sikelia") })
	{
		OutBeautifiedNames.Add(Name);
		OutTestCommands.Add(Name);
	}
}

bool FOWWorldWriteBackTest::RunTest(const FString& Parameters)
{
	const TCHAR* EnvName = Parameters == TEXT("Sikelia") ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA");
	if (!OWTestPaths::WorldGiven(*this, EnvName)) return true;
	UOWWorld* World = Open(*this, Parameters);
	if (!World) return false;
	const FString Root = WorldRoot(Parameters);
	TestEqual(TEXT("read errors"), World->GetErrors().Num(), 0);

	// The world's element files on disk, listed by Unreal's file manager (elements/ and spatial/ only: a world
	// folder may hold a .git): every one was read.
	TArray<FString> Source;
	for (const TCHAR* Part : { TEXT("elements"), TEXT("spatial") })
	{
		for (const FString& Rel : Tree(Root / Part))
		{
			const FString InWorld = FString(Part) / Rel;
			if (IsElementFile(InWorld)) Source.Add(InWorld);
		}
	}
	const int32 SourceElements = Source.Num();
	TestEqual(TEXT("every element file on disk was read"), World->GetElementCount(), SourceElements);

	// Unedited, written to a scratch folder.
	const FString Out = OWTestPaths::Scratch(Parameters);
	IFileManager::Get().DeleteDirectory(*Out, false, true);
	FString Error;
	const double T0 = FPlatformTime::Seconds();
	TestTrue(TEXT("written (") + Error + TEXT(")"), World->WriteFolder(Out, false, Error));
	const double WriteSeconds = FPlatformTime::Seconds() - T0;

	// Every file written is the file of the same path in the world folder, byte for byte.
	const TArray<FString> Written = Tree(Out);
	const int32 Expected = static_cast<int32>(World->Native()->ToFiles().size());
	TestEqual(TEXT("the scratch folder holds exactly the world's files"), Written.Num(), Expected);
	int32 Same = 0, Differ = 0;
	TArray<uint8> A, B;
	for (const FString& Rel : Written)
	{
		A.Reset();
		B.Reset();
		const bool bRead = FFileHelper::LoadFileToArray(A, *(Out / Rel)) && FFileHelper::LoadFileToArray(B, *(Root / Rel));
		if (bRead && A == B) ++Same;
		else if (++Differ <= 3) AddError(Rel + TEXT(" differs from the world folder's file"));
	}
	TestEqual(TEXT("unedited write-back byte-equal, every file"), Same, Written.Num());

	// And the other way: every element file of the world folder was written.
	TSet<FString> WrittenSet(Written);
	int32 Missing = 0;
	for (const FString& Rel : Source)
	{
		if (!WrittenSet.Contains(Rel) && ++Missing <= 3) AddError(Rel + TEXT(" was not written"));
	}
	TestEqual(TEXT("no element file left out"), Missing, 0);
	AddInfo(FString::Printf(TEXT("%s: %d files written in %.1f s, %d byte-equal to the folder read; %d element files on disk"),
		*Parameters, Written.Num(), WriteSeconds, Same, SourceElements));

	IFileManager::Get().DeleteDirectory(*Out, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsTest, "OnlyWorlds.World.Models", OWTestFlags)

bool FOWModelsTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	UOWWorld* World = Open(*this, TEXT("Moppetopia"));
	if (!World) return false;

	// The 22 accessors between them hold every element once.
	const int32 Total = UOWElements::GetAllAbilities(World).Num() + UOWElements::GetAllCharacters(World).Num()
		+ UOWElements::GetAllCollectives(World).Num() + UOWElements::GetAllConstructs(World).Num()
		+ UOWElements::GetAllCreatures(World).Num() + UOWElements::GetAllEvents(World).Num()
		+ UOWElements::GetAllFamilies(World).Num() + UOWElements::GetAllInstitutions(World).Num()
		+ UOWElements::GetAllLanguages(World).Num() + UOWElements::GetAllLaws(World).Num()
		+ UOWElements::GetAllLocations(World).Num() + UOWElements::GetAllMaps(World).Num()
		+ UOWElements::GetAllMarkers(World).Num() + UOWElements::GetAllNarratives(World).Num()
		+ UOWElements::GetAllObjects(World).Num() + UOWElements::GetAllPhenomena(World).Num()
		+ UOWElements::GetAllPins(World).Num() + UOWElements::GetAllRelations(World).Num()
		+ UOWElements::GetAllSpecies(World).Num() + UOWElements::GetAllTitles(World).Num()
		+ UOWElements::GetAllTraits(World).Num() + UOWElements::GetAllZones(World).Num();
	TestEqual(TEXT("the 22 types' structs hold every element"), Total, World->GetElementCount());

	// A pin as a struct: its fields are the file's.
	const TArray<FOWPin> Pins = UOWElements::GetAllPins(World);
	if (!TestTrue(TEXT("Moppetopia has pins"), Pins.Num() > 0)) return false;
	TestEqual(TEXT("pins by the accessor and by type"), Pins.Num(), World->GetElementIds(TEXT("pin")).Num());
	int32 Checked = 0;
	for (const FOWPin& Pin : Pins)
	{
		if (!Pin.bHasX || Pin.Map.IsEmpty()) continue;
		const OnlyWorlds::OWElement* E = World->Native()->Get(OWConvert::Utf8(Pin.Id));
		const OnlyWorlds::OWJsonValue* X = E ? E->body.Find("x") : nullptr;
		TestTrue(TEXT("the pin's x is the file's"), X && X->IsNumber() && static_cast<int64>(X->AsDouble()) == Pin.X);
		FOWMap Map;
		TestTrue(TEXT("the pin's map is found by its id"), UOWElements::FindMap(World, Pin.Map, Map) && Map.Id == Pin.Map);
		FOWPin Again;
		TestTrue(TEXT("the pin is found by its id"), UOWElements::FindPin(World, Pin.Id, Again) && Again.Name == Pin.Name);
		FOWCharacter NotACharacter;
		TestFalse(TEXT("a pin's id finds no character"), UOWElements::FindCharacter(World, Pin.Id, NotACharacter));
		++Checked;
	}
	TestTrue(TEXT("pins with a map and an x were checked"), Checked > 0);
	AddInfo(FString::Printf(TEXT("Moppetopia: %d elements, %d pins, %d checked against their files"), Total, Pins.Num(), Checked));

	// One edit: only that pin's file is written, with the new x.
	const FOWPin& First = Pins[0];
	TestTrue(TEXT("the edit applies"), World->SetIntField(First.Id, TEXT("x"), 12));
	const FString Out = OWTestPaths::Scratch(TEXT("ModelsEdit"));
	IFileManager::Get().DeleteDirectory(*Out, false, true);
	FString Error;
	TestTrue(TEXT("the edited file is written"), World->WriteFolder(Out, true, Error));
	const TArray<FString> Written = Tree(Out);
	TestEqual(TEXT("one file written"), Written.Num(), 1);
	FOWPin Edited;
	TestTrue(TEXT("the struct reads the new x"), UOWElements::FindPin(World, First.Id, Edited) && Edited.bHasX && Edited.X == 12);
	IFileManager::Get().DeleteDirectory(*Out, false, true);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
