// OWCalendar on the calendar profile's conformance fixture (schema-dist profiles/calendar.fixture.json, dist.91's
// copy): every case by name, a refused value refused, each step's words and breakdown as the fixture gives them, and
// the calendars found on a case's elements.
// Then the readings the fixture leaves open (its gaps), as npm @onlyworlds/sdk 4.102.4 answers them (the profile's
// reference: its built dist was run on each value on 2026-10-29), and the few npm cannot answer (it lists every day of
// a year, so a month of 10^12 days does not fit in its memory; two edges where it throws).

#include "ow_test.h"
#include "sha256.h"

#include "OWCalendar.h"
#include "OWFiles.h"
#include "OWJson.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.91's MANIFEST.json records it.
const char* const DistSha256 = "1dddb5c94d9b290bc0dcba9a966b3934dee5e4bf7e1dd8e579f4f783ff081d5e";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/calendar.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

OWJsonValue J(const std::string& text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? r.value : OWJsonValue();
}

int64_t Int(const OWJsonValue& v) { return static_cast<int64_t>(v.AsDouble()); }

OWJsonValue OptInt(const std::optional<int64_t>& n) { return n ? OWJsonValue::Number(*n) : OWJsonValue::Null(); }
OWJsonValue OptStr(const std::optional<std::string>& s) { return s ? OWJsonValue::String(*s) : OWJsonValue::Null(); }

// A breakdown as the fixture writes one (its era by name).
OWJsonValue AsJson(const OWCalendarConfig& cal, const OWCalendarBreakdown& b) {
    OWJsonValue o = OWJsonValue::Object();
    o.Set("absYear", OWJsonValue::Number(b.abs_year));
    o.Set("year", OWJsonValue::Number(b.year));
    o.Set("era", b.era >= 0 ? OWJsonValue::String(cal.eras[static_cast<size_t>(b.era)].name) : OWJsonValue::Null());
    o.Set("month", OptInt(b.month));
    o.Set("monthName", OptStr(b.month_name));
    o.Set("day", OptInt(b.day));
    o.Set("intercalary", OptStr(b.intercalary));
    o.Set("weekday", OptStr(b.weekday));
    return o;
}

std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    return out;
}

// The same keys and values, in any order (a number by its value).
bool SameBreakdown(const OWJsonValue& want, const OWJsonValue& got) {
    if (!want.IsObject() || want.Size() != got.Size()) return false;
    for (size_t i = 0; i < want.Size(); ++i) {
        const OWJsonValue* g = got.Find(want.Keys()[i]);
        const OWJsonValue& w = want.Items()[i];
        if (!g) return false;
        if (w.IsNumber() ? !(g->IsNumber() && g->AsDouble() == w.AsDouble()) : !(*g == w)) return false;
    }
    return true;
}

void Case(owtest::T& t, const OWJsonValue& c) {
    std::string name = Str(c, "name");
    const OWJsonValue* value = c.Find("value");
    const OWJsonValue* valid = c.Find("valid");
    if (!value || !valid || !valid->IsBool()) return t.Fail(name + ": no value or valid");
    std::optional<OWCalendarConfig> cal = OWCalendar::ReadValue(*value);
    if (!valid->AsBool()) {
        t.Ok(!cal, name + ": read a calendar the fixture refuses (" + Str(c, "why") + ")");
        t.Ok(!c.Has("steps"), name + ": a refused value has no steps");
        return;
    }
    t.Ok(cal.has_value(), name + ": refused a calendar the fixture reads (" + Str(c, "why") + ")");
    if (!cal) return;
    // A world's elements, and the ids a reader finds a calendar on, in list order.
    if (const OWJsonValue* elements = c.Find("elements")) {
        const OWJsonValue* found = c.Find("found");
        t.Ok(elements->IsArray() && found && found->IsArray(), name + ": its elements and the ids found");
        if (!elements->IsArray() || !found || !found->IsArray()) return;
        std::string want, got;
        for (const OWJsonValue& id : found->Items()) want += (id.IsString() ? id.Text() : std::string("?")) + " ";
        for (const OWWorldCalendar& w : OWCalendar::WorldCalendars(elements->Items())) got += w.id + " ";
        t.Eq(got, want, name + ": the calendars found");
    }
    const OWJsonValue* steps = c.Find("steps");
    t.Ok(steps && steps->IsArray() && steps->Size() > 0, name + ": its steps");
    if (!steps) return;
    for (const OWJsonValue& s : steps->Items()) {
        int64_t step = Int(*s.Find("step"));
        std::string at = name + " @" + std::to_string(step);
        t.Eq(OWCalendar::FormatStep(*cal, step), Str(s, "words"), at + " words");
        OWJsonValue got = AsJson(*cal, OWCalendar::Breakdown(*cal, step));
        const OWJsonValue* want = s.Find("breakdown");
        t.Ok(want && SameBreakdown(*want, got),
             at + " breakdown: gave " + Compact(got) + ", the fixture says " + (want ? Compact(*want) : "nothing"));
    }
}

