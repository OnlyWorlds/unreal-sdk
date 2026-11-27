// OWSync through the fake transport (fake_transport.h): no test touches the network. Ported test by test from the
// Godot SDK's tests/test_sync.gd, itself the Unity SDK's OWSyncTests with one test of Godot's own, each named as it is
// there; then Unreal's own. The shapes scripted below are keel's: an owner's tip cursor is the bare change_seq as a
// number; a guest's is "seq:<max uuid>:epoch", with the epoch beside it; a page's cursor is "seq:id" (a guest's
// "seq:id:epoch"). The keys they use are held to keel's API document by test_client_wire.cpp. The feed's rules: keel's
// changes page, https://onlyworlds.github.io/docs/development/api/changes

#include "ow_test.h"
#include "fake_transport.h"

#include "OWAddress.h"
#include "OWClient.h"
#include "OWJson.h"
#include "OWSync.h"
#include "OWWorldCache.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";
const std::string MaxUuid = "ffffffff-ffff-ffff-ffff-ffffffffffff";
const std::string IdA = "069620b9-0794-708d-8000-00000000000a";
const std::string IdB = "069620b9-0794-708d-8000-00000000000b";
const char* const EmptyPage = "{\"data\":[],\"has_more\":false,\"next_cursor\":null}";

std::shared_ptr<FakeTransport> Fake() { return std::make_shared<FakeTransport>(); }

OWClient Client(const std::shared_ptr<FakeTransport>& fake) {
    OWClientConfig config;
    config.api_key = "ow_w_test";
    config.api_pin = "1234";
    config.base_url = Base;
    config.transport = fake;
    return OWClient(std::move(config));
}

OWWorldCache Cache() { return OWWorldCache("w-1", "Test World"); }

OWJsonValue Body(const std::string& text) { return OWJson::Parse(text).value; }

void Hold(OWWorldCache& cache, const std::string& id, const std::string& name, const std::string& type = "character") {
    cache.Upsert(type, Body("{\"id\":\"" + id + "\",\"name\":\"" + name + "\"}"));
}

std::string Name(const OWWorldCache& cache, const std::string& id) {
    const OWCachedElement* e = cache.Get(id);
    const OWJsonValue* n = e ? e->body.Find("name") : nullptr;
    return n && n->IsString() ? n->Text() : "";
}

std::string Head(int64_t head) {
    std::string h = std::to_string(head);
    return "{\"cursor\":" + h + ",\"head\":" + h + ",\"changes\":[],\"has_more\":false,\"epoch\":null}";
}

std::string GuestTip(int64_t head, int64_t epoch) {
    return std::to_string(head) + ":" + MaxUuid + ":" + std::to_string(epoch);
}

std::string GuestHead(int64_t head, int64_t epoch) {
    return "{\"cursor\":\"" + GuestTip(head, epoch) + "\",\"head\":" + std::to_string(head) +
           ",\"changes\":[],\"has_more\":false,\"epoch\":" + std::to_string(epoch) + "}";
}

std::string Upsert(const std::string& id, int64_t seq, const std::string& name) {
    return "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"" + id + "\",\"change_seq\":" + std::to_string(seq) +
           ",\"updated_at\":\"2026-10-16T00:00:00Z\",\"element\":{\"id\":\"" + id + "\",\"name\":\"" + name +
           "\",\"level\":3}}";
}

void EnqueueBaseline(FakeTransport& fake, const std::string& head) {
    fake.Then(200, head);
    for (size_t i = 0; i < 22; ++i) fake.Then(200, EmptyPage);
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The since parameter of a request, decoded, or "<absent>".
std::string Since(const OWRequest& call) {
    size_t q = call.url.find('?');
    if (q == std::string::npos) return "<absent>";
    std::string query = call.url.substr(q + 1);
    size_t at = 0;
    while (at <= query.size()) {
        size_t amp = query.find('&', at);
        std::string pair = query.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
        if (pair.rfind("since=", 0) == 0) {
            std::string raw = pair.substr(6), out;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '%' && i + 2 < raw.size() && Hex(raw[i + 1]) >= 0 && Hex(raw[i + 2]) >= 0) {
                    out += static_cast<char>(Hex(raw[i + 1]) * 16 + Hex(raw[i + 2]));
                    i += 2;
                } else {
                    out += raw[i];
                }
            }
            return out;
        }
        if (amp == std::string::npos) break;
        at = amp + 1;
    }
    return "<absent>";
}

bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

template <typename T>
struct Got {
    OWReply<T> reply;
    int calls = 0;
    OWHandler<T> H() {
        return [this](OWReply<T> r) {
            reply = std::move(r);
            ++calls;
        };
    }
};

// The sync's result, with a failure recorded when it did not answer once with a value.
OWSyncResult Result(owtest::T& t, const Got<OWSyncResult>& g) {
    t.Eq(g.calls, 1, "the sync answered once");
    t.Ok(g.reply.Ok(), "the sync answered: " + (g.reply.error ? g.reply.error->ToString() : std::string()));
    return g.reply.Ok() ? g.reply.value : OWSyncResult();
}

std::string Resync(const char* reason) {
    return std::string("{\"error\":{\"type\":\"invalid_request\",\"code\":\"resync_required\",\"param\":\"since\","
                       "\"message\":\"m\"") +
           (reason ? std::string(",\"reason\":\"") + reason + "\"" : std::string()) + "}}";
}

// A tip, then a 409 on the walk from the cache's cursor, then a baseline naming only `named`.
void EnqueueReset(FakeTransport& fake, const std::string& tip, const std::string& resync, const std::string& named) {
    fake.Then(200, tip);
    fake.Then(409, resync);
    fake.Then(200, tip);
    for (const char* type : OWAddress::TypeWords)
        fake.Then(200, std::string(type) == "character"
                           ? "{\"data\":[{\"id\":\"" + named + "\",\"name\":\"Fresh\"}],\"has_more\":false}"
                           : std::string(EmptyPage));
}

// A copy holding two Characters: one the next walk names again (a), one it no longer names (b).
OWWorldCache CacheTwo(int64_t cursor, const std::string& token, std::optional<int64_t> epoch) {
    OWWorldCache cache = Cache();
    Hold(cache, "a", "Old A");
    Hold(cache, "b", "Out of view");
    cache.SetCursor(cursor, token, epoch, "t");
    return cache;
}

}  // namespace

// -- Baseline ---------------------------------------------------------------------------------------------------------

OW_TEST(sync_baseline_reads_the_tip_before_fetching) {
    // Order matters: taking the tip AFTER the walk means any change landing during it sits silently below the recorded
    // cursor and never syncs again.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    EnqueueBaseline(*fake, Head(500));
    Got<OWSyncResult> g;
    OWSync::Baseline(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Ok(!fake->calls.empty() && Contains(fake->calls[0].url, "head=true"), "the tip check must be the FIRST request");
    t.Eq(r.cursor, int64_t(500), "");
    t.Eq(cache.Cursor(), int64_t(500), "");
}

OW_TEST(sync_baseline_walks_every_element_type) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    EnqueueBaseline(*fake, Head(1));
    std::vector<std::string> walked;
    Got<OWSyncResult> g;
    OWSync::Baseline(c, cache, g.H(), [&](const std::string& type, size_t) { walked.push_back(type); });
    Result(t, g);
    t.Eq(fake->calls.size(), size_t(23), "1 tip check + one list call per type");
    t.Eq(walked.size(), size_t(22), "on_type once per type");
    bool order = walked.size() == 22;
    for (size_t i = 0; order && i < 22; ++i) order = walked[i] == OWAddress::TypeWords[i];
    t.Ok(order, "in the table's order (OWAddress::TypeWords)");
    t.Ok(fake->calls.size() > 1 && Contains(fake->calls[1].url, "/character/"), "the first list: Character");
}

OW_TEST(sync_baseline_stores_elements_and_clears_first) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    Hold(cache, "stale", "Old");
    fake->Then(200, Head(10));
    for (const char* type : OWAddress::TypeWords)
        fake->Then(200, std::string(type) == "character"
                            ? std::string("{\"data\":[{\"id\":\"c1\",\"name\":\"Fresh\"}],\"has_more\":false}")
                            : std::string(EmptyPage));
    Got<OWSyncResult> g;
    OWSync::Baseline(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Eq(r.fetched, int64_t(1), "");
    t.Ok(r.was_rebaselined, "");
    t.Ok(!cache.Has("stale"), "a baseline replaces; it does not merge");
    t.Eq(cache.Get("c1") ? cache.Get("c1")->type : std::string(), "character", "held as its list's type");
    t.Eq(Name(cache, "c1"), "Fresh", "");
}

