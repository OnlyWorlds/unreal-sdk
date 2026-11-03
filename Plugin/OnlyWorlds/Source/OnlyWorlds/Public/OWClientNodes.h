#pragma once
// The API client's read calls on Blueprints: one async node per call of the core's OWClient (Health, GetWorld, Me,
// Members, Get, List, ListAll, Changes, ChangesHead), each with OnSuccess and OnFailure. The core does the work: the
// credential rules, what a key may send at all (refused here, nothing sent), the cursor walk, keel's error envelope.
// Each node builds its own client from an FOWClientConfig and sends through FOWHttpTransport (Unreal's HTTP), so it
// answers later, on the game thread; a refusal answers at once, inside the node's activation.
//
// An element comes back as its body's JSON text, the form UOWWorld::GetElementJson gives a folder's element: the
// plugin has no Blueprint shape for an element that is not in an opened world (the FOW<Type> structs are read out of
// one). The members list comes back as the JSON text UOWMessages takes as MembersJson, so a graph wires one into the
// other. The world's settings, Me and the health pulse are JSON text too (no struct models them yet); the change feed
// is a struct, FOWChangesPage, as the core types it.
//
// A node's two delegates share one signature, since a graph's async node takes its data pins from the first delegate
// it finds (UK2Node_BaseAsyncTask::AllocateDefaultPins): one class per answer's shape, one factory per call.
//
// Never keep a key or a PIN in an asset: a Make node's literal is saved with the Blueprint.

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "OWClient.h"
#include "OWTransport.h"

#include <memory>

#include "OWClientNodes.generated.h"

/** Which of the core's four errors, or None on OnSuccess. */
UENUM(BlueprintType)
enum class EOWApiErrorKind : uint8
{
	/** No error: what OnSuccess carries. */
	None,
	/** keel answered and said no: branch on Code, point a person at DocUrl. */
	Api,
	/** No answer came (offline, the timeout, a redirect refused), or a list walk that would never end. */
	Transport,
	/** The client refused the call before sending anything: a caller's mistake, the message names it. */
	Refused,
	/** A 2xx whose body is not the answer the route documents. */
	Answer,
};