// npm's answers beside the fixture: the value (as JSON text), whether it reads, and each step's words.
struct NpmCase {
    const char* name;
    const char* json;
    bool valid;
    std::vector<std::pair<int64_t, const char*>> steps;
};

const std::vector<NpmCase>& NpmCases() {
    static const std::vector<NpmCase> cases = {
    {"len-max-safe", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[9007199254740991]}}}", true, {}},
    {"len-2^53", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[9007199254740992]}}}", false, {}},
    {"len-2^53+1", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[9007199254740993]}}}", false, {}},
    {"len-1e1", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[1e1]}}}", true, {{9LL, "10 A, 0"}, {10LL, "1 A, 1"}}},
    {"version-1.0", "{\"version\":1.0,\"grain\":\"day\"}", true, {{3LL, "3"}}},
    {"version-10e-1", "{\"version\":10e-1,\"grain\":\"day\"}", true, {{3LL, "3"}}},
    {"version-2.0-gregorian", "{\"version\":2.0,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[365]}},\"intercalary\":[{\"name\":\"Leap\",\"after_month\":1,\"every_years\":4.0,\"except\":[100.0,400],\"offset\":1.0}]}", true, {{0LL, "1 A, 0"}, {365LL, "1 A, 1"}, {1460LL, "Leap, 3"}, {1461LL, "1 A, 4"}}},
    {"version-string", "{\"version\":\"1\",\"grain\":\"day\"}", false, {}},
    {"version-true", "{\"version\":true,\"grain\":\"day\"}", false, {}},
    {"start-max-safe", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Far\",\"start\":9007199254740991}]}", true, {{0LL, "0"}, {9007199254740991LL, "1 Far"}}},
    {"start-2^53", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Far\",\"start\":9007199254740992}]}", false, {}},
    {"start-1e400", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Far\",\"start\":1e400}]}", false, {}},
    {"end-neg-2^53", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Far\",\"start\":0,\"end\":-9007199254740992}]}", false, {}},
    {"offset-neg-max-safe", "{\"version\":2,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"intercalary\":[{\"name\":\"Leap\",\"after_month\":2,\"every_years\":4,\"offset\":-9007199254740991}]}", true, {{0LL, "1 Frost, 0"}, {19LL, "10 Thaw, 0"}, {20LL, "1 Frost, 1"}, {21LL, "2 Frost, 1"}, {40LL, "1 Frost, 2"}}},
    {"offset-2^53", "{\"version\":2,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"intercalary\":[{\"name\":\"Leap\",\"after_month\":2,\"every_years\":4,\"offset\":9007199254740992}]}", false, {}},
    {"every-years-2^53", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"intercalary\":[{\"name\":\"Leap\",\"after_month\":2,\"every_years\":9007199254740992}]}", false, {}},
    {"year-base-neg-zero", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"start\":0,\"year_base\":-0}]}", true, {{0LL, "0 Age"}, {1LL, "1 Age"}}},
    {"year-base-1.0", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"start\":0,\"year_base\":1.0}]}", true, {{0LL, "1 Age"}, {1LL, "2 Age"}}},
    {"year-base-2", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"start\":0,\"year_base\":2}]}", false, {}},
    {"version-length-true", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[true]}}}", false, {}},
    {"year-base-false", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"start\":0,\"year_base\":false}]}", false, {}},
    {"start-false", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"start\":false}]}", false, {}},
    {"after-month-true", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"intercalary\":[{\"name\":\"Gap\",\"after_month\":true}]}", false, {}},
    {"restart-0", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":\"A\",\"start\":5,\"restart\":0}]}", true, {{5LL, "1 A"}, {7LL, "3 A"}}},
    {"restart-false", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":\"A\",\"start\":5,\"restart\":false}]}", true, {{5LL, "5 A"}, {7LL, "7 A"}}},
    {"restart-false-mid", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"eras\":[{\"name\":\"Late\",\"abbrev\":\"L\",\"start\":45,\"restart\":false}]}", true, {{45LL, "6 Frost, 2 L"}, {60LL, "1 Frost, 3 L"}}},
    {"restart-true-mid", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"eras\":[{\"name\":\"Late\",\"abbrev\":\"L\",\"start\":45}]}", true, {{45LL, "6 Frost, 1 L"}, {60LL, "1 Frost, 2 L"}}},
    {"in-week-0", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\",\"B\"],\"lengths\":[3,3]},\"week\":{\"length\":3,\"names\":[\"X\",\"Y\",\"Z\"]}},\"intercalary\":[{\"name\":\"Mid\",\"after_month\":1,\"in_week\":0}],\"format\":\"{weekday} {day} {month} {intercalary}\"}", true, {{2LL, "Z 3 A"}, {3LL, "Mid, 0"}, {4LL, "Y 1 B"}, {7LL, "Y 1 A"}}},
    {"in-week-false", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\",\"B\"],\"lengths\":[3,3]},\"week\":{\"length\":3,\"names\":[\"X\",\"Y\",\"Z\"]}},\"intercalary\":[{\"name\":\"Mid\",\"after_month\":1,\"in_week\":false}],\"format\":\"{weekday} {day} {month} {intercalary}\"}", true, {{2LL, "Z 3 A"}, {3LL, "Mid, 0"}, {4LL, "X 1 B"}, {7LL, "X 1 A"}, {-1LL, "Z 3 B"}, {-4LL, "Mid, -1"}}},
    {"months-zero", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":0}}", true, {{4LL, "4"}}},
    {"months-empty-string", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":\"\"}}", true, {{4LL, "4"}}},
    {"months-true", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":true}}", false, {}},
    {"months-array", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":[]}}", false, {}},
    {"week-false", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]},\"week\":false},\"x\":1}", false, {}},
    {"year-false", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"year\":false}}", false, {}},
    {"bundles-string", "{\"version\":1,\"grain\":\"day\",\"bundles\":\"months\"}", true, {{4LL, "4"}}},
    {"top-array", "[1]", false, {}},
    {"top-number", "5", false, {}},
    {"week-mode-other", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\",\"B\"],\"lengths\":[3,3]},\"week\":{\"length\":2,\"names\":[\"X\",\"Y\"],\"mode\":\"Nested\"}},\"format\":\"{weekday} {day} {month}\"}", true, {{3LL, "Y 1 B"}, {4LL, "X 2 B"}}},
    {"week-nested", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\",\"B\"],\"lengths\":[3,3]},\"week\":{\"length\":2,\"names\":[\"X\",\"Y\"],\"mode\":\"nested\"}},\"format\":\"{weekday} {day} {month}\"}", true, {{3LL, "X 1 B"}, {4LL, "Y 2 B"}}},
    {"week-no-months-free", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"year\":{\"steps\":5},\"week\":{\"length\":3,\"names\":[\"X\",\"Y\",\"Z\"]}},\"format\":\"{weekday} {year}\"}", true, {{0LL, "X 0"}, {4LL, "Y 0"}, {5LL, "Z 1"}, {-1LL, "Z -1"}}},
    {"tmpl-middot", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{weekday} \\u00b7 {day} {month} \\u00b7 {era}\"}", true, {{0LL, "1 Frost"}}},
    {"tmpl-edges", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\", ; {era} {day} {month} ,;\"}", true, {{0LL, "1 Frost"}}},
    {"tmpl-ideographic", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"\\u3000{weekday}\\u3000{day}\\u00a0{month}\\ufeff{era}\\u2003\"}", true, {{0LL, "1\xc2\xa0""Frost"}}},
    {"tmpl-nel", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{day}\\u0085{weekday}\\u0085{month}\"}", true, {{0LL, "1\xc2\x85 \xc2\x85""Frost"}}},
    {"tmpl-dash", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{day} - {era} - {month}\"}", true, {{0LL, "1 - - Frost"}}},
    {"tmpl-adjacent", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{day}{weekday}{month}{era}{year}\"}", true, {{0LL, "1 Frost 0"}}},
    {"tmpl-braces", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{{day}} {DAY} {day_x} {} {da-y} {step}}\"}", true, {{5LL, "{6} {DAY} {day_x} {} {da-y} 5}"}}},
    {"tmpl-two-markers", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{day}, {weekday}, {era}, {year}\"}", true, {{0LL, "1 0"}}},
    {"tmpl-separators-only", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{era}\"}", true, {{0LL, ""}}},
    {"tmpl-dollar", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"$& $1 {day}\"}", true, {{0LL, "$& $1 1"}}},
    {"tmpl-tabs", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"{day}\\t{month}\\t\\t{year}\"}", true, {{0LL, "1\x09""Frost 0"}}},
    {"tmpl-empty", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"format\":\"\"}", true, {{0LL, ""}}},
    {"tmpl-intercalary-null", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"intercalary\":[{\"name\":\"Gap\",\"after_month\":1}],\"format_intercalary\":null,\"format\":null}", true, {{10LL, "Gap, 0"}, {11LL, "1 Thaw, 0"}}},
    {"tmpl-marker-in-name", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Fr\\ue000ost\"],\"lengths\":[5]}}}", true, {{0LL, "1 Fr ost, 0"}}},
    {"month-name-empty", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"\"],\"lengths\":[5]}}}", true, {{0LL, "1 0"}}},
    {"abbrev-7.0", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":7.0,\"start\":0}]}", true, {{0LL, "1 7"}}},
    {"abbrev-2.5", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":2.5,\"start\":0}]}", true, {{0LL, "1 2.5"}}},
    {"abbrev-true", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":true,\"start\":0}]}", true, {{0LL, "1 true"}}},
    {"abbrev-array", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":[\"a\",1,null,[2,3],{}],\"start\":0}]}", true, {{0LL, "1 a,1,,2,3,[object Object]"}}},
    {"abbrev-object", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":{\"x\":1},\"start\":0}]}", true, {{0LL, "1 [object Object]"}}},
    {"abbrev-empty", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":\"\",\"start\":0}]}", true, {{0LL, "1"}}},
    {"abbrev-null", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":null,\"start\":0}]}", true, {{0LL, "1 Age"}}},
    {"abbrev-1e400", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":1e400,\"start\":0}]}", true, {{0LL, "1 Infinity"}}},
    {"abbrev-1e21", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Age\",\"abbrev\":1e21,\"start\":0}]}", true, {{0LL, "1 1e+21"}}},
    {"era-down-with-end", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Before\",\"abbrev\":\"B\",\"start\":0,\"end\":-5,\"direction\":\"down\"},{\"name\":\"After\",\"abbrev\":\"A\",\"start\":0}]}", true, {{-6LL, "-6"}, {-5LL, "5 B"}, {-1LL, "1 B"}, {0LL, "1 A"}, {3LL, "4 A"}}},
    {"era-up-end-before-start", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Odd\",\"start\":10,\"end\":5}]}", true, {{7LL, "7"}, {10LL, "10"}}},
    {"era-direction-DOWN", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"Shout\",\"start\":10,\"direction\":\"DOWN\"}]}", true, {{5LL, "5"}, {12LL, "3 Shout"}}},
    {"era-three-up-overlap", "{\"version\":1,\"grain\":\"year\",\"eras\":[{\"name\":\"A\",\"start\":0,\"end\":100},{\"name\":\"B\",\"start\":10,\"end\":20},{\"name\":\"C\",\"start\":10}]}", true, {{5LL, "6 A"}, {12LL, "3 C"}, {25LL, "16 C"}, {150LL, "141 C"}}},
    {"era-down-base-0", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Frost\",\"Thaw\"],\"lengths\":[10,10]}},\"eras\":[{\"name\":\"Before\",\"abbrev\":\"B\",\"start\":30,\"direction\":\"down\",\"year_base\":0}]}", true, {{0LL, "1 Frost, 1 B"}, {20LL, "1 Frost, 0 B"}, {29LL, "10 Frost, 0 B"}, {30LL, "1 Thaw, 1"}}},
    {"big-month", "{\"version\":1,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"Long\",\"Short\"],\"lengths\":[1000000000000,1]}}}", true, {}},
    {"gregorian-far", "{\"version\":2,\"grain\":\"day\",\"bundles\":{\"months\":{\"names\":[\"A\"],\"lengths\":[365]}},\"intercalary\":[{\"name\":\"Leap\",\"after_month\":1,\"every_years\":4,\"except\":[100,400],\"offset\":1}]}", true, {{1000000000000LL, "363 A, 2737907006"}, {-1000000000000LL, "5 A, -2737907007"}, {4000000000000000LL, "13 A, 10951628027954"}, {-4000000000000000LL, "355 A, -10951628027955"}}},
    };
    return cases;
}

