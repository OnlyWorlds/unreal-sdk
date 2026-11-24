// Every request OWClient's read and write calls make, held to keel's own document, and every key the client reads held
// to what keel serves. The client tests beside this one assert what the client SENDS, which cannot catch a client that
// sends the wrong thing consistently; this asserts what keel SERVES: the path is one keel has, the method one it takes
// there, every query parameter and header sent one keel declares on that operation, no body on a read or a delete and a
// JSON object on every other write, every body key one keel declares there (or one its body_patterns allow at the top
// level), a bulk item's element as its type's create; the other way round, every key the client's answers read is one
// keel's answer declares, every field of the element bodies the core's readers and the plugin's structs read
// (schema.json's base and the type's own, OWLens::FieldsOf) is one keel serves on a list row and a get, and every error
// code an OWApiError helper names is one keel lists. The checks are the Unity SDK's OWWireContractTests, those that
// apply to these calls, as the Godot SDK's test_client_wire.gd ports them.
//
// The fixture is keel's published document as the Unity SDK's tools/refresh_wire.py reduces it, vendored byte for byte
// from unity-sdk (Packages/com.onlyworlds.sdk/Tests/Editor/Fixtures/wire/keel-wire.json at 9670bd5; fetched
// 2026-11-24T15:48+01:00 from https://www.onlyworlds.com/api/v2/openapi.json): tests/data/keel-wire.json,
// sha256 12dbb1e1fbc4efe8b0db1a0f75996bb0caa0a1813342f93d7bcfe17cce3cfaa2, checked below. A trailing slash is ignored
// when matching (keel serves both spellings). Answer keys go one level down there: data[].name, world.id.

#include "ow_test.h"
#include "fake_transport.h"
#include "sha256.h"

#include "OWCharter.h"
#include "OWClient.h"
#include "OWFiles.h"
#include "OWJoin.h"
#include "OWJson.h"
#include "OWLens.h"

#include <algorithm>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;
using owtest::P;

namespace {

const char* const WireSha256 = "12dbb1e1fbc4efe8b0db1a0f75996bb0caa0a1813342f93d7bcfe17cce3cfaa2";
const std::string Base = "https://example.test/api/v2";

std::string WirePath() { return P().root + "/tests/data/keel-wire.json"; }

const OWJsonValue& Wire() {
    static const OWJsonValue doc = [] {
        OWBytes bytes;
        OWFiles::Read(WirePath(), bytes);
        return OWJson::Parse(bytes).value;
    }();
    return doc;
}

// Never a declared parameter in OpenAPI: a body's media type.
const std::set<std::string> Undeclarable = {"content-type"};

// Keys the client or the core's readers read that keel does not serve on that answer, on purpose, each with why. Each
// must stay absent from the document: when keel starts serving one, drop it here.
const std::map<std::string, std::string> NotServed = {
    {"world", "schema.json's base field (the folder format carries it); keel names an element's world by the key, and "
              "serves no world key on the body"},
};

// Keys a join model reads that keel does not serve on that answer, on purpose, each with why (the path, then the key).
const std::map<std::string, std::string> JoinNotServed = {
    {"/api/v2/join/preview world.id", "OWJoinWorld is shared with OWJoined, whose world carries the id; a preview's "
                                      "never does (the code is unspent)"},
};

// Codes a helper names that keel's document does not list yet, each with why. Each must stay unlisted.
const std::map<std::string, std::string> Reserved = {
    {"world_gone", "OWApiError::IsAuthError names it as reserved, as the Unity and TypeScript SDKs do (keel has no "
                   "world_gone row yet)"},
};

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::vector<std::string> Strings(const OWJsonValue* list) {
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& v : list->Items()) out.push_back(v.Text());
    return out;
}
bool Has(const std::vector<std::string>& list, const std::string& s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}
std::string Joined(const std::vector<std::string>& list) {
    std::string out;
    for (const std::string& s : list) out += (out.empty() ? "" : ", ") + s;
    return out;
}

std::string Decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// The document's spelling of a request's path: /api/v2 and the path under Base, without its query or a trailing slash.
std::string KeelPath(const OWRequest& r) {
    std::string path = r.url.substr(0, r.url.find('?'));
    path = path.substr(std::min(path.size(), Base.size()));
    while (!path.empty() && path.back() == '/') path.pop_back();
    return "/api/v2" + path;
}

