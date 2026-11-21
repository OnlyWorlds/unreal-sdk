// OWMapSpace on the map-space profile's conformance fixture (schema-dist profiles/map-space.fixture.json, dist.127's
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

// The fixture's sha256 as dist.144's MANIFEST.json records it (dist.137 added four cases and three layout cases,
// dist.139 the frame_cases, which no test here reads, dist.142 three cases of a Zone on a child Map and its parent,
// dist.144 the down_cases, which no test here reads either).
const char* const DistSha256 = "143d19bb65f8e552795a123658142d65b441b0c91f5ca66e23034a4c39f5a650";

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
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.144's");
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
    t.Eq(Cases().size(), size_t(46), "46 cases (34 at dist.106, 35 at dist.119, 39 at dist.126, 43 at dist.137, 46 at dist.142)");
    t.Eq(names.size(), size_t(46), "each name its own");
    t.Ok(names.count("layout-map-read-as-before") == 1, "dist.119's case: a layout map read as any Map (rule 11)");
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

// -- the layout reading (rules 8 to 10, serial 110): the fixture's layout_cases and the two packages' vectors -------

namespace {

std::vector<const OWJsonValue*> LayoutCases() {
    std::vector<const OWJsonValue*> out;
    if (const OWJsonValue* cases = Fixture()->Find("layout_cases"))
        for (const OWJsonValue& c : cases->Items()) out.push_back(&c);
    return out;
}

// A layout case read as the page says, once per Map row it names: by `map`, the one Map row with that string id; by
// `map_index` (since dist.111, for rows with no id to name them by), each row at those positions in its elements,
// every one a Map row. `maps` are the case's Map rows, `pins` its Pin rows. A case names its Map one way, never both and
// never neither: a lookup by an absent id finds a row with no id by accident and reads one row where the case names
// two. What is wrong with the case itself goes in `problem`, and nothing is read.
std::vector<OWMapLayout> ReadLayouts(const OWJsonValue& c, std::string* problem = nullptr) {
    auto refuse = [&](const std::string& why) {
        if (problem) *problem = why;
        return std::vector<OWMapLayout>{};
    };
    const OWJsonValue& elements = ElementsOf(c);
    OWJsonValue maps = OWJsonValue::Array(), pins = OWJsonValue::Array();
    for (const OWJsonValue& e : elements.Items()) {
        if (Str(e, "type") == "map") maps.Push(e);
        if (Str(e, "type") == "pin") pins.Push(e);
    }
    const OWJsonValue* by_id = c.Find("map");
    const OWJsonValue* by_index = c.Find("map_index");
    if (!by_id == !by_index) return refuse("a case names its Map by map or by map_index, one of the two");
    std::vector<const OWJsonValue*> rows;
    if (by_id) {
        if (!by_id->IsString()) return refuse("map is an id");
        for (const OWJsonValue& e : elements.Items()) {
            const OWJsonValue* id = e.Find("id");
            if (Str(e, "type") == "map" && id && id->IsString() && id->Text() == by_id->Text()) rows.push_back(&e);
        }
        if (rows.size() != 1) return refuse("no one Map row with the case's id");
    } else {
        if (!by_index->IsArray() || by_index->Items().empty()) return refuse("map_index is a list of positions");
        for (const OWJsonValue& i : by_index->Items()) {
            std::optional<int64_t> at = OWMapSpace::WholeOf(&i);
            if (!at || *at < 0 || *at >= int64_t(elements.Items().size()) || Str(elements.Items()[size_t(*at)], "type") != "map")
                return refuse("position " + OWJson::Stringify(i) + " is no Map row of the elements");
            rows.push_back(&elements.Items()[size_t(*at)]);
        }
    }
    std::vector<OWMapLayout> out;
    for (const OWJsonValue* row : rows) out.push_back(OWMapSpace::LayoutOf(*row, maps, pins));
    return out;
}

// A case that names one Map row (by its id): its one answer.
OWMapLayout ReadLayout(const OWJsonValue& c) {
    std::vector<OWMapLayout> all = ReadLayouts(c);
    return all.size() == 1 ? all[0] : OWMapSpace::LayoutOf(OWJsonValue(), OWJsonValue(), OWJsonValue());
}

void LayoutCase(owtest::T& t, const OWJsonValue& c) {
    const OWJsonValue* want = c.Find("want");
    if (!c.Find("elements") || !want || !want->IsObject()) return t.Fail("the case has no elements or no want");
    std::string problem;
    std::vector<OWMapLayout> answers = ReadLayouts(c, &problem);
    t.Eq(problem, std::string(), "the case names its Map rows");
    t.Ok(!answers.empty(), "a Map row read");
    for (size_t n = 0; n < answers.size(); ++n) {
        const std::string row = "row " + std::to_string(n + 1);
        t.Eq(answers[n].error, std::string(), row + ": no caller's mistake");
        OWJsonValue answer = answers[n].ToJson();
        t.Ok(answer.Keys() == std::vector<std::string>{"layout", "ground", "hand"}, row + ": the three keys, in order");
        t.Ok(Bytes(answer) == Bytes(*want),
             row + ": the whole answer: gave " + Compact(answer) + ", the case says " + Compact(*want));
    }
}

std::string AllBytes(const std::vector<OWMapLayout>& answers) {
    std::string out;
    for (const OWMapLayout& a : answers) out += Bytes(a.ToJson()) + "\n";
    return out;
}

OWMapLayout Layout(const char* map, const char* maps, const char* pins) {
    return OWMapSpace::LayoutOf(Parse(map), Parse(maps), Parse(pins));
}

const std::string NoLayout = R"({"layout": null,"ground": null,"hand": []})";

}  // namespace

