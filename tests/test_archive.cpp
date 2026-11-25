// OWWorldArchive::Read on every .owworld in the conformance set (schema-dist fixtures/owworld/, the pinned copy): the
// fixture's bytes are the set's, the reader verdict and the reason as verdicts.json gives them, the files a reader
// leaves out by reason (OW-25), OW-10's strip, and OW-9's round trip (read, pack, read again). As Unity's NUnit class and the Godot SDK's test_archive_fixtures do.

#include "ow_test.h"
#include "sha256.h"

#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <algorithm>
#include <iterator>
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
    const OWJsonValue* left = v.Find("left_out");
    if (r.ok && left && left->IsObject() && !v.Has("side")) {
        // A reader's left_out (OW-25): by reason, the files the world's reader skipped with that word, by path.
        std::unique_ptr<OWWorld> w = OWWorld::FromFiles(r.files);
        t.Ok(w != nullptr, "the files read as a world");
        for (size_t i = 0; w && i < left->Keys().size(); ++i) {
            const std::string& reason = left->Keys()[i];
            std::vector<std::string> want, skipped;
            for (const OWJsonValue& x : left->Items()[i].Items()) want.push_back(x.Text());
            for (const std::string& e : w->errors) {
                size_t at = e.find(": " + reason);
                if (at != std::string::npos && at > 0) skipped.push_back(e.substr(0, at));
            }
            std::sort(want.begin(), want.end());
            std::sort(skipped.begin(), skipped.end());
            t.Ok(skipped == want, "left out as " + reason + " (" + std::to_string(skipped.size()) + " skipped, " +
                                      std::to_string(want.size()) + " in the set)");
        }
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
    t.Eq(on_disk, generated, "every generated fixture is on disk, and nothing else (the count is verdicts.json's)");
    t.Ok(generated > 0, "a set with no fixtures checks nothing");
    std::printf("       the set: %zu fixtures in verdicts.json, %zu generated, %zu on disk, at %s\n", fixtures->Size(), generated,
                on_disk, P().fixtures.c_str());
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
    // Fourteen causes: section 4.7's twelve, section 4.8's name_case and OW-25's too_deep (a world.json past 200; both
    // ruled 2026-11-25). A set from before the serial that adds one holds every cause but the later ones (serial 145
    // lists twelve, 147 thirteen); a set from after both, the same fourteen.
    t.Eq(mine.size(), size_t(14), "the fourteen causes");
    t.Ok(std::includes(mine.begin(), mine.end(), theirs.begin(), theirs.end()), "every cause the set lists is one of ours");
    std::vector<std::string> missing;
    std::set_difference(mine.begin(), mine.end(), theirs.begin(), theirs.end(), std::back_inserter(missing));
    bool later = true;
    for (const std::string& m : missing) later = later && (m == "name_case" || m == "too_deep");
    t.Ok(later, "the set's list is ours but for causes a later serial adds");
}