OW_TEST(sync_baseline_stores_the_tips_own_cursor_and_epoch) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    EnqueueBaseline(*fake, GuestHead(40, 3));
    Got<OWSyncResult> g;
    OWSync::Baseline(c, cache, g.H());
    Result(t, g);
    t.Eq(cache.Cursor(), int64_t(40), "");
    t.Eq(cache.CursorToken(), GuestTip(40, 3),
         "the next sync resumes from the server's cursor; a guest's carries its epoch");
    t.Eq(cache.Epoch().value_or(-1), int64_t(3), "");
}

// -- Incremental ------------------------------------------------------------------------------------------------------

OW_TEST(sync_incremental_with_no_cursor_falls_back_to_baseline) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    EnqueueBaseline(*fake, Head(5));
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Ok(Result(t, g).was_rebaselined, "nothing cached yet means there is nothing to increment from");
}

OW_TEST(sync_incremental_at_the_tip_does_nothing) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(100));
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Ok(!Result(t, g).ChangedAnything(), "");
    t.Eq(fake->calls.size(), size_t(1), "a no-op sync costs exactly one tip check");
}

OW_TEST(sync_incremental_applies_upserts_from_the_element_they_carry) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(101));
    fake->Then(200, "{\"cursor\":\"101:" + IdA + "\",\"has_more\":false,\"head\":101,\"epoch\":null,\"changes\":[" +
                        Upsert(IdA, 101, "Updated") + "]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).fetched, int64_t(1), "");
    t.Eq(Name(cache, IdA), "Updated", "");
    const OWJsonValue* level = cache.Get(IdA) ? cache.Get(IdA)->body.Find("level") : nullptr;
    t.Eq(level ? level->Text() : std::string(), "3", "");
    t.Eq(fake->calls.size(), size_t(2), "an upsert carries the element whole: the tip and one page, no GET per element");
}

OW_TEST(sync_incremental_fetches_the_body_only_when_an_op_has_none) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(101));
    fake->Then(200, "{\"cursor\":\"101:" + IdA + "\",\"has_more\":false,\"head\":101,\"epoch\":null,\"changes\":[" +
                        "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"" + IdA + "\",\"change_seq\":101}]}");
    fake->Then(200, "{\"id\":\"" + IdA + "\",\"name\":\"Fetched\"}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    Result(t, g);
    t.Eq(Name(cache, IdA), "Fetched", "");
    t.Eq(fake->Last().url, Base + "/character/" + IdA + "/", "the GET by id");
}

OW_TEST(sync_incremental_resumes_from_the_stored_cursor_not_from_the_seq) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    std::string token = "100:" + IdA + ":3";
    cache.SetCursor(100, token, 3, "t");
    fake->Then(200, GuestHead(101, 3));
    fake->Then(200, "{\"cursor\":\"101:" + IdB + ":3\",\"has_more\":false,\"head\":101,\"epoch\":3,\"changes\":[" +
                        Upsert(IdB, 101, "Seen") + "]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    Result(t, g);
    t.Eq(fake->calls.size() > 1 ? Since(fake->calls[1]) : std::string(), token,
         "the cursor goes back as the server sent it; a bare 100 from a guest is a 409");
    t.Eq(cache.CursorToken(), "101:" + IdB + ":3", "the last page's cursor is the next resume point");
    t.Eq(cache.Cursor(), int64_t(101), "");
}

