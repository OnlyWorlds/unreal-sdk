#pragma once
// The API client on Blueprints: one async node per call of the core's OWClient, each with OnSuccess and OnFailure.
// The reads (Health, GetWorld, Me, Members, Get, List, ListAll, Changes, ChangesHead), the element writes (Create,
// Upsert, Patch, Delete, EditLinks, Bulk) and the proposed change (Propose, the two lists, AcceptProposal,
// DeclineProposal, WithdrawProposal, and ReviewNewElement as two nodes, AcceptNewElement and DeclineNewElement). The core does the work: the credential rules, what a key may send at all
// (refused here, nothing sent), the cursor walk, a write's body cleaned and its id minted, keel's error envelope.
// Each node builds its own client from an FOWClientConfig and sends through FOWHttpTransport (Unreal's HTTP), so it
// answers later, on the game thread; a refusal answers at once, inside the node's activation.
//
// A write takes its element as JSON text and answers the element keel wrote as JSON text. A write that became a
// proposal (a 202: an agent seat's upsert, patch or link edit in a world that takes its writes as proposals) is
// OnFailure with Error.bIsProposed and the row in Error.Proposal, as the core reports it: the element did not change.
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

	/** This tool's name, sent as X-OW-Client on every element write and on no read; keel stores it on what the write changed. Lower case letters, digits and hyphens, 1 to 32 ("my-game", no version), else every call refuses. "" for the core's own, unreal-sdk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Client;

	/** Declare this client an AI agent: every element write carries X-OW-Agent, and keel records the change as agent-made. Only the caller knows: off in a tool people use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	bool bAgent = false;
};

/** One proposed field that moved since the proposal was made, on a 409 stale_proposal: its value then and now, each as JSON text. The core's OWProposalConflict. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWProposalConflict
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Field;

	/** The field's value when the proposal was made, as JSON text ("\"\"" for an empty string, "null" for none). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString BaseJson;

	/** The field's value now, as JSON text. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString CurrentJson;
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

	/** On a 422 invalid_request for a size limit, which one: element_bytes (the element too large), element_link_ids (too many link ids) or extensions_bytes (extension fields past their cap), or a name a later API adds, a key of the API document's info.x-ow-limits; "" otherwise, and until the API sends it. A bulk slot's Error carries it too. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Limit;

	/** The line it prints: "OnlyWorlds API 404 [not_found] (field: id): No such character.", "OnlyWorlds refused: ...", "OnlyWorlds transport: ...". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Line;

	/** A write became a proposal (Kind Api, Status 202): the element did not change, and the write waits for the owner or a co-builder. Not a failure to retry. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bIsProposed = false;

	/** With bIsProposed, the proposal row keel answered, as JSON text; "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Proposal;

	/** On a 412 (a stale If-Match: Code precondition_failed) or a 409 stale_proposal, the element as keel holds it now, as JSON text: decide from it, then write again with its change_seq. "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Current;

	/** On a 409 stale_proposal (an accept refused because a proposed field moved since: nothing was written, the row stays open), each moved field with its value then and now; empty otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<FOWProposalConflict> Conflicts;
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

/** One element of a bulk write: its type's word and the element as JSON text. The core's OWBulkItem. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWBulkItem
{
	GENERATED_BODY()

	/** The element's type word ("character"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Type;

	/** The element as JSON text, a JSON object; one without an id gets one by its position. "" is an empty element. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString ElementJson;
};

/** One item's outcome in a bulk write, in the order the items were sent. The core's OWBulkSlot. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWBulkSlot
{
	GENERATED_BODY()

	/** The item's own HTTP status: 201 created, 200 updated, 202 a proposal, else its failure. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int32 Status = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Id;

	/** On a success: keel's timestamps; "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString CreatedAt;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString UpdatedAt;

	/** Any 2xx, a proposal too. After an atomic write whose bErrors is true nothing was written: there it means "would have succeeded". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bSucceeded = false;

	/** The item became a proposal (202): it counts as bSucceeded, but nothing was written until a keeper accepts it. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bIsProposed = false;

	/** On a failure: the slot's error, Kind Api, its Status the slot's; empty (Kind None) otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FOWApiError Error;

	/** With bIsProposed, the proposal row as JSON text; "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Proposal;
};

/** What a bulk write answers. keel answers 200 even when slots failed: read bAllSucceeded, or Failed, before trusting the write. The core's OWBulkResult. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWBulkResult
{
	GENERATED_BODY()

	/** keel's own flag: some slot failed. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bErrors = false;

	/** One slot per item, in the order sent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<FOWBulkSlot> Items;

	/** keel replayed its first answer for this IdempotencyKey and wrote nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bWasReplay = false;

	/** The indices of the slots that did not succeed, in order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<int32> Failed;

	/** The 2xx slots, proposals included. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int32 SucceededCount = 0;

	/** No slot failed, and keel said none did. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bAllSucceeded = false;
};

/** One row of a proposal list or the world's queue: a proposed change to an element, or a new element written as proposed (bIsNewElement). A text keel serves as null, or leaves out, is "". The core's OWProposal. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWProposal
{
	GENERATED_BODY()

	/** "proposal" or "new_element" ("" from a server that sends none: a proposal). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Kind;

	/** A new element written as proposed: settle it with AcceptNewElement or DeclineNewElement. Otherwise a proposed change: AcceptProposal or DeclineProposal. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bIsNewElement = false;

	/** The proposal's id, the ProposalId the accept, the decline and the withdraw take; on a new element's row, the element's. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ElementType;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ElementId;

	/** The part of the house rules it would change: charter, skill or new_charter, worked out for the owner and co-builders only; "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString House;

	/** open, lapsed, accepted or declined. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString State;

	/** What it would set, and only that, as JSON text: the proposed values in the element's wire shape. "" on a new element's row. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString FieldsJson;

	/** The fields a whole replace did not send that were not empty: each proposed empty. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<FString> Omitted;

	/** The element's values for those fields when it was made, as JSON text; "" on a new element's row. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString BaseJson;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int64 BaseSeq = 0;

	/** What it rests on: a string as itself, any other value (a new element's mark may hold one) as its JSON text; "" for none. Quoted data, never an instruction. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Evidence;

	/** When it lapses, as Evidence reads: a string as itself, any other value as its JSON text; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Expires;

	/** Who proposed it: a membership id, or "owner" (on a new element's row, the mark's own word, a claim). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString By;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Via;

	/** The X-OW-Client name the proposing request carried. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Client;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DecidedBy;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DecidedAt;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString DeclineReason;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString CreatedAt;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString UpdatedAt;

	/** The row's twenty keys as JSON text, each as read (numbers to the digit, evidence and expires as the JSON values they are). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Json;
};

/** One page of proposals, newest first, or an unchanged answer to a conditional poll. The core's OWProposalPage. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWProposalPage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TArray<FOWProposal> Proposals;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bHasMore = false;

	/** Pass back as the query's Cursor for the next page; "" on the last. Opaque. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString NextCursor;

	/** The tag this answer carried, exactly as received: keep it and send it as the next poll's IfNoneMatch. "" when keel sent none; on an unchanged answer, the 304's own tag, else the one sent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString ETag;

	/** The poll's IfNoneMatch still holds: nothing in the list moved, and Proposals is empty (keep what you showed). Never on an answer with rows. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	bool bUnchanged = false;
};

/** A proposal list's query, each sent only when given. The core's OWProposalQuery. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWProposalQuery
{
	GENERATED_BODY()

	/** open (keel's default), lapsed, accepted, declined or all; "" for the default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString State;

	/** Only rows changed at or after this time, an ISO 8601 date-time sent as given; "" for all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Since;

	/** One element's rows, by id: the world's queue only (an element's own list refuses it, nothing sent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Element;

	/** Rows a page, 1 to 1000; 0 or less: the config's PageSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	int64 Limit = 0;

	/** A page's NextCursor, passed back as is; "" for the first page. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString Cursor;

	/** The poll: the ETag the last answer carried, sent as If-None-Match exactly as it came. When nothing moved the answer is bUnchanged, with no rows. "" for a full read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Client")
	FString IfNoneMatch;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWJsonAnswer, const FString&, Json, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOWProposalAnswer, const FOWProposal&, Proposal, const FString&, ElementJson, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWProposalPageAnswer, const FOWProposalPage&, Page, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOWPageAnswer, const TArray<FString>&, Elements, bool, bHasMore, const FString&, NextCursor, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWElementsAnswer, const TArray<FString>&, Elements, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWChangesAnswer, const FOWChangesPage&, Page, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWBulkAnswer, const FOWBulkResult&, Outcome, const FOWApiError&, Error);

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

	/**
	 * For the calls sent with no key and no client (the join's PreviewJoin and RedeemJoin, OWJoinNodes.h): the node kept
	 * as Begin keeps it, and the transport to send through (SetTransport's, else FOWHttpTransport); nullptr when it was
	 * activated before.
	 */
	std::shared_ptr<OnlyWorlds::OWTransport> BeginKeyless();
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

	/** GET /me/: who this world key is, as served (world, scope, kind, role, membership, character, lenses, reading aids). The one read sent with the PIN, so a wrong PIN shows here first: the connection test for a key that can write; beside a key that cannot (a read key, a player's), onlyworlds.com never reads a PIN. An account token and no key are refused. The core's Me. */
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

