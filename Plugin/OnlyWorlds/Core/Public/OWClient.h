#pragma once
// A thin client for the OnlyWorlds v2 API (keel, onlyworlds.com): reads a world over the network as the core's JSON
// values, the element bodies every reader here takes (OWLens, OWCharter, OWMessage) and the plugin's structs are made
// from (OWFromJson), and writes its elements back. Deliberately thin: no cache, no sync state, no retry. What it holds
// is the wire's contract: which header carries which credential, where the PIN goes and where it never goes, what a key
// may send at all (refused here, nothing sent, rather than a 401 from keel), opaque cursors, the cursor walk driven by
// has_more, a write's body cleaned of what keel refuses (Sanitize), client-minted ids (OWId), and keel's error envelope
// as an OWApiError.
//
//   OWClientConfig config;
//   config.api_key = "ow_r_...";
//   config.transport = my_transport;                    // the plugin's, on Unreal's HTTP; a test's fake
//   OWClient client(config);                            // client.Error(): "" when built
//   client.ListAll("character", {}, [](OWReply<std::vector<OWJsonValue>> r) {
//       if (!r.Ok()) { Log(r.error->ToString()); return; }   // OnlyWorlds API 401 [invalid_credentials]: ...
//       for (const OWJsonValue& c : r.value) Show(c.Find("name"));
//   });
//
// Every call answers through its handler, called exactly once: before the call returns when nothing is sent (a
// refusal), else when the transport answers. A call's state lives as long as its request, so a client may be destroyed
// while a request is in flight: the handler is still called. Not thread-safe: use a client from one thread, the one the
// transport answers on.
//
// The credential rules are the Unity SDK's OWClient (RequestWithResponseAsync), one rule with the TypeScript SDK's
// OwV2Client: a world key (ow_w_, ow_r_, a 10-digit legacy key, or a key of no known shape) is sent as API-Key, its PIN
// as API-Pin only on GET /me among the reads (keel verifies a PIN wherever it is sent, at a cost) and on every request
// of a legacy key; an account token (ow_a_) as a Bearer, never with the PIN, and only on the reads that name a world
// with world= (set `world`) and the roster; no key at all reads a public world the same way, and with no world is the
// bare client, which sends the health pulse alone. The health pulse carries no credential. The writes (Create, Upsert,
// Patch, Delete, EditLinks, Bulk) go with the PIN, X-OW-Client (the config's `client`) and, on a client the config
// declares an agent, X-OW-Agent; an account token and a keyless client write nothing (refused, nothing sent). Mirrors
// the TypeScript SDK's `OwV2Client`.
//
//   OWClientConfig config;
//   config.api_key = "ow_w_...";
//   config.api_pin = "your-pin";
//   config.client = "my-game";
//   config.transport = my_transport;
//   OWClient writer(config);
//   writer.Create("character", ana, {}, [](OWReply<OWJsonValue> r) { /* r.value: Ana as keel wrote her */ });
//   OWWriteOptions when;
//   when.if_match = seq;                                  // the change_seq as read
//   writer.Patch("character", id, level4, when, [](OWReply<OWJsonValue> r) {
//       if (r.error && r.error->IsPreconditionFailed()) Show(*r.error->Current());   // someone wrote first
//   });

#include "OWApi.h"
#include "OWApiError.h"
#include "OWJson.h"
#include "OWTransport.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// AcceptJoin's answer, in OWJoin.h (include it to call AcceptJoin).
struct OWJoinedMembership;

// What kind of credential a key is, by its prefix: None for no key; Unknown for a key of no known shape, sent as a
// world key is.
enum class OWKeyKind : uint8_t { None, Unknown, Write, Read, Account, Legacy };

// What a call answers: `value` when it succeeded, else `error` says why. `response` is the transport's answer when one
// came (an error's status and headers beside it), absent when nothing was sent.
template <typename T>
struct OWReply {
    T value{};
    std::optional<OWApiError> error;
    std::optional<OWResponse> response;

