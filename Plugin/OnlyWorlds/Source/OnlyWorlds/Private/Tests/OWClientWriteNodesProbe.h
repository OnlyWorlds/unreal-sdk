#pragma once
// A listener for the write nodes' tests (OWClientWriteNodesTests.cpp): a UObject whose UFUNCTIONs a write node's
// OnSuccess and OnFailure are bound to with AddDynamic, as a graph's async node binds its custom events to them, each
// answer counted and kept. The read nodes' listener is OWClientNodesProbe.h; this one is the same, for the writes'
// two answer shapes. A test's own class: nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWClientNodes.h"

#include "OWClientWriteNodesProbe.generated.h"

UCLASS(Transient)
class UOWClientWriteNodesProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FString Json;
	FOWBulkResult Bulk;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWClientWrite* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientWriteNodesProbe::WriteSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientWriteNodesProbe::WriteFailed);
	}

	void Watch(UOWClientBulk* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientWriteNodesProbe::BulkSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientWriteNodesProbe::BulkFailed);
	}

	UFUNCTION()
	void WriteSucceeded(const FString& InJson, const FOWApiError& InError)
	{
		++Successes;
		Json = InJson;
		Error = InError;
	}

	UFUNCTION()
	void WriteFailed(const FString& InJson, const FOWApiError& InError)
	{
		++Failures;
		Json = InJson;
		Error = InError;
	}

	UFUNCTION()
	void BulkSucceeded(const FOWBulkResult& InOutcome, const FOWApiError& InError)
	{
		++Successes;
		Bulk = InOutcome;
		Error = InError;
	}

	UFUNCTION()
	void BulkFailed(const FOWBulkResult& InOutcome, const FOWApiError& InError)
	{
		++Failures;
		Bulk = InOutcome;
		Error = InError;
	}
};
