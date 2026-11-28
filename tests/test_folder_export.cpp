// OWFolderExport: an account world read over the API and written as a world folder, against the onlyworlds Python
// package it follows. The expected bytes are the Unity SDK's OWFolderExportTests (Testmoor) and OWKnownOfTests, which
// are what the package's write_folder, world_for_disk and known_of_for_disk wrote for the same wire bodies; the
// provenance cases are its OWFolderExportProvenanceTests, the package's tests/test_export.py case for case. Every
// request goes through a transport that answers by route: no test touches the network.

#include "ow_test.h"

#include "OWAddress.h"
#include "OWClient.h"
#include "OWFiles.h"
#include "OWFolderExport.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const std::string Key = "ow_w_s3cr3tkeyvalue";
const std::string Pin = "4242";
const std::string Base = "https://example.test/api/v2";
const std::string EmptyPage = R"({"data":[],"has_more":false,"next_cursor":null})";
// 2026-10-19T21:00:00.005Z, Testmoor's clock; 2026-10-19T21:30:00.250Z, the provenance cases'.
const int64_t TestmoorAt = 1792443600005;
const int64_t MoorAt = 1792445400250;

// Answers each request by its route under /v2/: the world, /me, a type's pages in turn (then an empty page), one
// element by type/id, or a refusal. Records every request.
struct Routes : OWTransport {
    std::vector<OWRequest> calls;
    std::string world = "{}";
    std::map<std::string, std::deque<std::string>> pages;
    std::map<std::string, std::pair<int, std::string>> refusals;
    // Called with the route before each answer (a test fills the folder mid-walk with it).
    std::function<void(const std::string&)> on_route;

    Routes& Serve(const std::string& route, std::vector<std::string> answers) {
        pages[route] = std::deque<std::string>(answers.begin(), answers.end());
        return *this;
    }

    static std::string RouteOf(const std::string& url) {
        std::string path = url.substr(0, url.find('?'));
        size_t v2 = path.find("/v2/");
        std::string route = path.substr(v2 + 4);
        while (!route.empty() && route.back() == '/') route.pop_back();
        return route;
    }

    void Send(OWRequest request, OWResponseHandler done) override {
        std::string route = RouteOf(request.url);
        calls.push_back(std::move(request));
        if (on_route) on_route(route);
        OWResponse r;
        auto refused = refusals.find(route);
        if (refused != refusals.end()) {
            r.status = refused->second.first;
            r.body = refused->second.second;
            return done(std::move(r));
        }
        r.status = 200;
        auto page = pages.find(route);
        if (route == "world") r.body = world;
        else if (page != pages.end() && !page->second.empty()) {
            r.body = page->second.front();
            page->second.pop_front();
        } else r.body = EmptyPage;
        done(std::move(r));
    }

    std::vector<std::string> RoutesSent() const {
        std::vector<std::string> out;
        for (const OWRequest& c : calls) out.push_back("/" + RouteOf(c.url) + "/");
        return out;
    }
};

std::string Page(const std::vector<std::string>& bodies, bool more = false, const std::string& next = "") {
    std::string out = R"({"data":[)";
    for (size_t i = 0; i < bodies.size(); ++i) out += (i ? "," : "") + bodies[i];
    return out + R"(],"has_more":)" + (more ? "true" : "false") + R"(,"next_cursor":)" +
           (next.empty() ? "null" : "\"" + next + "\"") + "}";
}

OWClient Client(std::shared_ptr<Routes> t, const std::string& key = Key, const std::string& pin = Pin,
                const std::string& world = "", const std::string& base = Base) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = pin;
    config.base_url = base;
    config.transport = t;
    if (!world.empty()) config.world = world;
    return OWClient(std::move(config));
}

// A fresh parent folder for one test.
std::string Parent() {
    std::string parent = OWFiles::Absolute(P().tmp + "/folder-export");
    OWFiles::RemoveTree(parent);
    OWFiles::MakeDirs(parent);
    return parent;
}

std::string Text(const std::string& path) {
    OWBytes b;
    if (!OWFiles::Read(path, b)) return "";
    return std::string(b.begin(), b.end());
}

// Everything in a folder, dot-names too, files then folders, each in byte order, joined by spaces.
std::string Entries(const std::string& path) {
    std::string out;
    for (bool dirs : {false, true})
        for (const std::string& n : OWFiles::List(path, dirs)) out += (out.empty() ? "" : " ") + n;
    return out;
}

OWJsonValue Json(const std::string& text) { return OWJson::Parse(text).value; }

std::string Keys(const OWJsonValue& v) {
    std::string out;
    for (const std::string& k : v.Keys()) out += (out.empty() ? "" : " ") + k;
    return out;
}

std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v);
    std::string out;
    bool in_string = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_string) {
            out += c;
            if (c == '\\' && i + 1 < s.size()) out += s[++i];
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
            out += c;
        } else if (c != ' ' && c != '\n') {
            out += c;
        }
    }
    return out;
}

OWFolderExportReport RunExport(OWClient& c, const std::string& folder, OWFolderExportOptions options = {}) {
    OWFolderExportReport out;
    bool answered = false;
    int times = 0;
    OWFolderExport::Export(c, folder, std::move(options), [&](OWFolderExportReport r) {
        out = std::move(r);
        answered = true;
        ++times;
    });
    if (!answered) out.message = "never answered";
    if (times > 1) out.message = "answered " + std::to_string(times) + " times";
    return out;
}

OWFolderExportOptions Pinned(int64_t at = TestmoorAt) {
    OWFolderExportOptions o;
    o.clock = [at] { return at; };
    return o;
}

OWJsonValue WorldJsonOf(const std::string& folder) { return Json(Text(folder + "/world.json")); }

const std::string WireWorld =
    R"({"id":"0695db52-0000-7000-8000-000000000001","name":"Testmoor","description":"A moor, José's.",)"
    R"("time_range_min":0,"time_range_current":453,"length_unit":"","public_read":true,"owner_character":null,)"
    R"("created_at":"2026-01-07T01:21:41.262478+00:00","updated_at":"2026-10-11T11:53:28.489198+00:00"})";
const std::string WireCharacter =
    R"({"id":"01a1206b-0000-7000-8000-00000000c001","type":"character","name":"Ann Á","level":2.50,)"
    R"("weight":1.0,"big":1e21,"tiny":0.000001,"tags":[],"x_tool":{"b":1,"a":[]},"location":null,)"
    R"("created_at":"2026-01-07T01:21:41.262478+00:00"})";
