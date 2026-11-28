#pragma once
// A listener for the proposal nodes' tests (OWClientProposalNodesTests.cpp): a UObject whose UFUNCTIONs a node's
// OnSuccess and OnFailure are bound to with AddDynamic, as a graph's async node binds its custom events to them, each
// answer counted and kept. The same as OWClientWriteNodesProbe.h, for the proposal nodes' two answer shapes and the
// review's (a write's). A test's own class: nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWClientNodes.h"

#include "OWClientProposalNodesProbe.generated.h"

UCLASS(Transient)
class UOWClientProposalNodesProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FOWProposal Proposal;
	FString ElementJson;
	FOWProposalPage Page;
	FString Json;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWClientProposal* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientProposalNodesProbe::ActSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientProposalNodesProbe::ActFailed);
	}

	void Watch(UOWClientProposals* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientProposalNodesProbe::PageSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientProposalNodesProbe::PageFailed);
	}

	void Watch(UOWClientWrite* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWClientProposalNodesProbe::WriteSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWClientProposalNodesProbe::WriteFailed);
	}

	UFUNCTION()
	void ActSucceeded(const FOWProposal& InProposal, const FString& InElementJson, const FOWApiError& InError)
	{
		++Successes;
		Proposal = InProposal;
		ElementJson = InElementJson;
		Error = InError;
	}

	UFUNCTION()
	void ActFailed(const FOWProposal& InProposal, const FString& InElementJson, const FOWApiError& InError)
	{
		++Failures;
		Proposal = InProposal;
		ElementJson = InElementJson;
		Error = InError;
	}

	UFUNCTION()
	void PageSucceeded(const FOWProposalPage& InPage, const FOWApiError& InError)
	{
		++Successes;
		Page = InPage;
		Error = InError;
	}

	UFUNCTION()
	void PageFailed(const FOWProposalPage& InPage, const FOWApiError& InError)
	{
		++Failures;
		Page = InPage;
		Error = InError;
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
};
