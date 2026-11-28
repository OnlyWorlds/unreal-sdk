#pragma once
// A listener for the join nodes' tests (OWJoinNodesTests.cpp, OWJoinLogTests.cpp): a UObject whose UFUNCTIONs a node's
// OnSuccess and OnFailure are bound to with AddDynamic, as a graph's async node binds its custom events to them, each
// answer counted and its pins kept. A test's own class: nothing else uses it.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWJoinNodes.h"

#include "OWJoinNodesProbe.generated.h"

UCLASS(Transient)
class UOWJoinNodesProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;

	// The last answer's pins, whichever delegate gave it.
	FOWJoinPreview Preview;
	FOWJoined Joined;
	FOWJoinedMembership Membership;
	FOWApiError Error;

	int32 Answers() const { return Successes + Failures; }

	void Watch(UOWClientJoinPreview* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWJoinNodesProbe::PreviewSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWJoinNodesProbe::PreviewFailed);
	}
	void Watch(UOWClientJoinRedeem* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWJoinNodesProbe::JoinedSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWJoinNodesProbe::JoinedFailed);
	}
	void Watch(UOWClientJoinAccept* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWJoinNodesProbe::MembershipSucceeded);
		Node->OnFailure.AddDynamic(this, &UOWJoinNodesProbe::MembershipFailed);
	}

	UFUNCTION()
	void PreviewSucceeded(const FOWJoinPreview& InPreview, const FOWApiError& InError) { ++Successes; Preview = InPreview; Error = InError; }
	UFUNCTION()
	void PreviewFailed(const FOWJoinPreview& InPreview, const FOWApiError& InError) { ++Failures; Preview = InPreview; Error = InError; }

	UFUNCTION()
	void JoinedSucceeded(const FOWJoined& InJoined, const FOWApiError& InError) { ++Successes; Joined = InJoined; Error = InError; }
	UFUNCTION()
	void JoinedFailed(const FOWJoined& InJoined, const FOWApiError& InError) { ++Failures; Joined = InJoined; Error = InError; }

	UFUNCTION()
	void MembershipSucceeded(const FOWJoinedMembership& InMembership, const FOWApiError& InError) { ++Successes; Membership = InMembership; Error = InError; }
	UFUNCTION()
	void MembershipFailed(const FOWJoinedMembership& InMembership, const FOWApiError& InError) { ++Failures; Membership = InMembership; Error = InError; }
};