OW_TEST(map_space_fixture_holds_thirty_two_layout_cases_beside_its_cases) {
    const OWJsonValue* cases = Fixture()->Find("layout_cases");
    t.Ok(cases && cases->IsArray(), "layout_cases beside cases");
    std::set<std::string> names;
    for (const OWJsonValue* c : LayoutCases()) names.insert(Str(*c, "name"));
    t.Eq(LayoutCases().size(), size_t(32), "32 layout cases (17 at dist.110, 21 at dist.111, 23 at dist.119, 26 at dist.126, 29 at dist.127, 32 at dist.137)");
    t.Eq(names.size(), size_t(32), "each name its own");
    t.Ok(names.count("parentless-layout-beside-ground") == 1 && names.count("ground-beside-parentless-layout") == 1,
         "dist.119's two cases: a layout Map and its ground, neither with a parent_map, each read in turn");
    t.Eq(Cases().size(), size_t(46), "the map-space reading's 46 cases beside them");
}

// The replay reads every row a case names: one per `map` case, each of a `map_index` case's positions (dist.111's
// map-without-a-string-id names two, a row with no id and one whose id is 7).
OW_TEST(map_space_layout_the_replay_reads_every_row_a_case_names) {
    std::vector<std::string> by_index;
    size_t rows = 0;
    std::string problems;
    for (const OWJsonValue* c : LayoutCases()) {
        if (c->Find("map_index")) by_index.push_back(Str(*c, "name"));
        std::string problem;
        rows += ReadLayouts(*c, &problem).size();
        if (!problem.empty()) problems += Str(*c, "name") + ": " + problem + "; ";
    }
    t.Ok(by_index == std::vector<std::string>{"map-without-a-string-id", "layout-map-empty-id-is-no-map"},
         "dist.111 names one case's Maps by map_index, dist.126 a second (a Map whose id is an empty string)");
    for (const OWJsonValue* c : LayoutCases()) {
        if (Str(*c, "name") == "map-without-a-string-id") t.Eq(ReadLayouts(*c).size(), size_t(2), "its two rows, each read");
        if (Str(*c, "name") == "layout-map-empty-id-is-no-map") t.Eq(ReadLayouts(*c).size(), size_t(1), "its one row, read");
    }
    t.Eq(rows, size_t(32 - 2 + 2 + 1), "every row a case names, read once (32 cases, one of them naming two rows)");
    t.Eq(problems, std::string(), "every case names its Map rows");
}

