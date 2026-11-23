#include "OWMakeFirstRunCommandlet.h"

#include "CoreGlobals.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"

#include "OWFirstRunGraph.h"

DEFINE_LOG_CATEGORY_STATIC(LogOWMakeFirstRun, Log, All);

namespace
{
	const TCHAR* const OWFirstRunPackage = TEXT("/Game/OW/BP_OWFirstRun");
	const TCHAR* const OWFirstRunAsset = TEXT("BP_OWFirstRun");
	const TCHAR* const OWFirstRunClass = TEXT("/Game/OW/BP_OWFirstRun.BP_OWFirstRun_C");
	// The README's Path, typed as its step 2 types it: relative, so taken from the project's folder.
	const TCHAR* const OWFirstRunPath = TEXT("Worlds/hyperion");

	bool MakeAndSave()
	{
		UPackage* Package = CreatePackage(OWFirstRunPackage);
		if (!Package || FindObject<UBlueprint>(Package, OWFirstRunAsset))
		{
			UE_LOG(LogOWMakeFirstRun, Error, TEXT("OWMakeFirstRun: %s is already loaded in this process; nothing written"), OWFirstRunPackage);
			return false;
		}
		// The package is written whole, never read: with its file already on disk (a second run) the save would refuse
		// it as "only partially loaded" (run, 2026-11-23).
		Package->MarkAsFullyLoaded();
		const OWFirstRunGraph::FMade Made = OWFirstRunGraph::Make(Package, FName(OWFirstRunAsset), OWFirstRunPath);
		for (const FString& Line : Made.Nodes) UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun:   %s"), *Line);
		UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun: %s: %d links (%d refused), %d compile errors, %d warnings%s%s"),
			OWFirstRunPackage, Made.Links, Made.Refused, Made.CompileErrors, Made.CompileWarnings, Made.Why.IsEmpty() ? TEXT("") : TEXT(": "),
			*Made.Why);
		if (!Made.Why.IsEmpty() || !Made.Blueprint)
		{
			UE_LOG(LogOWMakeFirstRun, Error, TEXT("OWMakeFirstRun: %s: not saved"), OWFirstRunPackage);
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(OWFirstRunPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;
		if (!UPackage::SavePackage(Package, Made.Blueprint, *Filename, SaveArgs))
		{
			UE_LOG(LogOWMakeFirstRun, Error, TEXT("OWMakeFirstRun: %s: the save to %s failed"), OWFirstRunPackage, *Filename);
			return false;
		}
		UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun: wrote %s (%lld bytes)"), *FPaths::ConvertRelativePathToFull(Filename),
			IFileManager::Get().FileSize(*Filename));
		return true;
	}

	bool PlaySaved()
	{
		UClass* Class = FSoftClassPath(OWFirstRunClass).TryLoadClass<UObject>();
		if (!Class)
		{
			UE_LOG(LogOWMakeFirstRun, Error, TEXT("OWMakeFirstRun: no class %s (run -run=OWMakeFirstRun first)"), OWFirstRunClass);
			return false;
		}
		const FString World = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OWFirstRunPath);
		UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun: playing %s; %s %s"), *Class->GetPathName(), *World,
			FPaths::FileExists(World / TEXT("world.json")) ? TEXT("holds a world.json") : TEXT("holds no world.json"));
		const OWFirstRunGraph::FPlayed Played = OWFirstRunGraph::Play(Class);
		UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun: BeginPlay %s; %d lines printed, %d OpenWorldFolder lines"),
			Played.bBegunPlay ? TEXT("ran") : TEXT("did NOT run"), Played.Printed.Num(), Played.Opened.Num());
		return Played.bBegunPlay;
	}
}

UOWMakeFirstRunCommandlet::UOWMakeFirstRunCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	HelpDescription = TEXT("Writes the README's Blueprint first run into Content/OW/BP_OWFirstRun; -Play runs it after, -PlayOnly runs the saved one.");
}

int32 UOWMakeFirstRunCommandlet::Main(const FString& Params)
{
	const bool bPlayOnly = FParse::Param(*Params, TEXT("PlayOnly"));
	const bool bPlay = bPlayOnly || FParse::Param(*Params, TEXT("Play"));
	bool bHeld = bPlayOnly || MakeAndSave();
	if (bHeld && bPlay) bHeld = PlaySaved();
	const int32 Code = bHeld ? 0 : 1;
	UE_LOG(LogOWMakeFirstRun, Display, TEXT("OWMakeFirstRun: done, exit %d"), Code);
	return Code;
}
