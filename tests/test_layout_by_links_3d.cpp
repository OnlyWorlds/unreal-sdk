// by-links-3d on its shared fixture: the pinned dist's profiles/layout-by-links-3d.fixture.json and the directions table
// beside it (the default layouts page, profiles/layout.md, serial 137), read from the vendored folder through OWJson,
// whose from_chars reads every double exactly. The fixture's unplaced suite is read whole (UnplacedByLinks3d): each case's
// want, the list of ids, equal exactly and in its order. When the pin moves the fixture: update the hashes.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLayout.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const FixtureSha256 = "acd6da7252493485cafc24356a84c53656d078872a28c695c0f7554009b6fb2a";
const char* const TableSha256 = "37dc2bbb4a1769e4ddafc2d0a27c9889229c3ef483e8f7cd6c9ba66a0d1afb87";

std::string Profiles() { return P().root + "/codegen/schema-dist/profiles/"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(Profiles() + "layout-by-links-3d.fixture.json", bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

std::vector<std::string> Names(const char* list) {
    std::vector<std::string> out;
    if (const OWJsonValue* l = Fixture()->Find(list))
        for (const OWJsonValue& c : l->Items()) out.push_back(c.Find("name")->Text());
    return out;
}

const OWJsonValue* Named(const char* list, const std::string& name) {
    const OWJsonValue* l = Fixture()->Find(list);
    if (!l) return nullptr;
    for (const OWJsonValue& c : l->Items())
        if (c.Find("name") && c.Find("name")->Text() == name) return &c;
    return nullptr;
}

std::string Joined(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) out += id + " ";
    return out;
}

void UnplacedCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    OWLayoutUnplaced got = OWLayout::UnplacedByLinks3d(*c.Find("graph"), *c.Find("placed"));
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, std::string("by-links-3d"), name + ": the method");
    std::vector<std::string> want;
    for (const auto& w : c.Find("want")->Items()) want.push_back(w.Text());
    t.Eq(Joined(got.unplaced), Joined(want), name + ": the unplaced ids, in their order");
}

}  // namespace

OW_TEST(layout_by_links_3d_fixture_and_table_are_the_pinned_dists_bytes) {
    OWBytes fixture, table, manifest;
    t.Ok(OWFiles::Read(Profiles() + "layout-by-links-3d.fixture.json", fixture), "the fixture is vendored");
    t.Ok(OWFiles::Read(Profiles() + "layout-by-links-3d.directions.json", table), "the directions table is vendored");
    t.Eq(owtest::Sha256Hex(fixture), std::string(FixtureSha256), "the fixture's bytes are the ones this test pins");
    t.Eq(owtest::Sha256Hex(table), std::string(TableSha256), "the table's bytes are the ones this test pins");
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* f = files ? files->Find("profiles/layout-by-links-3d.fixture.json") : nullptr;
    const OWJsonValue* d = files ? files->Find("profiles/layout-by-links-3d.directions.json") : nullptr;
    t.Ok(f && f->Text() == FixtureSha256, "MANIFEST.json records the fixture's sha256");
    t.Ok(d && d->Text() == TableSha256, "MANIFEST.json records the table's sha256");
    const OWJsonValue* named = Fixture()->Find("table_sha256");
    t.Ok(named && named->Text() == TableSha256, "the fixture names the table by its sha256");
    t.Eq(Names("cases").size(), static_cast<size_t>(116), "116 cases");
    t.Eq(Names("unplaced").size(), static_cast<size_t>(9), "nine unplaced cases");
}

OW_CASES(layout_by_links_3d_unplaced_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Names("unplaced").empty()) {
        out.emplace_back("layout-by-links-3d-unplaced:not-found", [](owtest::T& t) { t.Fail("the fixture did not load"); });
        return out;
    }
    for (const std::string& name : Names("unplaced"))
        out.emplace_back("layout-by-links-3d-unplaced:" + name, [doc, name](owtest::T& t) { UnplacedCase(t, *Named("unplaced", name)); });
    return out;
}