    bool Ok() const { return !error.has_value(); }
};

template <typename T>
using OWHandler = std::function<void(OWReply<T>)>;

// How a client is built. Mirrors the TypeScript SDK's `OwV2ClientConfig`.
struct OWClientConfig {
    // The key: an ow_w_ (write) or ow_r_ (read) world key, an ow_a_ account token (a Bearer, with `world` for that
    // world's reads), or a 10-digit legacy key. Empty with `world`: a keyless reader of a public world. Empty without:
    // the bare client, which sends Health and refuses every other call.
    std::string api_key;
    // The PIN (or an agent seat's ow_s_ secret): sent only where keel checks it, GET /me among the reads, and on every
    // request of a legacy key. An account token never sends it. Empty for none.
    std::string api_pin;
    // A world's id, for an account token or no key: List, ListAll, Get and GetWorld then read that world, sending
    // world=<id>, and an account token's Members its roster. Given with a world key, or empty or white space, it is
    // refused when the client is built. Absent by default.
    std::optional<std::string> world;
    // keel's API root; trailing slashes are trimmed.
    std::string base_url = "https://www.onlyworlds.com/api/v2";
    // Elements a page (keel clamps a list at 1000 silently; the change feed takes at most 1000). 0 or less: 100.
    int64_t page_size = 100;
    // What the requests go through. Required: the core has no HTTP of its own.
    std::shared_ptr<OWTransport> transport;
    // Declare this client an AI agent: every element write carries X-OW-Agent, and keel records it as agent-made (it
    // stores that the header was there, never its value). It describes the caller, so only the caller sets it: off in
    // a tool people use. An agent seat's key is recorded as an agent without it. Off by default.
    bool agent = false;
    // This client's own name, sent as X-OW-Client on every element write and on no read: keel stores it on the element
    // (changed_client) and on a feed's delete row (deleted_client), so a reader can tell which tool made a change.
    // Absent: OWClient::DefaultClientName. keel's rule, exact: lower case letters, digits and hyphens, 1 to 32
    // characters ([a-z0-9-]{1,32}), no version ("atlas", not "atlas-1.76"); anything else, "" included, is refused when
    // the client is built, nothing trimmed.
    std::optional<std::string> client;
};

// A create's options. Mirrors the TypeScript SDK's `create` options.
struct OWCreateOptions {
    // Sent as Idempotency-Key when not empty; an element without an id then gets the id derived from it
    // (OWId::Keyed, index 0), so a retry with the same key sends the same body and keel replays its first answer.
    std::string idempotency_key;
};

// A conditional write's options (Patch, Delete, EditLinks). Mirrors the TypeScript SDK's `patch` options.
struct OWWriteOptions {
    // The element's change_seq as read, sent as If-Match (quoted): the write lands only if nobody changed the element
    // since, else a 412 (OWApiError::IsPreconditionFailed). A whole number: a negative one is refused before sending.
    // Absent: an unconditional write.
    std::optional<int64_t> if_match;
};

// One link array's change: the ids to add and to remove, each sent only when given (an empty list is sent as one).
// Mirrors the TypeScript SDK's `OwLinkEdit`.
struct OWLinkEdit {
    std::optional<std::vector<std::string>> add;
    std::optional<std::vector<std::string>> remove;
};

// One element of a bulk write: its type's slug and the element (a JSON object; null is an empty one). Mirrors the
// TypeScript SDK's `OwBulkItem`.
struct OWBulkItem {
    std::string type;
    OWJsonValue element;
};

// A bulk write's options. Mirrors the TypeScript SDK's `bulk` options.
struct OWBulkOptions {
    // All or nothing: false by default, as keel's.
    bool atomic = false;
    // Sent as Idempotency-Key when not empty; an element without an id then gets the id derived from the key and its
    // position (OWId::Keyed), so a retry sends the same body and keel replays (OWBulkResult::was_replay).
    std::string idempotency_key;
    // True (sent as create_only): every item with an id is created as Create creates it, never replaced. An id the world
    // already holds answers that item's slot 409 id_conflict (its error's IsIdConflict), and the element stays as it
    // was; the rest of the batch goes on. False (the default) sends no create_only, the body earlier versions sent: an
    // item with an id held replaces it whole. A bool, so a value that is not true or false cannot be given.
    bool create_only = false;
};