OW_TEST(sync_incremental_a_cache_with_no_token_resumes_from_its_bare_seq) {
    // A cache saved with only its seq (a file written by hand, or before the token was kept) resumes exactly as it did.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(101));
    fake->Then(200, "{\"cursor\":\"101:" + IdA + "\",\"has_more\":false,\"head\":101,\"epoch\":null,\"changes\":[" +
                        Upsert(IdA, 101, "A") + "]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    Result(t, g);
    t.Eq(fake->calls.size() > 1 ? Since(fake->calls[1]) : std::string(), "100", "");
    t.Eq(cache.CursorToken(), "101:" + IdA, "");
}

OW_TEST(sync_incremental_after_baselining_an_empty_world_is_a_no_op) {
    // Head 0 used to read as "no cursor", so an empty world re-baselined on every sync.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    EnqueueBaseline(*fake, Head(0));
    Got<OWSyncResult> g;
    OWSync::Baseline(c, cache, g.H());
    Result(t, g);
    fake->calls.clear();
    fake->Then(200, Head(0));
    Got<OWSyncResult> g2;
    OWSync::Incremental(c, cache, g2.H());
    t.Ok(!Result(t, g2).ChangedAnything(), "");
    t.Eq(fake->calls.size(), size_t(1), "");
}

// -- The resync rule ----------------------------------------------------------------------------------------------------
//
// What a reset keeps (keel's changes page, "What a reset keeps"): a 409 walks the world again from the start; what the
// copy holds that the new walk does not name is KEPT on role or on no reason (sharing is forward-only: out of view, not
// gone) and DROPPED on view. A reset never empties a copy on its own.

OW_TEST(sync_incremental_a409_for_the_view_drops_what_the_new_walk_does_not_name) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, GuestTip(100, 2), 2);
    EnqueueReset(*fake, GuestHead(101, 3), Resync("view"), "a");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Ok(r.was_rebaselined, "");
    t.Eq(r.resync_reason.value_or("<absent>"), "view", "");
    t.Eq(r.kept, int64_t(0), "");
    t.Ok(cache.Has("a"), "");
    t.Eq(Name(cache, "a"), "Fresh", "the walk's body replaces the old one");
    t.Ok(!cache.Has("b"), "view: something left this key's view, so it goes");
    t.Eq(cache.CursorToken(), GuestTip(101, 3), "");
    t.Eq(cache.Epoch().value_or(-1), int64_t(3), "");
}

OW_TEST(sync_incremental_a409_for_the_role_keeps_what_the_new_walk_does_not_name) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, GuestTip(100, 2), 2);
    EnqueueReset(*fake, GuestHead(101, 3), Resync("role"), "a");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Ok(r.was_rebaselined, "");
    t.Eq(r.resync_reason.value_or("<absent>"), "role", "");
    t.Eq(r.kept, int64_t(1), "");
    t.Eq(Name(cache, "a"), "Fresh", "what the walk names, it refreshes");
    t.Ok(cache.Has("b"), "role: out of this key's view now, not gone; sharing is forward-only");
    t.Eq(Name(cache, "b"), "Out of view", "");
    t.Eq(cache.CursorToken(), GuestTip(101, 3), "the cursor is the new walk's");
    t.Eq(cache.Epoch().value_or(-1), int64_t(3), "");
}

OW_TEST(sync_incremental_a409_with_no_reason_is_kept_like_role) {
    // A guest key with a cache that holds only its seq: keel answers its bare since=100 with 409 resync_required. A
    // server older than 2026-10-20 sends no reason.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, "", std::nullopt);
    EnqueueReset(*fake, GuestHead(101, 2), Resync(nullptr), "a");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Ok(r.was_rebaselined, "");
    t.Ok(Contains(r.rebaseline_reason, "resync_required"), r.rebaseline_reason);
    t.Ok(!r.resync_reason, "no reason");
    t.Eq(r.kept, int64_t(1), "");
    t.Ok(cache.Has("b"), "no reason: kept, as for role; a reset never empties a copy");
    t.Eq(cache.CursorToken(), GuestTip(101, 2), "");
    t.Eq(cache.Epoch().value_or(-1), int64_t(2), "");
}

OW_TEST(sync_incremental_at_the_head_but_a_moved_epoch_is_not_current) {
    // A Character given or taken moves a guest's epoch with head unmoved.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, GuestTip(100, 3), 3);
    EnqueueReset(*fake, GuestHead(100, 4), Resync("view"), "a");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Ok(Result(t, g).was_rebaselined, "same head, another epoch: the walk answers 409 and the cache rebuilds");
    t.Eq(cache.Epoch().value_or(-1), int64_t(4), "");
    t.Ok(!cache.Has("b"), "");
}

