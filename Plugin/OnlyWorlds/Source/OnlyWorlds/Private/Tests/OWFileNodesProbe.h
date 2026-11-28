#pragma once
// A listener for the image and file nodes' tests (OWFileNodesTests.cpp): a UObject whose UFUNCTIONs a node's OnSuccess
// and OnFailure are bound to with AddDynamic, as a graph's async node binds its custom events to them, each answer
// counted and its pins kept. A test's own class: nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWFileNodes.h"

#include "OWFileNodesProbe.generated.h"

UCLASS(Transient)
class UOWFileNodesProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FOWUploadedImage Image;
	FOWRemovedImage Removal;
	TArray<FOWFileRow> Files;
	int64 UsedBytes = 0;
	int64 CapBytes = 0;
	FOWUploadedFile File;
	FOWFileDownload Link;
	FString Json;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWClientImage* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::ImageSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::ImageFailed);
	}
	void Watch(UOWClientImageRemoval* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::RemovalSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::RemovalFailed);
	}
	void Watch(UOWClientFiles* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::FilesSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::FilesFailed);
	}
	void Watch(UOWClientFileUpload* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::UploadSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::UploadFailed);
	}
	void Watch(UOWClientFileLink* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::LinkSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::LinkFailed);
	}
	void Watch(UOWClientFileRemoval* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFileNodesProbe::JsonSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWFileNodesProbe::JsonFailed);
	}

	UFUNCTION()
	void ImageSucceeded(const FOWUploadedImage& InImage, const FOWApiError& InError) { ++Successes; Image = InImage; Error = InError; }
	UFUNCTION()
	void ImageFailed(const FOWUploadedImage& InImage, const FOWApiError& InError) { ++Failures; Image = InImage; Error = InError; }

	UFUNCTION()
	void RemovalSucceeded(const FOWRemovedImage& InRemoval, const FOWApiError& InError) { ++Successes; Removal = InRemoval; Error = InError; }
	UFUNCTION()
	void RemovalFailed(const FOWRemovedImage& InRemoval, const FOWApiError& InError) { ++Failures; Removal = InRemoval; Error = InError; }

	UFUNCTION()
	void FilesSucceeded(const TArray<FOWFileRow>& InFiles, int64 InUsed, int64 InCap, const FOWApiError& InError)
	{
		++Successes;
		Files = InFiles;
		UsedBytes = InUsed;
		CapBytes = InCap;
		Error = InError;
	}
	UFUNCTION()
	void FilesFailed(const TArray<FOWFileRow>& InFiles, int64 InUsed, int64 InCap, const FOWApiError& InError)
	{
		++Failures;
		Files = InFiles;
		UsedBytes = InUsed;
		CapBytes = InCap;
		Error = InError;
	}

	UFUNCTION()
	void UploadSucceeded(const FOWUploadedFile& InFile, const FOWApiError& InError) { ++Successes; File = InFile; Error = InError; }
	UFUNCTION()
	void UploadFailed(const FOWUploadedFile& InFile, const FOWApiError& InError) { ++Failures; File = InFile; Error = InError; }

	UFUNCTION()
	void LinkSucceeded(const FOWFileDownload& InLink, const FOWApiError& InError) { ++Successes; Link = InLink; Error = InError; }
	UFUNCTION()
	void LinkFailed(const FOWFileDownload& InLink, const FOWApiError& InError) { ++Failures; Link = InLink; Error = InError; }

	UFUNCTION()
	void JsonSucceeded(const FString& InJson, const FOWApiError& InError) { ++Successes; Json = InJson; Error = InError; }
	UFUNCTION()
	void JsonFailed(const FString& InJson, const FOWApiError& InError) { ++Failures; Json = InJson; Error = InError; }
};
