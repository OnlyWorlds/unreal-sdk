// OWNames::TypeWord on the names profile's conformance fixture (schema-dist profiles/names.fixture.json, dist.92's
// copy, its sha256 held to MANIFEST.json): each case's word, or absent for the standard's name. Every case runs as its
// own test and is counted, so a dropped case fails. Then readings the fixture leaves open, each as the npm SDK's
// typeWord reads it.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWNames.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.92's MANIFEST.json records it.
const char* const DistSha256 = "7d012081b5df7bab3e2223333f87a347417c3061c62b38947fc952e8c4d2377a";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/names.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

OWJsonValue Parse(std::string_view text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? std::move(r.value) : OWJsonValue();
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* f = v.Find(key);
    return f && f->IsString() ? f->Text() : std::string();
}

// A word as the tests compare it: quoted, or null for the standard's name.
std::string Shown(const std::optional<std::string>& word) { return word ? "\"" + *word + "\"" : "null"; }
std::string Shown(const OWJsonValue* word) { return word && word->IsString() ? "\"" + word->Text() + "\"" : "null"; }

// A world whose x_names is `names` (JSON text).
OWJsonValue World(const std::string& names) { return Parse("{\"name\":\"W\",\"x_names\":" + names + "}"); }

std::string Word(const OWJsonValue& world, std::string_view type, std::optional<std::string_view> supertype = std::nullopt,
                 bool many = false) {
    return Shown(OWNames::TypeWord(world, type, supertype, many));
}

void Case(owtest::T& t, const OWJsonValue& c) {
    const OWJsonValue* map = c.Find("map");
    const OWJsonValue* many = c.Find("many");
    if (!map || !many || !many->IsBool() || !c.Has("word")) {
        t.Fail("a case with a map, many and a word");
        return;
    }
    OWJsonValue world = OWJsonValue::Object();
    world.Set("name", OWJsonValue::String("W"));
    world.Set(OWNames::Field, *map);
    const OWJsonValue* st = c.Find("supertype");
    std::optional<std::string_view> supertype;
    if (st && st->IsString()) supertype = st->Text();
    t.Eq(Word(world, Str(c, "type"), supertype, many->AsBool()), Shown(c.Find("word")), "the word");
}

}  // namespace

OW_TEST(names_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the names fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.94's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/names.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("x_names"), "the names profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::set<std::string> names;
    size_t standard = 0;
    for (const OWJsonValue& c : cases->Items()) {
        names.insert(Str(c, "name"));
        const OWJsonValue* w = c.Find("word");
        if (w && w->IsNull()) ++standard;
    }
    t.Eq(cases->Size(), size_t(29), "29 cases");
    t.Eq(names.size(), size_t(29), "each name its own");
    t.Eq(standard, size_t(14), "14 print the standard's name");
}