std::vector<std::string> Segments(const std::string& path) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        out.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return out;
}

// A document path matches when each of its segments does: {name} any one segment, anything else itself.
bool Matches(const std::string& pattern, const std::string& path) {
    std::vector<std::string> a = Segments(pattern), b = Segments(path);
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        bool param = a[i].size() > 2 && a[i].front() == '{' && a[i].back() == '}';
        if (param ? b[i].empty() : a[i] != b[i]) return false;
    }
    return true;
}

// The operation keel serves for this request, or nullptr and why not.
const OWJsonValue* Find(const OWRequest& r, std::string* why) {
    std::string path = KeelPath(r);
    const OWJsonValue* paths = Wire().Find("paths");
    if (!paths) {
        *why = "the fixture has no paths";
        return nullptr;
    }
    for (size_t i = 0; i < paths->Size(); ++i) {
        if (!Matches(paths->Keys()[i], path)) continue;
        const OWJsonValue* op = paths->Items()[i].Find(r.method);
        *why = op ? "" : "keel serves " + paths->Keys()[i] + " but not " + r.method + " there";
        return op;
    }
    *why = "keel serves no path matching " + path;
    return nullptr;
}

const OWJsonValue* Op(const std::string& path, const char* method) {
    const OWJsonValue* p = Wire().Find("paths") ? Wire().Find("paths")->Find(path) : nullptr;
    return p ? p->Find(method) : nullptr;
}
std::vector<std::string> Response(const std::string& path, const char* method) {
    const OWJsonValue* op = Op(path, method);
    return Strings(op ? op->Find("response") : nullptr);
}
// The answer's keys one level down under `prefix` ("data[].", "reading_aids."), not further.
std::vector<std::string> Below(const std::vector<std::string>& keys, const std::string& prefix) {
    std::vector<std::string> out;
    for (const std::string& k : keys) {
        if (k.rfind(prefix, 0) != 0) continue;
        std::string rest = k.substr(prefix.size());
        if (rest.find('.') == std::string::npos && rest.find("[]") == std::string::npos) out.push_back(rest);
    }
    return out;
}
std::vector<std::string> Top(const std::vector<std::string>& keys) { return Below(keys, ""); }

template <typename T>
OWHandler<T> Ignore() {
    return [](OWReply<T>) {};
}

OWJsonValue Json(std::string_view text) { return OWJson::Parse(text).value; }

// Every write, every option given, with bodies that hold each kind of key a write cleans or keeps: 7 requests.
void Writes(OWClient& c, FakeTransport& fake) {
    fake.body = "{}";
    c.Create("character",
             Json("{\"name\":\"Ana\",\"world\":\"w1\",\"created_at\":\"t\",\"background\":null,\"x_tool\":1}"), {},
             Ignore<OWJsonValue>());
    OWCreateOptions keyed;
    keyed.idempotency_key = "k1";
    c.Create("character", Json("{\"name\":\"Ana\"}"), keyed, Ignore<OWJsonValue>());
    c.Upsert("character", "c1",
             Json("{\"id\":\"c1\",\"name\":\"Ana\",\"type\":\"character\",\"change_seq\":4,\"friends\":[\"c2\"],"
                  "\"birthplace\":null,\"level\":3}"),
             Ignore<OWJsonValue>());
    OWWriteOptions when;
    when.if_match = 4;
    c.Patch("character", "c1", Json("{\"name\":\"Bo\",\"birthplace\":\"l1\"}"), when, Ignore<OWJsonValue>());
    c.Delete("character", "c1", when, Ignore<OWJsonValue>());
    OWLinkEdit edit;
    edit.add = std::vector<std::string>{"c2"};
    edit.remove = std::vector<std::string>{"c3"};
    c.EditLinks("character", "c1", "friends", edit, when, Ignore<OWJsonValue>());
    fake.body = "{\"errors\":false,\"items\":[]}";
    OWBulkOptions bulk;
    bulk.atomic = true;
    bulk.idempotency_key = "k2";
    bulk.create_only = true;
    c.Bulk({{"character", Json("{\"name\":\"A\",\"updated_at\":\"t\"}")},
            {"location", Json("{\"name\":\"Port\",\"parent_location\":null}")}},
           bulk, Ignore<OWBulkResult>());
    fake.body = "{}";
}