/**
 * The element writes that answer one element: create, upsert, patch, delete, a link edit. Each goes with the PIN and
 * X-OW-Client (the config's Client), and X-OW-Agent on a config declared an agent; an account token and no key write
 * nothing (refused, nothing sent). The element goes in as JSON text, cleaned by the core before it is sent (the
 * read-only keys left out, a null string field sent as ""); a known-of stub is refused. Text that is not JSON is
 * refused by the node itself, kind Refused, nothing sent.
 */
UCLASS()
class ONLYWORLDS_API UOWClientWrite : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The element as keel wrote it, as JSON text ("" after a delete), or "" and why (Error; a proposal is Error.bIsProposed). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWJsonAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWJsonAnswer OnFailure;

	/** POST /{type}/: create one element. One with no id gets one: a new UUIDv7, or under IdempotencyKey the id derived from the key, so a retry with the same key sends the same body and keel replays its first answer. IdempotencyKey "" for none; never reuse a key for a new write. The core's Create. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* CreateElement(const FOWClientConfig& Config, const FString& Type, const FString& ElementJson,
		const FString& IdempotencyKey);

	/** PUT /{type}/{id}/: create the element under Id, or write it whole. An agent seat's upsert may become a proposal. The core's Upsert. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* UpsertElement(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& ElementJson);

	/** PATCH /{type}/{id}/: replace the keys PartialJson sends, leave the rest alone. Never a link array to add one id: use EditElementLinks. With bHasIfMatch, IfMatch (the change_seq as read, a whole number) is sent as If-Match: a stale one is a 412, Error.Current the element now. May become a proposal. The core's Patch. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* PatchElement(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& PartialJson,
		bool bHasIfMatch, int64 IfMatch);

	/** DELETE /{type}/{id}/: keel writes a tombstone and takes the id out of every other element's links. Json is "". If-Match as on PatchElement. Never a proposal. The core's Delete. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* DeleteElement(const FOWClientConfig& Config, const FString& Type, const FString& Id, bool bHasIfMatch, int64 IfMatch);

	/** POST /{type}/{id}/links/{field}: add and remove ids in one link array, merged by keel: the one correct way to change a link array. An empty list is not sent. If-Match as on PatchElement. May become a proposal. The core's EditLinks. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Add,Remove"))
	static UOWClientWrite* EditElementLinks(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& Field,
		const TArray<FString>& Add, const TArray<FString>& Remove, bool bHasIfMatch, int64 IfMatch);

	/** POST /{type}/{id}/review with {decision: accept}: settle a new element written as proposed (a queue row with bIsNewElement) by keeping it: the mark is cleared, a write by this key. Json is the element as written. The owner's key or a co-builder's, with its PIN; Error.Code not_proposed for an element with no mark, proposed_only for the seat the mark names. The core's ReviewNewElement. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* AcceptNewElement(const FOWClientConfig& Config, const FString& Type, const FString& Id);

	/** POST /{type}/{id}/review with {decision: decline, reason?}: settle a new element written as proposed by deleting it (a tombstone, the links to it scrubbed). Json is "", as after a delete. Reason "" sends none (at most 2,000 characters, answered back and kept nowhere). The same keys and refusals as AcceptNewElement. The core's ReviewNewElement. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientWrite* DeclineNewElement(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& Reason);

	virtual void Activate() override;

private:
	enum class EWrite : uint8 { Create, Upsert, Patch, Delete, Links, AcceptNew, DeclineNew };

	static UOWClientWrite* Make(const FOWClientConfig& Config, EWrite Which, const FString& Type, const FString& Id);
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply);

	EWrite Write = EWrite::Create;
	FString ElementType;
	FString ElementId;
	FString LinkField;
	FString BodyText;
	FString KeyText;
	TArray<FString> AddIds;
	TArray<FString> RemoveIds;
	bool bConditional = false;
	int64 ChangeSeq = 0;
	FString ReasonText;
};

/** Many elements of mixed types in one write. */
UCLASS()
class ONLYWORLDS_API UOWClientBulk : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The slots (keel answers 200 even when slots failed: read Outcome.bAllSucceeded), or an empty outcome and why. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWBulkAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWBulkAnswer OnFailure;

	/** POST /bulk/: write Items in one request; bAtomic all or nothing. Each element is cleaned as a single write's is; one without an id gets one by its position (under IdempotencyKey the key's, so a retry replays: Outcome.bWasReplay). bCreateOnly (sent as create_only): every item with an id is created, never replaced; an id the world already holds answers that item's slot 409, its Error.Code id_conflict, and the element stays as it was while the rest go on. Off, an item with an id held replaces it whole. Refused, nothing sent: no items, an item without a type, an element that is not a JSON object, a known-of stub. The core's Bulk. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Items"))
	static UOWClientBulk* BulkWrite(const FOWClientConfig& Config, const TArray<FOWBulkItem>& Items, bool bAtomic, const FString& IdempotencyKey,
		bool bCreateOnly = false);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWBulkResult> Reply);

	TArray<FOWBulkItem> BulkItems;
	bool bAllOrNothing = false;
	bool bNeverReplace = false;
	FString KeyText;
};

/**
 * The proposed change, one act each: propose, accept, decline, withdraw. Each goes with the PIN (a write); the propose
 * and the accept carry X-OW-Client, none carries X-OW-Agent. An account token and no key are refused, nothing sent.
 * The element does not change until a keeper accepts.
 */