// One item's outcome in an OWBulkResult. Mirrors the TypeScript SDK's `OwBulkItemResult`.
struct OW_API OWBulkSlot {
    int status = 0;  // the item's own HTTP status: 201 created, 200 updated, 202 a proposal, else its failure
    // The element's id as served; nullopt when the slot answers none (a failure before the item was identified), as
    // the API document types it (a string or null).
    std::optional<std::string> id;
    // On a success: keel's timestamps, from which a sync baseline is set.
    std::optional<std::string> created_at;
    std::optional<std::string> updated_at;
    // On a failure: the slot's error, kind Api, its status the slot's.
    std::optional<OWApiError> error;
    // On a 202: the proposal row the item became (the element did not change), as served.
    std::optional<OWJsonValue> proposal;

    // Any 2xx. After an atomic request whose `errors` is true nothing was written, yet the items that would have
    // succeeded still say 201 with the ids they would have had: there this means "would have succeeded". Mirrors the
    // Unity SDK's `OWBulkSlot.Succeeded` (the TypeScript SDK reads the status).
    bool Succeeded() const { return status >= 200 && status < 300; }
    // The item became a proposal (202): it counts as Succeeded, but nothing was written until a keeper accepts it.
    // Mirrors the Unity SDK's `OWBulkSlot.IsProposed`.
    bool IsProposed() const { return status == 202; }

    // The keys this reads from a slot of keel's answer (held to keel's document by the wire tests).
    static const std::vector<std::string>& Keys();
};

// What Bulk answers. keel answers 200 even when slots failed: each slot's status is where the truth lives, so check
// Failed (or AllSucceeded) before trusting the write. Slots come back in the order of the items sent. Mirrors the
// TypeScript SDK's `OwBulkResponse`.
struct OW_API OWBulkResult {
    bool errors = false;  // keel's own flag: some slot failed
    std::vector<OWBulkSlot> items;
    // keel replayed the first answer for this Idempotency-Key and wrote nothing (its Idempotent-Replay header, by any
    // case). Never on the wire's body.
    bool was_replay = false;

    // The indices of the slots that did not succeed, in order. Mirrors the Unity SDK's `OWBulkResult.Failed`.
    std::vector<size_t> Failed() const;
    // The 2xx slots. Mirrors the Unity SDK's `OWBulkResult.SucceededCount`.
    size_t SucceededCount() const;
    // True when no slot failed and keel said none did. The Unity SDK's `ThrowIfAnyFailed` test, without the throw.
    bool AllSucceeded() const;

    static const std::vector<std::string>& Keys();
};

// One page's options, each sent only when given, in this order: limit, cursor, expand, fields, ids, created_by, name,
// name__icontains, supertype, subtype, the filter by field name, as last. A list is joined by commas. Mirrors the
// TypeScript SDK's `ListParams`.
struct OWListOptions {
    std::optional<int64_t> limit;     // the page size by default; sent as given
    std::string cursor;               // a page's next_cursor, passed back as is
    std::vector<std::string> expand;  // link fields to expand one level into stubs
    std::vector<std::string> fields;  // a sparse body
    std::vector<std::string> ids;     // only these (1 to 200), in the list's own order
    std::string created_by;           // a membership id, or "owner"
    std::string name;                 // exact match on name
    std::string name_contains;        // a case-insensitive substring of name, sent as name__icontains
    std::string supertype;
    std::string subtype;
    // Single-link filters by field name ({"parent_location": id}); an empty value lists the rows with none. Sent in
    // field order (byte order). Refused before sending: `world` (the config names it), a key that is no field name, and
    // a key an option above sends in the same request (limit always).
    std::map<std::string, std::string> filter;
    // View as, sent as `as`: the owner's key reads as another reader would (a membership id, a Character id for a
    // guest of it, "public"). A member key gets 403 view_as_owner_only.
    std::string view_as;
};