/** How a node's client is built: the core's OWClientConfig. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWClientConfig
{
	GENERATED_BODY()

	/** An ow_w_ (write) or ow_r_ (read) world key, an ow_a_ account token (a Bearer, with World for that world's reads), or a 10-digit legacy key. "" with World: a keyless reader of a public world; "" without: only ReadHealth works. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString ApiKey;

	/** The PIN (or an agent seat's ow_s_ secret): sent only on ReadMe among the reads, and on every request of a legacy key. An account token never sends it. "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString ApiPin;

	/** A world's id, for an account token or no key: those then read that world (world=<id>), and an account token's ReadMembers its roster. "" for none. Given with a world key, or white space, every call refuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString World;

	/** keel's API root; trailing slashes are trimmed, "" is this default. Never an address that redirects: the transport refuses a redirected answer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString BaseUrl = TEXT("https://www.onlyworlds.com/api/v2");

	/** Elements a page (keel clamps a list at 1000 silently; the change feed takes at most 1000). 0 or less: 100. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	int64 PageSize = 100;
};

/** Why a call has no value: the core's OWApiError, keel's envelope or the client's own word. Empty (Kind None) on OnSuccess. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWApiError
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	EOWApiErrorKind Kind = EOWApiErrorKind::None;

	/** The HTTP status; 0 when no answer came or nothing was sent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int32 Status = 0;

	/** The broad class (invalid_request); "" when the answer carried none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Type;

	/** keel's machine-readable code (not_found, rate_limited, resync_required); "" when the answer carried none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Code;

	/** keel's message, the body's start when it was not the envelope, or the client's words. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Message;

	/** The field the error is about, or "". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Param;

	/** The documentation link for Code, or "". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DocUrl;

	/** The body as received, for what the envelope does not model. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString RawBody;

	/** Retry-After was sent and readable (keel sends it on 503, 429 and some 409s). The client never retries on its own. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bHasRetryAfter = false;

	/** Seconds to wait before a retry, when bHasRetryAfter; 0 otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int32 RetryAfter = 0;

	/** Worth sending again later: a 429 or a 5xx, and the three 409s that clear by themselves; never a used-up daily allowance. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bRetryable = false;

	/** With Code resync_required, why: "role" (keep what the copy holds that a walk from the start no longer names) or "view" (drop it); "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ResyncReason;

	/** The line it prints: "OnlyWorlds API 404 [not_found] (field: id): No such character.", "OnlyWorlds refused: ...", "OnlyWorlds transport: ...". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Line;
};

/** One page's options, each sent only when given, in the core's order. The core's OWListOptions. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWListOptions
{
	GENERATED_BODY()

	/** The page's size; 0 or less: the config's PageSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	int64 Limit = 0;

	/** A page's NextCursor, passed back as is; "" for the first page. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Cursor;

	/** Link fields to expand one level into stubs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	TArray<FString> Expand;

	/** A sparse body: only these fields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	TArray<FString> Fields;

	/** Only these elements (1 to 200), in the list's own order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	TArray<FString> Ids;

	/** A membership id, or "owner". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString CreatedBy;

	/** An exact match on name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Name;

	/** A case-insensitive part of name, sent as name__icontains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString NameContains;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Supertype;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Subtype;

	/** Single-link filters by field name (parent_location to an id); an empty value lists the rows with none. Refused before sending: world, a key that is no field name, and a key an option above sends (limit always). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	TMap<FString, FString> Filter;

	/** View as, sent as `as`: the owner's key reads as another reader would (a membership id, a Character id, "public"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString ViewAs;
};

/** One row of the change feed: an upsert carries the element whole, a delete its type and id. A text keel serves as null, or leaves out, is "". */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWChange
{
	GENERATED_BODY()

	/** upsert or delete. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Op;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Id;

	/** The change's place in the world's feed. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int64 ChangeSeq = 0;

	/** On an upsert: when the element changed. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString UpdatedAt;

	/** On a hide's or an unhide's upsert: when, by whom (a membership id; "" for the owner), by which door. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ChangedAt;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ChangedBy;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ChangedVia;

	/** On an upsert: the element's body as JSON text, as ReadElement gives it; "" on a delete. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Element;

	/** On a delete: when, by which membership ("" for the owner), through which door, the deleting client's name. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DeletedAt;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DeletedBy;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DeletedVia;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DeletedClient;

	/** On a delete: why (hidden for an element that left this key's view). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Reason;
};

/** A page of the change feed, or its tip (ReadChangesHead). World-settings edits are not in the feed: compare ReadWorld's updated_at. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWChangesPage
{
	GENERATED_BODY()

	/** Where the next page starts: pass it back as Since. Opaque: never parse or build one (a number keel sends is its text, "17"). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Cursor;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<FOWChange> Changes;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bHasMore = false;

	/** The world's current change_seq; a position past it means a rewind. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int64 Head = 0;

	/** A guest's or a contributor's view epoch, when bHasEpoch. Compare it with what you applied, never with the last poll's. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int64 Epoch = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bHasEpoch = false;

	/** Why the epoch last moved (role or view), or "". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString EpochReason;

	/** On a tip read: keel answered 304 to the tag the client held. A node is its own client and holds no tag, so it is always false here. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bUnchanged = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWJsonAnswer, const FString&, Json, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOWPageAnswer, const TArray<FString>&, Elements, bool, bHasMore, const FString&, NextCursor, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWElementsAnswer, const TArray<FString>&, Elements, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWChangesAnswer, const FOWChangesPage&, Page, const FOWApiError&, Error);

/**
 * What every client node shares: its config, its client, its transport, and its keep. Activated, a node is kept (in
 * the root set, as the engine's UAsyncTaskDownloadImage keeps itself) until it answered, once; then it is let go.
 */
UCLASS(Abstract)
class ONLYWORLDS_API UOWClientNode : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/**
	 * C++ only, before Activate: the transport this node's client sends through, in place of FOWHttpTransport (a C++
	 * caller's own, or a test's fake). Never a pin.
	 */
	void SetTransport(std::shared_ptr<OnlyWorlds::OWTransport> InTransport);

protected:
	/** The client, built from Settings, and the node kept; nullptr when it was activated before (it answers once). */
	OnlyWorlds::OWClient* Begin();
	/** True the first time it is asked: the node answers once. */
	bool Answering();
	/** Lets the node go: out of the root set, ready to be collected. */
	void Release();

	FOWClientConfig Settings;

private:
	std::shared_ptr<OnlyWorlds::OWTransport> Transport;
	std::unique_ptr<OnlyWorlds::OWClient> Client;
	bool bActivated = false;
	bool bAnswered = false;
};

