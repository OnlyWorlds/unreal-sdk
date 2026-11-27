#pragma once
// A listener for the sync nodes' tests (OWSyncTests.cpp): a UObject whose UFUNCTIONs a node's OnSuccess and OnFailure
// are bound to with AddDynamic, as a graph's async node binds its custom events, each answer counted and kept. A
// test's own class, as OWClientNodesProbe.h is: nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWSyncNodes.h"

#include "OWSyncProbe.generated.h"

UCLASS(Transient)
class UOWSyncProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FOWSyncResult Result;
	bool bHasChanges = false;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWSyncWorld* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWSyncProbe::SyncSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWSyncProbe::SyncFailed);
	}

	void Watch(UOWHasWorldChanges* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWSyncProbe::HasSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWSyncProbe::HasFailed);
	}

	UFUNCTION()
	void SyncSucceeded(const FOWSyncResult& InResult, const FOWApiError& InError)
	{
		++Successes;
		Result = InResult;
		Error = InError;
	}

	UFUNCTION()
	void SyncFailed(const FOWSyncResult& InResult, const FOWApiError& InError)
	{
		++Failures;
		Result = InResult;
		Error = InError;
	}

	UFUNCTION()
	void HasSucceeded(bool bInHasChanges, const FOWApiError& InError)
	{
		++Successes;
		bHasChanges = bInHasChanges;
		Error = InError;
	}

	UFUNCTION()
	void HasFailed(bool bInHasChanges, const FOWApiError& InError)
	{
		++Failures;
		bHasChanges = bInHasChanges;
		Error = InError;
	}
};