// Every read call and every write, every option given, on each credential: keel's requests, which the checks below
// judge. An agent seat's client reads too, so a declaration sent on a read would be seen.
const std::vector<OWRequest>& Record() {
    static const std::vector<OWRequest> calls = [] {
        auto fake = std::make_shared<FakeTransport>();
        auto make = [&](const std::string& key, const std::string& pin, std::optional<std::string> world,
                        bool agent = false) {
            OWClientConfig config;
            config.api_key = key;
            config.api_pin = pin;
            config.world = std::move(world);
            config.base_url = Base;
            config.transport = fake;
            if (agent) {
                config.agent = true;
                config.client = "wire-test";
            }
            return std::make_unique<OWClient>(std::move(config));
        };
        OWListOptions every;
        every.limit = 5;
        every.cursor = "c";
        every.fields = {"name"};
        every.name = "n";
        every.name_contains = "m";
        every.supertype = "s";
        every.subtype = "u";
        every.expand = {"friends"};
        every.ids = {"c1", "c2"};
        every.created_by = "owner";
        every.view_as = "public";
        // Single-link filters: each field must be one keel declares on that list operation.
        OWListOptions filtered;
        filtered.filter = {{"parent_location", "p1"}, {"zone", ""}};
        OWListOptions walk;
        walk.supertype = "s";
        OWGetOptions get;
        get.expand = {"friends"};
        get.fields = {"name"};
        get.view_as = "public";
        OWChangesOptions feed;
        feed.since = "4:069620b9-0794-708d-8000-42b0ea685f38";
        feed.after = "2026-10-22T00:00:00Z";
        for (auto [key, pin] : std::vector<std::pair<std::string, std::string>>{
                 {"ow_w_test", "0000"}, {"0123456789", "0000"}, {"ow_r_test", ""}, {"ow_w_seat", "ow_s_x"}}) {
            auto c = make(key, pin, std::nullopt, key == "ow_w_seat");
            Writes(*c, *fake);
            fake->body = "{}";
            c->Health(Ignore<OWJsonValue>());
            c->GetWorld(Ignore<OWJsonValue>());
            c->Me(Ignore<OWJsonValue>());
            c->Members(Ignore<OWJsonValue>());
            fake->body = "{\"data\":[],\"has_more\":false}";
            c->List("character", every, Ignore<OWPage>());
            c->List("location", filtered, Ignore<OWPage>());
            c->ListAll("location", walk, Ignore<std::vector<OWJsonValue>>());
            fake->body = "{}";
            c->Get("character", "c1", {}, Ignore<OWJsonValue>());
            c->Get("character", "c1", get, Ignore<OWJsonValue>());
            fake->body = "{\"changes\":[],\"has_more\":false,\"head\":0,\"cursor\":\"0\"}";
            c->Changes(feed, Ignore<OWChangesPage>());
            fake->headers = {{"etag", "W/\"0:-\""}};
            c->ChangesHead(Ignore<OWChangesPage>());
            c->ChangesHead(Ignore<OWChangesPage>());  // sends If-None-Match
            fake->headers = {};
        }
        // The world reads an account token, or no key, sends with a world: world= beside each read's own query.
        for (const std::string& key : {std::string("ow_a_test"), std::string("")}) {
            auto c = make(key, "0000", "w1");
            fake->body = "{}";
            c->Health(Ignore<OWJsonValue>());
            c->GetWorld(Ignore<OWJsonValue>());
            c->Get("character", "c1", get, Ignore<OWJsonValue>());
            c->Members(Ignore<OWJsonValue>());  // the account token's; the keyless client's is refused, nothing sent
            fake->body = "{\"data\":[],\"has_more\":false}";
            OWListOptions one = every;
            one.ids = {"c1"};
            c->List("character", one, Ignore<OWPage>());
            OWListOptions parent;
            parent.filter = {{"parent_location", "p1"}};
            c->List("location", parent, Ignore<OWPage>());
            Writes(*c, *fake);  // refused, nothing sent: neither writes
        }
        // Joining is keyless, so it is not on the client; the same transport records it. The redeem passes every
        // option. A person's join goes on an account token.
        fake->body = "{}";
        OWJoinConfig where;
        where.transport = fake;
        where.base_url = Base;
        OWJoin::Preview("ow_j_x", where, Ignore<OWJoinPreview>());
        OWRedeemOptions all;
        all.name = "N";
        all.agent_name = "A";
        all.character = "c1";
        OWJoin::Redeem("ow_j_x", all, where, Ignore<OWJoined>());
        make("ow_a_test", "0000", std::nullopt)->AcceptJoin("ow_j_x", Ignore<OWJoinedMembership>());
        return fake->calls;
    }();
    return calls;
}

}  // namespace