// A get's options, sent in this order when given. Mirrors the TypeScript SDK's `get` options.
struct OWGetOptions {
    std::vector<std::string> expand;
    std::vector<std::string> fields;
    std::string view_as;
};

// A change feed page's options. Mirrors the TypeScript SDK's `changes` options.
struct OWChangesOptions {
    // The previous page's cursor, passed back as is; "" starts from the beginning. Persist the cursor, not a number:
    // a guest key's feed answers a bare change_seq with 409 resync_required.
    std::string since;
    // Only the changes made at or after this time, an ISO 8601 date and time sent as given ("2026-10-22T00:00:00Z"):
    // a filter, not a position, so walk the window by passing each page's cursor back with the same `after`.
    std::string after;
};

// One cursor page of a type: {data, has_more, next_cursor}. A page shorter than asked is no end: keel clamps the limit
// at 1000 silently, so walk by has_more (as ListAll does).
struct OW_API OWPage {
    std::vector<OWJsonValue> data;  // the rows, each the element's body as served
    bool has_more = false;
    std::string next_cursor;        // opaque; "" when there is none

    // The keys this reads from keel's answer (held to keel's document by the wire tests).
    static const std::vector<std::string>& Keys();
};

// One row of the change feed: an upsert carries the element whole, a delete its type and id. A text keel serves as
// null, or leaves out, is absent.
struct OW_API OWChange {
    std::string op;    // upsert or delete
    std::string type;
    std::string id;
    int64_t change_seq = 0;                    // the change's place in the world's feed
    std::optional<std::string> updated_at;     // on an upsert: when the element changed
    // On a hide's or an unhide's upsert row only: when, by whom (a membership id; absent for the owner), by which door.
    std::optional<std::string> changed_at;
    std::optional<std::string> changed_by;
    std::optional<std::string> changed_via;
    std::optional<OWJsonValue> element;        // on an upsert: the element as GET /{type}/{id} serves it
    // On a delete: when, by which membership (absent for the owner), through which door, the deleting client's name.
    std::optional<std::string> deleted_at;
    std::optional<std::string> deleted_by;
    std::optional<std::string> deleted_via;
    std::optional<std::string> deleted_client;
    std::optional<std::string> reason;         // on a delete: why (hidden for an element that left this key's view)

    static const std::vector<std::string>& Keys();
};

// A page of the change feed, or its tip (ChangesHead). World-settings edits are not in the feed: poll GetWorld's
// updated_at for them.
struct OW_API OWChangesPage {
    // Where the next page starts: pass it back as `since`. Opaque: never parse or build one. keel sends a bare number
    // on a tip read; it is its text here ("17").
    std::string cursor;
    std::vector<OWChange> changes;
    bool has_more = false;
    int64_t head = 0;                        // the world's current change_seq; a position past it means a rewind
    std::optional<int64_t> epoch;            // a guest's or a contributor's view epoch; absent for other keys
    std::optional<std::string> epoch_reason; // why the epoch last moved (role or view), or absent
    // On a tip read: keel answered 304 to the tag this client held, so head, cursor and epoch are the last 200's. False
    // on every 200. Never on the wire.
    bool unchanged = false;

    static const std::vector<std::string>& Keys();
};

class OW_API OWClient {
public:
    // Production's API root, the config's default.
    static const char* const DefaultBaseUrl;  // "https://www.onlyworlds.com/api/v2"
    // keel's cap on one page of the change feed; past it keel answers 422, where the element lists clamp.
    static constexpr int64_t MaxChangesLimit = 1000;

    explicit OWClient(OWClientConfig config);
    OWClient(const OWClient&) = delete;
    OWClient& operator=(const OWClient&) = delete;
    OWClient(OWClient&&) noexcept;
    OWClient& operator=(OWClient&&) noexcept;
    ~OWClient();