OW_CASES(names_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("names_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("names_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// -- readings the fixture leaves open ---------------------------------------------------------------------------------

namespace {
const char* const Employee = "{\"version\":1,\"types\":{\"character\":{\"one\":\"Employee\"}}}";
const char* const Agent = "{\"version\":1,\"supertypes\":{\"character\":{\"Agent\":{\"one\":\"AI agent\"}}}}";
}  // namespace

// The number 1 as JavaScript's === 1 reads it: any spelling of it, never true or text.
OW_TEST(names_version_is_the_number_one) {
    const std::string types = ",\"types\":{\"character\":{\"one\":\"Employee\"}}}";
    t.Eq(Word(World("{\"version\":1" + types), "character"), std::string("\"Employee\""), "1");
    t.Eq(Word(World("{\"version\":1.0" + types), "character"), std::string("\"Employee\""), "1.0");
    t.Eq(Word(World("{\"version\":1e0" + types), "character"), std::string("\"Employee\""), "1e0");
    t.Eq(Word(World("{\"version\":\"1\"" + types), "character"), std::string("null"), "\"1\"");
    t.Eq(Word(World("{\"version\":true" + types), "character"), std::string("null"), "true");
    t.Eq(Word(World("{\"version\":1.5" + types), "character"), std::string("null"), "1.5");
    t.Eq(Word(World("{\"version\":null" + types), "character"), std::string("null"), "null");
    t.Eq(Word(World("{\"types\":{\"character\":{\"one\":\"Employee\"}}}"), "character"), std::string("null"), "no version");
}

OW_TEST(names_no_world_no_map_or_a_map_that_is_not_an_object) {
    t.Eq(Word(OWJsonValue(), "character"), std::string("null"), "a null world");
    t.Eq(Word(Parse("{\"name\":\"W\"}"), "character"), std::string("null"), "no map");
    t.Eq(Word(World("null"), "character"), std::string("null"), "a null map");
    t.Eq(Word(World("\"Employee\""), "character"), std::string("null"), "a string");
    t.Eq(Word(World(std::string("[") + Employee + "]"), "character"), std::string("null"), "a list");
    t.Eq(Word(Parse("[]"), "character"), std::string("null"), "a world that is not an object");
}

// npm refuses a types or supertypes key that is present and null (only an absent one is no part of the map).
OW_TEST(names_a_null_part_is_present_and_refused) {
    const std::string agent = "\"supertypes\":{\"character\":{\"Agent\":{\"one\":\"AI agent\"}}}";
    t.Eq(Word(World("{\"version\":1," + agent + "}"), "character", "Agent"), std::string("\"AI agent\""), "the word, without the null");
    t.Eq(Word(World("{\"version\":1,\"types\":null," + agent + "}"), "character", "Agent"), std::string("null"), "types: null");
    const std::string types = "{\"version\":1,\"types\":{\"character\":{\"one\":\"Employee\"}}";
    t.Eq(Word(World(types + ",\"supertypes\":null}"), "character"), std::string("null"), "supertypes: null");
    t.Eq(Word(World(types + ",\"supertypes\":{\"character\":null}}"), "character"), std::string("null"),
         "a supertypes value that is null");
    t.Eq(Word(World(types + ",\"supertypes\":{\"zone\":[]}}"), "character"), std::string("null"),
         "a supertypes value that is not an object refuses the map, for another type too");
    t.Eq(Word(World(types + ",\"supertypes\":{}}"), "character"), std::string("\"Employee\""), "empty supertypes");
}

OW_TEST(names_the_slug_is_matched_as_the_schema_spells_it) {
    OWJsonValue world = World(Employee);
    t.Eq(Word(world, "character"), std::string("\"Employee\""), "the slug");
    t.Eq(Word(world, "Character"), std::string("null"), "a type slug is lowercase; the fold is the supertype's");
    t.Eq(Word(world, " character"), std::string("null"), "a slug is never trimmed");
    t.Eq(Word(world, ""), std::string("null"), "no type");
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    const OWJsonValue* map = cases && cases->Size() ? cases->Items()[0].Find("map") : nullptr;
    t.Ok(map != nullptr, "the fixture's first map");
    if (!map) return;
    OWJsonValue full = OWJsonValue::Object();
    full.Set(OWNames::Field, *map);
    t.Eq(Word(full, "employee"), std::string("null"),
         "a key that is not one of the 22 is never read, asked for by name (the fixture asks only for species)");
}

OW_TEST(names_the_supertype_is_trimmed_by_unicode_white_space) {
    OWJsonValue world = World(Agent);
    t.Eq(Word(world, "character", "\xE3\x80\x80" "Agent" "\xC2\xA0"), std::string("\"AI agent\""),
         "ideographic and no-break spaces trim");
    t.Eq(Word(world, "character", "\xEF\xBB\xBF" "Agent"), std::string("null"), "a byte order mark is not White_Space and stays");
    t.Eq(Word(world, "character", "\xE2\x80\x8B" "Agent"), std::string("null"), "a zero width space is not White_Space and stays");
}

OW_TEST(names_the_supertype_is_folded_by_simple_case_folding) {
    // Σοφός, and ΣΟΦΌΣ: capital sigma folds with final sigma.
    OWJsonValue world =
        World("{\"version\":1,\"supertypes\":{\"character\":{\"\xCE\xA3\xCE\xBF\xCF\x86\xCF\x8C\xCF\x82\":{\"one\":\"Sage\"}}}}");
    t.Eq(Word(world, "character", "\xCE\xA3\xCE\x9F\xCE\xA6\xCE\x8C\xCE\xA3"), std::string("\"Sage\""), "capital sigma folds with final sigma");
}

OW_TEST(names_a_blank_supertype_goes_straight_to_the_types_word) {
    OWJsonValue world = World(
        "{\"version\":1,\"types\":{\"character\":{\"one\":\"Employee\"}},"
        "\"supertypes\":{\"character\":{\"\":{\"one\":\"Nobody\"},\" \":{\"one\":\"Nobody\"}}}}");
    t.Eq(Word(world, "character", "  "), std::string("\"Employee\""), "white space only");
    t.Eq(Word(world, "character", ""), std::string("\"Employee\""), "empty");
    t.Eq(Word(world, "character"), std::string("\"Employee\""), "none");
}

// Two keys that fold to one supertype: the first in the map's order with a word prints.
OW_TEST(names_keys_that_fold_alike_go_in_the_maps_order) {
    OWJsonValue world = World(
        "{\"version\":1,\"supertypes\":{\"character\":{"
        "\"agent\":{\"one\":\" \"},\"AGENT\":{\"one\":\"Second\"},\"Agent\":{\"one\":\"Third\"}}}}");
    t.Eq(Word(world, "character", "Agent"), std::string("\"Second\""), "an ignored entry is no entry; the next match prints");
}

OW_TEST(names_the_word_is_printed_as_written) {
    OWJsonValue world = World("{\"version\":1,\"types\":{\"character\":{\"one\":\"eMPLOYEE\",\"many\":\"ai Agents\"}}}");
    t.Eq(Word(world, "character"), std::string("\"eMPLOYEE\""), "one");
    t.Eq(Word(world, "character", std::nullopt, true), std::string("\"ai Agents\""), "many");
}