OW_TEST(client_wire_the_fixture_is_the_vendored_bytes) {
    OWBytes bytes;
    t.Ok(OWFiles::Read(WirePath(), bytes), "tests/data/keel-wire.json is there");
    t.Eq(owtest::Sha256Hex(bytes.data(), bytes.size()), WireSha256, "unity-sdk 9670bd5's copy, byte for byte");
    const OWJsonValue* fetched = Wire().Find("fetched");
    t.Eq(fetched ? fetched->Text() : "", "2026-11-24T15:48+01:00", "");
    const OWJsonValue* paths = Wire().Find("paths");
    t.Ok(paths && paths->Size() > 300, "the document read whole");
}

OW_TEST(client_wire_every_call_hits_a_path_and_method_keel_serves) {
    for (const OWRequest& call : Record()) {
        std::string why;
        t.Ok(Find(call, &why) != nullptr, call.method + " " + call.url + ": " + why);
    }
}

OW_TEST(client_wire_every_query_parameter_is_one_keel_declares) {
    for (const OWRequest& call : Record()) {
        std::string why;
        const OWJsonValue* op = Find(call, &why);
        size_t q = call.url.find('?');
        if (!op || q == std::string::npos) continue;
        std::vector<std::string> declared = Strings(op->Find("query"));
        std::string query = call.url.substr(q + 1);
        size_t start = 0;
        while (start <= query.size()) {
            size_t amp = query.find('&', start);
            std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
            std::string name = Decode(pair.substr(0, pair.find('=')));
            t.Ok(Has(declared, name), call.method + " " + call.url + ": '" + name +
                                          "' is not a query parameter keel declares there (" + Joined(declared) + ")");
            if (amp == std::string::npos) break;
            start = amp + 1;
        }
    }
}

OW_TEST(client_wire_every_header_is_one_keel_declares) {
    std::set<std::string> auth;
    for (const std::string& h : Strings(Wire().Find("auth_headers"))) auth.insert(Lower(h));
    t.Ok(auth.count("api-key") && auth.count("api-pin") && auth.count("authorization"), "the auth headers read");
    for (const OWRequest& call : Record()) {
        std::string why;
        const OWJsonValue* op = Find(call, &why);
        if (!op) continue;
        std::set<std::string> declared;
        for (const std::string& h : Strings(op->Find("header"))) declared.insert(Lower(h));
        for (auto& [name, value] : call.headers) {
            std::string n = Lower(name);
            t.Ok(auth.count(n) || declared.count(n) || Undeclarable.count(n),
                 call.method + " " + call.url + ": header '" + name + "' is not one keel declares there");
        }
    }
}

OW_TEST(client_wire_no_read_sends_a_body_and_every_write_body_is_a_json_object) {
    for (const OWRequest& call : Record()) {
        std::string label = call.method + " " + call.url;
        if (call.method == "GET" || call.method == "DELETE") {
            t.Ok(!call.body.has_value(), label);
            t.Ok(call.Header("Content-Type") == nullptr, label + ": no body, no media type");
            continue;
        }
        OWJsonParseResult parsed = OWJson::Parse(call.body.value_or(""));
        t.Ok(parsed.ok && parsed.value.IsObject(), label + ": a JSON object");
        t.Eq(call.Header("Content-Type") ? *call.Header("Content-Type") : std::string(), "application/json", label);
    }
}

namespace {

// The body keys keel declares on an operation: the top-level ones, or those under `prefix` ("items[].").
std::vector<std::string> DeclaredBody(const OWJsonValue& op, const std::string& prefix = "") {
    return Below(Strings(op.Find("body")), prefix);
}
// A top-level key keel's request schema allows by pattern (body_patterns: its extension keys), never a nested one.
bool MatchesAPattern(const std::string& key, const OWJsonValue& op) {
    for (const std::string& p : Strings(op.Find("body_patterns")))
        if (std::regex_search(key, std::regex(p))) return true;
    return false;
}
// The keys of `body` that are neither declared nor allowed by the operation's patterns (none for a nested object).
std::string Undeclared(const OWJsonValue& body, const std::vector<std::string>& declared, const OWJsonValue* op) {
    std::string out;
    for (const std::string& k : body.Keys())
        if (!Has(declared, k) && !(op && MatchesAPattern(k, *op))) out += (out.empty() ? "" : ", ") + k;
    return out;
}

}  // namespace

