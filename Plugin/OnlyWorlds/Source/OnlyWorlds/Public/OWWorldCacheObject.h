#pragma once
// A world held by a game and kept current from the API, on Blueprints: a UObject around the core's
// OnlyWorlds::OWWorldCache, with the one OWClient its syncs go through, so the tip read's ETag lives between polls (a
// client node is its own client and keeps none). The nodes that fill it are in OWSyncNodes.h (SyncWorld,
// BaselineWorld, HasWorldChanges). GetHeldWorld is the held world as a UOWWorld, the object every reader of the plugin
// takes (UOWElements, UOWLens, UOWGraph, UOWMessages, ...): the same object for the cache's life, its contents the
// held world's after each sync that changed anything.
//
// The config is kept as the core's, never as a property: a key is never saved with anything. The cache's file
// (WriteFile, OpenWorldCache) holds the world and where the last sync stopped, never the key.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWClient.h"
#include "OWClientNodes.h"
#include "OWTransport.h"
#include "OWWorldCache.h"

#include <memory>

#include "OWWorldCacheObject.generated.h"

class UOWWorld;

UCLASS(BlueprintType)
class ONLYWORLDS_API UOWWorldCache : public UObject
{
	GENERATED_BODY()

public:
	/** An empty held world, its syncs sent with Config. WorldId and WorldName are informational; a read-only cache is a frozen snapshot every sync refuses. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync")
	static UOWWorldCache* MakeWorldCache(const FOWClientConfig& Config, const FString& WorldId, const FString& WorldName, bool bReadOnly);

	/** A held world read from its file (WriteFile), its syncs sent with Config: an empty one when there is no file (a first launch), none and why (Error) when the file is not a cache's. A relative path is taken from the project's folder. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync")
	static UOWWorldCache* OpenWorldCache(const FOWClientConfig& Config, const FString& Path, FString& Error);

	/** Write the held world and where its last sync stopped to Path (never the key). False, and why, when it could not. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Sync")
	bool WriteFile(const FString& Path, FString& Error) const;

	/** The held world as a UOWWorld, for every node that reads one. Its world.json holds the id and name alone: world settings are not in the change feed (ReadWorld reads them). */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	UOWWorld* GetHeldWorld() const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetWorldId() const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetWorldName() const;

	/** A frozen snapshot: every sync refuses it, nothing sent. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	bool IsReadOnly() const;

	/** A SyncWorld or a BaselineWorld is in flight on this cache: another is refused until it answered. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	bool IsSyncing() const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	int32 GetElementCount() const;

	/** The ids of the elements of one type ("character"), in the order first held; every element's when Type is "". */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	TArray<FString> GetElementIds(const FString& Type) const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	bool HasElement(const FString& Id) const;

	/** The type it is held as ("character"); "" when there is no such element. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetElementType(const FString& Id) const;

	/** Its body as served, as JSON text (each number as served); "" when there is no such element. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetElementJson(const FString& Id) const;

	/** Held as a known-of stub (known_of true): its unset fields are not the element's values. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	bool IsKnownOf(const FString& Id) const;

	/** The change_seq the last sync reached; 0 before any. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	int64 GetCursor() const;

	/** The feed's own cursor where the last sync stopped, opaque; "" when there is none. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetCursorToken() const;

	/** The view epoch the last sync ran under (a guest's or a contributor's key), when bHasEpoch. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	int64 GetEpoch(bool& bHasEpoch) const;

	/** When the last sync stopped (ISO 8601, UTC); "" before any. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Sync")
	FString GetSyncedAt() const;

	/** C++ only, before the first sync: the transport its client sends through, in place of FOWHttpTransport (a test's fake). */
	void SetTransport(std::shared_ptr<OnlyWorlds::OWTransport> InTransport);

	/** C++ only: the core's cache (never null), and the client its syncs go through, built at the first call. */
	OnlyWorlds::OWWorldCache& Native() const { return *Held; }
	OnlyWorlds::OWClient& Client();

	/** C++ only, after a sync that changed anything: GetHeldWorld's contents made the held world's. A pointer into the old ones does not survive it. */
	void RefreshWorld();

private:
	void Take(std::unique_ptr<OnlyWorlds::OWWorldCache> InHeld, const FOWClientConfig& Config);

	std::unique_ptr<OnlyWorlds::OWWorldCache> Held = std::make_unique<OnlyWorlds::OWWorldCache>();
	OnlyWorlds::OWClientConfig Core;
	std::unique_ptr<OnlyWorlds::OWClient> Sender;

	UPROPERTY(Transient)
	TObjectPtr<UOWWorld> View;
};