const std::string WireMap =
    R"({"id":"069620b8-0000-7000-8000-00000000a001","type":"map","name":"The Moor",)"
    R"("image_url":"https://media.example.test/w/maps/the%20moor.webp?v=2"})";

std::shared_ptr<Routes> Testmoor() {
    auto t = std::make_shared<Routes>();
    t->world = WireWorld;
    t->Serve("character", {Page({WireCharacter})}).Serve("map", {Page({WireMap})});
    return t;
}

std::shared_ptr<Routes> WorldOnly(const std::string& world = R"({"id":"w","name":"n"})") {
    auto t = std::make_shared<Routes>();
    t->world = world;
    return t;
}

// Known-of: the package's whole_dev, stub_dev and public_stub_dev, and what it wrote for them.
const std::string WorldW = R"({"id":"w0000000-0000-7000-8000-000000000000","name":"W"})";
const std::string WholeDev =
    R"({"id":"c0000000-0000-7000-8000-00000000000d","type":"character","name":"Dev","description":"Dev desc",)"
    R"("supertype":"Ferryman","subtype":"","image_url":null,"physicality":"zzphys","location":"l1",)"
    R"("friends":["c0000000-0000-7000-8000-00000000000a"],"STR":14,"created_at":"2026-10-20T00:00:00Z",)"
    R"("updated_at":"2026-10-20T01:00:00Z","change_seq":9,"created_by":"m-1","changed_by":"m-1",)"
    R"("changed_via":"api","x_secret":"zzext"})";
const std::string StubDev =
    R"({"id":"c0000000-0000-7000-8000-00000000000d","type":"character","name":"Dev","description":"Dev desc",)"
    R"("supertype":"Ferryman","subtype":"","image_url":null,"physicality":"","location":null,"friends":[],)"
    R"("STR":null,"created_at":"2026-10-20T00:00:00Z","updated_at":"2026-10-20T01:00:00Z","change_seq":9,)"
    R"("created_by":null,"changed_by":null,"changed_via":null,"known_of":true})";
const std::string PublicStubDev =
    R"({"id":"c0000000-0000-7000-8000-00000000000d","type":"character","name":"Dev","description":"Dev desc",)"
    R"("supertype":"Ferryman","subtype":"","image_url":null,"physicality":"Tall","location":null,"friends":[],)"
    R"("STR":null,"created_at":"2026-10-20T00:00:00Z","updated_at":"2026-10-20T01:00:00Z","change_seq":9,)"
    R"("created_by":null,"changed_by":null,"changed_via":null,"height":180,"weight":null,)"
    R"("species":["s0000000-0000-7000-8000-000000000001"],"reputation":"","known_of":true})";
const std::string StubDevOnDisk =
    "{\n  \"id\": \"c0000000-0000-7000-8000-00000000000d\",\n  \"type\": \"character\",\n  \"name\": \"Dev\",\n"
    "  \"description\": \"Dev desc\",\n  \"supertype\": \"Ferryman\",\n  \"subtype\": \"\",\n  \"image_url\": null,\n"
    "  \"created_at\": \"2026-10-20T00:00:00Z\",\n  \"updated_at\": \"2026-10-20T01:00:00Z\",\n  \"change_seq\": 9,\n"
    "  \"known_of\": true\n}\n";
const std::string PublicStubDevOnDisk =
    "{\n  \"id\": \"c0000000-0000-7000-8000-00000000000d\",\n  \"type\": \"character\",\n  \"name\": \"Dev\",\n"
    "  \"description\": \"Dev desc\",\n  \"supertype\": \"Ferryman\",\n  \"subtype\": \"\",\n  \"image_url\": null,\n"
    "  \"physicality\": \"Tall\",\n  \"created_at\": \"2026-10-20T00:00:00Z\",\n  \"updated_at\": \"2026-10-20T01:00:00Z\",\n"
    "  \"change_seq\": 9,\n  \"height\": 180,\n  \"species\": [\n    \"s0000000-0000-7000-8000-000000000001\"\n  ],\n"
    "  \"known_of\": true\n}\n";
const std::string WholeDevOnDisk =
    "{\n  \"id\": \"c0000000-0000-7000-8000-00000000000d\",\n  \"type\": \"character\",\n  \"name\": \"Dev\",\n"
    "  \"description\": \"Dev desc\",\n  \"supertype\": \"Ferryman\",\n  \"subtype\": \"\",\n  \"image_url\": null,\n"
    "  \"physicality\": \"zzphys\",\n  \"location\": \"l1\",\n  \"friends\": [\n    \"c0000000-0000-7000-8000-00000000000a\"\n  ],\n"
    "  \"STR\": 14,\n  \"created_at\": \"2026-10-20T00:00:00Z\",\n  \"updated_at\": \"2026-10-20T01:00:00Z\",\n"
    "  \"change_seq\": 9,\n  \"created_by\": \"m-1\",\n  \"changed_by\": \"m-1\",\n  \"changed_via\": \"api\",\n"
    "  \"x_secret\": \"zzext\"\n}\n";

const std::string Moor = R"({"id":"w-1","name":"Moor"})";

std::shared_ptr<Routes> MoorRoutes(const std::string& me = "") {
    auto t = WorldOnly(Moor);
    if (!me.empty()) t->Serve("me", {me});
    return t;
}

// The provenance cases' export: a read key, production's base URL, the pinned clock; world.json parsed back.
OWJsonValue Provenance(std::shared_ptr<Routes> t, std::string* message = nullptr) {
    std::string folder = Parent() + "/world";
    OWClient c = Client(t, "ow_r_test", "", "", "https://www.onlyworlds.com/api/v2");
    OWFolderExportReport r = RunExport(c, folder, Pinned(MoorAt));
    if (message) *message = r.message;
    return r.ok ? WorldJsonOf(folder) : OWJsonValue::Object();
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : "<not a string>"; }

}  // namespace

// -- WorldForDisk: the package's world_for_disk, case for case -----------------------------------------------------------

