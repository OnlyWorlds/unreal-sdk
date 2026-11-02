// OWMapSpace on the map-space profile's conformance fixture (schema-dist profiles/map-space.fixture.json, dist.106's
// copy, its sha256 held to MANIFEST.json): every case by name, its elements read as served and the whole answer
// compared as bytes (key order, list order and each number's spelling as JSON.stringify writes it); then the profile's
// invariants over every case (four keys in order, read twice the same bytes, the rows held one per id read forwards and
// backwards the same bytes, every Marker and Pin held placed or counted once) and readings the fixture leaves open (a
// caller's mistake, rows that are no element, a whole number's edges).

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWMapSpace.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.106's MANIFEST.json records it.
const char* const DistSha256 = "7622c9cc44f7254481b29add8846e2077e431353f993e8fc95702d7a530279af";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/map-space.fixture.json"; }

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

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }
std::string Str(const OWJsonValue& v, const char* key) { return Str(v.Find(key)); }

// The bytes JSON.stringify writes (section 5's form): key order and list order kept, a number spelled as JavaScript
// spells it, so the fixture's `100.0` and the answer's `100` are one value, as the packages compare them.
std::string Bytes(const OWJsonValue& v) { return OWJson::Stringify(v); }

std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::vector<const OWJsonValue*> Cases() {
    std::vector<const OWJsonValue*> out;
    if (const OWJsonValue* cases = Fixture()->Find("cases"))
        for (const OWJsonValue& c : cases->Items()) out.push_back(&c);
    return out;
}

const OWJsonValue& ElementsOf(const OWJsonValue& c) {
    static const OWJsonValue none;
    const OWJsonValue* e = c.Find("elements");
    return e ? *e : none;
}

void Case(owtest::T& t, const OWJsonValue& c) {
    const OWJsonValue* want = c.Find("want");
    if (!c.Find("elements") || !want || !want->IsObject()) return t.Fail("the case has no elements or no want");
    OWWorldMapSpace got = OWMapSpace::MapSpaceOf(ElementsOf(c));
    t.Eq(got.error, std::string(), "no caller's mistake");
    OWJsonValue answer = got.ToJson();
    t.Ok(Bytes(answer) == Bytes(*want),
         "the whole answer: gave " + Compact(answer) + ", the case says " + Compact(*want));
}

bool IsRow(const OWJsonValue& e) {
    const OWJsonValue* type = e.Find("type");
    const OWJsonValue* id = e.Find("id");
    return e.IsObject() && type && type->IsString() && id && id->IsString() && !id->Text().empty();
}

// The elements as the reader holds them: the rows only, one per type and id, the last served (reading I).
OWJsonValue Served(const OWJsonValue& elements) {
    std::vector<const OWJsonValue*> rows;
    std::map<std::pair<std::string, std::string>, size_t> at;
    for (const OWJsonValue& e : elements.Items()) {
        if (!IsRow(e)) continue;
        auto [it, fresh] = at.emplace(std::make_pair(Str(e, "type"), Str(e, "id")), rows.size());
        if (fresh) rows.push_back(&e);
        else rows[it->second] = &e;
    }
    OWJsonValue out = OWJsonValue::Array();
    for (const OWJsonValue* r : rows) out.Push(*r);
    return out;
}

}  // namespace

OW_TEST(map_space_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the map-space fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.106's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/map-space.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("map-space"), "the map-space profile's fixture");
    std::set<std::string> names, choices;
    for (const OWJsonValue* c : Cases()) {
        names.insert(Str(*c, "name"));
        if (const OWJsonValue* ch = c->Find("choice")) choices.insert(Str(ch));
    }
    t.Eq(Cases().size(), size_t(34), "34 cases");
    t.Eq(names.size(), size_t(34), "each name its own");
    t.Ok(choices == std::set<std::string>{"A", "B", "C", "D", "E", "G", "H", "I", "J", "K"},
         "the cases that carry a ruled reading: A to E, G to K");
}