    // Why the client was not built ("" when it was): a config it refuses. Every call then answers that refusal, kind
    // Refused, and sends nothing.
    const std::string& Error() const;
    const std::string& BaseUrl() const;
    int64_t PageSize() const;
    OWKeyKind KeyKind() const;
    // The world an account token or a keyless client reads, or absent.
    const std::optional<std::string>& World() const;

    // A key's kind by its prefix: ow_w_ Write, ow_r_ Read, ow_a_ Account, ten ASCII digits Legacy, "" None, anything
    // else Unknown. Mirrors the TypeScript SDK's `detectKeyKind`.
    static OWKeyKind KindOf(std::string_view key);

    // GET /health/: is the server up. keel's liveness answer, the body as served ({"status": "ok", ...}). Sent with no
    // credential at all on every client, and no world=. Mirrors the TypeScript SDK's `health`.
    void Health(OWHandler<OWJsonValue> done);
    // GET /world/: the world's settings, the body as served (its name, its time fields, counts as this reader may read
    // them). On a client with a world set it reads that world (world=<id>). World-settings edits are not in the change
    // feed: compare updated_at. Mirrors the TypeScript SDK's `getWorld`.
    void GetWorld(OWHandler<OWJsonValue> done);
    // GET /me/: who this world key is, the body as served: {world, scope, kind, role, membership, character, lenses,
    // name?, front_door, reading_aids}; OWCharter::Served(reply.value) reads its reading aids (the Charter, the Guide,
    // the Domains, the strand). The one read sent with the PIN, so a wrong PIN shows on this call first, which makes it
    // the connection test for a key that can write; beside a key that cannot (a read key, a player's), onlyworlds.com
    // never reads a PIN. A world key's call: an account token and a keyless client are refused, nothing sent. Mirrors
    // the TypeScript SDK's `me`.
    void Me(OWHandler<OWJsonValue> done);
    // GET /members/: the world's roster, the rows of keel's {data} (no paging) as a JSON array, each row as served: id
    // (the membership id created_by holds, null for the owner), kind, role, character, status (removed rows stay),
    // lenses, lenses_from, and name where this key is served it; an answer without rows is an empty array. The list
    // OWMessage takes as `members`. A world key reads its own world's; an account token the one `world` names (refused
    // without it); a keyless client is refused: the roster has no public reader. Mirrors the TypeScript SDK's `members`.
    void Members(OWHandler<OWJsonValue> done);

    // GET /{type}/: one cursor page of a type (OWListOptions says what is sent). On a client with a world set it reads
    // that world, world=<id> after the page's own parameters. Mirrors the TypeScript SDK's `list`.
    void List(const std::string& type, const OWListOptions& options, OWHandler<OWPage> done);
    // Every element of a type, every page, as one list: List page by page, each with the same options and the last
    // page's cursor, until a page says has_more false (never by its size: a short page is no end). Starts at
    // options.cursor when given, else the first page. Stops, answering no half list: at a page's error; at has_more true
    // with no cursor (an end, rather than a loop); at a page that hands back the cursor it was sent (an error of kind
    // Transport, rather than reading it forever). `on_page`, when set, is called after each page with the page's number
    // and the count so far. Mirrors the TypeScript SDK's `listAll`.
    void ListAll(const std::string& type, const OWListOptions& options, OWHandler<std::vector<OWJsonValue>> done,
                 std::function<void(int, size_t)> on_page = {});
    // GET /{type}/{id}/: one element's body as served (OWGetOptions says what is sent). On a client with a world set it
    // reads that world. Mirrors the TypeScript SDK's `get`.
    void Get(const std::string& type, const std::string& id, const OWGetOptions& options, OWHandler<OWJsonValue> done);

