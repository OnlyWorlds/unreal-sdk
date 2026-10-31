#pragma once
// A thin client for the OnlyWorlds v2 API (keel, onlyworlds.com): reads a world over the network as the core's JSON
// values, the element bodies every reader here takes (OWLens, OWCharter, OWMessage) and the plugin's structs are made
// from (OWFromJson). Deliberately thin: no cache, no sync state, no retry. What it holds is the wire's contract: which
// header carries which credential, where the PIN goes and where it never goes, what a key may send at all (refused
// here, nothing sent, rather than a 401 from keel), opaque cursors, the cursor walk driven by has_more, and keel's
// error envelope as an OWApiError.
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
// bare client, which sends the health pulse alone. The health pulse carries no credential. Mirrors the TypeScript SDK's
// `OwV2Client`.

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
struct OWPage {
    std::vector<OWJsonValue> data;  // the rows, each the element's body as served
    bool has_more = false;
    std::string next_cursor;        // opaque; "" when there is none

    // The keys this reads from keel's answer (held to keel's document by the wire tests).
    static const std::vector<std::string>& Keys();
};

// One row of the change feed: an upsert carries the element whole, a delete its type and id. A text keel serves as
// null, or leaves out, is absent.
struct OWChange {
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
struct OWChangesPage {
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

class OWClient {
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
    // the Domains, the strand). The connection test: the one read sent with the PIN. A world key's call: an account
    // token and a keyless client are refused, nothing sent. Mirrors the TypeScript SDK's `me`.
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

    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace OnlyWorlds