OW_CASES(map_space_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Cases().empty()) {
        out.emplace_back("map_space_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue* c : Cases())
        out.emplace_back("map_space_fixture:" + Str(*c, "name"), [doc, c](owtest::T& t) { Case(t, *c); });
    return out;
}

// -- the profile's invariants over every case ----------------------------------------------------------------------

OW_TEST(map_space_every_case_holds_the_profiles_invariants) {
    for (const OWJsonValue* c : Cases()) {
        const std::string name = Str(*c, "name");
        const OWJsonValue& elements = ElementsOf(*c);
        OWWorldMapSpace got = OWMapSpace::MapSpaceOf(elements);
        OWJsonValue answer = got.ToJson();
        t.Ok(answer.Keys() == std::vector<std::string>{"maps", "zones", "pins", "out"},
             name + ": the four keys, in order");
        const std::string once = Bytes(answer);
        t.Eq(Bytes(OWMapSpace::MapSpaceOf(elements).ToJson()), once, name + ": read twice, the same bytes");
        // The rows as the reader holds them (one per type and id, the last served) answer the same; and reversed, no
        // row order decides anything once each id is one row.
        const OWJsonValue served = Served(elements);
        t.Eq(Bytes(OWMapSpace::MapSpaceOf(served).ToJson()), once, name + ": from the rows held, one per id");
        OWJsonValue back = served;
        std::reverse(back.Items().begin(), back.Items().end());
        t.Eq(Bytes(OWMapSpace::MapSpaceOf(back).ToJson()), once, name + ": from the rows held, reversed");
        // Every Marker and Pin held is placed or counted, once.
        int64_t rows = 0, placed = 0;
        for (const OWJsonValue& e : served.Items())
            if (Str(e, "type") == "marker" || Str(e, "type") == "pin") ++rows;
        for (const OWMapSpaceOutline& o : got.zones) placed += int64_t(o.points.size());
        placed += int64_t(got.pins.size());
        t.Eq(placed + got.out.no_point + got.out.dangling + got.out.no_map, rows,
             name + ": every Marker and Pin placed or counted once");
        // Zones: each served one has an outline or is counted no_markers.
        std::set<std::string> zones, outlined;
        for (const OWJsonValue& e : served.Items())
            if (Str(e, "type") == "zone") zones.insert(Str(e, "id"));
        for (const OWMapSpaceOutline& o : got.zones) outlined.insert(o.zone);
        t.Eq(int64_t(outlined.size()) + got.out.no_markers, int64_t(zones.size()),
             name + ": every Zone outlined or counted no_markers");
        for (const OWMapSpaceOutline& o : got.zones)
            t.Ok(o.shape == "polygon" || o.shape == "line", name + ": a shape of the two");
    }
}

// -- readings the fixture leaves open -------------------------------------------------------------------------------

OW_TEST(map_space_the_tables) {
    t.Ok(OWMapSpace::OutReasons() == std::vector<std::string>{"no_point", "no_markers", "dangling", "no_map"},
         "the four reasons, in the page's order");
    t.Ok(OWMapSpace::Shapes() == std::vector<std::string>{"polygon", "line"}, "the two shapes");
    std::set<std::string> reasons;
    for (const OWJsonValue* c : Cases())
        if (const OWJsonValue* out = c->Find("want")->Find("out"))
            for (const std::string& k : out->Keys()) reasons.insert(k);
    t.Eq(reasons.size(), size_t(4), "the fixture's answers count all four reasons");
}

OW_TEST(map_space_a_caller_mistake_and_rows_that_are_no_element) {
    const std::string empty = R"({"maps": [],"zones": [],"pins": [],"out": {}})";
    for (const char* json : {"null", "{\"elements\": []}", "3"}) {
        OWWorldMapSpace bad = OWMapSpace::MapSpaceOf(Parse(json));
        t.Ok(!bad.error.empty(), std::string("not a list is a caller's mistake: ") + json);
        t.Eq(Compact(bad.ToJson()), empty, std::string("and nothing is read: ") + json);
    }
    OWWorldMapSpace none = OWMapSpace::MapSpaceOf(Parse("[]"));
    t.Ok(none.error.empty(), "an empty list is no mistake");
    t.Eq(Compact(none.ToJson()), empty, "an empty list: the four keys, empty");
    // As npm's test: rows it cannot read are passed over, never counted.
    OWWorldMapSpace junk =
        OWMapSpace::MapSpaceOf(Parse(R"([null, 3, "x", {"id": "a"}, {"type": 7}, [1], {"type": "pin", "id": 5}])"));
    t.Ok(junk.error.empty(), "rows it cannot read are no mistake");
    t.Eq(Compact(junk.ToJson()), empty, "and are passed over, never counted");
}

OW_TEST(map_space_a_whole_number_at_its_edges) {
    auto whole = [](const char* json) { OWJsonValue v = Parse(json); return OWMapSpace::WholeOf(&v); };
    t.Ok(whole("2.0") == std::optional<int64_t>(2), "2.0 is 2");
    t.Ok(whole("1e2") == std::optional<int64_t>(100), "1e2 is 100");
    t.Ok(whole("-3") == std::optional<int64_t>(-3), "a negative whole number");
    t.Ok(whole("9007199254740993") == std::optional<int64_t>(9007199254740993LL), "an integer literal read exactly");
    t.Ok(!whole("1.5"), "1.5 is not whole");
    t.Ok(!whole("\"2\""), "a string is not a number");
    t.Ok(!whole("true"), "nor a boolean");
    t.Ok(!whole("null"), "nor null");
    t.Ok(!whole("1e400"), "a number past every double is none");
    // The page's platform range: a whole number past the reader's own integer range (int64 here) reads as not whole.
    t.Ok(whole("9223372036854775807") == std::optional<int64_t>(9223372036854775807LL),
         "int64's largest reads exactly");
    t.Ok(!whole("9223372036854775808"), "an integer literal past int64 is not whole");
    t.Ok(!whole("-9223372036854775809"), "nor one below it");
    t.Ok(!whole("1e19"), "nor a value past int64 spelled as a decimal");
    t.Ok(!OWMapSpace::WholeOf(nullptr), "an absent key is none");
    // A literal with a fraction or an exponent is read as a double, which holds whole numbers exactly only to 2^53 - 1:
    // past it a rounded value cannot be told from an exact one, so it is not whole (the reader's range is the range it
    // holds exactly, npm 4.129.2's isSafeInteger).
    t.Ok(whole("9007199254740991.0") == std::optional<int64_t>(9007199254740991LL), "2^53 - 1 as a double is whole");
    t.Ok(whole("-9007199254740991.0") == std::optional<int64_t>(-9007199254740991LL), "and its negative");
    t.Ok(!whole("9007199254740992.0"), "2^53 as a double is not whole");
    t.Ok(!whole("-9007199254740992.0"), "nor its negative");
    t.Ok(!whole("9007199254740993.0"), "nor 2^53 + 1, which the parse rounds to 2^53");
    t.Ok(!whole("1e16"), "nor 1e16");
    t.Ok(!whole("-9223372036854775809.0"), "nor a decimal the parse rounds to exactly -2^63");
    // A Marker with a non-whole z reads z 0 (rule 1 since serial 106; the fixture's z-not-whole-reads-0 too).
    OWWorldMapSpace m = OWMapSpace::MapSpaceOf(Parse(R"([{"type": "map", "id": "m", "width": 9, "height": 9},
        {"type": "zone", "id": "z"}, {"type": "marker", "id": "k", "map": "m", "zone": "z", "x": 1, "y": 2, "z": 1.5}])"));
    t.Ok(m.zones.size() == 1 && m.zones[0].points.size() == 1 && m.zones[0].points[0].z == 0, "a z of 1.5 reads 0");
}

// The npm SDK's vector (4.129.2, test/map-space.test.mjs), read from JSON text as a reader is served it. Written as
// npm writes it, every number an integer literal, which this reader holds exactly within int64: 2^53 + 1 and 2^53 read
// as themselves (npm's doubles round them, so there they are not whole), and -9223372036854775809, past int64, has no
// point, as in npm. Written with a fraction, each is a double, and the answer is npm's.
OW_TEST(map_space_a_number_the_parse_had_to_round_is_not_whole) {
    auto vector = [](const char* k2x, const char* k3z, const char* p1x) {
        return std::string(R"([
    {"type": "map", "id": "m", "width": 10, "height": 10},
    {"type": "zone", "id": "z"},
    {"type": "marker", "id": "k1", "map": "m", "zone": "z", "x": 1, "y": 1, "z": 9007199254740991},
    {"type": "marker", "id": "k2", "map": "m", "zone": "z", "x": )") + k2x + R"(, "y": 2},
    {"type": "marker", "id": "k3", "map": "m", "zone": "z", "x": 3, "y": 3, "z": )" + k3z + R"(},
    {"type": "pin", "id": "p1", "map": "m", "x": )" + p1x + R"(, "y": 1}
  ])";
    };
    auto points = [](const OWWorldMapSpace& got) {
        std::vector<std::vector<int64_t>> out;
        for (const OWMapSpaceOutline& o : got.zones)
            for (const OWMapSpacePoint& p : o.points) out.push_back({p.x, p.y, p.z});
        return out;
    };
    using Points = std::vector<std::vector<int64_t>>;

    // As npm writes it: integer literals, read exactly.
    OWWorldMapSpace exact = OWMapSpace::MapSpaceOf(Parse(vector("-9223372036854775809", "9007199254740993", "9007199254740992")));
    t.Eq(exact.error, std::string(), "no caller's mistake");
    t.Ok(exact.zones.size() == 1 && exact.zones[0].zone == "z" && exact.zones[0].shape == "polygon", "one outline, z's");
    t.Ok(points(exact) == Points{{1, 1, 9007199254740991LL}, {3, 3, 9007199254740993LL}},
         "k2 has no point; k3's z, an integer literal within int64, is read exactly");
    t.Ok(exact.pins.size() == 1 && exact.pins[0].point.x == 9007199254740992LL, "p1's x, 2^53 as an integer literal, is placed");
    t.Eq(exact.out.no_point, int64_t(1), "only k2 has no point");
    t.Ok(!exact.maps.empty() && exact.maps[0].z_max == std::optional<int64_t>(9007199254740993LL), "z_max is k3's z, exactly");

    // As doubles: past 2^53 - 1, not whole. npm's answer.
    OWWorldMapSpace rounded =
        OWMapSpace::MapSpaceOf(Parse(vector("-9223372036854775809.0", "9007199254740993.0", "9007199254740992.0")));
    t.Eq(rounded.error, std::string(), "no caller's mistake");
    t.Ok(points(rounded) == Points{{1, 1, 9007199254740991LL}, {3, 3, 0}},
         "k3's z, rounded by the parse, reads 0 as any z that is not whole, and the point stays placed");
    t.Ok(rounded.pins.empty(), "p1's x, 2^53 as a double, is not whole: no pin");
    t.Eq(rounded.out.no_point, int64_t(2), "k2 and p1 have no point");
    t.Ok(!rounded.maps.empty() && rounded.maps[0].z_max == std::optional<int64_t>(9007199254740991LL), "z_max is 2^53 - 1");
    t.Eq(Compact(rounded.ToJson()),
         std::string(R"({"maps": [{"id": "m","width": 10,"height": 10,"depth": null,"z_max": 9007199254740991,"scale": null,)"
                     R"("location": null,"parent_map": null}],"zones": [{"zone": "z","map": "m","shape": "polygon","points": )"
                     R"([[1,1,9007199254740991],[3,3,0]]}],"pins": [],"out": {"no_point": 2}})"),
         "the whole answer");
}