OW_TEST(sync_incremental_a_reset_whose_walk_fails_leaves_the_copy_and_its_cursor_as_they_were) {
    // A tool never empties a copy on its own: a reset that cannot finish its walk changes nothing.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, GuestTip(100, 2), 2);
    fake->Then(200, GuestHead(101, 3));
    fake->Then(409, Resync("view"));
    fake->Then(200, GuestHead(101, 3));
    fake->Then(503, "{\"error\":{\"type\":\"api_error\",\"code\":\"server_busy\",\"message\":\"busy\"}}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(g.calls, 1, "answered once");
    t.Ok(g.reply.error && g.reply.error->status == 503, "the walk's failure is the answer");
    t.Eq(cache.Count(), size_t(2), "");
    t.Eq(Name(cache, "a"), "Old A", "");
    t.Ok(cache.Has("b"), "");
    t.Eq(cache.Cursor(), int64_t(100), "");
    t.Eq(cache.CursorToken(), GuestTip(100, 2), "");
    t.Eq(cache.Epoch().value_or(-1), int64_t(2), "");
    t.Eq(cache.SyncedAt(), "t", "");
}

OW_TEST(sync_incremental_a_delete_row_for_a_hide_is_dropped) {
    // A contributor learns of a hide from a delete op with reason hidden: dropped, as for view.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    Hold(cache, "c1", "Hidden now");
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(102));
    fake->Then(200, "{\"cursor\":\"c-1\",\"has_more\":false,\"head\":102,\"changes\":[{\"op\":\"delete\",\"type\":"
                    "\"character\",\"id\":\"c1\",\"change_seq\":102,\"reason\":\"hidden\"}]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).removed, int64_t(1), "");
    t.Ok(!cache.Has("c1"), "");
}

OW_TEST(sync_incremental_applies_deletes) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    Hold(cache, "c1", "Doomed");
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(102));
    fake->Then(200, "{\"cursor\":\"c-1\",\"has_more\":false,\"head\":102,\"changes\":[{\"op\":\"delete\",\"type\":"
                    "\"character\",\"id\":\"c1\",\"change_seq\":102}]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).removed, int64_t(1), "");
    t.Ok(!cache.Has("c1"), "");
}

// -- Paging the changes feed ------------------------------------------------------------------------------------------