OW_TEST(folder_export_world_for_disk_renames_the_now_in_place_and_stamps_the_version_last) {
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"n","time_range_current":3,"x":1})"));
    t.Ok(disk.has_value(), "a world");
    if (!disk) return;
    // package: {'id': 'w', 'name': 'n', 'time_current': 3, 'x': 1, 'format_version': '0.3.25'}
    t.Eq(Keys(*disk), std::string("id name time_current x format_version"), "the now renamed in place, the version last");
    t.Eq(Compact(*disk), std::string(R"({"id":"w","name":"n","time_current":3,"x":1,"format_version":"0.3.25"})"), "");
}

OW_TEST(folder_export_world_for_disk_keeps_both_spellings_when_both_come) {
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"","time_range_current":3,"time_current":4})"));
    t.Eq(disk ? Compact(*disk) : std::string(),
         std::string(R"({"id":"w","name":"","time_range_current":3,"time_current":4,"format_version":"0.3.25"})"), "both kept");
}

OW_TEST(folder_export_world_for_disk_keeps_a_carried_format_version_even_null) {
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"n","format_version":null})"));
    t.Eq(disk ? Compact(*disk) : std::string(), std::string(R"({"id":"w","name":"n","format_version":null})"), "");
}

OW_TEST(folder_export_world_for_disk_drops_snapshot_counts_without_snapshot_of) {
    std::map<std::string, size_t> counts = {{"character", 2}};
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"n","snapshot_counts":{"a":1}})"), &counts);
    t.Ok(disk && !disk->Has("snapshot_counts"), "a count belongs to a snapshot alone");
}

OW_TEST(folder_export_world_for_disk_recounts_a_snapshots_counts_in_place_zeros_left_out) {
    std::map<std::string, size_t> counts = {{"character", 2}, {"map", 0}};
    auto disk = OWFolderExport::WorldForDisk(
        Json(R"({"id":"w","name":"n","snapshot_of":"o","snapshot_counts":{"a":9}})"), &counts);
    t.Eq(disk ? Compact(*disk) : std::string(),
         std::string(R"({"id":"w","name":"n","snapshot_of":"o","snapshot_counts":{"character":2},"format_version":"0.3.25"})"),
         "recounted from what was written");
}

OW_TEST(folder_export_world_for_disk_drops_a_snapshots_counts_when_none_are_given) {
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"n","snapshot_of":"o","snapshot_counts":{"a":9}})"));
    t.Ok(disk && !disk->Has("snapshot_counts"), "");
}

OW_TEST(folder_export_world_for_disk_leaves_its_input_alone) {
    OWJsonValue wire = Json(R"({"id":"w","name":"n","time_range_current":3,"x":{"deep":[1]}})");
    std::string before = Compact(wire);
    auto disk = OWFolderExport::WorldForDisk(wire);
    t.Ok(disk.has_value(), "a world");
    if (disk && disk->Find("x") && disk->Find("x")->Find("deep")) disk->Find("x")->Find("deep")->Push(OWJsonValue::Number(int64_t(2)));
    t.Eq(Compact(wire), before, "the wire body is not changed");
}

OW_TEST(folder_export_world_for_disk_refuses_a_world_without_its_two_required_keys) {
    for (const char* wire : {R"({"name":"n"})", R"({"id":"","name":"n"})", R"({"id":7,"name":"n"})", R"({"id":"w"})",
                             R"({"id":"w","name":null})"}) {
        OWJsonValue body = Json(wire);
        t.Ok(!OWFolderExport::WorldForDisk(body).has_value(), wire);
        t.Ok(OWFolderExport::WorldRefusal(body).rfind("world.json requires", 0) == 0, wire);
    }
}

namespace {
const std::string FirstImport =
    R"({"source":{"server":"https://www.onlyworlds.com","world":"w0","at":"2026-10-01T09:00:00Z"},)"
    R"("producer":{"tool":"atlas","version":"1.82.0","input":"bare"}})";
const std::string ThisFoldersSource = R"({"server":"https://www.onlyworlds.com","world":"w","at":"2026-10-19T21:00:00Z"})";
}  // namespace

OW_TEST(folder_export_world_for_disk_carries_origin_as_given_beside_the_folders_own_source) {
    auto disk = OWFolderExport::WorldForDisk(
        Json(R"({"id":"w","name":"n","origin":)" + FirstImport + R"(,"source":)" + ThisFoldersSource + "}"));
    t.Ok(disk.has_value(), "");
    if (!disk) return;
    t.Eq(Keys(*disk), std::string("id name origin source format_version"), "");
    t.Ok(disk->Find("origin") && *disk->Find("origin") == Json(FirstImport), "the first import, as given");
    t.Ok(disk->Find("source") && *disk->Find("source") == Json(ThisFoldersSource), "beside this folder's own line");
}

OW_TEST(folder_export_world_for_disk_derives_no_origin_from_source_or_producer) {
    auto disk = OWFolderExport::WorldForDisk(Json(R"({"id":"w","name":"n","source":)" + ThisFoldersSource +
                                                  R"(,"producer":{"tool":"atlas","version":"1.82.0","input":"bare"}})"));
    t.Eq(disk ? Keys(*disk) : std::string(), std::string("id name source producer format_version"), "no origin made up");
}

OW_TEST(folder_export_world_for_disk_keeps_the_worlds_extension_keys_and_an_origin_of_nulls) {
    auto disk = OWFolderExport::WorldForDisk(Json(
        R"({"id":"w","name":"n","origin":{"source":null,"producer":null},"x_names":{"version":1},"atlas_c":1,"shadow_s":[1]})"));
    t.Eq(disk ? Keys(*disk) : std::string(), std::string("id name origin x_names atlas_c shadow_s format_version"), "");
    t.Eq(disk && disk->Find("origin") ? Compact(*disk->Find("origin")) : std::string(), std::string(R"({"source":null,"producer":null})"),
         "an origin of nulls is a served value");
}

// -- The folder, in the package's bytes ----------------------------------------------------------------------------------

