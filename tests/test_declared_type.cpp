// A declared type (the folder format's section 4.1, ruled 2026-11-22): a string exactly equal to one of the 22 type
// slugs as the standard spells them; anything else in "type" (an unknown word, another case such as "Location", a
// number, null) declares nothing, and the directory fills it. A file whose body declares one of the 22 and sits in
// another type's directory is read as the body says, under spatial/ as well (any of the 22, not only the four).
//
// The cases (tests/data/declared-type.cases.json) are the same bytes as the Python package's
// tests/fixtures/declared-type.cases.json and the TypeScript SDK's test/fixtures/declared-type.cases.json, held by their
// sha256. The folder on disk (ReadFolder) and the same files in memory (FromFiles, which a world file reads through) are
// each asked every case.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const CasesSha256 = "89968d85deaa4d5974a20a4d73aab80c7e8ec374ee4236b0809e00c405831b96";
const std::string WorldJson = "{\"id\": \"w\", \"name\": \"W\"}";

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const OWBytes& CasesBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/declared-type.cases.json", bytes);
        read = true;
    }
    return bytes;
}

struct Case {
    std::string name, path, text, want;
};

std::vector<Case> Cases() {
    std::vector<Case> out;
    OWJsonParseResult r = OWJson::Parse(CasesBytes());
    const OWJsonValue* cases = r.ok ? r.value.Find("cases") : nullptr;
    if (!cases) return out;
    for (const OWJsonValue& c : cases->Items())
        out.push_back({c.Find("name")->Text(), c.Find("path")->Text(), c.Find("text")->Text(), c.Find("want")->Text()});
    return out;
}

OWFileMap Files(const Case& c) {
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    files[c.path] = Bytes(c.text);
    return files;
}

std::string Served(OWWorld* w) {
    if (!w) return "<no world>";
    OWElement* e = w->Get("x1");
    return e ? e->Type() : "<none>";
}

const Case* Named(const std::vector<Case>& cases, const std::string& name) {
    for (const Case& c : cases)
        if (c.name == name) return &c;
    return nullptr;
}

}  // namespace

OW_TEST(declared_type_the_cases_file_is_the_pinned_bytes) {
    t.Eq(owtest::Sha256Hex(CasesBytes()), std::string(CasesSha256), "the cases file's sha256 (the Python package's bytes)");
    t.Eq(Cases().size(), size_t(21), "21 cases");
}

OW_TEST(declared_type_every_case_the_folder_on_disk_serves_the_type_the_case_wants) {
    std::string off;
    int n = 0;
    for (const Case& c : Cases()) {
        std::string root = P().tmp + "/declared-case-" + std::to_string(n++);
        OWFiles::RemoveTree(root);
        OWFiles::MakeDirs(root);
        for (const auto& kv : Files(c)) OWFiles::Write(root + "/" + kv.first, kv.second, true);
        std::unique_ptr<OWWorld> w = OWWorld::ReadFolder(root);
        std::string got = Served(w.get());
        if (got != c.want) off += (off.empty() ? "" : "; ") + c.name + ": served '" + got + "', want '" + c.want + "'";
        OWFiles::RemoveTree(root);
    }
    t.Eq(off, std::string(), "every case");
}

OW_TEST(declared_type_every_case_the_same_files_in_memory_serve_the_type_the_case_wants) {
    std::string off;
    for (const Case& c : Cases()) {
        std::unique_ptr<OWWorld> w = OWWorld::FromFiles(Files(c));
        std::string got = Served(w.get());
        if (got != c.want) off += (off.empty() ? "" : "; ") + c.name + ": served '" + got + "', want '" + c.want + "'";
    }
    t.Eq(off, std::string(), "every case");
}

OW_TEST(declared_type_the_body_stays_as_written_and_of_type_asks_the_served_type) {
    std::vector<Case> cases = Cases();
    const Case* capital = Named(cases, "another type, capital first");
    const Case* other = Named(cases, "another type's slug");
    t.Ok(capital && other, "the two cases are in the file");
    if (!capital || !other) return;
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(Files(*capital));
    t.Eq(w->Get("x1")->body.Find("type")->Text(), std::string("Location"), "the body keeps what it says");
    t.Eq(w->Get("x1")->folder_type, std::string("character"), "and the folder is the folder");
    w = OWWorld::FromFiles(Files(*other));
    t.Eq(w->OfType("location").size(), size_t(1), "a Location in elements/character/ is found by its type");
    t.Eq(w->OfType("character").size(), size_t(0), "and not by its folder");
    t.Eq(w->Get("x1")->source_path, other->path, "and keeps the file it was read from");
}

OW_TEST(declared_type_an_added_element_serves_its_added_type_unless_its_body_declares_one_of_the_22) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", Bytes(WorldJson)}});
    w->Add("character", OWJson::Parse(Bytes("{\"id\":\"a1\",\"type\":\"Location\"}")).value);
    w->Add("character", OWJson::Parse(Bytes("{\"id\":\"a2\",\"type\":\"location\"}")).value);
    w->Add("character", OWJson::Parse(Bytes("{\"id\":\"a3\",\"type\":7}")).value);
    t.Eq(w->Get("a1")->Type(), std::string("character"), "\"Location\" declares nothing");
    t.Eq(w->Get("a2")->Type(), std::string("location"), "\"location\" is declared");
    t.Eq(w->Get("a3")->Type(), std::string("character"), "7 declares nothing");
}

// Not in the shared cases: under spatial/ a body may declare any of the 22, not only the four the layout held (the
// Python package's reader serves any of ELEMENT_TYPES there; the TypeScript SDK's the same).
OW_TEST(declared_type_under_spatial_any_of_the_22_is_declared) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", Bytes(WorldJson)},
                                                     {"spatial/pin/x.json", Bytes("{\"id\":\"x1\",\"type\":\"character\"}")},
                                                     {"spatial/pin/y.json", Bytes("{\"id\":\"y1\",\"type\":\"Character\"}")}});
    t.Eq(w->Get("x1")->Type(), std::string("character"), "spatial/pin/ declaring character");
    t.Eq(w->Get("y1")->Type(), std::string("pin"), "spatial/pin/ declaring Character declares nothing");
}