OW_TEST(sync_incremental_does_not_claim_the_tip_when_the_feed_breaks_early) {
    // A server that says "more changes exist" but hands back no cursor to reach them. Breaking is right, but changes
    // below the tip were never applied: recording the tip anyway would lose them silently, with a cache that looks
    // current.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(10, "", std::nullopt, "t");
    fake->Then(200, Head(900));
    fake->Then(200, "{\"changes\":[" + Upsert(IdA, 11, "One") + "],\"has_more\":true,\"cursor\":\"11:" + IdA +
                        "\",\"head\":900,\"epoch\":null}");
    fake->Then(200, "{\"changes\":[" + Upsert(IdB, 12, "Two") + "],\"has_more\":true,\"cursor\":null,\"head\":900,"
                                                                "\"epoch\":null}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).fetched, int64_t(2), "");
    t.Eq(cache.CursorToken(), "11:" + IdA, "the next sync resumes from the last cursor the server gave");
    t.Eq(cache.Cursor(), int64_t(10),
         "the seq stays put: 12 is no claim, and the tip would lose every change between 12 and 900");
}

OW_TEST(sync_incremental_walks_every_page_and_takes_the_tip_when_it_finishes) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(10, "", std::nullopt, "t");
    fake->Then(200, Head(30));
    fake->Then(200, "{\"changes\":[" + Upsert(IdA, 11, "One") + "],\"has_more\":true,\"cursor\":\"11:" + IdA +
                        "\",\"head\":30,\"epoch\":null}");
    fake->Then(200, "{\"changes\":[" + Upsert(IdB, 12, "Two") + "],\"has_more\":false,\"cursor\":\"12:" + IdB +
                        "\",\"head\":30,\"epoch\":null}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).fetched, int64_t(2), "both pages must be applied");
    t.Ok(cache.Has(IdA), "");
    t.Ok(cache.Has(IdB), "");
    t.Eq(fake->calls.size() > 2 ? Since(fake->calls[2]) : std::string(), "11:" + IdA,
         "page two starts at page one's cursor, as is");
    t.Eq(cache.Cursor(), int64_t(30), "having reached the end of the feed, the tip is the correct cursor");
    t.Eq(cache.CursorToken(), "12:" + IdB, "");
}

// -- The rewind rule ----------------------------------------------------------------------------------------------------

OW_TEST(sync_incremental_rebaselines_when_cursor_is_ahead_of_head_and_keeps_what_the_walk_does_not_name) {
    // A cursor ahead of head means the server was restored from a backup: the world is walked again, and what the copy
    // holds that the walk does not name is kept (a reset never empties a copy).
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    Hold(cache, "stale", "Old");
    cache.SetCursor(9999, "", std::nullopt, "t");
    fake->Then(200, Head(100));  // the tip check: head is BEHIND our cursor
    EnqueueBaseline(*fake, Head(100));
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Ok(r.was_rebaselined, "");
    t.Eq(cache.Cursor(), int64_t(100), "the cursor must come down to the server's truth");
    t.Ok(cache.Has("stale"), "a restore rewinds the server, not what this copy read");
    t.Eq(r.kept, int64_t(1), "");
    t.Ok(!r.resync_reason, "no 409 was involved");
    t.Ok(Contains(r.rebaseline_reason, "restored from a backup"), r.rebaseline_reason);
    t.Ok(Contains(r.rebaseline_reason, "9999"), "the cursor rewound FROM");
    t.Ok(Contains(r.rebaseline_reason, "head 100"), "the head rewound TO");
}

// -- Frozen snapshots ---------------------------------------------------------------------------------------------------

OW_TEST(sync_refuses_a_read_only_cache) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache snapshot("w-1", "Chapter 1", true);
    Got<OWSyncResult> g;
    OWSync::Incremental(c, snapshot, g.H());
    t.Eq(g.calls, 1, "answered once, at once");
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Refused, "refused");
    t.Ok(g.reply.error && Contains(g.reply.error->message, "read-only"), "");
    t.Eq(fake->calls.size(), size_t(0), "it must refuse before touching the network");
    Got<OWSyncResult> b;
    OWSync::Baseline(c, snapshot, b.H());
    t.Ok(b.reply.error && b.reply.error->kind == OWApiError::Kind::Refused, "a baseline too");
    t.Eq(fake->calls.size(), size_t(0), "");
}

// -- Cheap tip check ----------------------------------------------------------------------------------------------------

OW_TEST(sync_has_changes_compares_head_to_cursor) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(100));
    Got<bool> g;
    OWSync::HasChanges(c, cache, g.H());
    t.Ok(g.reply.Ok() && !g.reply.value, "head at the cursor: false");
    fake->Then(200, Head(101));
    OWSync::HasChanges(c, cache, g.H());
    t.Ok(g.reply.Ok() && g.reply.value, "head past it: true");
    t.Eq(g.calls, 2, "");
}

OW_TEST(sync_has_changes_sees_a_guests_moved_epoch) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, GuestTip(100, 3), 3, "t");
    fake->Then(200, GuestHead(100, 3));
    Got<bool> g;
    OWSync::HasChanges(c, cache, g.H());
    t.Ok(g.reply.Ok() && !g.reply.value, "");
    fake->Then(200, GuestHead(100, 4));
    OWSync::HasChanges(c, cache, g.H());
    t.Ok(g.reply.Ok() && g.reply.value, "head unmoved, view changed: a sync is due");
}

// -- Godot's own --------------------------------------------------------------------------------------------------------

