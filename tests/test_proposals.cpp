// OWClient's proposed changes through a fake transport (tests/fake_transport.h): no test touches the network. Propose,
// ListProposals, WorldProposals, AcceptProposal, DeclineProposal, WithdrawProposal and ReviewNewElement: each route's
// method, path, query, headers and body against what is sent, the rows read (OWProposal, both kinds of the queue's
// row, a new element's mark values as stored, to the digit), the answers, the eight codes' helpers and a
// stale_proposal's conflicts, each refusal with nothing sent, and the queue's conditional poll. The Unity SDK's
// Tests/Editor/OWProposalTests.cs, ported test by test (the names follow its), with the core's own beside them; the
// refusals' words are the TypeScript SDK's.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"
#include "OWJson.h"
#include "OWProposal.h"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";
const std::string El = "018f0000-0000-7000-8000-0000000000e1";
const std::string Pid = "018f0000-0000-7000-8000-0000000000a1";
const std::string NewEl = "018f0000-0000-7000-8000-0000000000e2";
const std::string By = "018f0000-0000-7000-8000-0000000000b1";

// One row as the document's Proposal component gives it: nineteen keys (no house).
const std::string Proposal =
    R"({"kind":"proposal","id":")" + Pid + R"(","element_type":"location","element_id":")" + El +
    R"(","state":"open","fields":{"description":"A harbour town."},"omitted":[],"base":{"description":""},)"
    R"("base_seq":41,"evidence":"Chapter 3 names the harbour.","expires":"2026-11-21T16:00:00Z","by":")" + By +
    R"(","via":"api","client":"unreal-sdk","decided_by":null,"decided_at":null,"decline_reason":null,)"
    R"("created_at":"2026-10-22T16:00:00Z","updated_at":"2026-10-22T16:00:00Z"})";

// A queue row for a new element written as proposed (the document's ProposedNewElement): ten keys.
const std::string NewElementRow =
    R"({"kind":"new_element","id":")" + NewEl + R"(","element_type":"character","element_id":")" + NewEl +
    R"(","state":"open","by":")" + By + R"(","evidence":"Met her in chapter 4.","expires":"2026-11-22",)"
    R"("created_at":"2026-10-23T07:00:00Z","updated_at":"2026-10-23T07:00:00Z"})";

const std::string Element =
    R"({"id":")" + El + R"(","name":"Harbour","type":"location","description":"A harbour town.","change_seq":42})";

std::string Proposed(const std::string& row) { return "{\"proposal\":" + row + "}"; }
std::string Page(const std::string& rows, const std::string& tail = ",\"has_more\":true,\"next_cursor\":\"c2\"") {
    return "{\"data\":[" + rows + "]" + tail + "}";
}
std::string Envelope(const std::string& code, const std::string& extra = "") {
    return R"({"error":{"type":"invalid_request","code":")" + code + R"(","message":")" + code +
           R"(","param":null,"doc_url":"https://onlyworlds.github.io/api/errors#)" + code + "\"}" + extra + "}";
}

std::shared_ptr<FakeTransport> Fake(int status = 200, std::string body = "{}") {
    auto f = std::make_shared<FakeTransport>();
    f->status = status;
    f->body = std::move(body);
    return f;
}

OWClientConfig Config(const std::shared_ptr<FakeTransport>& fake, const std::string& key = "ow_w_test",
                      const std::string& pin = "1234") {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = pin;
    config.base_url = Base;
    config.transport = fake;
    return config;
}

OWClient Make(const std::shared_ptr<FakeTransport>& fake, int64_t page_size = 100) {
    OWClientConfig config = Config(fake);
    config.page_size = page_size;
    return OWClient(std::move(config));
}

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
    bool Refused() const { return reply.error && reply.error->kind == OWApiError::Kind::Refused; }
    bool Answer() const { return reply.error && reply.error->kind == OWApiError::Kind::Answer; }
    std::string Why() const { return reply.error ? reply.error->ToString() : "no error"; }
};

OWJsonValue Parse(std::string_view text) { return OWJson::Parse(text).value; }
// A value's text with every number as written: two values compared to the digit.
std::string Text(const OWJsonValue& v) { return OWJson::Stringify(v, /*keep_number_text=*/true); }
std::string HeaderNames(const OWRequest& r) {
    std::string out;
    for (auto& [k, v] : r.headers) out += (out.empty() ? "" : ",") + k;
    return out;
}
std::string HeaderOr(const OWRequest& r, const char* name) {
    const std::string* v = r.Header(name);
    return v ? *v : "<absent>";
}
std::string BodyOr(const OWRequest& r) { return r.body ? *r.body : "<none>"; }
std::string Str(const std::optional<std::string>& s) { return s ? *s : "<absent>"; }
bool EndsNothingSent(const std::string& m) {
    return m.size() >= 17 && m.compare(m.size() - 17, 17, "Nothing was sent.") == 0;
}

// A queue row for a new element whose mark holds `json` at `key`, read through WorldProposals.
std::optional<OWProposal> RowWith(const std::string& key, const std::string& json) {
    OWJsonValue row = Parse(NewElementRow);
    row.Set(key, Parse(json));
    auto fake = Fake(200, "{\"data\":[" + OWJson::Stringify(row, true) + "],\"has_more\":false,\"next_cursor\":null}");
    Got<OWProposalPage> g;
    Make(fake).WorldProposals({}, g.H());
    if (!g.reply.Ok() || g.reply.value.data.size() != 1) return std::nullopt;
    return g.reply.value.data[0];
}

// The values keel's queue serves a new element's mark's evidence and expires as, each as itself: the Unity SDK's 14.
const std::vector<std::pair<const char*, const char*>>& StoredValues() {
    static const std::vector<std::pair<const char*, const char*>> values = {
        {"a line of text", R"("the Harbour Ledger, entry 14")"},
        {"an empty string", R"("")"},
        {"White_Space alone", R"("   ")"},
        {"a list of ids", R"(["0199c3a0-0000-7000-8000-0000000000a1","0199c3a0-0000-7000-8000-0000000000b2"])"},
        {"an empty list", "[]"},
        {"an object", R"({"k":1})"},
        {"an integer", "14"},
        {"a date as a number", "20991121"},
        {"a fraction", "2.5"},
        {"true", "true"},
        {"false", "false"},
        {"null", "null"},
        {"a date-time string stays a string", R"("2026-11-22T07:00:00Z")"},
        {"an integer past 2^53", "9007199254740993"},
    };
    return values;
}

}  // namespace

// -- propose -------------------------------------------------------------------------------------------------------------

