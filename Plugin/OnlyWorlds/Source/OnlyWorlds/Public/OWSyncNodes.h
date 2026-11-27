#pragma once
// Keeping a held world current on Blueprints: async nodes over the core's OWSync, each on a UOWWorldCache
// (OWWorldCacheObject.h), which holds the world, where its last sync stopped, and the one client whose tip read keeps
// its ETag between polls. SyncWorld applies what changed (the first call walks the whole world), BaselineWorld walks it
// again from the start, HasWorldChanges asks from one tip read whether a sync would do anything: while nothing moved,
// keel answers that read 304, given back to the request budget.
//
//   BeginPlay: OpenWorldCache(Config, Path) -> SyncWorld -> OnSuccess: WriteFile; read GetHeldWorld with any node
//   A timer:   HasWorldChanges -> OnSuccess(bHasChanges) -> Branch -> SyncWorld
//
// Each node is kept (in the root set) from its activation until it answered, once, and holds its cache meanwhile, so
// the cache outlives the walk. A sync on a cache with one in flight, on a read-only cache, or with no cache is refused,
// kind Refused, nothing sent. A node's two delegates share one signature, as the client nodes' do (OWClientNodes.h).

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "OWClientNodes.h"
#include "OWSync.h"

#include "OWSyncNodes.generated.h"

class UOWWorldCache;

/** What a sync did. The core's OWSyncResult. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWSyncResult
{
	GENERATED_BODY()

	/** Elements written into the cache. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	int64 Fetched = 0;

	/** Elements the feed deleted (or hid) that the cache held. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	int64 Removed = 0;

	/** The change_seq the cache stands at after the sync. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	int64 Cursor = 0;

	/** The cache was walked again from the start rather than updated. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	bool bWasRebaselined = false;

	/** Why it was walked again, in words; "" when it was not. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	FString RebaselineReason;

	/** On a walk the server asked for (409 resync_required): its reason, role or view; "" when it sent none or asked for nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	FString ResyncReason;

	/** Elements the cache held that the new walk did not name, and kept (role, no reason, a rewound server). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	int64 Kept = 0;

	/** The sync changed the cache. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Sync")
	bool bChangedAnything = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWSyncAnswer, const FOWSyncResult&, Result, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWHasChangesAnswer, bool, bHasChanges, const FOWApiError&, Error);

/** A sync onto a held world: what changed since its cursor, or the whole world again. */
UCLASS()
class ONLYWORLDS_API UOWSyncWorld : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Sync")
	FOWSyncAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Sync")
	FOWSyncAnswer OnFailure;

	/** Apply everything that changed since the cache's cursor; the first call walks the whole world. Walks again from the start when the server asks (409 resync_required: view drops what the new walk does not name, role or no reason keeps it) or was restored (a cursor ahead of its head: kept). A walk that fails leaves the cache as it was. The core's OWSync::Incremental. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync", meta = (BlueprintInternalUseOnly = "true"))
	static UOWSyncWorld* SyncWorld(UOWWorldCache* Cache);

	/** Walk every element type into the cache, replacing what it held, after a tip read whose head, cursor and epoch it records. A walk that fails leaves the cache as it was. The core's OWSync::Baseline. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync", meta = (BlueprintInternalUseOnly = "true"))
	static UOWSyncWorld* BaselineWorld(UOWWorldCache* Cache);

	virtual void Activate() override;

private:
	void Answer(const OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult>& Reply);

	UPROPERTY()
	TObjectPtr<UOWWorldCache> Held;

	bool bBaseline = false;
	bool bActivated = false;
	bool bAnswered = false;
};

/** Whether a sync would do anything, from one tip read. */
UCLASS()
class ONLYWORLDS_API UOWHasWorldChanges : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Sync")
	FOWHasChangesAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Sync")
	FOWHasChangesAnswer OnFailure;

	/** True when the head is not the cache's cursor (either way) or the view epoch moved: a sync is not a no-op, not that new content exists. Sent with the cache's last tip tag, so an unmoved head is a 304. Never refused for a sync in flight or a read-only cache. The core's OWSync::HasChanges. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync", meta = (BlueprintInternalUseOnly = "true"))
	static UOWHasWorldChanges* HasWorldChanges(UOWWorldCache* Cache);

	virtual void Activate() override;

private:
	void Answer(const OnlyWorlds::OWReply<bool>& Reply);

	UPROPERTY()
	TObjectPtr<UOWWorldCache> Held;

	bool bActivated = false;
	bool bAnswered = false;
};