    // GET /changes/: one page of the world's change feed: since, limit (the page size, at most 1000), after. A 409
    // resync_required (IsResyncRequired) drops the tip read's tag, so the next ChangesHead is a full one. A world key's
    // call. Mirrors the TypeScript SDK's `changes`.
    void Changes(const OWChangesOptions& options, OWHandler<OWChangesPage> done);
    // GET /changes/?head=true: where the feed is now, with no page: head (the world's change_seq), cursor (a position to
    // pass as `since` that re-sends nothing at the head), epoch (a guest's or a contributor's view epoch). Conditional:
    // the client keeps the last 200's ETag and sends it as If-None-Match, exactly as received; when nothing moved keel
    // answers 304 (refunded against the request budget) and this answers the last 200's head, cursor and epoch with
    // unchanged true. Compare head and epoch with what you applied, never with the last poll's; the interval is yours,
    // the client never polls on its own. The tag is per client; a 409 resync_required from this or Changes drops it. A
    // 304 while nothing is held is an error of kind Api with status 304: the client never makes up a head. Mirrors the
    // TypeScript SDK's `changesHead`.
    void ChangesHead(OWHandler<OWChangesPage> done);

    // -- Joining by link --------------------------------------------------------------------------------------------

    // POST /account/join: accept a person's invite link as the account this client's token belongs to, 201 with the
    // new membership (OWJoinedMembership, in OWJoin.h). `code` is the link's ow_j_ code (OWJoin::ParseLink reads it
    // out of a pasted link), sent as given in {"code"} alone, with the Bearer and never a PIN: accepting needs none, for
    // any role. keel's refusals leave the code unspent: every bad code (unknown, used, revoked or expired), and a code
    // made for an AI agent, is the one 404 not_found (OWApiError::IsNotFound); an account already in the world is 409
    // already_member (IsAlreadyMember). Refused, nothing sent: any key but an account token (ow_a_; a player's or an
    // agent's code is redeemed with no key, OWJoin::Redeem) and an empty code. Mirrors the TypeScript SDK's
    // `acceptJoin` (4.12.0) and the Unity SDK's `AcceptJoinAsync`.
    void AcceptJoin(const std::string& code, OWHandler<OWJoinedMembership> done);

    // -- Element writes ---------------------------------------------------------------------------------------------
    //
    // Each takes the element as a JSON object, never changed here: the body is a copy cleaned by Sanitize, written
    // compact (numbers as JSON.stringify writes them). A known-of stub (IsKnownOf) is refused before sending: its empty
    // fields are not the element's values, and writing it would blank them. Every write goes with the PIN, X-OW-Client,
    // and X-OW-Agent on an agent client; an account token and a keyless client are refused, nothing sent. The type, the
    // id and a link field are each one path segment, checked and escaped as the reads' are.

