// OWWorldArchive::Read on every .owworld in the conformance set (schema-dist fixtures/owworld/, the pinned copy): the
// fixture's bytes are the set's, the reader verdict and the reason as verdicts.json gives them, OW-10's strip, and
// OW-9's round trip (read, pack, read again). As Unity's NUnit class and the Godot SDK's test_archive_fixtures do.

#include "ow_test.h"
#include "sha256.h"

#include "OWJson.h"
#include "OWWorldArchive.h"

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace OnlyWorlds;
using owtest::P;

namespace {

std::shared_ptr<OWJsonValue> Verdicts() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWBytes bytes;
        OWFiles::Read(P().fixtures + "/verdicts.json", bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

uint64_t g_largest = 0;  // the largest buffer any fixture's inflate allocated (lean 3)
std::string g_largest_at;

void Case(owtest::T& t, const OWJsonValue& v) {
    if (Str(v, "status") == "not_generated") {
        t.Skip("not generated (on demand only): " + Str(v, "why").substr(0, 80));
        return;
    }
    std::string path = P().fixtures + "/" + Str(v, "file");
    OWBytes raw;
    if (!OWFiles::Read(path, raw)) {
        t.Fail("the fixture is missing: " + path);
        return;
    }
    t.Eq(owtest::Sha256Hex(raw), Str(v, "sha256"), "the fixture's bytes are the set's");
    OWArchiveRead r = OWWorldArchive::Read(raw);
    if (r.largest_allocation > g_largest) {
        g_largest = r.largest_allocation;
        g_largest_at = Str(v, "file");
    }
    std::string got = r.ok ? "accept" : "refuse";
    t.Eq(got, Str(v, "reader"), "reader verdict (" + r.reason + ": " + r.message + ")");
    if (Str(v, "reader") == "refuse") t.Eq(r.reason, Str(v, "reason"), "reason (" + r.message + ")");
    if (r.ok && v.Has("strips")) {
        std::string key = Str(v, "strips");
        t.Ok(!r.world.Has(key), key + " stripped from the world");
        OWJsonParseResult back = OWJson::Parse(r.files["world.json"]);
        t.Ok(back.ok && !back.value.Has(key), key + " not in the returned world.json");
    }
    const OWJsonValue* rt = v.Find("roundtrip");
    if (r.ok && rt && rt->IsBool() && rt->AsBool()) {
        std::string top = r.prefix.empty() ? "" : r.prefix.substr(0, r.prefix.size() - 1);
        OWPacked packed = OWWorldArchive::PackFiles(r.files, top);
        t.Ok(packed.ok, "repacked");
        OWArchiveRead again = OWWorldArchive::Read(packed.bytes);
        t.Ok(again.ok, "the repacked file reads (" + again.message + ")");
        t.Ok(again.files == r.files, "round trip: every entry byte-equal");
    }
}

}  // namespace

OW_TEST(archive_the_set_is_the_pinned_one) {
    auto doc = Verdicts();
    const OWJsonValue* fixtures = doc->Find("fixtures");
    t.Ok(fixtures && fixtures->IsArray(), "verdicts.json parses, with its fixtures");
    if (!fixtures) return;
    size_t generated = 0;
    for (const OWJsonValue& v : fixtures->Items())
        if (Str(v, "status") != "not_generated") ++generated;
    size_t on_disk = 0;
    for (const std::string& f : OWFiles::List(P().fixtures, false)) {
        auto ends = [&](const char* x) { std::string s = x; return f.size() >= s.size() && f.compare(f.size() - s.size(), s.size(), s) == 0; };
        if (ends(".owworld") || ends(".zip")) ++on_disk;
    }
    t.Eq(on_disk, generated, "every generated fixture is on disk, and nothing else");
    t.Eq(generated, size_t(96), "the set's 96 generated fixtures (dist.61)");
    std::printf("       the set: %zu fixtures in verdicts.json, %zu generated, %zu on disk\n", fixtures->Size(), generated, on_disk);
}

OW_CASES(archive_fixture_cases) {
    owtest::Cases out;
    auto doc = Verdicts();
    const OWJsonValue* fixtures = doc->Find("fixtures");
    if (!fixtures) return out;
    for (const OWJsonValue& v : fixtures->Items()) {
        std::string name = "archive_fixture:" + Str(v, "file");
        out.emplace_back(name, [doc, &v](owtest::T& t) { Case(t, v); });
    }
    // Lean 3: what the largest single allocation was, over the whole set (runs after every case).
    out.emplace_back("archive_largest_allocation", [](owtest::T& t) {
        std::printf("       the largest inflate buffer over the set: %llu bytes (%s)\n",
                    static_cast<unsigned long long>(g_largest), g_largest_at.c_str());
        t.Ok(g_largest < (1ull << 20), "no fixture asks the reader for a buffer of a megabyte or more");
    });
    return out;
}

OW_TEST(archive_reasons_are_the_spec_list) {
    std::vector<std::string> mine(std::begin(OWWorldArchive::Reasons), std::end(OWWorldArchive::Reasons));
    auto doc = Verdicts();
    const OWJsonValue* reasons = doc->Find("reasons");
    t.Ok(reasons && reasons->IsArray(), "verdicts.json lists the reasons");
    if (!reasons) return;
    std::vector<std::string> theirs;
    for (const OWJsonValue& r : reasons->Items()) theirs.push_back(r.Text());
    std::sort(mine.begin(), mine.end());
    std::sort(theirs.begin(), theirs.end());
    t.Ok(mine == theirs, "the twelve causes, the same list");
}
