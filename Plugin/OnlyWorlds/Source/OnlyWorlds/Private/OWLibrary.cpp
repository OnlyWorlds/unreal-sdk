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
	std::unique_ptr<OnlyWorlds::OWWorld> World = OnlyWorlds::OWWorld::ReadFolder(OWConvert::Path(Path));
	if (!World)
	{
		Error = TEXT("no world.json in ") + Path;
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
	return Wrap(OnlyWorlds::OWWorld::FromFiles(R.files));
}

FOWArchiveVerdict UOWLibrary::CheckWorldFile(const FString& Path)
{
	return VerdictOf(OnlyWorlds::OWWorldArchive::ReadFile(OWConvert::Path(Path)));
}

bool UOWLibrary::PackWorldFolder(const FString& Folder, const FString& ArchivePath, FString& Error,
	bool bIncludeAuthorFields)
{
	OnlyWorlds::OWPacked P = OnlyWorlds::OWWorldArchive::Pack(OWConvert::Path(Folder), OWConvert::Path(ArchivePath),
		false, false, bIncludeAuthorFields);
	Error = P.ok ? FString() : Str(P.message);
	return P.ok;
}