    // POST /{type}/: create one element, answering it as keel wrote it (the body as served). An element with no id
    // (absent, null or "") gets one minted here: a UUIDv7, or with options.idempotency_key the id derived from the key
    // (OWId::Keyed, index 0), so a retry with the same key sends the same body and keel replays its first answer (24
    // hours), writing nothing; the same key with another body is a 409 (IsIdempotencyConflict). The key goes as
    // Idempotency-Key only when given. An unkeyed retry of an element without an id creates a second one; one that
    // carries its id answers 409 id_conflict when it already landed (IsIdConflict). Never reuse a key for a new write:
    // an undo that re-creates under the first key gets the old answer back. Mirrors the TypeScript SDK's `create`.
    void Create(const std::string& type, const OWJsonValue& element, const OWCreateOptions& options,
                OWHandler<OWJsonValue> done);
    // PUT /{type}/{id}/: upsert by the caller's own id, creating the element under `id` or writing it whole: the
    // local-first write. keel's document declares no Idempotency-Key or If-Match here, so none is sent. An agent seat's
    // PUT on an existing element, in a world that takes its writes as proposals, answers 202: an error with IsProposed,
    // and the element did not change. Mirrors the TypeScript SDK's `upsert`.
    void Upsert(const std::string& type, const std::string& id, const OWJsonValue& element,
                OWHandler<OWJsonValue> done);
    // PATCH /{type}/{id}/: a partial update, answering the element whole. Destructive on the keys sent: whatever is in
    // `partial` replaces what keel holds, so send only what changed; a key left out is left alone. Never PATCH a link
    // array to add one id: it replaces the array whole, so a writer adding one erases what another added since; change
    // it with EditLinks. options.if_match as OWWriteOptions says. A 202 is a proposal, as on Upsert; a stale If-Match is
    // still a 412 first. Mirrors the TypeScript SDK's `patch`.
    void Patch(const std::string& type, const std::string& id, const OWJsonValue& partial,
               const OWWriteOptions& options, OWHandler<OWJsonValue> done);
    // DELETE /{type}/{id}/: delete an element. keel writes a tombstone and takes the id out of every other element's
    // links in the same write. Answers null (an element already gone is the same 204). options.if_match as on Patch: a
    // stale one is a 412 and nothing is deleted. Never a proposal. Mirrors the TypeScript SDK's `delete`.
    void Delete(const std::string& type, const std::string& id, const OWWriteOptions& options,
                OWHandler<OWJsonValue> done);
    // POST /{type}/{id}/links/{field} with {add, remove}: change one link array, merged by keel (ids already there or
    // already gone are fine), answering the element whole. The one correct way to change a link array. options.if_match
    // as on Patch: a link write moves the element too. A 202 is a proposal, as on Upsert. Mirrors the TypeScript SDK's
    // `editLinks`.
    void EditLinks(const std::string& type, const std::string& id, const std::string& field, const OWLinkEdit& edit,
                   const OWWriteOptions& options, OWHandler<OWJsonValue> done);
    // POST /bulk/: write many elements of mixed types in one request, {items: [{type, element}], atomic}, and
    // create_only: true when options.create_only is (each item with an id created, never replaced). Each element
    // is cleaned as a single write's is, and one without an id gets one by its position: under
    // options.idempotency_key the id derived from the key and the position, so a retry sends the same body and replays
    // (was_replay), else a fresh UUIDv7. keel answers 200 even when slots failed: read the slots. Refused, nothing
    // sent: no items, an item without a type, an element that is not an object, a known-of stub (each named by its
    // index). Mirrors the TypeScript SDK's `bulk`.
    void Bulk(const std::vector<OWBulkItem>& items, const OWBulkOptions& options, OWHandler<OWBulkResult> done);

    // The name sent as X-OW-Client when the config gives none.
    static const char* const DefaultClientName;  // "unreal-sdk"
    // The name this client sends as X-OW-Client on element writes.
    const std::string& ClientName() const;
    // True when the config declared this client an AI agent (X-OW-Agent on every element write).
    bool Agent() const;

    // The keys a write never sends: keel refuses them (a 422) or names them itself (world, from the key): world, type,
    // created_at, updated_at, change_seq, known_of. A blacklist that must never become a whitelist: extension keys
    // (x_*) pass through every write untouched. Mirrors the TypeScript SDK's `READ_ONLY_FIELDS`.
    static const std::vector<std::string>& ReadOnlyFields();
    // True for a known-of stub (known_of is the JSON true), never a whole element: absent, false or any other value is
    // a whole element. Mirrors the TypeScript SDK's `isKnownOf`.
    static bool IsKnownOf(const OWJsonValue& body);
    // A write's body: a copy of `payload` with ReadOnlyFields left out and each key `type`'s schema declares a scalar
    // string (the base name, description, supertype, subtype, image_url, and the type's own) sent as "" where it is
    // null: "" is the wire's unset, and keel has no null to store (string-empty-is-unset). A single link's null, an
    // x_* value and an absent key are left as they are (on a PATCH an absent key means "leave it alone"). Anything but
    // an object is answered as given. Mirrors the TypeScript SDK's `sanitizePayload`, with the Unity SDK's null-string
    // rule (`Sanitize`), which the TypeScript SDK has not.
    static OWJsonValue Sanitize(const OWJsonValue& payload, std::string_view type);

    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace OnlyWorlds