OW_TEST(client_wire_every_body_key_is_one_keel_declares) {
    const OWJsonValue* paths = Wire().Find("paths");
    size_t checked = 0;
    for (const OWRequest& call : Record()) {
        if (!call.body) continue;
        std::string why;
        const OWJsonValue* op = Find(call, &why);
        if (!op) continue;
        std::string label = call.method + " " + KeelPath(call);
        t.Ok(op->Find("body") && op->Find("body")->IsArray(), label + ": keel declares a JSON body there");
        OWJsonValue body = OWJson::Parse(*call.body).value;
        t.Eq(Undeclared(body, DeclaredBody(*op), op), "", label + ": every key one keel declares on the body");
        ++checked;
        const OWJsonValue* items = body.Find("items");
        if (!items) continue;
        for (const OWJsonValue& item : items->Items()) {
            t.Eq(Undeclared(item, DeclaredBody(*op, "items[]."), nullptr), "", label + ": an item's keys");
            std::string type = item.Find("type") ? item.Find("type")->Text() : "";
            const OWJsonValue* path = paths ? paths->Find("/api/v2/" + type) : nullptr;
            const OWJsonValue* create = path ? path->Find("POST") : nullptr;
            t.Ok(create != nullptr, label + ": keel creates a " + type);
            const OWJsonValue* element = item.Find("element");
            if (!create || !element) continue;
            t.Eq(Undeclared(*element, DeclaredBody(*create), create), "",
                 label + ": a " + type + " item's element, every key one POST /" + type + "/ declares");
            ++checked;
        }
    }
    t.Eq(checked, size_t(4 * (6 + 2) + 3), "six bodies and two bulk items on each world key, and the three joins");
}

OW_TEST(client_wire_the_recording_covers_every_read_and_write) {
    // An empty recording would pass the checks above with nothing checked.
    const std::vector<OWRequest>& calls = Record();
    t.Eq(calls.size(), size_t(4 * (12 + 7) + 6 + 5 + 3),
         "12 reads and 7 writes on each world key (ListAll one page), 6 reads on an account token, 5 on no key (its "
         "roster refused); neither writes; the keyless preview and redeem, and a person's join on an account token");
    std::set<std::string> paths;
    size_t world = 0, agent_pin_reads = 0;
    bool tag = false, pin = false, bearer = false, head = false, after = false;
    for (const OWRequest& c : calls) {
        paths.insert(c.method + " " + KeelPath(c));
        if (c.url.find("world=") != std::string::npos) ++world;
        tag = tag || c.Header("If-None-Match");
        pin = pin || c.Header("API-Pin");
        bearer = bearer || c.Header("Authorization");
        head = head || c.url.find("head=true") != std::string::npos;
        after = after || c.url.find("after=") != std::string::npos;
        if (c.method == "GET" && c.Header("API-Pin") && c.Header("API-Key") && *c.Header("API-Key") == "ow_w_seat")
            ++agent_pin_reads;
    }
    for (const char* p : {"GET /api/v2/health", "GET /api/v2/world", "GET /api/v2/me", "GET /api/v2/members",
                          "GET /api/v2/character", "GET /api/v2/location", "GET /api/v2/character/c1",
                          "GET /api/v2/changes", "POST /api/v2/character", "PUT /api/v2/character/c1",
                          "PATCH /api/v2/character/c1", "DELETE /api/v2/character/c1",
                          "POST /api/v2/character/c1/links/friends", "POST /api/v2/bulk", "POST /api/v2/join/preview",
                          "POST /api/v2/join", "POST /api/v2/account/join"})
        t.Ok(paths.count(p) == 1, std::string(p) + " is recorded");
    for (const char* h : {"If-Match", "Idempotency-Key", "X-OW-Client", "X-OW-Agent", "Content-Type"})
        t.Ok(std::any_of(calls.begin(), calls.end(), [&](const OWRequest& c) { return c.Header(h) != nullptr; }),
             std::string(h) + " is recorded");
    t.Eq(agent_pin_reads, size_t(1), "the agent seat reads too (its /me alone with the PIN)");
    t.Eq(world, size_t(9), "the world reads of an account token and of no key");
    t.Ok(tag, "a tip read with a tag is recorded");
    t.Ok(pin, "a PIN is recorded");
    t.Ok(bearer, "a Bearer is recorded");
    t.Ok(head, "a tip read is recorded");
    t.Ok(after, "a feed page's after is recorded");
    size_t create_only = 0;
    for (const OWRequest& c : calls) {
        if (c.method != "POST" || KeelPath(c) != "/api/v2/bulk" || !c.body) continue;
        const OWJsonValue* v = Json(*c.body).Find("create_only");
        if (v && v->IsBool() && v->AsBool()) ++create_only;
    }
    t.Eq(create_only, size_t(4), "a bulk write with create_only on each world key");
}

