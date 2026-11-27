#include "OWLibrary.h"

#include "OWConvert.h"
#include "OWLog.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"
#include "OWWorldObject.h"
#include "UObject/Package.h"

using OWConvert::Str;

namespace
{
	FOWArchiveVerdict VerdictOf(const OnlyWorlds::OWArchiveRead& R)
	{
		FOWArchiveVerdict V;
		V.bAccepted = R.ok;
		V.Reason = Str(R.reason);
		V.Message = Str(R.message);
		V.WorldFault = Str(R.world_fault);
		V.WorldId = Str(R.world_id);
		return V;
	}

	UOWWorld* Wrap(std::unique_ptr<OnlyWorlds::OWWorld> World)
	{
		UOWWorld* Out = NewObject<UOWWorld>(GetTransientPackage());
		Out->SetNative(std::move(World));
		return Out;
	}
}

UOWWorld* UOWLibrary::OpenWorldFolder(const FString& Path, FString& Error)
{
	const std::string Native = OWConvert::Path(Path);
	std::unique_ptr<OnlyWorlds::OWWorld> World = OnlyWorlds::OWWorld::ReadFolder(Native);
	if (!World)
	{
		// world.json, elements/ or spatial/ in another case (name_case) says so, both spellings named; else no world.json.
		const std::string Why = OnlyWorlds::OWWorld::FolderFault(Native);
		Error = Why.empty() ? TEXT("no world.json in ") + Path : Path + TEXT(" is refused, ") + Str(Why);
		UE_LOG(LogOnlyWorlds, Log, TEXT("OpenWorldFolder: %s"), *Error);
		return nullptr;
	}
	Error.Reset();
	return Wrap(std::move(World));
}

UOWWorld* UOWLibrary::OpenWorldFile(const FString& Path, FOWArchiveVerdict& Verdict)
{
	OnlyWorlds::OWArchiveRead R = OnlyWorlds::OWWorldArchive::ReadFile(OWConvert::Path(Path));
	Verdict = VerdictOf(R);
	if (!R.ok)
	{
		UE_LOG(LogOnlyWorlds, Log, TEXT("OpenWorldFile: %s refused (%s: %s)"), *Path, *Verdict.Reason, *Verdict.Message);
		return nullptr;
	}
	std::unique_ptr<OnlyWorlds::OWWorld> World = OnlyWorlds::OWWorld::FromFiles(R.files);
	return World ? Wrap(std::move(World)) : nullptr;  // never null after an accepted read: the archive refuses first
}

FOWArchiveVerdict UOWLibrary::CheckWorldFile(const FString& Path)
{
	return VerdictOf(OnlyWorlds::OWWorldArchive::ReadFile(OWConvert::Path(Path)));
}

bool UOWLibrary::PackWorldFolder(const FString& Folder, const FString& ArchivePath, FString& Error,
	bool bIncludeAuthorFields, bool bAsShare)
{
	OnlyWorlds::OWPacked P = OnlyWorlds::OWWorldArchive::Pack(OWConvert::Path(Folder), OWConvert::Path(ArchivePath),
		false, false, bIncludeAuthorFields, !bAsShare);
	Error = P.ok ? FString() : Str(P.message);
	return P.ok;
}