OW_TEST(folder_export_writes_the_packages_bytes) {
    std::string parent = Parent();
    std::string folder = parent + "/world";
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, folder, Pinned());
    t.Ok(r.ok, r.message);

    t.Eq(Text(folder + "/world.json"),
         std::string("{\n  \"id\": \"0695db52-0000-7000-8000-000000000001\",\n  \"name\": \"Testmoor\",\n"
                     "  \"description\": \"A moor, Jos\xC3\xA9's.\",\n  \"time_range_min\": 0,\n  \"time_current\": 453,\n"
                     "  \"length_unit\": \"\",\n  \"public_read\": true,\n  \"owner_character\": null,\n"
                     "  \"created_at\": \"2026-01-07T01:21:41.262478+00:00\",\n"
                     "  \"updated_at\": \"2026-10-11T11:53:28.489198+00:00\",\n"
                     "  \"source\": {\n    \"server\": \"https://example.test\",\n"
                     "    \"world\": \"0695db52-0000-7000-8000-000000000001\",\n    \"at\": \"2026-10-19T21:00:00.005Z\"\n  },\n"
                     "  \"producer\": {\n    \"tool\": \"onlyworlds-unreal\",\n    \"version\": \"") +
             OWFolderExport::ProducerVersion +
             "\",\n    \"input\": \"server\"\n  },\n  \"format_version\": \"0.3.25\"\n}\n",
         "world.json");
    t.Eq(Text(folder + "/elements/character/ann-a--0000c001.json"),
         std::string("{\n  \"id\": \"01a1206b-0000-7000-8000-00000000c001\",\n  \"type\": \"character\",\n"
                     "  \"name\": \"Ann \xC3\x81\",\n  \"level\": 2.5,\n  \"weight\": 1,\n  \"big\": 1e+21,\n  \"tiny\": 0.000001,\n"
                     "  \"tags\": [],\n  \"x_tool\": {\n    \"b\": 1,\n    \"a\": []\n  },\n  \"location\": null,\n"
                     "  \"created_at\": \"2026-01-07T01:21:41.262478+00:00\"\n}\n"),
         "the character");
    t.Eq(Text(folder + "/elements/map/the-moor--0000a001.json"),
         std::string("{\n  \"id\": \"069620b8-0000-7000-8000-00000000a001\",\n  \"type\": \"map\",\n  \"name\": \"The Moor\",\n"
                     "  \"image_url\": \"https://media.example.test/w/maps/the%20moor.webp?v=2\"\n}\n"),
         "the map");
    // No BOM, and nothing else in the tree: two type folders, no media, no temp beside it.
    t.Eq(Text(folder + "/world.json").substr(0, 1), std::string("{"), "no byte order mark");
    t.Eq(Entries(folder), std::string("world.json elements"), "world.json and elements/ only");
    t.Eq(Entries(folder + "/elements"), std::string("character map"), "a folder per type with elements");
    t.Eq(Entries(parent), std::string("world"), "no temp left beside it");
}

OW_TEST(folder_export_keeps_the_served_trimmed_mark_as_served_and_the_folder_reads_it_back) {
    std::string folder = Parent() + "/world";
    auto routes = Testmoor();
    routes->Serve("narrative", {Page({R"({"id":"019a3b10-0000-7000-8000-000000000c01","type":"narrative","name":"Hello",)"
                                      R"("supertype":"Message","narrator":null,"characters":[],"x_served_trimmed":["characters","narrator"]})"})});
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    std::string file = Text(folder + "/elements/narrative/hello--00000c01.json");
    t.Ok(file.find("  \"x_served_trimmed\": [\n    \"characters\",\n    \"narrator\"\n  ]\n") != std::string::npos, "kept as served: " + file);
    auto back = OWWorld::ReadFolder(folder);
    t.Ok(back && back->OfType("narrative").size() == 1, "read back");
    const OWJsonValue* mark = back && back->OfType("narrative").size() == 1 ? back->OfType("narrative")[0]->body.Find("x_served_trimmed") : nullptr;
    t.Eq(mark ? Compact(*mark) : std::string(), std::string(R"(["characters","narrator"])"), "");
}

OW_TEST(folder_export_keeps_the_served_doors_on_a_whole_element_and_on_a_stub_that_holds_them) {
    std::string folder = Parent() + "/world";
    auto routes = Testmoor();
    routes->Serve("character", {Page({R"({"id":"01a1206b-0000-7000-8000-00000000c003","type":"character","name":"Cy","changed_via":"mcp",)"
                                      R"("changed_client":"atlas","doors":{"atlas":"2026-10-29T10:00:00+00:00","mcp":"2026-10-28T09:00:00+00:00"}})",
                                      R"({"id":"01a1206b-0000-7000-8000-00000000c004","type":"character","name":"Di","known_of":true,)"
                                      R"("doors":{"atlas":"2026-10-29T10:00:00+00:00"},"physicality":""})"})});
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    std::string doors = "  \"doors\": {\n    \"atlas\": \"2026-10-29T10:00:00+00:00\",\n";
    t.Ok(Text(folder + "/elements/character/cy--0000c003.json").find(doors + "    \"mcp\": \"2026-10-28T09:00:00+00:00\"\n  }\n") != std::string::npos,
         "the whole element's doors");
    std::string stub = Text(folder + "/elements/character/di--0000c004.json");
    t.Ok(stub.find("  \"doors\": {\n    \"atlas\": \"2026-10-29T10:00:00+00:00\"\n  },\n  \"known_of\": true\n}\n") != std::string::npos,
         "the stub's: " + stub);
    t.Ok(stub.find("physicality") == std::string::npos, "a stub's unset field is left out");
}

OW_TEST(folder_export_reports_what_it_wrote) {
    std::string folder = Parent() + "/world";
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    t.Eq(r.message, std::string(), "no message on a write");
    t.Ok(!r.error.has_value(), "no error");
    t.Eq(r.folder, folder, "the folder, absolute");
    t.Eq(r.world_id, std::string("0695db52-0000-7000-8000-000000000001"), "");
    t.Eq(r.world_name, std::string("Testmoor"), "");
    t.Ok(r.counts == std::map<std::string, size_t>{{"character", 1}, {"map", 1}}, "types with none left out");
    t.Eq(r.Total(), size_t(2), "");
}

OW_TEST(folder_export_reads_all_22_types_with_gets_only) {
    auto routes = Testmoor();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, Parent() + "/world");
    t.Ok(r.ok, r.message);
    bool gets = true;
    for (const OWRequest& q : routes->calls) gets = gets && q.method == "GET";
    t.Ok(gets, "GETs only");
    std::vector<std::string> want = {"/world/", "/me/"};
    for (const char* type : OWFolderExport::Types) want.push_back("/" + std::string(type) + "/");
    t.Ok(routes->RoutesSent() == want, "the world, who the key is, then the 22 types in the standard's order");
}