OW_TEST(proposals_propose_posts_fields_evidence_and_expires_and_answers_the_row) {
    auto fake = Fake(201, Proposed(Proposal));
    Got<OWProposal> g;
    OWProposeOptions when;
    when.expires = "2026-11-21T16:00:00Z";
    Make(fake).Propose("location", El, Parse(R"({"description":"A harbour town."})"), "Chapter 3 names the harbour.", when,
                       g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.reply.value.id, Pid, "the row's id");
    t.Eq(g.reply.value.state, "open", "");
    t.Eq(g.reply.value.base_seq, int64_t{41}, "");
    t.Eq(Text(g.reply.value.fields), Text(Parse(R"({"description":"A harbour town."})")), "the fields");
    t.Eq(fake->calls.size(), size_t{1}, "one request");
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals", "the path, npm's spelling");
    t.Eq(fake->Last().method, "POST", "");
    t.Eq(BodyOr(fake->Last()),
         R"({"fields":{"description":"A harbour town."},"evidence":"Chapter 3 names the harbour.","expires":"2026-11-21T16:00:00Z"})",
         "the body, expires as given");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin,Content-Type,X-OW-Client", "no Idempotency-Key unless given");
    t.Eq(HeaderOr(fake->Last(), "X-OW-Client"), "unreal-sdk", "");
}