std::optional<OWCalendarConfig> Read(const std::string& json) { return OWCalendar::ReadValue(J(json)); }

const char* const TwoMonths = R"("bundles":{"months":{"names":["Frost","Thaw"],"lengths":[10,10]}})";
std::string Plain(const std::string& extra = "") {
    return std::string(R"({"version":1,"grain":"day",)") + TwoMonths + extra + "}";
}

}  // namespace

OW_TEST(calendar_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the calendar fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.91's");
    // And the vendored manifest says the same.
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/calendar.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("atlas_calendar"), "the calendar profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    size_t refused = 0, read = 0, steps = 0;
    for (const OWJsonValue& c : cases->Items()) {
        const OWJsonValue* valid = c.Find("valid");
        if (valid && valid->IsBool() && valid->AsBool()) {
            ++read;
            if (const OWJsonValue* s = c.Find("steps")) steps += s->Size();
        } else {
            ++refused;
        }
    }
    t.Eq(cases->Size(), size_t(76), "76 cases (the profile page says 76)");
    t.Eq(read, size_t(34), "34 calendars");
    t.Eq(refused, size_t(42), "42 values a reader must refuse");
    t.Eq(steps, size_t(209), "209 steps");
}

OW_CASES(calendar_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("calendar_fixture:not-found", [](owtest::T& t) { t.Fail("the calendar fixture did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("calendar_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// -- the readings the fixture leaves open, as npm answers them -------------------------------------------------------

OW_TEST(calendar_beside_the_fixture_as_npm_answers) {
    // Numbers as JavaScript reads them (1.0 is 1, safe to 2^53 - 1, 1e400 Infinity), booleans that are not numbers,
    // falsy and truthy shapes, the template's white space and separators, the era label's String(), eras, far steps.
    size_t n = 0;
    for (const NpmCase& c : NpmCases()) {
        ++n;
        std::optional<OWCalendarConfig> cal = Read(c.json);
        t.Eq(cal.has_value(), c.valid, std::string(c.name) + ": reads");
        // The same value as a JSON string reads the same.
        t.Eq(OWCalendar::ReadValue(OWJsonValue::String(c.json)).has_value(), c.valid,
             std::string(c.name) + ": reads as a JSON string");
        if (!cal) continue;
        for (const auto& [step, words] : c.steps)
            t.Eq(OWCalendar::FormatStep(*cal, step), std::string(words),
                 std::string(c.name) + " @" + std::to_string(step));
    }
    t.Eq(n, size_t(72), "npm's 72 values");
}

OW_TEST(calendar_grain_is_something_after_the_trim) {
    // The profile, ruled ahead of its fixture case: grain is a string with something left after trimming the room
    // profile's White_Space set, the presence rule the marks use. Both the object and the JSON-string reading.
    auto reads = [](const std::string& json) {
        return Read(json).has_value() && OWCalendar::ReadValue(OWJsonValue::String(json)).has_value();
    };
    auto refused = [](const std::string& json) {
        return !Read(json).has_value() && !OWCalendar::ReadValue(OWJsonValue::String(json)).has_value();
    };
    t.Ok(refused(R"({"version":1,"grain":""})"), "an empty grain");
    t.Ok(refused(R"({"version":1,"grain":" \t\n"})"), "ASCII white space only");
    t.Ok(refused(R"({"version":2,"grain":" 　 "})"), "Unicode White_Space only");
    t.Ok(reads(R"({"version":1,"grain":" day "})"), "a padded grain reads");
    t.Ok(reads(R"({"version":1,"grain":"​"})"), "U+200B is not White_Space, so it is something left");
    // npm 4.102.4 read an empty grain (its table's row grain-empty now lives here, as the first refusal above).
}

OW_TEST(calendar_after_month_names_a_month_the_year_has) {
    // The profile (serial 89): an intercalary after_month that names a month the year does not have, past the last or
    // below the first, is not a calendar; with no months, anything above 0. npm 4.102.4 read the first two values (its
    // table's rows after-month-1e300 and after-month-neg, moved here) and placed the day in no month.
    auto reads = [](const std::string& json) {
        return Read(json).has_value() && OWCalendar::ReadValue(OWJsonValue::String(json)).has_value();
    };
    auto refused = [](const std::string& json) {
        return !Read(json).has_value() && !OWCalendar::ReadValue(OWJsonValue::String(json)).has_value();
    };
    auto after = [](const std::string& am) { return Plain(R"(,"intercalary":[{"name":"Lost","after_month":)" + am + "}]"); };
    t.Ok(refused(after("1e300")), "1e300: past the last month");
    t.Ok(refused(after("-1")), "-1: below the first");
    t.Ok(refused(after("3")), "3 on a year of two months");
    t.Ok(refused(after("-1e300")), "-1e300");
    t.Ok(reads(after("0")), "0: before month 1");
    t.Ok(reads(after("-0")), "-0 is 0");
    t.Ok(reads(after("2")), "2: after the last month");
    t.Ok(reads(after("2.0")), "2.0 is 2");
    const std::string no_months = R"({"version":1,"grain":"day","bundles":{"year":{"steps":10}},"intercalary":[{"name":"Gap","after_month":)";
    // With no months there is no place between months, so any intercalary day refuses, 0 included (ruled ahead of the
    // fixture's own case); an empty intercalary list still reads.
    t.Ok(refused(no_months + "0}]}"), "no months: 0 has no place either");
    t.Ok(refused(no_months + "1}]}"), "no months: 1 is past the last");
    t.Ok(reads(R"({"version":1,"grain":"day","bundles":{"year":{"steps":10}},"intercalary":[]})"),
         "no months, no intercalary days: reads");
    // npm 4.102.4 read this one and counted the leap day in a monthless year (its table's row leap-no-months, moved here).
    t.Ok(refused(R"({"version":1,"grain":"day","bundles":{"year":{"steps":3}},"intercalary":[{"name":"Leap","after_month":0,"every_years":2}]})"),
         "no months: a leap day at 0 has no place either");
    // One entry out of range refuses the whole value, a leap entry too.
    t.Ok(refused(Plain(R"(,"intercalary":[{"name":"Gap","after_month":1},{"name":"Leap","after_month":3,"every_years":4}])")),
         "a leap entry past the last month");
    // A falsy months value is no months.
    t.Ok(refused(R"({"version":1,"grain":"day","bundles":{"months":0},"intercalary":[{"name":"Gap","after_month":1}]})"),
         "months 0 is no months: 1 is past the last");
    t.Ok(refused(R"({"version":1,"grain":"day","bundles":{"months":0},"intercalary":[{"name":"Gap","after_month":0}]})"),
         "months 0 is no months: 0 has no place");
}

OW_TEST(calendar_json_string_is_strict_json) {
    const std::string ok = R"({"version":1,"grain":"day"})";
    auto reads = [](const std::string& text) { return OWCalendar::ReadValue(OWJsonValue::String(text)).has_value(); };
    t.Ok(reads(ok), "the object as a string");
    t.Ok(reads(" \n\r\t" + ok + "\t "), "JSON's own white space around it");
    t.Ok(!reads(R"({"version":1,"grain":"day",})"), "a trailing comma");
    t.Ok(!reads("\xEF\xBB\xBF" + ok), "a byte order mark (JSON.parse refuses it; OWJson::Parse would read past it)");
    t.Ok(!reads("\xC2\xA0" + ok), "a no-break space before it");
    t.Ok(!reads(R"({"version":01,"grain":"day"})"), "a leading zero");
    t.Ok(!reads("{\"version\":1,\"grain\":\"day\",\"x\":\"a\tb\"}"), "a raw tab inside a string");
    t.Ok(!reads(R"({"version":1,"grain":"d\ay"})"), "a bad escape");
    t.Ok(!reads(R"("{\"version\":1,\"grain\":\"day\"}")"), "a JSON string holding a JSON string");
    t.Ok(reads(R"({"version":3,"grain":"day","version":1})"), "a key given twice takes its last value");
    t.Ok(!reads(""), "an empty string");
    t.Ok(!OWCalendar::ReadValue(OWJsonValue::Null()).has_value(), "null");
    t.Ok(!OWCalendar::ReadValue(OWJsonValue::Number(int64_t(1))).has_value(), "a number");
    t.Ok(!OWCalendar::ReadValue(J("[1]")).has_value(), "a list");
    // A lone surrogate escape: JSON.parse takes it (npm reads this calendar); OWJson keeps it as WTF-8, so it reads.
    t.Ok(reads(R"({"version":1,"grain":"\ud800"})"), "a lone surrogate escape, as JSON.parse takes it");
}

OW_TEST(calendar_numbers_npm_cannot_answer) {
    // npm lists every day of a year (yearSlots), so these do not fit in its memory; here a month is walked by
    // arithmetic. The answers are the profile's, by hand.
    std::optional<OWCalendarConfig> big =
        Read(R"({"version":1,"grain":"day","bundles":{"months":{"names":["Long","Short"],"lengths":[1000000000000,1]}}})");
    t.Ok(big.has_value(), "a month of 10^12 days reads");
    if (big) {
        t.Eq(OWCalendar::FormatStep(*big, 999999999999), std::string("1000000000000 Long, 0"), "its last day");
        t.Eq(OWCalendar::FormatStep(*big, 1000000000000), std::string("1 Short, 0"), "the next month");
        t.Eq(OWCalendar::FormatStep(*big, 1000000000001), std::string("1 Long, 1"), "the next year");
        t.Eq(OWCalendar::FormatStep(*big, -1), std::string("1 Short, -1"), "the step before 0");
    }
    std::optional<OWCalendarConfig> safe =
        Read(R"({"version":1,"grain":"day","bundles":{"months":{"names":["A"],"lengths":[9007199254740991]}}})");
    t.Ok(safe.has_value(), "a month of 2^53 - 1 days reads");
    if (safe) {
        t.Eq(OWCalendar::FormatStep(*safe, 9007199254740990), std::string("9007199254740991 A, 0"), "its last day");
        t.Eq(OWCalendar::FormatStep(*safe, 9007199254740991), std::string("1 A, 1"), "year 1");
        OWCalendarBreakdown b = OWCalendar::Breakdown(*safe, -9007199254740991);
        t.Eq(b.abs_year, int64_t(-1), "step -(2^53 - 1) is year -1");
        t.Eq(b.day.value_or(0), int64_t(1), "its first day");
    }
    // A number past a double's range is Infinity to JavaScript. std::from_chars leaves its value unset there by the
    // standard, and MSVC's sets infinity (measured: OWJsonValue::AsDouble("1e400") is inf here), so the reader decides
    // it from the text and relies on neither.
    t.Ok(!Read(R"({"version":1,"grain":"day","bundles":{"months":{"names":["A"],"lengths":[1e400]}}})"),
         "a length of 1e400 is not a whole number from 1");
    t.Ok(!Read(R"({"version":1,"grain":"year","bundles":{"year":{"steps":1e400}}})"), "nor a year of 1e400 steps");
    t.Ok(Read(R"({"version":1,"grain":"year","bundles":{"year":{"steps":1e-400}}})") == std::nullopt,
         "1e-400 is 0: not from 1");
}

OW_TEST(calendar_edges_where_npm_throws) {
    // Two days after months the calendar does not have, and a free week: npm 4.102.4's weekday walk reads past its list
    // of the year's days and throws a TypeError on the second (calendar.ts:507-510; Python raises the same, calendar.py
    // weekday). The profile (serial 89) settles it at read time: neither day has a position, so the value is not a
    // calendar, and the step prints bare.
    const std::string lost_json =
        R"({"version":1,"grain":"day","bundles":{"months":{"names":["A"],"lengths":[2]},"week":{"length":2,"names":["X","Y"]}},"intercalary":[{"name":"L1","after_month":5},{"name":"L2","after_month":6}]})";
    std::optional<OWCalendarConfig> lost = Read(lost_json);
    t.Ok(!lost.has_value(), "not a calendar (npm 4.102.4 read it)");
    t.Ok(!OWCalendar::ReadValue(OWJsonValue::String(lost_json)).has_value(), "nor as a JSON string");
    t.Eq(OWCalendar::FormatStep(lost, 3), std::string("3"), "the bare step where npm threw");
    // A format that is not a string: npm 4.102.4 throws at template.replace, Python raises. The profile (serial 89)
    // ignores it, and the default template prints; the fixture's two cases hold a number and an object, these a number
    // and a list.
    std::optional<OWCalendarConfig> numeric = Read(Plain(R"(,"format":5,"format_intercalary":[1],"intercalary":[{"name":"Gap","after_month":1}])"));
    t.Ok(numeric.has_value(), "a calendar with a number for its format reads (npm reads it too)");
    if (numeric) {
        t.Eq(OWCalendar::FormatStep(*numeric, 0), std::string("1 Frost, 0"), "the default template");
        t.Eq(OWCalendar::FormatStep(*numeric, 10), std::string("Gap, 0"), "the default for a day outside the months");
    }
}

OW_TEST(calendar_restart_false_reads_the_absolute_year) {
    // The fixture's restart false sits where the era's year equals the absolute year, so a reader ignoring it passed
    // there (the Godot lane's gap 4). An era from step 45 (year 2): restart false reads year 2, a restart reads 1.
    std::optional<OWCalendarConfig> label = Read(Plain(R"(,"eras":[{"name":"Late","abbrev":"L","start":45,"restart":false}])"));
    std::optional<OWCalendarConfig> count = Read(Plain(R"(,"eras":[{"name":"Late","abbrev":"L","start":45}])"));
    t.Ok(label && count, "both read");
    if (!label || !count) return;
    t.Eq(OWCalendar::Breakdown(*label, 45).year, int64_t(2), "restart false: the absolute year");
    t.Eq(OWCalendar::Breakdown(*count, 45).year, int64_t(1), "a restart: year_base");
    // Only a JSON false is false: 0 restarts.
    std::optional<OWCalendarConfig> zero = Read(Plain(R"(,"eras":[{"name":"Late","start":45,"restart":0}])"));
    t.Ok(zero && OWCalendar::Breakdown(*zero, 45).year == 1, "restart 0 restarts");
}

OW_TEST(calendar_world_calendars_by_the_field) {
    std::vector<OWJsonValue> elements;
    elements.push_back(J(R"({"id":"a","name":"Reckoning","supertype":"Calendar","atlas_calendar":)" + Plain() + "}"));
    OWJsonValue b = J(R"({"id":"b","name":""})");
    b.Set(OWCalendar::Field, OWJsonValue::String(Plain()));
    elements.push_back(b);
    elements.push_back(J(R"({"id":"c","name":"Broken","supertype":"calendar","atlas_calendar":{"version":3,"grain":"day"}})"));
    elements.push_back(J(R"({"id":"d","name":"Typed only","supertype":"calendar"})"));
    elements.push_back(J(R"({"id":"e","name":"Null","atlas_calendar":null})"));
    elements.push_back(J(R"({"id":"f","atlas_calendar":{"version":1,"grain":"year"}})"));
    std::vector<OWWorldCalendar> found = OWCalendar::WorldCalendars(elements);
    // npm worldCalendars on the same elements: [a, Reckoning], [b, (unnamed calendar)], [f, (unnamed calendar)].
    t.Eq(found.size(), size_t(3), "three calendars, found by the field, never by supertype");
    if (found.size() != 3) return;
    t.Eq(found[0].id + "|" + found[0].name, std::string("a|Reckoning"), "an object");
    t.Eq(found[1].id + "|" + found[1].name, std::string("b|(unnamed calendar)"), "a JSON string, an empty name");
    t.Eq(found[2].id + "|" + found[2].name, std::string("f|(unnamed calendar)"), "no name");
    t.Eq(std::string(OWCalendar::Field), std::string("atlas_calendar"), "the field");
    t.Eq(std::string(OWCalendar::Supertype), std::string("calendar"), "the convention's supertype");
}

OW_TEST(calendar_keeps_the_value_and_prints_a_bare_step_without_one) {
    std::string text = R"({"version":1,"grain":"day","x_note":"kept","bundles":{"months":{"names":["Frost"],"lengths":[3]},"extra":[1]}})";
    std::optional<OWCalendarConfig> cal = OWCalendar::ReadValue(OWJsonValue::String(text));
    t.Ok(cal.has_value(), "reads");
    if (cal) {
        t.Ok(cal->raw == J(text), "raw is the value parsed, unknown keys and their order kept");
        t.Eq(cal->grain, std::string("day"), "grain");
    }
    t.Eq(OWCalendar::FormatStep(std::nullopt, -42), std::string("-42"), "no calendar: the bare step");
    t.Eq(OWCalendar::FloorDiv(-1, 10), int64_t(-1), "floor division");
    t.Eq(OWCalendar::FloorMod(-1, 10), int64_t(9), "floor modulo");
    t.Eq(OWCalendar::FloorDiv(-10, 10), int64_t(-1), "floor division on a multiple");
    t.Eq(OWCalendar::FloorMod(-10, 10), int64_t(0), "floor modulo on a multiple");
}