// -- What the client and the readers read ----------------------------------------------------------------------------------

OW_TEST(client_wire_every_field_an_element_reader_reads_is_one_keel_serves) {
    OWBytes bytes;
    OWFiles::Read(P().root + "/codegen/schema-dist/schema.json", bytes);
    OWJsonValue schema = OWJson::Parse(bytes).value;
    std::vector<std::string> base;
    if (const OWJsonValue* b = schema.Find("base"); b && b->IsArray())
        for (const OWJsonValue& f : b->Items())
            if (const OWJsonValue* n = f.Find("name")) base.push_back(n->Text());
    t.Ok(Has(base, "id") && Has(base, "name"), "schema.json's base fields read");
    // What the core's readers read beside the schema's fields: the type (OWElement::Type), the server's stamps
    // (OWCharter's newest by created_at, OWLens's stub base).
    for (const char* k : {"type", "created_at", "updated_at", "change_seq"}) base.push_back(k);
    t.Eq(OWLens::TypeOrder().size(), size_t(22), "the 22 types");
    for (const std::string& type : OWLens::TypeOrder()) {
        std::vector<std::string> listed = Below(Response("/api/v2/" + type, "GET"), "data[].");
        std::vector<std::string> got = Top(Response("/api/v2/" + type + "/{id}", "GET"));
        t.Ok(!listed.empty() && !got.empty(), type + ": keel documents its list and its get");
        std::vector<std::string> keys = base;
        for (const auto& f : OWLens::FieldsOf(type)) keys.push_back(f.name);
        t.Ok(keys.size() > base.size(), type + ": its own fields read");
        for (const std::string& key : keys) {
            if (NotServed.count(key)) {
                t.Ok(!Has(listed, key) && !Has(got, key), type + "." + key + ": keel serves it now; drop it from NotServed");
                continue;
            }
            t.Ok(Has(listed, key), type + "." + key + " is not a key GET /" + type + "/ serves on a row");
            t.Ok(Has(got, key), type + "." + key + " is not a key GET /" + type + "/{id} serves");
        }
        std::vector<std::string> page = Response("/api/v2/" + type, "GET");
        for (const std::string& k : OWPage::Keys())
            t.Ok(Has(page, k), "OWPage reads " + k + ", which GET /" + type + "/ serves");
    }
}

OW_TEST(client_wire_the_changes_page_reads_what_keel_serves_and_every_key_of_a_row) {
    std::vector<std::string> served = Response("/api/v2/changes", "GET");
    for (const std::string& k : OWChangesPage::Keys())
        t.Ok(Has(served, k), "OWChangesPage reads " + k + ", which keel serves");
    std::vector<std::string> rows = Below(served, "changes[].");
    std::vector<std::string> read = OWChange::Keys();
    std::sort(rows.begin(), rows.end());
    std::sort(read.begin(), read.end());
    t.Eq(Joined(read), Joined(rows), "OWChange reads every key of a row, and only those");
}