OW_TEST(folder_export_types_are_the_standards_22_in_its_order) {
    std::vector<std::string> types(std::begin(OWFolderExport::Types), std::end(OWFolderExport::Types));
    t.Eq(types.size(), size_t(22), "");
    std::string joined;
    for (const std::string& s : types) joined += s + " ";
    t.Eq(joined, std::string("ability collective character construct creature event family institution language law location "
                             "map marker narrative object phenomenon pin relation species title trait zone "),
         "the Python package's ELEMENT_TYPES");
    for (const char* w : OWAddress::TypeWords)
        t.Ok(std::find(types.begin(), types.end(), std::string(w)) != types.end(), std::string("holds ") + w);
}

OW_TEST(folder_export_tells_its_progress_after_each_type) {
    std::vector<std::string> told;
    OWFolderExportOptions o;
    o.on_progress = [&](const std::string& type, int done, int total) {
        told.push_back(type + " " + std::to_string(done) + "/" + std::to_string(total));
    };
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, Parent() + "/world", o);
    t.Ok(r.ok, r.message);
    t.Eq(told.size(), size_t(22), "once per type");
    t.Eq(told.empty() ? std::string() : told.front(), std::string("ability 1/22"), "");
    t.Eq(told.empty() ? std::string() : told.back(), std::string("zone 22/22"), "");
}

OW_TEST(folder_export_collision_rule_id_ascending_case_folded_whatever_the_order) {
    // Arrival order is the reverse of id order, so a writer that settles by arrival names them wrong.
    auto routes = WorldOnly();
    routes->Serve("character", {Page({"{\"id\":\"44444444-0000-7000-8000-000000000002\",\"name\":\"\xE6\xA0\xAA\xE5\xBC\x8F\"}",
                                      R"({"id":"33333333-0000-7000-8000-000000000001"})",
                                      R"({"id":"22222222-0000-7000-8000-00000000ABCD","name":"ann"})",
                                      R"({"id":"11111111-0000-7000-8000-00000000abcd","name":"Ann"})",
                                      R"({"id":"00000000-0000-7000-8000-00000000abcd","name":"Ann"})"})});
    std::string folder = Parent() + "/world";
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    // package resolve_filenames, the same five:
    t.Eq(Entries(folder + "/elements/character"),
         std::string("33333333-0000-7000-8000-000000000001.json 44444444-0000-7000-8000-000000000002.json ann--0000abcd.json "
                     "ann--11111111-0000-7000-8000-00000000abcd.json ann--22222222-0000-7000-8000-00000000ABCD.json"),
         "the lowest id keeps the short name");
    t.Eq(Str(Json(Text(folder + "/elements/character/ann--0000abcd.json")).Find("id")),
         std::string("00000000-0000-7000-8000-00000000abcd"), "");
}

OW_TEST(folder_export_collision_rule_by_code_point_above_the_basic_plane) {
    // U+1F600 sorts below U+E000 by UTF-16 units and above it by code point: the private-use id is the lowest, and
    // arrives second.
    const std::string astral = "\xF0\x9F\x98\x80-0000-7000-8000-00000000abcd";
    const std::string private_use = "\xEE\x80\x80-0000-7000-8000-00000000abcd";
    auto routes = WorldOnly();
    routes->Serve("character", {Page({"{\"id\":\"" + astral + "\",\"name\":\"Ann\"}", "{\"id\":\"" + private_use + "\",\"name\":\"Ann\"}"})});
    std::string folder = Parent() + "/world";
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    t.Eq(Str(Json(Text(folder + "/elements/character/ann--0000abcd.json")).Find("id")), private_use, "by code point");
    t.Ok(OWFiles::IsFile(folder + "/elements/character/ann--" + astral + ".json"), "the other takes its full id");
}

OW_TEST(folder_export_an_id_served_twice_keeps_its_last_body) {
    // A page walk that met a write serves the element again on a later page, newer.
    auto routes = WorldOnly();
    routes->Serve("character", {Page({R"({"id":"a-1","name":"Old"})", R"({"id":"b-2","name":"Bea"})"}, true, "c2"),
                                Page({R"({"id":"a-1","name":"New"})"})});
    std::string folder = Parent() + "/world";
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    t.Eq(Entries(folder + "/elements/character"), std::string("bea--b-2.json new--a-1.json"), "the later copy, once");
    t.Eq(r.counts["character"], size_t(2), "");
}

OW_TEST(folder_export_a_stub_that_later_arrives_whole_is_written_whole) {
    auto routes = WorldOnly(WorldW);
    routes->Serve("character", {Page({StubDev}, true, "c2"), Page({WholeDev})});
    std::string folder = Parent() + "/world";
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    t.Eq(Text(folder + "/elements/character/dev--0000000d.json"), WholeDevOnDisk, "whole, and known_of gone");
}

OW_TEST(folder_export_refuses_one_id_under_two_types_and_leaves_nothing) {
    auto routes = WorldOnly();
    routes->Serve("character", {Page({R"({"id":"same","name":"A"})"})}).Serve("location", {Page({R"({"id":"same","name":"B"})"})});
    std::string parent = Parent();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(!r.ok, "refused");
    t.Eq(r.message, std::string("Element id same came back under both character and location; nothing was written."), "");
    t.Ok(r.counts.empty(), "no counts on a refusal");
    t.Eq(Entries(parent), std::string(), "nothing left");
}

OW_TEST(folder_export_refuses_an_element_without_an_id) {
    auto routes = WorldOnly();
    routes->Serve("character", {Page({R"({"name":"Nobody"})"})});
    std::string parent = Parent();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(!r.ok, "refused");
    t.Eq(r.message, std::string("A character element came back without a non-empty string id; nothing was written."), "");
    t.Eq(Entries(parent), std::string(), "nothing left");
}

OW_TEST(folder_export_refuses_an_id_that_cannot_be_a_file_name_and_writes_nothing) {
    auto routes = WorldOnly();
    routes->Serve("character", {Page({R"({"id":"c-1","name":"Fine"})", R"({"id":"../x","name":""})"})});
    std::string parent = Parent();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, parent + "/box/world");
    t.Ok(!r.ok, "refused");
    t.Eq(r.message,
         std::string("character element id \"../x\" cannot be a file name: it holds a path separator, / or \\. Nothing was written."),
         "the writer's words");
    t.Eq(Entries(parent), std::string(), "nothing written, inside the folder or beside it");
}