/** The reads that answer one JSON value: the health pulse, the world, Me, the members, one element. */
UCLASS()
class ONLYWORLDS_API UOWClientRead : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The body (Json), or "" and why (Error). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWJsonAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWJsonAnswer OnFailure;

	/** GET /health/: is the server up. keel's answer as served ({"status": "ok", ...}), sent with no credential at all. The core's Health. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientRead* ReadHealth(const FOWClientConfig& Config);

	/** GET /world/: the world's settings as served (its name, its time fields, counts as this reader may read them). The core's GetWorld. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientRead* ReadWorld(const FOWClientConfig& Config);

	/** GET /me/: who this world key is, as served (world, scope, kind, role, membership, character, lenses, reading aids). The one read sent with the PIN; an account token and no key are refused. The core's Me. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientRead* ReadMe(const FOWClientConfig& Config);

	/** GET /members/: the world's roster as a JSON array of keel's rows (id, null for the owner; kind, role, character, status, ...), "[]" for none: the MembersJson UOWMessages takes. A world key reads its own world's, an account token the one World names; no key is refused. The core's Members. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientRead* ReadMembers(const FOWClientConfig& Config);

	/** GET /{type}/{id}/: one element's body as served. Type a type word ("character"), Id one path segment, else refused. The core's Get. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Expand,Fields"))
	static UOWClientRead* ReadElement(const FOWClientConfig& Config, const FString& Type, const FString& Id,
		const TArray<FString>& Expand, const TArray<FString>& Fields, const FString& ViewAs);

	virtual void Activate() override;

private:
	enum class ECall : uint8 { Health, World, Me, Members, Element };

	static UOWClientRead* Make(const FOWClientConfig& Config, ECall Which);
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply);

	ECall Call = ECall::Health;
	FString ElementType;
	FString ElementId;
	TArray<FString> ExpandFields;
	TArray<FString> SparseFields;
	FString ReadAs;
};

/** One cursor page of a type. */
UCLASS()
class ONLYWORLDS_API UOWClientList : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The page's rows, each its body's JSON text, whether more follow, and the cursor to pass back for the next. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWPageAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWPageAnswer OnFailure;

	/** GET /{type}/: one page (Options says what is sent). A page shorter than asked is no end: walk by bHasMore. The core's List. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Options"))
	static UOWClientList* ListElements(const FOWClientConfig& Config, const FString& Type, const FOWListOptions& Options);

	virtual void Activate() override;

private:
	FString ListType;
	FOWListOptions ListOptions;
};

/** Every element of a type, every page. */
UCLASS()
class ONLYWORLDS_API UOWClientListAll : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** Every row, each its body's JSON text, or none and why: a walk that fails answers no half list. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWElementsAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWElementsAnswer OnFailure;

	/** Every page of a type as one list, by has_more, starting at Options' cursor when given. Stops at a page's error, and at a page that hands back the cursor it was sent (kind Transport). The core's ListAll. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Options"))
	static UOWClientListAll* ListAllElements(const FOWClientConfig& Config, const FString& Type, const FOWListOptions& Options);

	virtual void Activate() override;

private:
	FString ListType;
	FOWListOptions ListOptions;
};

/** The change feed: a page, or its tip. A world key's calls: an account token and no key are refused. */
UCLASS()
class ONLYWORLDS_API UOWClientChanges : public UOWClientNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWChangesAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWChangesAnswer OnFailure;

	/** GET /changes/: one page of the world's feed, from Since (a page's Cursor; "" the beginning), at most the page size (1000), only the changes at or after After (an ISO 8601 time, "" for all). The core's Changes. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientChanges* ReadChanges(const FOWClientConfig& Config, const FString& Since, const FString& After);

	/** GET /changes/?head=true: where the feed is now (Head, Cursor, Epoch), with no rows. Never conditional here: a node is its own client and holds no ETag, so each read is a full one. The core's ChangesHead. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientChanges* ReadChangesHead(const FOWClientConfig& Config);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWChangesPage> Reply);

	bool bHead = false;
	FString SinceCursor;
	FString AfterTime;
};