OW_TEST(sync_incremental_a_bodiless_upsert_whose_get_fails_answers_that_error_and_leaves_the_cursor) {
    // The feed names an upsert with no body, and the GET for it fails. The sync answers that error and claims nothing,
    // so the next sync replays from where this one began.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "100:x", std::nullopt, "t");
    fake->Then(200, Head(102));
    fake->Then(200, "{\"cursor\":\"102:" + IdA + "\",\"has_more\":false,\"head\":102,\"epoch\":null,\"changes\":[" +
                        "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"" + IdA + "\",\"change_seq\":102}]}");
    fake->Then(404, "{\"error\":{\"type\":\"invalid_request\",\"code\":\"not_found\",\"message\":\"No such "
                    "character.\",\"param\":\"id\"}}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(g.calls, 1, "answered once");
    t.Ok(g.reply.error && g.reply.error->IsNotFound(), "the GET's error is the answer");
    t.Eq(cache.Cursor(), int64_t(100), "");
    t.Eq(cache.CursorToken(), "100:x", "");
    t.Ok(!cache.Has(IdA), "");
}

// -- Unreal's own -------------------------------------------------------------------------------------------------------

OW_TEST(sync_a_bodiless_upserts_409_is_that_gets_error_not_a_reset) {
    // A 409 resync_required resets only from a feed page (keel names it for the feed: param since); one from a read by
    // id is answered as it is, the cache and its cursor unmoved.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = CacheTwo(100, "100:x", std::nullopt);
    fake->Then(200, Head(102));
    fake->Then(200, "{\"cursor\":\"102:" + IdA + "\",\"has_more\":false,\"head\":102,\"epoch\":null,\"changes\":[" +
                        "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"" + IdA + "\",\"change_seq\":102}]}");
    fake->Then(409, Resync("view"));
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Ok(g.reply.error && g.reply.error->IsResyncRequired(), "the GET's 409 is the answer");
    t.Eq(fake->calls.size(), size_t(3), "no walk after it");
    t.Eq(cache.Count(), size_t(2), "");
    t.Eq(cache.CursorToken(), "100:x", "");
}

OW_TEST(sync_a_body_with_no_id_takes_its_rows_and_one_with_an_id_is_held_under_its_own) {
    // The cache and its OWWorld view index one id: the body's. A body with none takes the row's.
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(102));
    fake->Then(200, "{\"cursor\":\"102:z\",\"has_more\":false,\"head\":102,\"changes\":["
                    "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"row-1\",\"change_seq\":101,"
                    "\"element\":{\"name\":\"No id\"}},"
                    "{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"row-2\",\"change_seq\":102,"
                    "\"element\":{\"id\":\"body-2\",\"name\":\"Its own\"}}]}");
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    t.Eq(Result(t, g).fetched, int64_t(2), "");
    const OWCachedElement* one = cache.Get("row-1");
    const OWJsonValue* id = one ? one->body.Find("id") : nullptr;
    t.Ok(id && id->IsString() && id->Text() == "row-1", "the row's id written into the body");
    t.Ok(cache.Has("body-2") && !cache.Has("row-2"), "held under the body's own id");
    std::unique_ptr<OWWorld> view = cache.ToWorld();
    t.Ok(view->Get("row-1") && view->Get("body-2"), "the view indexes the same ids");
}

OW_TEST(sync_a_second_sync_while_one_is_in_flight_is_refused) {
    auto fake = Fake();
    fake->deferred = true;
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(100, "", std::nullopt, "t");
    fake->Then(200, Head(101));  // the first sync's tip
    fake->Then(200, Head(101));  // the tip read beside it
    fake->Then(200, "{\"cursor\":\"101:" + IdA + "\",\"has_more\":false,\"head\":101,\"changes\":[" +
                        Upsert(IdA, 101, "A") + "]}");
    Got<OWSyncResult> first, second, third;
    OWSync::Incremental(c, cache, first.H());
    t.Ok(cache.Syncing(), "in flight");
    OWSync::Incremental(c, cache, second.H());
    OWSync::Baseline(c, cache, third.H());
    t.Eq(second.calls, 1, "answered at once");
    t.Ok(second.reply.error && second.reply.error->kind == OWApiError::Kind::Refused, "refused");
    t.Ok(third.reply.error && third.reply.error->kind == OWApiError::Kind::Refused, "a baseline too");
    t.Eq(fake->calls.size(), size_t(1), "nothing sent for them");
    Got<bool> has;
    OWSync::HasChanges(c, cache, has.H());
    t.Eq(fake->calls.size(), size_t(2), "a tip read is never refused for it");
    fake->Flush();
    t.Eq(first.calls, 1, "the first answers");
    t.Ok(first.reply.Ok() && first.reply.value.fetched == 1, "and applies its page");
    t.Ok(!cache.Syncing(), "and lets go");
    Got<OWSyncResult> after;
    fake->Then(200, Head(101));
    OWSync::Incremental(c, cache, after.H());
    fake->Flush();
    t.Ok(after.reply.Ok(), "the next one runs");
}