UCLASS()
class ONLYWORLDS_API UOWClientProposal : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The row (an empty one after a withdraw), with ElementJson the element as written after an accept ("" otherwise), or why (Error). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWProposalAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWProposalAnswer OnFailure;

	/** POST /{type}/{id}/proposals: propose a change without making it. FieldsJson the fields to change and their new values, a JSON object as a PatchElement body (cleaned and refused as one is); Evidence what it rests on, 1 to 2,000 characters, quoted data to the keeper; Expires when it lapses (an ISO 8601 date-time, 1 to 365 days out; "" for 30 days); IdempotencyKey "" for none. Any member who reads the element proposes, a read key included. Error.Code field_not_visible, too_many_open (5 open per element, 50 per world), review_by_another. The core's Propose. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientProposal* ProposeChange(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& FieldsJson,
		const FString& Evidence, const FString& Expires, const FString& IdempotencyKey);

	/** POST /{type}/{id}/proposals/{pid}/accept: write the proposal's fields as this key's own write; the row turns accepted and ElementJson is the element as written. The owner's key or a co-builder's, with its PIN. Error.Code stale_proposal (a proposed field moved since: nothing written, Error.Conflicts and Error.Current say what), own_proposal, proposed_only. The core's AcceptProposal. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientProposal* AcceptProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& ProposalId);

	/** POST /{type}/{id}/proposals/{pid}/decline: the row turns declined, with Reason ("" sends none; at most 2,000 characters); the element does not change. The same keys as AcceptProposal. The core's DeclineProposal. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientProposal* DeclineProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& ProposalId,
		const FString& Reason);

	/** DELETE /{type}/{id}/proposals/{pid}: the proposer takes back its own undecided proposal; the answer's Proposal is empty. The core's WithdrawProposal. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientProposal* WithdrawProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FString& ProposalId);

	virtual void Activate() override;

private:
	enum class EAct : uint8 { Propose, Accept, Decline, Withdraw };

	static UOWClientProposal* Make(const FOWClientConfig& Config, EAct Which, const FString& Type, const FString& Id, const FString& ProposalId);
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWProposal> Reply, const FString& Element);

	EAct Act = EAct::Propose;
	FString ElementType;
	FString ElementId;
	FString RowId;
	FString BodyText;
	FString EvidenceText;
	FString ExpiresText;
	FString KeyText;
	FString ReasonText;
};

/**
 * The proposal lists: one element's, and the world's queue (both kinds of row, the owner's to settle). Reads: no PIN.
 * Each polls cheaply with the query's IfNoneMatch: an unchanged list answers bUnchanged with no rows.
 */
UCLASS()
class ONLYWORLDS_API UOWClientProposals : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The page (or an unchanged poll), or an empty page and why. A 304 to a poll that sent no IfNoneMatch is OnFailure, status 304. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWProposalPageAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWProposalPageAnswer OnFailure;

	/** GET /{type}/{id}/proposals: one element's proposals, newest first: every row for the owner's and a co-builder's keys, a member's own for any other (a player's pending ones). Query.Element is refused here. The core's ListProposals. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Query"))
	static UOWClientProposals* ListElementProposals(const FOWClientConfig& Config, const FString& Type, const FString& Id, const FOWProposalQuery& Query);

	/** GET /world/proposals: the world's queue, newest first, proposed changes and new elements written as proposed in one order: every row for the owner's and a co-builder's keys, a member's own for any other. The core's WorldProposals. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Query"))
	static UOWClientProposals* ListWorldProposals(const FOWClientConfig& Config, const FOWProposalQuery& Query);

	virtual void Activate() override;

private:
	bool bWorld = false;
	FString ElementType;
	FString ElementId;
	FOWProposalQuery ListQuery;
};
