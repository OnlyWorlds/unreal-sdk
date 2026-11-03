#pragma once
// A listener for the client nodes' tests (OWClientNodesTests.cpp): a UObject whose UFUNCTIONs a node's OnSuccess and
// OnFailure are bound to with AddDynamic, as a graph's async node binds its custom events to them, each answer counted
// and kept. A test's own class (as the engine keeps its test UObjects beside its tests, Engine/Private/Tests/AutoRTFM):
// nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWClientNodes.h"

#include "OWClientNodesProbe.generated.h"

UCLASS(Transient)
class UOWClientNodesProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FString Json;
	TArray<FString> Elements;
	bool bMore = false;
	FString Cursor;
	FOWChangesPage Page;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWClientRead* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientNodesProbe::ReadSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientNodesProbe::ReadFailed);
	}

	void Watch(UOWClientList* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientNodesProbe::ListSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientNodesProbe::ListFailed);
	}

	void Watch(UOWClientListAll* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientNodesProbe::ListAllSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientNodesProbe::ListAllFailed);
	}

	void Watch(UOWClientChanges* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientNodesProbe::ChangesSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientNodesProbe::ChangesFailed);
	}

	UFUNCTION()
	void ReadSucceeded(const FString& InJson, const FOWApiError& InError)
	{
		++Successes;
		Json = InJson;
		Error = InError;
	}

	UFUNCTION()
	void ReadFailed(const FString& InJson, const FOWApiError& InError)
	{
		++Failures;
		Json = InJson;
		Error = InError;
	}

	UFUNCTION()
	void ListSucceeded(const TArray<FString>& InElements, bool bInHasMore, const FString& InNextCursor, const FOWApiError& InError)
	{
		++Successes;
		Elements = InElements;
		bMore = bInHasMore;
		Cursor = InNextCursor;
		Error = InError;
	}

	UFUNCTION()
	void ListFailed(const TArray<FString>& InElements, bool bInHasMore, const FString& InNextCursor, const FOWApiError& InError)
	{
		++Failures;
		Elements = InElements;
		bMore = bInHasMore;
		Cursor = InNextCursor;
		Error = InError;
	}

	UFUNCTION()
	void ListAllSucceeded(const TArray<FString>& InElements, const FOWApiError& InError)
	{
		++Successes;
		Elements = InElements;
		Error = InError;
	}

	UFUNCTION()
	void ListAllFailed(const TArray<FString>& InElements, const FOWApiError& InError)
	{
		++Failures;
		Elements = InElements;
		Error = InError;
	}

	UFUNCTION()
	void ChangesSucceeded(const FOWChangesPage& InPage, const FOWApiError& InError)
	{
		++Successes;
		Page = InPage;
		Error = InError;
	}

	UFUNCTION()
	void ChangesFailed(const FOWChangesPage& InPage, const FOWApiError& InError)
	{
		++Failures;
		Page = InPage;
		Error = InError;
	}
};
