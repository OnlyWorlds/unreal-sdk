// Every request OWClient's read calls make, held to keel's own document, and every key the client reads held to what
// keel serves. The client tests beside this one assert what the client SENDS, which cannot catch a client that sends
// the wrong thing consistently; this asserts what keel SERVES: the path is one keel has, the method one it takes there,
// every query parameter and header sent one keel declares on that operation, no body on a read; the other way round,
// every key the client's answers read is one keel's answer declares, every field of the element bodies the core's
// readers and the plugin's structs read (schema.json's base and the type's own, OWLens::FieldsOf) is one keel serves on
// a list row and a get, and every error code an OWApiError helper names is one keel lists. The checks are the Unity
// SDK's OWWireContractTests, those that apply to reads, as the Godot SDK's test_client_wire.gd ports them.
//
// The fixture is keel's published document as the Unity SDK's tools/refresh_wire.py reduces it, vendored byte for byte
// from unity-sdk (Packages/com.onlyworlds.sdk/Tests/Editor/Fixtures/wire/keel-wire.json at 37d2cf6, unchanged through
// 167f2cb; fetched 2026-10-31T07:00+01:00 from https://www.onlyworlds.com/api/v2/openapi.json): tests/data/keel-wire.json,
// sha256 608ef73a97a54d5f91b32ba8ee156c51833fb20746206d875775fb05c9bdadc3, checked below. A trailing slash is ignored
// when matching (keel serves both spellings). Answer keys go one level down there: data[].name, world.id.

#include "ow_test.h"
#include "fake_transport.h"
#include "sha256.h"

#include "OWCharter.h"
#include "OWClient.h"
#include "OWFiles.h"
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

const char* const WireSha256 = "608ef73a97a54d5f91b32ba8ee156c51833fb20746206d875775fb05c9bdadc3";
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

// Every read call, every option given, on each credential: keel's requests, which the checks below judge.
const std::vector<OWRequest>& Record() {
    static const std::vector<OWRequest> calls = [] {
        auto fake = std::make_shared<FakeTransport>();
        auto make = [&](const std::string& key, const std::string& pin, std::optional<std::string> world) {
            OWClientConfig config;
            config.api_key = key;
            config.api_pin = pin;
            config.world = std::move(world);
            config.base_url = Base;
            config.transport = fake;
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
                 {"ow_w_test", "0000"}, {"0123456789", "0000"}, {"ow_r_test", ""}}) {
            auto c = make(key, pin, std::nullopt);
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
        }
        return fake->calls;
    }();
    return calls;
}

}  // namespace

OW_TEST(client_wire_the_fixture_is_the_vendored_bytes) {
    OWBytes bytes;
    t.Ok(OWFiles::Read(WirePath(), bytes), "tests/data/keel-wire.json is there");
    t.Eq(owtest::Sha256Hex(bytes.data(), bytes.size()), WireSha256, "unity-sdk 37d2cf6's copy, byte for byte");
    const OWJsonValue* fetched = Wire().Find("fetched");
    t.Eq(fetched ? fetched->Text() : "", "2026-10-31T07:00+01:00", "");
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

OW_TEST(client_wire_no_read_sends_a_body) {
    for (const OWRequest& call : Record()) {
        t.Ok(!call.body.has_value(), call.method + " " + call.url);
        t.Ok(call.Header("Content-Type") == nullptr, call.method + " " + call.url + ": no body, no media type");
    }
}

OW_TEST(client_wire_the_recording_covers_every_read) {
    // An empty recording would pass the checks above with nothing checked.
    const std::vector<OWRequest>& calls = Record();
    t.Eq(calls.size(), size_t(3 * 12 + 6 + 5),
         "12 requests on each world key (ListAll one page), 6 on an account token, 5 on no key (its roster refused)");
    std::set<std::string> paths;
    size_t world = 0;
    bool tag = false, pin = false, bearer = false, head = false, after = false;
    for (const OWRequest& c : calls) {
        paths.insert(c.method + " " + KeelPath(c));
        if (c.url.find("world=") != std::string::npos) ++world;
        tag = tag || c.Header("If-None-Match");
        pin = pin || c.Header("API-Pin");
        bearer = bearer || c.Header("Authorization");
        head = head || c.url.find("head=true") != std::string::npos;
        after = after || c.url.find("after=") != std::string::npos;
    }
    for (const char* p : {"GET /api/v2/health", "GET /api/v2/world", "GET /api/v2/me", "GET /api/v2/members",
                          "GET /api/v2/character", "GET /api/v2/location", "GET /api/v2/character/c1",
                          "GET /api/v2/changes"})
        t.Ok(paths.count(p) == 1, std::string(p) + " is recorded");
    t.Eq(world, size_t(9), "the world reads of an account token and of no key");
    t.Ok(tag, "a tip read with a tag is recorded");
    t.Ok(pin, "a PIN is recorded");
    t.Ok(bearer, "a Bearer is recorded");
    t.Ok(head, "a tip read is recorded");
    t.Ok(after, "a feed page's after is recorded");
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