// -- A folder's birth ----------------------------------------------------------------------------------------------------

OW_TEST(folder_export_refuses_a_folder_that_holds_anything_and_touches_it_not) {
    std::string parent = Parent();
    std::string folder = parent + "/world";
    OWFiles::Write(folder + "/notes.txt", OWBytes{'m', 'i', 'n', 'e'});
    auto routes = Testmoor();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(!r.ok, "refused");
    t.Eq(r.message, "'" + folder + "' exists and is not an empty directory; a world folder is written whole, once. Nothing was read.", "");
    t.Eq(routes->calls.size(), size_t(0), "refused before the first read");
    t.Eq(Entries(folder), std::string("notes.txt"), "the folder as it was");
    t.Eq(Entries(parent), std::string("world"), "no temp beside it");
}

OW_TEST(folder_export_refuses_a_folder_holding_only_a_hidden_folder) {
    std::string folder = Parent() + "/world";
    OWFiles::MakeDirs(folder + "/.git");
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(!r.ok, "a dot-folder is something");
    t.Eq(Entries(folder), std::string(".git"), "");
}

OW_TEST(folder_export_refuses_a_path_that_is_a_file) {
    std::string folder = Parent() + "/world";
    OWFiles::Write(folder, OWBytes{'a', ' ', 'f', 'i', 'l', 'e'});
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(!r.ok, "refused");
    t.Eq(Text(folder), std::string("a file"), "the file as it was");
}

OW_TEST(folder_export_into_an_empty_directory_works) {
    std::string parent = Parent();
    OWFiles::MakeDirs(parent + "/world");
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(r.ok, r.message);
    t.Ok(OWFiles::IsFile(parent + "/world/world.json"), "");
    t.Eq(Entries(parent), std::string("world"), "");
}

OW_TEST(folder_export_into_a_folder_whose_parent_is_missing_makes_the_parent) {
    std::string parent = Parent();
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, parent + "/a/b/world");
    t.Ok(r.ok, r.message);
    t.Ok(OWFiles::IsFile(parent + "/a/b/world/world.json"), "");
    t.Eq(Entries(parent + "/a/b"), std::string("world"), "no temp beside it");
}

OW_TEST(folder_export_takes_a_relative_path_with_a_trailing_separator) {
    std::string parent = Parent();
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, P().tmp + "/folder-export/world/");
    t.Ok(r.ok, r.message);
    t.Eq(r.folder, parent + "/world", "absolute, no trailing separator");
}

OW_TEST(folder_export_a_world_that_cannot_be_written_leaves_nothing) {
    auto routes = Testmoor();
    routes->world = R"({"name":"no id"})";
    std::string parent = Parent();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(!r.ok, "refused");
    t.Eq(r.message, std::string("world.json requires a non-empty string id (folder spec section 4.1); nothing was written."), "");
    t.Eq(Entries(parent), std::string(), "nothing left");
}

OW_TEST(folder_export_a_folder_filled_while_the_world_was_read_is_left_alone_and_the_build_removed) {
    // The cleanup on an error after the build: the folder is checked again before the move.
    std::string parent = Parent();
    std::string folder = parent + "/world";
    auto routes = Testmoor();
    routes->on_route = [folder](const std::string& route) {
        if (route == "zone") OWFiles::Write(folder + "/someone-elses.txt", OWBytes{'t', 'h', 'e', 'i', 'r', 's'});
    };
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(!r.ok, "refused at the move");
    const std::string tail = "a world folder is written whole, once. Nothing was written.";
    t.Ok(r.message.size() >= tail.size() && r.message.compare(r.message.size() - tail.size(), tail.size(), tail) == 0, r.message);
    t.Eq(Entries(parent), std::string("world"), "the build beside it is gone");
    t.Eq(Entries(folder), std::string("someone-elses.txt"), "the folder as its other writer left it");
}

// -- Errors and secrets --------------------------------------------------------------------------------------------------

OW_TEST(folder_export_a_refused_read_answers_the_clients_error_with_no_key_or_pin) {
    auto routes = Testmoor();
    routes->refusals["location"] = {401, R"({"error":{"type":"authentication_error","code":"invalid_credentials","message":"Invalid API key or PIN."}})"};
    std::string parent = Parent();
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(!r.ok, "refused");
    t.Ok(r.error.has_value() && r.error->IsAuthError(), "the client's error, an auth error");
    t.Ok(r.message.find(Key) == std::string::npos && r.message.find(Pin) == std::string::npos, "no key or PIN in the words: " + r.message);
    t.Eq(Entries(parent), std::string(), "nothing left");
}

OW_TEST(folder_export_no_file_carries_the_key_or_the_pin) {
    std::string parent = Parent();
    OWClient c = Client(Testmoor());
    OWFolderExportReport r = RunExport(c, parent + "/world");
    t.Ok(r.ok, r.message);
    for (const char* rel : {"world/world.json", "world/elements/character/ann-a--0000c001.json", "world/elements/map/the-moor--0000a001.json"}) {
        std::string text = Text(parent + "/" + rel);
        t.Ok(!text.empty() && text.find(Key) == std::string::npos && text.find(Pin) == std::string::npos, rel);
    }
}

OW_TEST(folder_export_answers_once_with_a_transport_that_answers_later) {
    // Unreal's HTTP answers on a later tick: the export carries its own state between answers.
    struct Later : Routes {
        std::deque<std::function<void()>> held;
        void Send(OWRequest request, OWResponseHandler done) override {
            auto self = this;
            auto shared = std::make_shared<OWResponseHandler>(std::move(done));
            OWRequest copy = request;
            held.push_back([self, copy, shared]() mutable { self->Routes::Send(std::move(copy), std::move(*shared)); });
        }
    };
    auto later = std::make_shared<Later>();
    later->world = WireWorld;
    later->Serve("character", {Page({WireCharacter})});
    OWClientConfig config;
    config.api_key = Key;
    config.api_pin = Pin;
    config.base_url = Base;
    config.transport = later;
    OWClient c(std::move(config));
    std::string folder = Parent() + "/world";
    int answers = 0;
    OWFolderExportReport out;
    OWFolderExport::Export(c, folder, {}, [&](OWFolderExportReport r) { ++answers; out = std::move(r); });
    t.Eq(answers, 0, "nothing answered inside the call");
    while (!later->held.empty()) {
        auto next = std::move(later->held.front());
        later->held.pop_front();
        next();
    }
    t.Eq(answers, 1, "answered once");
    t.Ok(out.ok, out.message);
    t.Eq(out.counts["character"], size_t(1), "");
}

