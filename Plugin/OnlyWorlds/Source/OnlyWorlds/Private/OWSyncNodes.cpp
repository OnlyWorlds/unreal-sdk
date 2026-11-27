#include "OWSyncNodes.h"

#include "UObject/WeakObjectPtrTemplates.h"

#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWWorldCacheObject.h"

#include <utility>

using OWConvert::Str;

FOWSyncResult OWClientShared::SyncResultOf(const OnlyWorlds::OWSyncResult& R)
{
	FOWSyncResult Out;
	Out.Fetched = R.fetched;
	Out.Removed = R.removed;
	Out.Cursor = R.cursor;
	Out.bWasRebaselined = R.was_rebaselined;
	Out.RebaselineReason = Str(R.rebaseline_reason);
	Out.ResyncReason = R.resync_reason ? Str(*R.resync_reason) : FString();
	Out.Kept = R.kept;
	Out.bChangedAnything = R.ChangedAnything();
	return Out;
}

// -- UOWSyncWorld -----------------------------------------------------------------------------------------------------

UOWSyncWorld* UOWSyncWorld::SyncWorld(UOWWorldCache* Cache)
{
	UOWSyncWorld* Node = NewObject<UOWSyncWorld>();
	Node->Held = Cache;
	return Node;
}

UOWSyncWorld* UOWSyncWorld::BaselineWorld(UOWWorldCache* Cache)
{
	UOWSyncWorld* Node = SyncWorld(Cache);
	Node->bBaseline = true;
	return Node;
}

void UOWSyncWorld::Activate()
{
	if (bActivated) return;
	bActivated = true;
	// Kept until it answered, and its cache with it (Held): the walk writes into the cache from each answer. The keep
	// comes first, since a refusal answers inside the call.
	AddToRoot();
	if (!Held)
	{
		OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult> R;
		R.error = OnlyWorlds::OWApiError::Of(OnlyWorlds::OWApiError::Kind::Refused,
			std::string(bBaseline ? "BaselineWorld" : "SyncWorld") + ": no cache was given. Nothing was sent.");
		return Answer(R);
	}
	TWeakObjectPtr<UOWSyncWorld> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult> Reply)
	{
		if (UOWSyncWorld* Node = Self.Get()) Node->Answer(Reply);
	};
	if (bBaseline) OnlyWorlds::OWSync::Baseline(Held->Client(), Held->Native(), Done);
	else OnlyWorlds::OWSync::Incremental(Held->Client(), Held->Native(), Done);
}

void UOWSyncWorld::Answer(const OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult>& Reply)
{
	if (bAnswered) return;
	bAnswered = true;
	if (Reply.Ok())
	{
		if (Reply.value.ChangedAnything()) Held->RefreshWorld();
		OnSuccess.Broadcast(OWClientShared::SyncResultOf(Reply.value), FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(FOWSyncResult(), OWClientShared::ErrorOf(*Reply.error));
	}
	if (IsRooted()) RemoveFromRoot();
	SetReadyToDestroy();
}

// -- UOWHasWorldChanges -----------------------------------------------------------------------------------------------

UOWHasWorldChanges* UOWHasWorldChanges::HasWorldChanges(UOWWorldCache* Cache)
{
	UOWHasWorldChanges* Node = NewObject<UOWHasWorldChanges>();
	Node->Held = Cache;
	return Node;
}

void UOWHasWorldChanges::Activate()
{
	if (bActivated) return;
	bActivated = true;
	AddToRoot();
	if (!Held)
	{
		OnlyWorlds::OWReply<bool> R;
		R.error = OnlyWorlds::OWApiError::Of(OnlyWorlds::OWApiError::Kind::Refused,
			"HasWorldChanges: no cache was given. Nothing was sent.");
		return Answer(R);
	}
	TWeakObjectPtr<UOWHasWorldChanges> Self(this);
	OnlyWorlds::OWSync::HasChanges(Held->Client(), Held->Native(), [Self](OnlyWorlds::OWReply<bool> Reply)
	{
		if (UOWHasWorldChanges* Node = Self.Get()) Node->Answer(Reply);
	});
}

void UOWHasWorldChanges::Answer(const OnlyWorlds::OWReply<bool>& Reply)
{
	if (bAnswered) return;
	bAnswered = true;
	if (Reply.Ok()) OnSuccess.Broadcast(Reply.value, FOWApiError());
	else OnFailure.Broadcast(false, OWClientShared::ErrorOf(*Reply.error));
	if (IsRooted()) RemoveFromRoot();
	SetReadyToDestroy();
}