OW_TEST(proposals_propose_sends_an_idempotency_key_when_given_and_no_expires_unless_given) {
    auto fake = Fake(201, Proposed(Proposal));
    Got<OWProposal> g;
    OWProposeOptions keyed;
    keyed.idempotency_key = "prop-1";
    Make(fake).Propose("location", El, Parse(R"({"name":"Harbour"})"), "x", keyed, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(HeaderOr(fake->Last(), "Idempotency-Key"), "prop-1", "");
    t.Eq(BodyOr(fake->Last()), R"({"fields":{"name":"Harbour"},"evidence":"x"})", "");
}

OW_TEST(proposals_propose_cleans_the_fields_as_a_patch_body_is_cleaned) {
    auto fake = Fake(201, Proposed(Proposal));
    Got<OWProposal> g;
    Make(fake).Propose("location", El,
                       Parse(R"({"name":"Harbour","x_tangle_battle":3,"world":"w","type":"location","created_at":"a",)"
                             R"("updated_at":"b","change_seq":9,"supertype":null})"),
                       "x", {}, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(BodyOr(fake->Last()), R"({"fields":{"name":"Harbour","x_tangle_battle":3,"supertype":""},"evidence":"x"})",
         "the read-only keys left out, a null string field sent as \"\"");
}

OW_TEST(proposals_propose_refuses_a_known_of_stub_a_body_that_is_not_an_object_and_a_wrong_kind) {
    auto fake = Fake(201, Proposed(Proposal));
    OWClient c = Make(fake);
    Got<OWProposal> stub, none, list, kind, nan;
    c.Propose("location", El, Parse(R"({"name":"","known_of":true})"), "x", {}, stub.H());
    t.Ok(stub.Refused(), "a known-of stub: " + stub.Why());
    if (stub.reply.error) t.Ok(stub.reply.error->message.rfind("propose: this body is a known-of stub", 0) == 0, stub.Why());
    c.Propose("location", El, OWJsonValue(), "x", {}, none.H());
    c.Propose("location", El, Parse("[]"), "x", {}, list.H());
    for (auto* g : {&none, &list}) {
        t.Ok(g->Refused(), "not an object: " + g->Why());
        if (g->reply.error)
            t.Eq(g->reply.error->message,
                 "propose: fields must be an object of the fields to change and their new values, as in a PATCH "
                 "body. Nothing was sent.",
                 "the TypeScript SDK's words");
    }
    // The TypeScript SDK's sixth case of 4.220.0's kind check, here and in test_client_write.cpp.
    c.Propose("character", El, Parse(R"({"height":1.5})"), "a source", {}, kind.H());
    t.Ok(kind.Refused(), kind.Why());
    if (kind.reply.error)
        t.Eq(kind.reply.error->message,
             "propose: character.height is an integer field; 1.5 would be stored as another value. Nothing was sent.", "");
    OWJsonValue weight = OWJsonValue::Object();
    weight.Set("x_weight", OWJsonValue::Number(std::numeric_limits<double>::quiet_NaN()));
    c.Propose("character", El, weight, "a source", {}, nan.H());
    t.Ok(nan.Refused() && nan.reply.error->message.find("NaN at fields.x_weight") != std::string::npos,
         "a NaN named with its place in the body: " + nan.Why());
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(proposals_the_routes_send_no_agent_header_and_the_clients_own_name) {
    // keel's document lists X-OW-Client on propose, accept and review, and X-OW-Agent on none of the proposal routes.
    auto fake = Fake(201, Proposed(Proposal));
    OWClientConfig config = Config(fake);
    config.agent = true;
    config.client = "atlas";
    OWClient c(std::move(config));
    Got<OWProposal> p;
    c.Propose("location", El, Parse(R"({"name":"H"})"), "x", {}, p.H());
    fake->body = "{\"proposal\":" + Proposal + ",\"element\":" + Element + "}";
    Got<OWProposalAccepted> a;
    c.AcceptProposal("location", El, Pid, a.H());
    fake->body = "{\"deleted\":\"" + NewEl + "\"}";
    Got<OWReviewed> r;
    c.ReviewNewElement("character", NewEl, OWReviewed::Decline, std::nullopt, r.H());
    t.Ok(p.reply.Ok() && a.reply.Ok() && r.reply.Ok(), p.Why() + " / " + a.Why() + " / " + r.Why());
    t.Eq(fake->calls.size(), size_t{3}, "three requests");
    for (const OWRequest& call : fake->calls) {
        t.Ok(!call.Header("X-OW-Agent"), call.url + ": no X-OW-Agent");
        t.Eq(HeaderOr(call, "X-OW-Client"), "atlas", call.url + ": the client's own name");
    }
}

// -- the lists -------------------------------------------------------------------------------------------------------------

OW_TEST(proposals_list_sends_state_since_limit_and_cursor_and_reads_the_page) {
    auto fake = Fake(200, Page(Proposal));
    Got<OWProposalPage> g;
    OWProposalQuery q;
    q.state = "all";
    q.since = "2026-10-22T00:00:00Z";
    q.limit = 5;
    q.cursor = "c1";
    Make(fake).ListProposals("location", El, q, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.reply.value.data.size(), size_t{1}, "one row");
    if (!g.reply.value.data.empty()) t.Eq(g.reply.value.data[0].id, Pid, "");
    t.Ok(g.reply.value.has_more, "has_more");
    t.Eq(g.reply.value.next_cursor, "c2", "");
    t.Eq(fake->Last().url,
         Base + "/location/" + El + "/proposals?state=all&since=2026-10-22T00%3A00%3A00Z&limit=5&cursor=c1",
         "state, since, limit, cursor, in Unity's order");
    t.Eq(fake->Last().method, "GET", "");
    t.Ok(!fake->Last().body, "no body");
    t.Eq(HeaderNames(fake->Last()), "API-Key", "a read: no PIN");
}

OW_TEST(proposals_list_with_no_query_sends_only_the_page_size) {
    auto fake = Fake(200, Page(Proposal));
    Got<OWProposalPage> g;
    Make(fake, 25).ListProposals("location", El, {}, g.H());
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals?limit=25", "");
}

OW_TEST(proposals_list_refuses_the_world_queues_element_filter) {
    auto fake = Fake(200, Page(Proposal));
    Got<OWProposalPage> g;
    OWProposalQuery q;
    q.element = El;
    Make(fake).ListProposals("location", El, q, g.H());
    t.Ok(g.Refused(), g.Why());
    if (g.reply.error)
        t.Eq(g.reply.error->message,
             "ListProposals: element filters the world's queue (WorldProposals); one element's list is already one "
             "element's. Nothing was sent.",
             "");
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(proposals_world_sends_state_since_element_limit_and_cursor) {
    auto fake = Fake(200, Page(Proposal));
    Got<OWProposalPage> g;
    OWProposalQuery q;
    q.state = "open";
    q.since = "2026-10-21T00:00:00Z";
    q.element = El;
    q.limit = 10;
    q.cursor = "c1";
    OWClient c = Make(fake);
    c.WorldProposals(q, g.H());
    t.Ok(g.reply.Ok() && !g.reply.value.data.empty() && g.reply.value.data[0].id == Pid, g.Why());
    t.Eq(fake->Last().url,
         Base + "/world/proposals?state=open&since=2026-10-21T00%3A00%3A00Z&element=" + El + "&limit=10&cursor=c1", "");
    t.Eq(fake->Last().method, "GET", "");
    Got<OWProposalPage> bare;
    c.WorldProposals({}, bare.H());
    t.Eq(fake->Last().url, Base + "/world/proposals?limit=100", "the page size alone");
}

OW_TEST(proposals_world_reads_both_kinds_in_keels_order_and_sends_the_cursor_back_as_it_came) {
    // The cursor is opaque (<microseconds>.<id> today), never the last row's id.
    const std::string opaque = "1761202800000000." + NewEl;
    auto fake = Fake(200, Page(NewElementRow + "," + Proposal, ",\"has_more\":true,\"next_cursor\":\"" + opaque + "\""));
    OWClient c = Make(fake);
    Got<OWProposalPage> g;
    OWProposalQuery all;
    all.state = "all";
    c.WorldProposals(all, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.reply.value.data.size(), size_t{2}, "both rows");
    if (g.reply.value.data.size() == 2) {
        const OWProposal& marked = g.reply.value.data[0];
        const OWProposal& proposal = g.reply.value.data[1];
        t.Eq(marked.kind + "," + proposal.kind, "new_element,proposal", "keel's order");
        t.Ok(marked.IsNewElement() && !proposal.IsNewElement(), "IsNewElement");
        t.Eq(marked.id, NewEl, "");
        t.Eq(marked.element_type, "character", "");
        t.Eq(marked.element_id, NewEl, "");
        t.Eq(marked.state, "open", "");
        t.Eq(Str(marked.by), By, "");
        t.Ok(marked.evidence && marked.evidence->IsString() && marked.evidence->Text() == "Met her in chapter 4.",
             "the mark's evidence, a string");
        t.Eq(Str(marked.ExpiresText()), "2026-11-22", "");
        t.Eq(Str(marked.created_at), "2026-10-23T07:00:00Z", "");
        t.Ok(marked.fields.IsNull() && marked.base.IsNull(), "no fields, no base");
        t.Ok(!marked.omitted, "no omitted");
        t.Eq(proposal.id, Pid, "");
        t.Ok(proposal.omitted && proposal.omitted->empty(), "omitted, empty");
        t.Eq(Str(proposal.EvidenceText()), "Chapter 3 names the harbour.", "");
        t.Ok(proposal.expires && proposal.expires->IsString(), "a date-time string stays a string");
    }
    t.Eq(g.reply.value.next_cursor, opaque, "");
    OWProposalQuery next;
    next.cursor = g.reply.value.next_cursor;
    Got<OWProposalPage> g2, g3;
    c.WorldProposals(next, g2.H());
    t.Eq(fake->Last().url, Base + "/world/proposals?limit=100&cursor=" + opaque, "sent back as it came");
    OWProposalQuery old;
    old.cursor = Pid;  // an old bare proposal id still reads as a cursor: sent as given
    c.WorldProposals(old, g3.H());
    t.Eq(fake->Last().url, Base + "/world/proposals?limit=100&cursor=" + Pid, "");
}

// -- a new element's evidence and expires, as stored ------------------------------------------------------------------

OW_TEST(proposals_a_new_element_row_reads_each_stored_value_as_itself_and_writes_it_back_as_served) {
    for (const char* key : {"evidence", "expires"}) {
        for (const auto& [what, json] : StoredValues()) {
            const std::string label = std::string(key) + ", " + what;
            std::optional<OWProposal> row = RowWith(key, json);
            if (!row) {
                t.Fail(label + ": no row read");
                continue;
            }
            const std::optional<OWJsonValue>& read = std::string(key) == "evidence" ? row->evidence : row->expires;
            const OWJsonValue served = Parse(json);
            if (served.IsNull()) {
                t.Ok(!read, label + ": a JSON null reads as none");
            } else {
                t.Ok(read && read->kind() == served.kind(), label + ": the same JSON kind");
                t.Eq(read ? Text(*read) : "<absent>", Text(served), label + ": the same value, to the digit");
            }
            const OWJsonValue written = row->ToJson();
            const OWJsonValue* back = written.Find(key);
            t.Ok(back != nullptr, label + ": written");
            t.Eq(back ? Text(*back) : "<absent>", Text(served), label + ": written back as served");
        }
    }
}

OW_TEST(proposals_a_new_element_row_without_the_keys_reads_none_and_writes_null) {
    OWJsonValue row = Parse(NewElementRow);
    row.Erase("evidence");
    row.Erase("expires");
    auto fake = Fake(200, "{\"data\":[" + OWJson::Stringify(row, true) + "],\"has_more\":false}");
    Got<OWProposalPage> g;
    Make(fake).WorldProposals({}, g.H());
    t.Ok(g.reply.Ok() && g.reply.value.data.size() == 1, g.Why());
    if (g.reply.value.data.size() != 1) return;
    const OWProposal& p = g.reply.value.data[0];
    t.Ok(!p.evidence && !p.expires && !p.EvidenceText() && !p.ExpiresText(), "none");
    const OWJsonValue written = p.ToJson();
    t.Ok(written.Find("evidence") && written.Find("evidence")->IsNull(), "evidence written null");
    t.Ok(written.Find("expires") && written.Find("expires")->IsNull(), "expires written null");
}

OW_TEST(proposals_a_queue_row_reads_its_house_on_both_kinds_and_null_as_none) {
    struct Case {
        const char* kind;
        const char* served;  // nullptr for JSON null
        const char* expected;
    };
    const Case cases[] = {
        {"proposal", "charter", OWProposal::HouseCharter},    {"proposal", "skill", OWProposal::HouseSkill},
        {"proposal", "new_charter", OWProposal::HouseNewCharter}, {"new_element", "new_charter", OWProposal::HouseNewCharter},
        {"new_element", "skill", OWProposal::HouseSkill},     {"proposal", nullptr, nullptr},
        {"new_element", nullptr, nullptr},
    };
    for (const Case& k : cases) {
        OWJsonValue row = Parse(std::string(k.kind) == "proposal" ? Proposal : NewElementRow);
        row.Set("house", k.served ? OWJsonValue::String(k.served) : OWJsonValue::Null());
        auto fake = Fake(200, "{\"data\":[" + OWJson::Stringify(row, true) + "],\"has_more\":false}");
        Got<OWProposalPage> g;
        Make(fake).WorldProposals({}, g.H());
        const std::string label = std::string(k.kind) + " " + (k.served ? k.served : "null");
        if (!g.reply.Ok() || g.reply.value.data.size() != 1) {
            t.Fail(label + ": " + g.Why());
            continue;
        }
        const OWProposal& p = g.reply.value.data[0];
        t.Eq(Str(p.house), k.expected ? k.expected : "<absent>", label);
        t.Eq(p.IsNewElement(), std::string(k.kind) == "new_element", label + ": its kind");
    }
    auto fake = Fake(200, Page(Proposal + "," + NewElementRow, ",\"has_more\":false"));
    Got<OWProposalPage> g;
    Make(fake).WorldProposals({}, g.H());
    t.Eq(g.reply.value.data.size(), size_t{2}, "a server that sends no house: " + g.Why());
    for (const OWProposal& p : g.reply.value.data) t.Ok(!p.house, p.kind + ": no house");
}

OW_TEST(proposals_the_text_readers_read_a_string_as_itself_and_any_other_value_as_its_json_text) {
    struct Case {
        const char* key;
        const char* json;
        const char* expected;  // nullptr for none
    };
    const Case cases[] = {
        {"evidence", R"("Met her in chapter 4.")", "Met her in chapter 4."},
        {"evidence", R"("   ")", "   "},
        {"evidence", "null", nullptr},
        {"evidence", R"(["0199c3a0-0000-7000-8000-0000000000a1","0199c3a0-0000-7000-8000-0000000000b2"])",
         R"(["0199c3a0-0000-7000-8000-0000000000a1","0199c3a0-0000-7000-8000-0000000000b2"])"},
        {"evidence", R"({"k":1})", R"({"k":1})"},
        {"evidence", "true", "true"},
        {"expires", R"("2026-11-22")", "2026-11-22"},
        {"expires", "20991121", "20991121"},
        {"expires", "2.5", "2.5"},
    };
    for (const Case& k : cases) {
        std::optional<OWProposal> row = RowWith(k.key, k.json);
        const std::string label = std::string(k.key) + " " + k.json;
        if (!row) {
            t.Fail(label + ": no row read");
            continue;
        }
        std::optional<std::string> got = std::string(k.key) == "evidence" ? row->EvidenceText() : row->ExpiresText();
        t.Eq(Str(got), k.expected ? k.expected : "<absent>", label);
    }
}

OW_TEST(proposals_to_json_writes_the_twenty_wire_keys_and_nothing_else) {
    OWProposal p;
    p.evidence = Parse(R"(["0199c3a0-0000-7000-8000-0000000000a1"])");
    p.expires = OWJsonValue::Number(int64_t{20991121});
    const OWJsonValue written = p.ToJson();
    std::string keys;
    for (const std::string& k : written.Keys()) keys += (keys.empty() ? "" : ",") + k;
    std::string want;
    for (const std::string& k : OWProposal::Keys()) want += (want.empty() ? "" : ",") + k;
    t.Eq(keys, want, "the keys, in Keys' order");
    t.Eq(OWProposal::Keys().size(), size_t{20}, "twenty");
    t.Eq(written.Find("evidence") ? Text(*written.Find("evidence")) : "", Text(Parse(R"(["0199c3a0-0000-7000-8000-0000000000a1"])")), "");
    t.Eq(written.Find("expires") ? Text(*written.Find("expires")) : "", Text(Parse("20991121")), "");
    // A row read whole and written back is the row served.
    std::optional<OWProposal> read = OWProposal::Of(Parse(Proposal));
    t.Ok(read.has_value(), "the row read");
    if (read) {
        OWJsonValue served = Parse(Proposal);
        served.Set("house", OWJsonValue::Null());
        for (const std::string& k : read->ToJson().Keys()) {
            const OWJsonValue* a = read->ToJson().Find(k);
            const OWJsonValue* b = served.Find(k);
            t.Eq(a ? Text(*a) : "<absent>", b ? Text(*b) : "<absent>", k + ": written back as served");
        }
    }
}

OW_TEST(proposals_a_proposal_row_reads_its_kind_and_omitted_and_a_row_with_no_kind_is_a_proposal) {
    // An upsert's proposal names the fields it would empty.
    OWJsonValue row = Parse(Proposal);
    row.Set("omitted", Parse(R"(["supertype","location"])"));
    auto fake = Fake(201, Proposed(OWJson::Stringify(row, true)));
    OWClient c = Make(fake);
    Got<OWProposal> g;
    c.Propose("location", El, Parse(R"({"name":"H"})"), "x", {}, g.H());
    t.Eq(g.reply.value.kind, "proposal", g.Why());
    t.Ok(!g.reply.value.IsNewElement(), "");
    t.Ok(g.reply.value.omitted && *g.reply.value.omitted == std::vector<std::string>{"supertype", "location"}, "omitted");
    // A row from an older server carries no kind and is a proposal.
    row.Erase("kind");
    fake->body = Proposed(OWJson::Stringify(row, true));
    fake->status = 201;
    Got<OWProposal> old;
    c.Propose("location", El, Parse(R"({"name":"H"})"), "x", {}, old.H());
    t.Ok(old.reply.Ok(), old.Why());
    t.Eq(old.reply.value.kind, "", "no kind");
    t.Ok(!old.reply.value.IsNewElement(), "a proposal");
}

// -- a new element written as proposed ------------------------------------------------------------------------------------

OW_TEST(proposals_review_accept_posts_the_decision_with_the_pin_and_x_ow_client_and_answers_the_element) {
    const std::string settled = R"({"id":")" + NewEl + R"(","name":"Harbour","type":"character","x_review_proposed":null})";
    auto fake = Fake(200, "{\"element\":" + settled + "}");
    Got<OWReviewed> g;
    Make(fake).ReviewNewElement("character", NewEl, OWReviewed::Accept, std::nullopt, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Ok(g.reply.value.element && Text(*g.reply.value.element) == Text(Parse(settled)), "the element as written");
    t.Ok(!g.reply.value.deleted, "no deleted");
    t.Eq(fake->calls.size(), size_t{1}, "");
    t.Eq(fake->Last().url, Base + "/character/" + NewEl + "/review", "");
    t.Eq(fake->Last().method, "POST", "");
    t.Eq(BodyOr(fake->Last()), R"({"decision":"accept"})", "");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin,Content-Type,X-OW-Client", "");
    t.Eq(HeaderOr(fake->Last(), "X-OW-Client"), "unreal-sdk", "");
}

OW_TEST(proposals_review_decline_sends_the_reason_when_given_none_without_and_answers_deleted) {
    auto fake = Fake(200, "{\"deleted\":\"" + NewEl + "\"}");
    OWClient c = Make(fake);
    Got<OWReviewed> g, bare;
    c.ReviewNewElement("character", NewEl, OWReviewed::Decline, std::string("Not canon."), g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(Str(g.reply.value.deleted), NewEl, "");
    t.Ok(!g.reply.value.element, "no element");
    t.Eq(BodyOr(fake->Last()), R"({"decision":"decline","reason":"Not canon."})", "");
    c.ReviewNewElement("character", NewEl, OWReviewed::Decline, std::nullopt, bare.H());
    t.Eq(BodyOr(fake->Last()), R"({"decision":"decline"})", "");
}

OW_TEST(proposals_review_refuses_another_decision_or_a_reason_on_an_accept_before_sending) {
    auto fake = Fake(200, "{\"deleted\":\"" + NewEl + "\"}");
    OWClient c = Make(fake);
    for (const char* bad : {"Accept", "delete", "approve", ""}) {
        Got<OWReviewed> g;
        c.ReviewNewElement("character", NewEl, bad, std::nullopt, g.H());
        t.Ok(g.Refused(), std::string(bad) + ": " + g.Why());
        if (g.reply.error)
            t.Eq(g.reply.error->message,
                 std::string("ReviewNewElement: decision must be 'accept' or 'decline', got \"") + bad +
                     "\". Nothing was sent.",
                 "the TypeScript SDK's words");
    }
    Got<OWReviewed> r;
    c.ReviewNewElement("character", NewEl, OWReviewed::Accept, std::string("fine"), r.H());
    t.Ok(r.Refused(), r.Why());
    if (r.reply.error)
        t.Eq(r.reply.error->message, "ReviewNewElement: a reason goes with a decline only. Nothing was sent.", "");
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(proposals_review_an_answer_without_the_key_its_decision_settles_is_kind_answer) {
    const std::pair<const char*, std::string> cases[] = {
        {OWReviewed::Accept, "{\"deleted\":\"" + NewEl + "\"}"},
        {OWReviewed::Decline, R"({"element":{}})"},
        {OWReviewed::Accept, "{}"},
    };
    for (const auto& [decision, answer] : cases) {
        auto fake = Fake(200, answer);
        Got<OWReviewed> g;
        Make(fake).ReviewNewElement("character", NewEl, decision, std::nullopt, g.H());
        t.Ok(g.Answer(), std::string(decision) + " " + answer + ": " + g.Why());
        if (g.reply.error) t.Ok(g.reply.error->message.find("answered 200 without its") != std::string::npos, g.Why());
    }
}

OW_TEST(proposals_review_not_proposed_and_proposed_only_have_their_helpers) {
    struct Case {
        int status;
        const char* code;
        bool not_proposed, proposed_only;
    };
    for (const Case& k : {Case{422, "not_proposed", true, false}, Case{403, "proposed_only", false, true},
                          Case{422, "invalid_request", false, false}}) {
        auto fake = Fake(k.status, Envelope(k.code));
        Got<OWReviewed> g;
        Make(fake).ReviewNewElement("character", NewEl, OWReviewed::Accept, std::nullopt, g.H());
        t.Ok(g.reply.error.has_value(), k.code);
        if (!g.reply.error) continue;
        t.Eq(g.reply.error->IsNotProposed(), k.not_proposed, std::string(k.code) + ": IsNotProposed");
        t.Eq(g.reply.error->IsProposedOnly(), k.proposed_only, std::string(k.code) + ": IsProposedOnly");
    }
}

// -- deciding -------------------------------------------------------------------------------------------------------------

OW_TEST(proposals_accept_posts_with_no_body_and_answers_the_row_and_the_element_as_written) {
    OWJsonValue accepted = Parse(Proposal);
    accepted.Set("state", OWJsonValue::String("accepted"));
    accepted.Set("decided_by", OWJsonValue::String("owner"));
    accepted.Set("decided_at", OWJsonValue::String("2026-10-22T17:00:00Z"));
    auto fake = Fake(200, "{\"proposal\":" + OWJson::Stringify(accepted, true) + ",\"element\":" + Element + "}");
    Got<OWProposalAccepted> g;
    Make(fake).AcceptProposal("location", El, Pid, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.reply.value.proposal.state, "accepted", "");
    t.Eq(Str(g.reply.value.proposal.decided_by), "owner", "");
    t.Eq(g.reply.value.element.Find("change_seq") ? g.reply.value.element.Find("change_seq")->Text() : "", "42", "");
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals/" + Pid + "/accept", "");
    t.Eq(fake->Last().method, "POST", "");
    t.Ok(!fake->Last().body, "no body");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin,X-OW-Client", "no media type without a body");
}

OW_TEST(proposals_decline_sends_the_reason_when_given_no_body_without_and_answers_the_row) {
    OWJsonValue declined = Parse(Proposal);
    declined.Set("state", OWJsonValue::String("declined"));
    declined.Set("decided_by", OWJsonValue::String("owner"));
    declined.Set("decline_reason", OWJsonValue::String("Not canon."));
    auto fake = Fake(200, Proposed(OWJson::Stringify(declined, true)));
    OWClient c = Make(fake);
    Got<OWProposal> g, bare, blank;
    c.DeclineProposal("location", El, Pid, std::string("Not canon."), g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.reply.value.state, "declined", "");
    t.Eq(Str(g.reply.value.decline_reason), "Not canon.", "");
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals/" + Pid + "/decline", "");
    t.Eq(fake->Last().method, "POST", "");
    t.Eq(BodyOr(fake->Last()), R"({"reason":"Not canon."})", "");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin,Content-Type", "no X-OW-Client: keel declares none here");
    c.DeclineProposal("location", El, Pid, std::nullopt, bare.H());
    t.Ok(!fake->Last().body, "no body without a reason");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin", "");
    // An empty reason is a reason, sent as given (the Unity and TypeScript SDKs' rule).
    c.DeclineProposal("location", El, Pid, std::string(), blank.H());
    t.Eq(BodyOr(fake->Last()), R"({"reason":""})", "");
}

OW_TEST(proposals_withdraw_deletes_204_and_answers_nothing) {
    auto fake = Fake(204, "");
    Got<OWJsonValue> g;
    Make(fake).WithdrawProposal("location", El, Pid, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Ok(g.reply.value.IsNull(), "null");
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals/" + Pid, "");
    t.Eq(fake->Last().method, "DELETE", "");
    t.Ok(!fake->Last().body, "no body");
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin", "no X-OW-Client: keel declares none here");
}

// -- the codes ------------------------------------------------------------------------------------------------------------

namespace {

struct Code {
    const char* name;
    int status;
    const char* code;
    std::function<bool(const OWApiError&)> is;
};
const std::vector<Code>& Codes() {
    static const std::vector<Code> codes = {
        {"IsFieldNotVisible", 422, "field_not_visible", [](const OWApiError& e) { return e.IsFieldNotVisible(); }},
        {"IsTooManyOpen", 422, "too_many_open", [](const OWApiError& e) { return e.IsTooManyOpen(); }},
        {"IsStaleProposal", 409, "stale_proposal", [](const OWApiError& e) { return e.IsStaleProposal(); }},
        {"IsProposedOnly", 403, "proposed_only", [](const OWApiError& e) { return e.IsProposedOnly(); }},
        {"IsOwnProposal", 403, "own_proposal", [](const OWApiError& e) { return e.IsOwnProposal(); }},
        {"IsNotProposed", 422, "not_proposed", [](const OWApiError& e) { return e.IsNotProposed(); }},
        {"IsReviewByAnother", 403, "review_by_another", [](const OWApiError& e) { return e.IsReviewByAnother(); }},
    };
    return codes;
}

}  // namespace

OW_TEST(proposals_each_code_has_a_helper_true_for_its_own_status_and_code_only) {
    for (const Code& k : Codes()) {
        const std::string extra =
            std::string(k.code) == "stale_proposal" ? ",\"conflicts\":[],\"current\":" + Element : std::string();
        auto fake = Fake(k.status, Envelope(k.code, extra));
        Got<OWProposalAccepted> g;
        Make(fake).AcceptProposal("location", El, Pid, g.H());
        t.Ok(g.reply.error.has_value(), k.name);
        if (!g.reply.error) continue;
        for (const Code& other : Codes())
            t.Eq(other.is(*g.reply.error), std::string(other.name) == k.name, std::string(k.name) + ": " + other.name);
        t.Ok(!g.reply.error->IsReviewMarkEmpty() && !g.reply.error->IsProposed(), std::string(k.name) + ": no other");
        // The status matters: the same code at another status is not the helper.
        auto off = Fake(400, Envelope(k.code));
        Got<OWProposalAccepted> o;
        Make(off).AcceptProposal("location", El, Pid, o.H());
        t.Ok(o.reply.error && !k.is(*o.reply.error), std::string(k.name) + " at 400");
    }
    // A 412 is no proposal (the half of the Unity SDK's IsProposed_IsFalseForEveryOtherError the write tests do not hold).
    auto stale = Fake(412, Envelope("precondition_failed", ",\"current\":" + Element));
    Got<OWJsonValue> p;
    OWWriteOptions when;
    when.if_match = 4;
    Make(stale).Patch("location", El, Parse(R"({"name":"x"})"), when, p.H());
    t.Ok(p.reply.error && p.reply.error->IsPreconditionFailed() && !p.reply.error->IsProposed(), "a 412: " + p.Why());
}

OW_TEST(proposals_a_write_with_an_empty_mark_is_422_review_mark_empty) {
    auto fake = Fake(422, Envelope("review_mark_empty"));
    Got<OWJsonValue> g;
    Make(fake).Patch("location", El, Parse(R"({"x_review_proposed":{}})"), {}, g.H());
    t.Ok(g.reply.error && g.reply.error->IsReviewMarkEmpty(), g.Why());
    if (g.reply.error)
        for (const Code& other : Codes()) t.Ok(!other.is(*g.reply.error), other.name);
    auto off = Fake(400, Envelope("review_mark_empty"));
    Got<OWJsonValue> o;
    Make(off).Patch("location", El, Parse(R"({"name":"x"})"), {}, o.H());
    t.Ok(o.reply.error && !o.reply.error->IsReviewMarkEmpty(), "at 400");
    auto np = Fake(422, Envelope("not_proposed"));
    Got<OWJsonValue> n;
    Make(np).Patch("location", El, Parse(R"({"name":"x"})"), {}, n.H());
    t.Ok(n.reply.error && !n.reply.error->IsReviewMarkEmpty(), "not_proposed");
}

OW_TEST(proposals_stale_proposal_carries_its_conflicts_and_current) {
    const std::string current = R"({"id":")" + El + R"(","name":"Harbour","type":"location","description":"A fishing village.","change_seq":43})";
    const std::string conflicts = R"([{"field":"description","base":"","current":"A fishing village."},7,{"field":3},{"field":"name","base":9007199254740993}])";
    auto fake = Fake(409, Envelope("stale_proposal", ",\"conflicts\":" + conflicts + ",\"current\":" + current));
    Got<OWProposalAccepted> g;
    Make(fake).AcceptProposal("location", El, Pid, g.H());
    t.Ok(g.reply.error && g.reply.error->IsStaleProposal(), g.Why());
    if (g.reply.error) {
        std::vector<OWProposalConflict> c = g.reply.error->Conflicts();
        t.Eq(c.size(), size_t{2}, "a conflict is an object with a string field: the other two left out");
        if (c.size() == 2) {
            t.Eq(c[0].field, "description", "");
            t.Ok(c[0].base.IsString() && c[0].base.Text().empty(), "base \"\"");
            t.Ok(c[0].current.IsString() && c[0].current.Text() == "A fishing village.", "current");
            t.Eq(c[1].field, "name", "");
            t.Eq(Text(c[1].base), Text(Parse("9007199254740993")), "to the digit");
            t.Ok(c[1].current.IsNull(), "no current: null");
        }
        t.Ok(g.reply.error->Current() && Text(*g.reply.error->Current()) == Text(Parse(current)), "the element now");
    }
    // A stale_proposal with neither still parses.
    auto bare = Fake(409, Envelope("stale_proposal"));
    Got<OWProposalAccepted> b;
    Make(bare).AcceptProposal("location", El, Pid, b.H());
    t.Ok(b.reply.error && b.reply.error->IsStaleProposal() && b.reply.error->Conflicts().empty() &&
             !b.reply.error->Current(),
         "bare: " + b.Why());
    // Another 409 is not one, and carries neither.
    auto other = Fake(409, Envelope("id_conflict", ",\"conflicts\":" + conflicts));
    Got<OWJsonValue> o;
    Make(other).Create("location", Parse(R"({"name":"x"})"), {}, o.H());
    t.Ok(o.reply.error && !o.reply.error->IsStaleProposal() && o.reply.error->Conflicts().empty(), "id_conflict");
}

// -- reading a 202's row, the world's switch ------------------------------------------------------------------------------

OW_TEST(proposals_of_reads_a_202s_row_and_nothing_else) {
    auto fake = Fake(202, Proposed(Proposal));
    Got<OWJsonValue> g;
    Make(fake).Patch("location", El, Parse(R"({"name":"x"})"), {}, g.H());
    t.Ok(g.reply.error && g.reply.error->IsProposed() && g.reply.error->proposal, g.Why());
    if (g.reply.error && g.reply.error->proposal) {
        std::optional<OWProposal> row = OWProposal::Of(*g.reply.error->proposal);
        t.Ok(row && row->id == Pid && row->state == "open" && Str(row->client) == "unreal-sdk", "the row, typed");
    }
    for (const char* not_a_row : {"null", "[]", "\"p1\"", "7"})
        t.Ok(!OWProposal::Of(Parse(not_a_row)), std::string(not_a_row) + ": no row");
    t.Ok(OWProposal::Of(Parse("{}")).has_value(), "an empty object is a row with nothing in it");
}

OW_TEST(proposals_get_world_passes_agent_writes_proposed_through) {
    // A pass-through: GetWorld answers the body as served, so this one needs no new code.
    auto fake = Fake(200, R"({"id":"w","name":"W","agent_writes_proposed":true})");
    Got<OWJsonValue> g;
    Make(fake).GetWorld(g.H());
    const OWJsonValue* on = g.reply.value.Find("agent_writes_proposed");
    t.Ok(on && on->IsBool() && on->AsBool(), g.Why());
}

// -- the core's own ---------------------------------------------------------------------------------------------------------

OW_TEST(proposals_each_call_checks_its_path_values_and_its_key_before_sending) {
    using Run = std::function<std::optional<OWApiError>(OWClient&)>;
    const OWJsonValue fields = Parse(R"({"name":"H"})");
    auto all = [&](const std::string& type, const std::string& id, const std::string& pid) {
        return std::vector<std::pair<std::string, Run>>{
            {"propose", [=](OWClient& c) { Got<OWProposal> g; c.Propose(type, id, fields, "x", {}, g.H()); return g.reply.error; }},
            {"list", [=](OWClient& c) { Got<OWProposalPage> g; c.ListProposals(type, id, {}, g.H()); return g.reply.error; }},
            {"accept", [=](OWClient& c) { Got<OWProposalAccepted> g; c.AcceptProposal(type, id, pid, g.H()); return g.reply.error; }},
            {"decline", [=](OWClient& c) { Got<OWProposal> g; c.DeclineProposal(type, id, pid, std::nullopt, g.H()); return g.reply.error; }},
            {"withdraw", [=](OWClient& c) { Got<OWJsonValue> g; c.WithdrawProposal(type, id, pid, g.H()); return g.reply.error; }},
            {"review", [=](OWClient& c) { Got<OWReviewed> g; c.ReviewNewElement(type, id, "accept", std::nullopt, g.H()); return g.reply.error; }},
        };
    };
    // A value that is not one segment, named.
    struct Bad {
        const char* type;
        const char* id;
        const char* pid;
        const char* named;
        bool uses_pid;
    };
    for (const Bad& b : {Bad{"Location", "e1", "p1", "type \"Location\"", false}, Bad{"location", "e1/hidden", "p1", "id \"e1/hidden\"", false},
                         Bad{"location", "e1", "p1/../x", "pid \"p1/../x\"", true}, Bad{"location", "e1", "", "pid \"\"", true}}) {
        for (auto& [name, run] : all(b.type, b.id, b.pid)) {
            bool pid_call = name == "accept" || name == "decline" || name == "withdraw";
            if (b.uses_pid && !pid_call) continue;
            auto fake = Fake();
            OWClient c = Make(fake);
            std::optional<OWApiError> e = run(c);
            t.Ok(e && e->kind == OWApiError::Kind::Refused && e->message.rfind(b.named, 0) == 0 &&
                     EndsNothingSent(e->message),
                 name + " " + b.named + ": " + (e ? e->ToString() : "no error"));
            t.Eq(fake->calls.size(), size_t{0}, name + ": nothing sent");
        }
    }
    // An account token and a keyless client: refused on every call, the queue's too; a client not built, the same.
    for (const char* key : {"ow_a_test", ""}) {
        auto fake = Fake();
        OWClientConfig config = Config(fake, key);
        config.world = "w1";
        OWClient c(std::move(config));
        auto calls = all("location", "e1", "p1");
        calls.emplace_back("queue", [](OWClient& k) { Got<OWProposalPage> g; k.WorldProposals({}, g.H()); return g.reply.error; });
        for (auto& [name, run] : calls) {
            std::optional<OWApiError> e = run(c);
            t.Ok(e && e->kind == OWApiError::Kind::Refused, name + " on " + (*key ? key : "no key") + ": " + (e ? e->ToString() : "sent"));
        }
        t.Eq(fake->calls.size(), size_t{0}, std::string(*key ? key : "no key") + ": nothing sent");
    }
    auto fake = Fake();
    OWClientConfig unbuilt = Config(fake);
    unbuilt.client = "Not A Name";
    OWClient c(std::move(unbuilt));
    for (auto& [name, run] : all("location", "e1", "p1")) {
        std::optional<OWApiError> e = run(c);
        t.Ok(e && e->kind == OWApiError::Kind::Refused && e->message.rfind("OWClient was not built", 0) == 0,
             name + ": " + (e ? e->ToString() : "sent"));
    }
    t.Eq(fake->calls.size(), size_t{0}, "not built: nothing sent");
}

OW_TEST(proposals_an_answer_that_is_not_the_routes_is_kind_answer) {
    struct Case {
        const char* what;
        int status;
        const char* body;
        std::function<Got<OWProposal>(OWClient&)> run;
    };
    auto propose = [](OWClient& c) { Got<OWProposal> g; c.Propose("location", "e1", Parse(R"({"name":"H"})"), "x", {}, g.H()); return g; };
    auto decline = [](OWClient& c) { Got<OWProposal> g; c.DeclineProposal("location", "e1", "p1", std::nullopt, g.H()); return g; };
    for (const Case& k : {Case{"propose, no proposal", 201, "{}", propose}, Case{"propose, a row that is a list", 201, R"({"proposal":[]})", propose},
                          Case{"propose, not JSON", 201, "<html>", propose}, Case{"decline, no proposal", 200, R"({"id":"p1"})", decline}}) {
        auto fake = Fake(k.status, k.body);
        OWClient c = Make(fake);
        Got<OWProposal> g = k.run(c);
        t.Ok(g.Answer() && g.reply.error->status == k.status, std::string(k.what) + ": " + g.Why());
    }
    for (const char* body : {R"({"element":{}})", R"({"proposal":null,"element":{}})", R"({"proposal":{},"element":[]})", "[]"}) {
        auto fake = Fake(200, body);
        Got<OWProposalAccepted> g;
        Make(fake).AcceptProposal("location", "e1", "p1", g.H());
        t.Ok(g.Answer(), std::string("accept ") + body + ": " + g.Why());
    }
    for (const char* body : {R"({"data":{}})", R"({"data":[7]})", "[]"}) {
        auto fake = Fake(200, body);
        Got<OWProposalPage> g;
        Make(fake).WorldProposals({}, g.H());
        t.Ok(g.Answer(), std::string("queue ") + body + ": " + g.Why());
    }
    // No data at all, or a null one, is an empty page, as List reads one.
    for (const char* body : {"{}", R"({"data":null,"has_more":false,"next_cursor":null})"}) {
        auto fake = Fake(200, body);
        Got<OWProposalPage> g;
        Make(fake).WorldProposals({}, g.H());
        t.Ok(g.reply.Ok() && g.reply.value.data.empty() && g.reply.value.next_cursor.empty(), std::string(body) + ": " + g.Why());
    }
}

// -- the queue's conditional poll ---------------------------------------------------------------------------------------

OW_TEST(proposals_the_lists_send_a_tag_exactly_as_given_and_none_without) {
    auto fake = Fake(200, Page(Proposal));
    OWClient c = Make(fake);
    const std::string tag = "W/\"2026-10-22T16:00:00.000000Z:1:0\"";
    OWProposalQuery q;
    q.if_none_match = tag;
    Got<OWProposalPage> a, b, d, e;
    c.ListProposals("location", El, q, a.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), tag, "an element's list: the tag as given, W/ and quotes");
    t.Eq(fake->Last().url, Base + "/location/" + El + "/proposals?limit=100", "the tag is no query");
    c.WorldProposals(q, b.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), tag, "the queue");
    t.Eq(HeaderNames(fake->Last()), "API-Key,If-None-Match", "no PIN on a read");
    c.ListProposals("location", El, {}, d.H());
    t.Ok(!fake->Last().Header("If-None-Match"), "none without a tag");
    c.WorldProposals({}, e.H());
    t.Ok(!fake->Last().Header("If-None-Match"), "none without a tag");
}

OW_TEST(proposals_a_200_hands_back_its_etag_or_none_and_unchanged_false) {
    const std::string tag = "W/\"2026-10-22T16:00:00.000000Z:1:0\"";
    auto fake = Fake(200, Page(Proposal));
    fake->Then(200, Page(Proposal), {{"etag", tag}});
    fake->Then(200, Page(Proposal));
    OWClient c = Make(fake);
    Got<OWProposalPage> a, b;
    c.WorldProposals({}, a.H());
    t.Ok(a.reply.Ok(), a.Why());
    t.Eq(Str(a.reply.value.etag), tag, "the ETag as received, by any case");
    t.Ok(!a.reply.value.unchanged, "a 200 is never unchanged");
    t.Eq(a.reply.value.data.size(), size_t{1}, "its rows");
    c.ListProposals("location", El, {}, b.H());
    t.Ok(b.reply.Ok() && !b.reply.value.etag && !b.reply.value.unchanged, "no ETag: none (" + b.Why() + ")");
    // Nothing is kept on the client: a call after a tagged answer sends no tag of its own.
    Got<OWProposalPage> d;
    c.WorldProposals({}, d.H());
    t.Ok(!fake->Last().Header("If-None-Match"), "the caller holds the tag, not the client");
}

OW_TEST(proposals_a_304_to_a_tagged_call_is_unchanged_with_no_rows) {
    const std::string tag = "W/\"2026-10-22T16:00:00.000000Z:1:0\"";
    for (bool queue : {true, false}) {
        const std::string label = queue ? "the queue" : "an element's list";
        auto fake = Fake();
        fake->Then(304, "", {{"ETag", tag}});
        fake->Then(304, "");
        OWClient c = Make(fake);
        OWProposalQuery q;
        q.if_none_match = tag;
        Got<OWProposalPage> g, bare;
        auto run = [&](Got<OWProposalPage>& into) {
            if (queue) c.WorldProposals(q, into.H());
            else c.ListProposals("location", El, q, into.H());
        };
        run(g);
        t.Ok(g.reply.Ok(), label + ": " + g.Why());
        t.Ok(g.reply.value.unchanged, label + ": unchanged");
        t.Ok(g.reply.value.data.empty() && !g.reply.value.has_more && g.reply.value.next_cursor.empty(), label + ": no rows");
        t.Eq(Str(g.reply.value.etag), tag, label + ": the 304's own ETag");
        t.Ok(g.reply.response && g.reply.response->status == 304, label + ": the response kept");
        run(bare);
        t.Ok(bare.reply.Ok() && bare.reply.value.unchanged && !bare.reply.value.etag, label + ": a 304 with no ETag: none (" + bare.Why() + ")");
    }
}

OW_TEST(proposals_a_304_to_a_call_that_sent_no_tag_is_an_error) {
    for (bool queue : {true, false}) {
        auto fake = Fake(304, "");
        Got<OWProposalPage> g;
        if (queue) Make(fake).WorldProposals({}, g.H());
        else Make(fake).ListProposals("location", El, {}, g.H());
        t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Api && g.reply.error->status == 304,
             std::string(queue ? "the queue" : "an element's list") + ": " + g.Why());
        t.Ok(!g.reply.value.unchanged, "never unchanged");
    }
}