OW_TEST(sync_a_baseline_answered_later_is_the_baseline_answered_at_once) {
    // Unreal's HTTP answers later, the fake inside Send: the walk is the same state machine for both.
    auto run = [](bool later, OWWorldCache& cache, Got<OWSyncResult>& g, std::vector<std::string>& urls) {
        auto fake = Fake();
        fake->deferred = later;
        OWClient c = Client(fake);
        fake->Then(200, GuestHead(7, 1));
        for (const char* type : OWAddress::TypeWords)
            fake->Then(200, std::string(type) == "pin"
                                ? std::string("{\"data\":[{\"id\":\"p1\",\"name\":\"P\"}],\"has_more\":true,"
                                              "\"next_cursor\":\"n1\"}")
                                : std::string(EmptyPage));
        fake->Then(200, "{\"data\":[{\"id\":\"p2\",\"name\":\"Q\"}],\"has_more\":false}");
        OWSync::Baseline(c, cache, g.H());
        while (fake->Pending() > 0) fake->Flush();
        for (const OWRequest& r : fake->calls) urls.push_back(r.url);
    };
    OWWorldCache now = Cache(), later = Cache();
    Got<OWSyncResult> a, b;
    std::vector<std::string> ua, ub;
    run(false, now, a, ua);
    run(true, later, b, ub);
    t.Ok(a.reply.Ok() && b.reply.Ok(), "both answered");
    t.Eq(b.calls, 1, "once");
    t.Eq(b.reply.value.fetched, a.reply.value.fetched, "the same count");
    t.Ok(ua == ub, "the same requests in the same order");
    t.Eq(later.Count(), now.Count(), "");
    t.Eq(later.CursorToken(), now.CursorToken(), "");
    t.Ok(later.Has("p1") && later.Has("p2"), "a type's second page too");
}

OW_TEST(sync_a_long_feed_through_a_transport_that_answers_at_once_keeps_the_stack_flat) {
    // 20,000 one-row pages, each answered inside Send: a walk that recursed per page would overflow the stack.
    const int pages = 20000;
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(1, "", std::nullopt, "t");
    fake->Then(200, Head(pages + 1));
    for (int i = 0; i < pages; ++i) {
        std::string seq = std::to_string(i + 2);
        fake->Then(200, "{\"changes\":[" + Upsert("e" + std::to_string(i % 50), i + 2, "N") + "],\"has_more\":" +
                            (i + 1 < pages ? "true" : "false") + ",\"cursor\":\"" + seq + ":x\",\"head\":" +
                            std::to_string(pages + 1) + "}");
    }
    Got<OWSyncResult> g;
    OWSync::Incremental(c, cache, g.H());
    OWSyncResult r = Result(t, g);
    t.Eq(r.fetched, int64_t(pages), "every page applied");
    t.Eq(cache.Count(), size_t(50), "");
    t.Eq(cache.Cursor(), int64_t(pages + 1), "");
}

OW_TEST(sync_has_changes_on_one_client_sends_the_tips_tag_and_a_304_is_no_change) {
    auto fake = Fake();
    OWClient c = Client(fake);
    OWWorldCache cache = Cache();
    cache.SetCursor(9, "9", std::nullopt, "t");
    fake->Then(200, Head(9), {{"ETag", "W/\"9:-\""}});
    fake->Then(304, "");
    Got<bool> g;
    OWSync::HasChanges(c, cache, g.H());
    t.Ok(g.reply.Ok() && !g.reply.value, "at the cursor");
    t.Ok(!fake->Last().Header("If-None-Match"), "nothing held: no tag");
    OWSync::HasChanges(c, cache, g.H());
    const std::string* tag = fake->Last().Header("If-None-Match");
    t.Eq(tag ? *tag : std::string("<absent>"), "W/\"9:-\"", "the tag sent back exactly as received");
    t.Ok(g.reply.Ok() && !g.reply.value, "a 304: no change");
    t.Ok(g.reply.response && g.reply.response->status == 304, "");
    // A sync's own tip read rides the same tag.
    fake->Then(304, "");
    Got<OWSyncResult> s;
    OWSync::Incremental(c, cache, s.H());
    t.Ok(s.reply.Ok() && !s.reply.value.ChangedAnything(), "a 304 tip: current, nothing else sent");
    t.Eq(fake->calls.size(), size_t(3), "");
}