// The core's JSON joins an escaped surrogate pair into one code point (four UTF-8 bytes), which sorts after U+FFFF
// (measured, as the fixture's hand-in-code-point-order holds its ids raw, not escaped).
OW_TEST(map_space_layout_an_escaped_surrogate_pair_is_one_code_point_in_code_point_order) {
    const std::string bs(1, '\x5c');
    const std::string text = R"([{"type": "pin", "id": "p)" + bs + "ud83d" + bs + R"(udea2", "map": "m"}, {"type": "pin", "id": "p)" + bs +
                             R"(uffff", "map": "m"}])";
    OWJsonValue pins = Parse(text);
    t.Ok(pins.IsArray() && pins.Items().size() == 2, "the text parses");
    t.Eq(pins.IsArray() ? Str(pins.Items()[0], "id") : std::string(), std::string("p\xF0\x9F\x9A\xA2"), "the pair is one code point");
    OWMapLayout a = OWMapSpace::LayoutOf(Parse(R"({"type": "map", "id": "m", "x_layout": {"method": "hand"}})"), Parse("[]"), pins);
    t.Ok(a.hand == std::vector<std::string>{"p\xEF\xBF\xBF", "p\xF0\x9F\x9A\xA2"}, "U+FFFF first");
}

OW_CASES(map_space_layout_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (LayoutCases().empty()) {
        out.emplace_back("layout_fixture:not-found", [](owtest::T& t) { t.Fail("the layout cases did not load"); });
        return out;
    }
    for (const OWJsonValue* c : LayoutCases())
        out.emplace_back("layout_fixture:" + Str(*c, "name"), [doc, c](owtest::T& t) { LayoutCase(t, *c); });
    return out;
}

// The map-space reading is unchanged by rules 8 to 10: on every layout case its answer carries no layout key.
OW_TEST(map_space_layout_the_map_space_reading_carries_no_layout_key) {
    for (const OWJsonValue* c : LayoutCases()) {
        const std::string name = Str(*c, "name");
        OWJsonValue answer = OWMapSpace::MapSpaceOf(ElementsOf(*c)).ToJson();
        t.Ok(answer.Keys() == std::vector<std::string>{"maps", "zones", "pins", "out"}, name + ": the four keys");
        for (const char* part : {"maps", "pins"})
            for (const OWJsonValue& row : answer.Find(part)->Items())
                for (const std::string& k : row.Keys())
                    t.Ok(k.rfind("x_", 0) != 0 && k != "layout" && k != "hand", name + ": " + k);
    }
}

// The reading never moves the rows, the same case gives the same bytes, and the answer is the caller's own.
OW_TEST(map_space_layout_never_moves_the_rows) {
    for (const OWJsonValue* c : LayoutCases()) {
        const std::string name = Str(*c, "name");
        const std::string before = Bytes(ElementsOf(*c));
        const std::string once = AllBytes(ReadLayouts(*c));
        t.Eq(AllBytes(ReadLayouts(*c)), once, name + ": read twice, the same bytes");
        t.Eq(Bytes(ElementsOf(*c)), before, name + ": the rows unchanged");
    }
    for (const OWJsonValue* c : LayoutCases()) {
        if (Str(*c, "name") != "board-every-pin-is-hand") continue;
        OWMapLayout got = ReadLayout(*c);
        got.hand.push_back("planted");
        t.Eq(Bytes(ReadLayout(*c).ToJson()), Bytes(*c->Find("want")), "an answer is the caller's own");
    }
}

// A caller's mistake is named and nothing is read.
OW_TEST(map_space_layout_a_callers_mistake) {
    const char* board = R"({"type": "map", "id": "m", "x_layout": {"method": "hand"}})";
    struct Call { const char* map; const char* maps; const char* pins; };
    for (const Call& c : {Call{"null", "[]", "[]"}, Call{"[]", "[]", "[]"}, Call{"\"m\"", "[]", "[]"},
                          Call{board, "null", "[]"}, Call{board, "[]", R"({"pins": []})"}}) {
        OWMapLayout a = Layout(c.map, c.maps, c.pins);
        t.Ok(!a.error.empty(), std::string("a mistake: ") + c.map + " " + c.maps + " " + c.pins);
        t.Eq(Compact(a.ToJson()), NoLayout, std::string("nothing read: ") + c.map);
    }
    t.Ok(Layout(board, "[]", "[]").error.empty(), "a board with no Pins is no mistake");
}