OW_TEST(client_wire_the_bulk_result_reads_what_keel_serves_and_every_key_of_a_slot) {
    std::vector<std::string> served = Response("/api/v2/bulk", "POST");
    t.Ok(!served.empty(), "keel documents the bulk answer");
    t.Ok(!OWBulkResult::Keys().empty(), "OWBulkResult reads keys");
    for (const std::string& k : OWBulkResult::Keys()) t.Ok(Has(served, k), "OWBulkResult reads " + k + ", which keel serves");
    std::vector<std::string> slots = Below(served, "items[].");
    std::vector<std::string> read = OWBulkSlot::Keys();
    std::sort(slots.begin(), slots.end());
    std::sort(read.begin(), read.end());
    t.Eq(Joined(read), Joined(slots), "OWBulkSlot reads every key of a slot, and only those");
}

OW_TEST(client_wire_the_reading_aids_are_the_packet_owcharter_served_reads) {
    // reading_aids beside front_door on /me (and an agent's join answer): OWCharter::Served reads {charter, guide,
    // domains, strand}, the packet ToJson writes. keel always serves the strand (serial 100).
    OWJsonValue empty = OWJson::Parse(
                            "{\"reading_aids\":{\"charter\":null,\"guide\":null,\"domains\":[],\"strand\":{\"narrative\":"
                            "null,\"event\":null,\"location\":null,\"character\":null,\"now\":null,\"same\":null}}}")
                            .value;
    std::optional<OWReadingAids> aids = OWCharter::Served(empty);
    t.Ok(aids.has_value(), "");
    if (!aids) return;
    std::vector<std::string> read = aids->ToJson().Keys();
    std::sort(read.begin(), read.end());
    for (auto [path, method] : std::vector<std::pair<std::string, const char*>>{{"/api/v2/me", "GET"}, {"/api/v2/join", "POST"}}) {
        std::vector<std::string> served = Below(Response(path, method), "reading_aids.");
        std::sort(served.begin(), served.end());
        t.Eq(Joined(read), Joined(served), std::string(method) + " " + path);
    }
}

OW_TEST(client_wire_every_key_a_join_model_reads_is_one_keel_serves) {
    // The join answers, one level down as the fixture lists them: a glimpse's things and links are not listed below
    // that, so their keys are held by the glimpse vector in test_join.cpp instead.
    std::vector<std::string> preview = Response("/api/v2/join/preview", "POST");
    std::vector<std::string> joined = Response("/api/v2/join", "POST");
    std::vector<std::string> membership = Response("/api/v2/account/join", "POST");
    t.Ok(!preview.empty() && !joined.empty() && !membership.empty(), "keel documents the three answers");
    struct Case {
        const std::vector<std::string>* read;
        std::vector<std::string> served;
        const char* label;
        const char* not_served_prefix;
    };
    for (const Case& c : std::vector<Case>{
             {&OWJoinPreview::Keys(), Top(preview), "OWJoinPreview", ""},
             {&OWJoinWorld::Keys(), Below(preview, "world."), "a preview's OWJoinWorld", "/api/v2/join/preview world."},
             {&OWJoinPick::Keys(), Below(preview, "characters[]."), "OWJoinPick", ""},
             {&OWJoinGlimpse::Keys(), Below(preview, "glimpse."), "OWJoinGlimpse", ""},
             {&OWJoined::Keys(), Top(joined), "OWJoined", ""},
             {&OWJoinWorld::Keys(), Below(joined, "world."), "a join's OWJoinWorld", ""},
             {&OWJoinCharacter::Keys(), Below(joined, "character."), "OWJoinCharacter", ""},
             {&OWJoinSeat::Keys(), Below(joined, "seat."), "OWJoinSeat", ""},
             {&OWJoinedMembership::Keys(), Top(membership), "OWJoinedMembership", ""},
         }) {
        std::string unserved;
        for (const std::string& k : *c.read)
            if (!Has(c.served, k) && !JoinNotServed.count(c.not_served_prefix + k))
                unserved += (unserved.empty() ? "" : ", ") + k;
        t.Eq(unserved, std::string(), std::string(c.label) + " reads only keys keel serves (" + Joined(c.served) + ")");
    }
    const std::string on_preview = "/api/v2/join/preview ";
    for (const auto& [key, why] : JoinNotServed) {
        t.Ok(key.rfind(on_preview, 0) == 0, key + ": a preview's key (" + why + ")");
        t.Ok(!Has(preview, key.substr(on_preview.size())), "keel serves " + key + " now; drop it from JoinNotServed");
    }
}