// -- Known-of stubs: the package's known_of_for_disk ---------------------------------------------------------------------

OW_TEST(folder_export_writes_a_stub_without_its_unset_fields_and_a_whole_element_as_served) {
    const std::string mara = R"({"id":"c0000000-0000-7000-8000-00000000000a","type":"character","name":"Mara","description":"",)"
                             R"("friends":["c0000000-0000-7000-8000-00000000000d"]})";
    auto routes = WorldOnly(WorldW);
    routes->Serve("character", {Page({StubDev, mara})});
    std::string folder = Parent() + "/world";
    OWClient c = Client(routes);
    OWFolderExportReport r = RunExport(c, folder);
    t.Ok(r.ok, r.message);
    t.Eq(Text(folder + "/elements/character/dev--0000000d.json"), StubDevOnDisk, "the package's known_of_for_disk bytes");
    t.Ok(Json(Text(folder + "/elements/character/mara--0000000a.json")) == Json(mara), "a whole element as served");
}

OW_TEST(folder_export_known_of_for_disk_keeps_a_stubs_public_fields_in_their_wire_place) {
    t.Eq(OWJson::Stringify(OWFolderExport::KnownOfForDisk(Json(PublicStubDev))), PublicStubDevOnDisk, "");
}

OW_TEST(folder_export_known_of_for_disk_keeps_zero_and_false) {
    OWJsonValue stub = Json(R"({"id":"c1","name":"","level":0,"dead":false,"tags":[],"x":{"a":[1]},"known_of":true})");
    std::string before = Compact(stub);
    OWJsonValue disk = OWFolderExport::KnownOfForDisk(stub);
    t.Eq(Keys(disk), std::string("id name level dead x known_of"), "a kept key even empty, 0 and false are values");
    t.Eq(Compact(stub), before, "the input is not changed");
    std::string keeps;
    for (const char* k : OWFolderExport::KnownOfKeeps) keeps += std::string(k) + " ";
    t.Eq(keeps, std::string("id type name description supertype subtype image_url created_at updated_at change_seq "), "keel's KNOWN_OF_BASE");
}

// -- Provenance: source, exported_as, producer, origin (the package's tests/test_export.py) ------------------------------