// A JSON 1 and 1.0 are no mark, nor "true": only the JSON true.
OW_TEST(map_space_layout_only_true_is_the_mark_as_numbers) {
    OWMapLayout a = Layout(R"({"type": "map", "id": "m", "x_layout": {"method": "trail-by-place"}})", "[]",
                           R"([{"type": "pin", "id": "a", "map": "m", "x_hand": 1}, {"type": "pin", "id": "b", "map": "m", "x_hand": 1.0},
                               {"type": "pin", "id": "c", "map": "m", "x_hand": true}, {"type": "pin", "id": "d", "map": "m", "x_hand": "true"}])");
    t.Ok(a.hand == std::vector<std::string>{"c"}, "1, 1.0 and \"true\" are not true");
}

// The TypeScript SDK 4.142.0's vectors beyond the fixture (test/layout-of.test.mjs).

// U+FF5E sorts before U+1F600 by code point; as UTF-16 units the surrogate D83D sorts first.
OW_TEST(map_space_layout_npm_hand_is_in_code_point_order_never_utf16_order) {
    const std::string low = "\xEF\xBD\x9E", high = "\xF0\x9F\x98\x80";
    const std::string pins = R"([{"type": "pin", "id": ")" + high + R"(", "map": "m1", "x_hand": true}, {"type": "pin", "id": ")" + low +
                             R"(", "map": "m1", "x_hand": true}])";
    const char* trail = R"({"type": "map", "id": "m1", "x_layout": {"method": "trail-by-place"}})";
    t.Ok(Layout(trail, (std::string("[") + trail + "]").c_str(), pins.c_str()).hand == std::vector<std::string>{low, high}, "a trail");
    t.Ok(Layout(R"({"type": "map", "id": "m1", "x_layout": {"method": "hand"}})", "[]", pins.c_str()).hand ==
             std::vector<std::string>{low, high},
         "a board");
}

OW_TEST(map_space_layout_npm_a_row_without_an_id_is_no_pin_and_the_last_row_served_is_read) {
    const char* trail = R"({"type": "map", "id": "m1", "x_layout": {"method": "trail-by-place"}})";
    OWMapLayout a = Layout(trail, "[]", R"([
        {"type": "pin", "map": "m1", "x_hand": true},
        {"type": "pin", "id": "", "map": "m1", "x_hand": true},
        {"type": "pin", "id": 7, "map": "m1", "x_hand": true},
        null,
        "p",
        {"type": "pin", "id": "p1", "map": "m1", "x_hand": true},
        {"type": "pin", "id": "p1", "map": "other", "x_hand": true},
        {"type": "pin", "id": "p2", "map": "m1"},
        {"type": "pin", "id": "p2", "map": "m1", "x_hand": true}])");
    t.Eq(Compact(a.ToJson()), std::string(R"({"layout": "trail-by-place","ground": null,"hand": ["p2"]})"), "the answer");
}

OW_TEST(map_space_layout_npm_a_map_row_without_an_id_is_no_map_and_a_list_x_layout_is_no_layout) {
    const char* pins = R"([{"type": "pin", "id": "p1", "map": "m1", "x_hand": true}])";
    t.Eq(Compact(Layout(R"({"type": "map", "x_layout": {"method": "hand"}})", "[]", pins).ToJson()), NoLayout, "no id");
    t.Eq(Compact(Layout(R"({"type": "map", "id": "m1", "x_layout": [{"method": "hand"}]})", "[]", pins).ToJson()), NoLayout, "a list");
}

// Reading H says a non-empty string id (dist.119's words): a Map row whose id is "" is no Map, so no layout map, even
// with a method and a Pin naming "" as its map. No fixture case holds this row yet.
OW_TEST(map_space_layout_a_map_row_whose_id_is_the_empty_string_is_no_layout_map) {
    const char* map = R"({"type": "map", "id": "", "x_layout": {"method": "hand"}})";
    t.Eq(Compact(Layout(map, (std::string("[") + map + "]").c_str(), R"([{"type": "pin", "id": "p1", "map": ""}])").ToJson()),
         NoLayout, "the answer");
}