OW_TEST(client_wire_the_preview_reads_every_key_keel_serves_on_it) {
    // The other way round, for the preview alone (the Unity SDK's ThePreview_ReadsEveryKeyKeelServesOnIt; npm's wire
    // check holds OwJoinPreview and its glimpse both ways since 4.189.0): a screen reads a preview whole before anyone
    // joins, so a key keel adds there fails here until it is read.
    std::vector<std::string> served = Response("/api/v2/join/preview", "POST");
    t.Ok(!served.empty(), "keel documents the preview");
    const std::vector<std::pair<std::string, const std::vector<std::string>*>> below = {
        {"world.", &OWJoinWorld::Keys()}, {"characters[].", &OWJoinPick::Keys()}, {"glimpse.", &OWJoinGlimpse::Keys()}};
    std::string unread;
    for (const std::string& k : served) {
        const std::vector<std::string>* read = &OWJoinPreview::Keys();
        std::string key = k;
        for (const auto& [prefix, keys] : below) {
            if (k.rfind(prefix, 0) != 0) continue;
            read = keys;
            key = k.substr(prefix.size());
        }
        if (!Has(*read, key)) unread += (unread.empty() ? "" : ", ") + k;
    }
    t.Eq(unread, std::string(), "OWJoinPreview reads every key keel serves on a preview");
    for (const char* k : {"glimpse.things", "glimpse.links", "glimpse.picked_by", "note", "characters[].taken"})
        t.Ok(Has(served, k), std::string("keel serves ") + k + " on a preview");
}

OW_TEST(client_wire_the_members_and_me_answers_serve_what_their_docs_name) {
    // Members and Me answer the bodies as served; their doc comments name these keys, and Members reads data.
    std::vector<std::string> members = Response("/api/v2/members", "GET");
    t.Ok(Has(members, "data"), "GET /members serves data");
    std::vector<std::string> rows = Below(members, "data[].");
    for (const char* k : {"id", "kind", "role", "character", "status", "lenses", "lenses_from", "name"})
        t.Ok(Has(rows, k), std::string("GET /members serves data[].") + k);
    std::vector<std::string> me = Top(Response("/api/v2/me", "GET"));
    for (const char* k : {"world", "scope", "kind", "role", "membership", "character", "lenses", "reading_aids", "front_door"})
        t.Ok(Has(me, k), std::string("GET /me serves ") + k);
}

// -- Errors ------------------------------------------------------------------------------------------------------------------

OW_TEST(client_wire_every_error_code_a_helper_names_is_one_keel_lists) {
    const OWJsonValue* listed = Wire().Find("error_codes");
    t.Ok(listed && listed->IsObject() && listed->Size() > 50, "keel's error codes read");
    if (!listed) return;
    OWBytes bytes;
    OWFiles::Read(P().root + "/Plugin/OnlyWorlds/Core/Private/OWApiError.cpp", bytes);
    std::string source(bytes.begin(), bytes.end());
    std::set<std::string> named;
    std::regex compare("code == \"([a-z_]+)\"");
    for (auto it = std::sregex_iterator(source.begin(), source.end(), compare); it != std::sregex_iterator(); ++it)
        named.insert((*it)[1].str());
    t.Ok(named.size() > 5, "the helpers' codes were read");
    t.Ok(named.count("not_found") && named.count("media_unavailable"), "both a helper's code and IsRetryable's");
    for (const std::string& code : named)
        t.Ok(listed->Has(code) || Reserved.count(code),
             "'" + code + "' is named by an OWApiError helper but keel's document does not list it");
    std::regex pair("status == (\\d+) && code == \"([a-z_]+)\"");
    int pairs = 0;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), pair); it != std::sregex_iterator(); ++it) {
        ++pairs;
        std::string status = (*it)[1].str(), code = (*it)[2].str();
        const OWJsonValue* want = listed->Find(code);
        if (!want) continue;
        bool ok = false;
        if (want->IsNumber()) ok = want->Text() == status;
        else
            for (const OWJsonValue& s : want->Items()) ok = ok || s.Text() == status;
        t.Ok(ok, "'" + code + "': a helper checks status " + status + ", keel lists " + OWJson::Stringify(*want));
    }
    t.Ok(pairs >= 4, "the status-and-code helpers were read");
    for (auto& [code, why] : Reserved) t.Ok(!listed->Has(code), "'" + code + "' is listed by keel now; drop it from Reserved");
}