OW_TEST(folder_export_world_json_says_where_it_came_from_after_the_wires_keys) {
    std::string message;
    OWJsonValue world = Provenance(MoorRoutes(), &message);
    t.Eq(message, std::string(), "");
    const OWJsonValue* source = world.Find("source");
    t.Eq(source ? Compact(*source) : std::string(),
         std::string(R"({"server":"https://www.onlyworlds.com","world":"w-1","at":"2026-10-19T21:30:00.250Z"})"),
         "the API's origin, not its path; toISOString's shape");
    t.Eq(Keys(world), std::string("id name source producer format_version"), "");
}

OW_TEST(folder_export_the_clock_is_read_before_the_world) {
    auto routes = MoorRoutes();
    size_t read = 99;
    OWFolderExportOptions o;
    o.clock = [&] { read = routes->calls.size(); return MoorAt; };
    OWClient c = Client(routes, "ow_r_test", "");
    OWFolderExportReport r = RunExport(c, Parent() + "/world", o);
    t.Ok(r.ok, r.message);
    t.Eq(read, size_t(0), "source.at is when the export began");
}

OW_TEST(folder_export_the_clock_writes_any_moment_as_to_iso_string_does) {
    for (const auto& [ms, want] : std::vector<std::pair<int64_t, std::string>>{
             {0, "1970-01-01T00:00:00.000Z"}, {951782400999, "2000-02-29T00:00:00.999Z"},
             {1792443600005, "2026-10-19T21:00:00.005Z"}, {4102444799000, "2099-12-31T23:59:59.000Z"}}) {
        auto routes = MoorRoutes();
        OWClient c = Client(routes, "ow_r_test", "");
        std::string folder = Parent() + "/world";
        OWFolderExportReport r = RunExport(c, folder, Pinned(ms));
        t.Ok(r.ok, r.message);
        t.Eq(Str(WorldJsonOf(folder).Find("source") ? WorldJsonOf(folder).Find("source")->Find("at") : nullptr), want, want);
    }
}

OW_TEST(folder_export_an_owner_key_exports_the_owners_slice) {
    OWJsonValue world = Provenance(MoorRoutes(R"({"kind":"owner","role":"owner","character":"c-own"})"));
    const OWJsonValue* slice = world.Find("exported_as");
    t.Eq(slice ? Compact(*slice) : std::string(), std::string(R"({"kind":"owner","role":"owner","character":null})"),
         "the whole world, even with an owner_character");
    t.Eq(Keys(world), std::string("id name source exported_as producer format_version"), "");
}

OW_TEST(folder_export_a_members_key_exports_the_roles_slice_no_character) {
    for (std::string role : {"contributor", "co_builder"}) {
        OWJsonValue world = Provenance(MoorRoutes(R"({"kind":"person","role":")" + role + R"(","character":"c-9"})"));
        const OWJsonValue* slice = world.Find("exported_as");
        t.Eq(slice ? Compact(*slice) : std::string(), R"({"kind":"member","role":")" + role + R"(","character":null})", role);
    }
}

OW_TEST(folder_export_a_guests_key_exports_its_characters_slice_the_name_read_from_the_character) {
    auto routes = MoorRoutes(R"({"kind":"player","role":"guest","character":"c-pip"})");
    routes->Serve("character/c-pip", {R"({"id":"c-pip","type":"character","name":"Pip"})"});
    OWJsonValue world = Provenance(routes);
    const OWJsonValue* slice = world.Find("exported_as");
    t.Eq(slice ? Compact(*slice) : std::string(),
         std::string(R"({"kind":"character","role":"guest","character":{"id":"c-pip","name":"Pip"}})"), "");
}

OW_TEST(folder_export_no_me_writes_no_exported_as_and_the_export_stands) {
    auto routes = MoorRoutes();
    routes->refusals["me"] = {404, R"({"error":{"code":"not_found","message":"no"}})"};
    OWJsonValue world = Provenance(routes);
    t.Ok(!world.Has("exported_as"), "absent, never guessed");
    t.Ok(world.Has("source"), "the export stands");
    t.Ok(world.Find("producer") && *world.Find("producer") == OWFolderExport::Producer(),
         "the slice can be unknown; who made the folder never is");
}

OW_TEST(folder_export_a_reader_that_names_its_world_asks_no_me) {
    for (std::string key : {"ow_a_test", ""}) {
        auto routes = MoorRoutes(R"({"kind":"owner","role":"owner","character":null})");
        std::string folder = Parent() + "/world";
        OWClient c = Client(routes, key, "", "w-1");
        OWFolderExportReport r = RunExport(c, folder);
        t.Ok(r.ok, r.message);
        OWJsonValue world = WorldJsonOf(folder);
        t.Ok(!world.Has("exported_as"), "an account token or no key has no /me: the slice is unknown (" + key + ")");
        t.Eq(Str(world.Find("source") ? world.Find("source")->Find("world") : nullptr), std::string("w-1"), "");
        std::vector<std::string> sent = routes->RoutesSent();
        t.Ok(std::find(sent.begin(), sent.end(), "/me/") == sent.end(), "no /me was asked");
    }
}

OW_TEST(folder_export_a_guest_whose_character_cannot_be_read_writes_no_exported_as) {
    auto routes = MoorRoutes(R"({"kind":"person","role":"guest","character":"c-x"})");
    routes->refusals["character/c-x"] = {403, R"({"error":{"code":"forbidden","message":"no"}})"};
    OWJsonValue world = Provenance(routes);
    t.Ok(world.Has("source") && !world.Has("exported_as"), "");
}

OW_TEST(folder_export_a_fresh_export_writes_the_producer_line_in_the_specs_key_order) {
    OWJsonValue world = Provenance(MoorRoutes(R"({"kind":"owner","role":"owner","character":null})"));
    const OWJsonValue* producer = world.Find("producer");
    t.Eq(producer ? Compact(*producer) : std::string(),
         std::string(R"({"tool":"onlyworlds-unreal","version":")") + OWFolderExport::ProducerVersion + R"(","input":"server"})",
         "the spec's key order");
}

OW_TEST(folder_export_an_export_is_a_birth_so_its_lines_are_its_own_where_the_body_had_them) {
    auto routes = WorldOnly(R"({"id":"w-1","name":"Moor","source":{"server":null,"world":"w0","at":"2026-10-01T00:00:00.000Z"},)"
                            R"("producer":{"tool":"azgaar-converter","version":"0.4.0","input":"azgaar-map"}})");
    OWJsonValue world = Provenance(routes);
    t.Ok(world.Find("producer") && *world.Find("producer") == OWFolderExport::Producer(), "");
    t.Eq(Str(world.Find("source") ? world.Find("source")->Find("world") : nullptr), std::string("w-1"), "");
    t.Eq(Keys(world), std::string("id name source producer format_version"), "replaced where they stood");
}

OW_TEST(folder_export_an_export_carries_the_servers_origin_beside_its_own_lines) {
    const std::string first = R"({"source":null,"producer":{"tool":"lantern","version":"0.9.0","input":"bare"}})";
    OWJsonValue world = Provenance(WorldOnly(R"({"id":"w-1","name":"W","public_read":false,"origin":)" + first + "}"));
    t.Eq(world.Find("origin") ? Compact(*world.Find("origin")) : std::string(), first, "as served");
    t.Eq(Str(world.Find("source") ? world.Find("source")->Find("world") : nullptr), std::string("w-1"), "this export's own line, fresh");
    t.Eq(Keys(world), std::string("id name public_read origin source producer format_version"), "");
}

OW_TEST(folder_export_an_export_of_a_world_serving_no_origin_writes_none) {
    OWJsonValue world = Provenance(MoorRoutes(R"({"kind":"owner","role":"owner","character":null})"));
    t.Ok(world.Has("source") && !world.Has("origin"), "");
}

OW_TEST(folder_export_the_producers_version_is_the_plugins_version) {
    OWBytes raw;
    t.Ok(OWFiles::Read(P().root + "/Plugin/OnlyWorlds/OnlyWorlds.uplugin", raw), "the .uplugin read");
    OWJsonValue plugin = OWJson::Parse(raw).value;
    t.Eq(std::string(OWFolderExport::ProducerVersion), Str(plugin.Find("VersionName")),
         "ProducerVersion moves with the .uplugin's VersionName: a release changes both");
    t.Eq(std::string(OWFolderExport::ProducerTool), std::string("onlyworlds-unreal"), "the name the release zips publish under");
}

OW_TEST(folder_export_the_mapping_names_only_what_it_knows) {
    auto pip = [](const std::string&) { return std::optional<std::string>("Pip"); };
    auto map = [&](const char* me, std::function<std::optional<std::string>(const std::string&)> name) {
        OWJsonValue body = Json(me);
        auto out = OWFolderExport::ExportedAs(&body, name);
        return out ? Compact(*out) : std::string("absent");
    };
    t.Ok(!OWFolderExport::ExportedAs(nullptr, pip).has_value(), "no /me");
    t.Eq(map("{}", pip), std::string("absent"), "an answer with no kind or role");
    t.Eq(map(R"({"kind":"person","role":"viewer"})", pip), std::string("absent"), "a role the table does not name");
    t.Eq(map(R"({"kind":"player","role":"guest","character":null})", pip), std::string("absent"), "a guest with no Character");
    t.Eq(map(R"({"kind":"player","role":"guest","character":"c1"})", [](const std::string&) { return std::optional<std::string>(); }),
         std::string("absent"), "a name that cannot be read");
    t.Eq(map(R"({"kind":"player","role":"guest","character":"c1"})", [](const std::string&) { return std::optional<std::string>(""); }),
         std::string(R"({"kind":"character","role":"guest","character":{"id":"c1","name":""}})"), "an empty name is a name served");
    t.Eq(map(R"({"kind":"owner","role":"guest","character":"c1"})", pip), std::string(R"({"kind":"owner","role":"owner","character":null})"),
         "kind owner decides first");
}