OW_TEST(map_space_layout_npm_a_ground_named_by_a_row_that_is_no_map_is_no_ground) {
    const char* grounded = R"({"type": "map", "id": "m1", "x_layout": {"method": "trail-by-place", "settings": {"ground": {"map": "g"}}}})";
    t.Ok(Layout(grounded, R"([{"type": "map", "id": "g"}])", "[]").ground == std::optional<std::string>("g"), "served");
    t.Ok(!Layout(grounded, R"([{"type": "map", "name": "g"}])", "[]").ground, "a row without an id");
    t.Ok(!Layout(R"({"type": "map", "id": "m1", "x_layout": {"method": "trail-by-place", "settings": {"ground": "g"}}})",
                 R"([{"type": "map", "id": "g"}])", "[]")
              .ground,
         "a ground that is a string");
}

// The Python package 0.110.0's vectors beyond the fixture (tests/test_layout_of.py).

OW_TEST(map_space_layout_py_rows_are_read_as_the_map_space_reading_reads_them) {
    const char* board = R"({"type": "map", "id": "m", "x_layout": {"method": "hand"}})";
    OWMapLayout a = Layout(board, "[]", R"([
        {"type": "pin", "id": "b", "map": "m"},
        {"type": "pin", "id": "", "map": "m"},
        {"type": "pin", "id": 7, "map": "m"},
        {"type": "pin", "map": "m"},
        "not a row",
        {"type": "pin", "id": "a", "map": "m"},
        {"type": "pin", "id": "a", "map": "elsewhere"}])");
    t.Eq(Compact(a.ToJson()), std::string(R"({"layout": "hand","ground": null,"hand": ["b"]})"), "the answer");
}

OW_TEST(map_space_layout_py_a_method_that_is_no_non_empty_string_is_no_layout) {
    for (const char* method : {"true", "false", "1", "1.0", "null", R"(["hand"])", R"({"method": "hand"})", R"("")"}) {
        const std::string map = std::string(R"({"type": "map", "id": "m", "x_layout": {"method": )") + method + "}}";
        t.Eq(Compact(Layout(map.c_str(), "[]", R"([{"type": "pin", "id": "p", "map": "m", "x_hand": true}])").ToJson()), NoLayout,
             std::string("method ") + method);
    }
}

// U+FFFF sorts before U+10000 by code point, after it by UTF-16 unit.
OW_TEST(map_space_layout_py_hand_is_in_code_point_order_above_u_ffff) {
    const std::string edge = "\xEF\xBF\xBF", above = "\xF0\x90\x80\x80";
    const std::string pins = R"([{"type": "pin", "id": ")" + above + R"(", "map": "m"}, {"type": "pin", "id": ")" + edge + R"(", "map": "m"}])";
    t.Ok(Layout(R"({"type": "map", "id": "m", "x_layout": {"method": "hand"}})", "[]", pins.c_str()).hand ==
             std::vector<std::string>{edge, above},
         "U+FFFF, then U+10000");
}

OW_TEST(map_space_layout_py_settings_are_never_returned) {
    OWMapLayout a = Layout(
        R"({"type": "map", "id": "m", "x_layout": {"method": "trail-by-place", "settings": {"ground": {"map": "g", "width": 3, "height": 2}}}})",
        R"([{"type": "map", "id": "g", "width": 30, "height": 20}])", "[]");
    t.Eq(Compact(a.ToJson()), std::string(R"({"layout": "trail-by-place","ground": "g","hand": []})"), "the answer");
}

OW_TEST(map_space_layout_py_a_ground_map_that_is_no_id_is_no_ground) {
    for (const char* named : {R"("")", "5", "true", "null", R"(["g"])", R"({"id": "g"})"}) {
        const std::string map = std::string(R"({"type": "map", "id": "m", "x_layout": {"method": "trail-by-place", "settings": {"ground": {"map": )") +
                                named + "}}}}";
        t.Ok(!Layout(map.c_str(), R"([{"type": "map", "id": "g"}])", "[]").ground, std::string("ground map ") + named);
    }
}
