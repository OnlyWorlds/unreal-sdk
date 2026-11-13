// OWRecord on the record profile's conformance fixture (schema-dist profiles/record.fixture.json, dist.123's copy, its
// sha256 held to MANIFEST.json): every case by name, the mark (MarkOf) and the records (RecordsOf) over the fixture's
// one world, the mark's field and members held equal to the fixture's; then readings the fixture leaves open (the
// instant's arithmetic, played_at's form at its edges, the tool's and the outcome's bounds, an element that cannot be
// named, a name that is not text, a multi-link or a role that is not the shape, a caller's mistake, a reading that
// never moves the world).

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWRecord.h"

#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.123's MANIFEST.json records it.
const char* const DistSha256 = "a0dc588db339eeb9ae2732a6828af34412235ac8a9aaa27bef566391685f611e";

const char* const Fluffington = "019a8000-0000-7000-8000-000000000101";
const char* const Ford = "019a8000-0000-7000-8000-000000000201";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/record.fixture.json"; }

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

// Equal as JSON values: an object's keys in any order, a list's items in order, a number by its value.
bool Equal(const OWJsonValue& a, const OWJsonValue& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
        case OWJsonValue::Kind::Null: return true;
        case OWJsonValue::Kind::Bool: return a.AsBool() == b.AsBool();
        case OWJsonValue::Kind::Number: return OWJson::CanonicalNumber(a.Text()) == OWJson::CanonicalNumber(b.Text());
        case OWJsonValue::Kind::String: return a.Text() == b.Text();
        case OWJsonValue::Kind::Array:
            if (a.Size() != b.Size()) return false;
            for (size_t i = 0; i < a.Size(); ++i)
                if (!Equal(a.Items()[i], b.Items()[i])) return false;
            return true;
        case OWJsonValue::Kind::Object:
            if (a.Size() != b.Size()) return false;
            for (size_t i = 0; i < a.Size(); ++i) {
                const OWJsonValue* other = b.Find(a.Keys()[i]);
                if (!other || !Equal(a.Items()[i], *other)) return false;
            }
            return true;
    }
    return false;
}

void Same(owtest::T& t, const OWJsonValue& got, const OWJsonValue* want, const std::string& what) {
    t.Ok(want && Equal(got, *want), what + ": gave " + Compact(got) + ", the case says " + (want ? Compact(*want) : "nothing"));
}

OWJsonValue MarkJson(const OWJsonValue& element) {
    std::optional<OWRecordMark> m = OWRecord::MarkOf(element);
    return m ? m->ToJson() : OWJsonValue::Null();
}

// The fixture's one world's elements (a case's world is always a name here).
OWJsonValue Elements(const std::string& world) {
    const OWJsonValue* worlds = Fixture()->Find("worlds");
    const OWJsonValue* w = worlds ? worlds->Find(world) : nullptr;
    const OWJsonValue* els = w ? w->Find("elements") : nullptr;
    return els ? *els : OWJsonValue::Null();
}

const OWJsonValue* ById(const OWJsonValue& elements, const std::string& id) {
    for (const OWJsonValue& el : elements.Items())
        if (Str(el, "id") == id) return &el;
    return nullptr;
}

void Case(owtest::T& t, const OWJsonValue& c) {
    OWJsonValue elements = Elements(Str(c, "world"));
    if (!elements.IsArray()) return t.Fail("no world " + Str(c, "world"));
    std::string kind = Str(c, "kind");
    if (kind == "mark") {
        const OWJsonValue* el = ById(elements, Str(c, "element"));
        if (!el) return t.Fail("no element " + Str(c, "element"));
        Same(t, MarkJson(*el), c.Find("want"), "the mark");
    } else if (kind == "records") {
        OWCharacterRecords got = OWRecord::RecordsOf(Str(c, "character"), elements);
        t.Eq(got.error, std::string(), "no caller's mistake");
        Same(t, got.ToJson(), c.Find("want"), "the records");
    } else {
        t.Fail("a case of unknown kind " + kind);
    }
}

// One Event of Fluffington's with the given x_record (raw JSON), as an elements list.
OWJsonValue OneEvent(const std::string& mark) {
    return Parse(std::string(R"([{"id": "e1", "type": "event", "name": "A run", "characters": [")") + Fluffington +
                 R"("], "x_record": )" + mark + "}]");
}

// A mark that is no JSON at all is the test's own mistake: it fails the test, whatever was asked of it.
bool Marked(owtest::T& t, const std::string& mark) {
    OWJsonValue els = OneEvent(mark);
    if (els.Size() != 1) {
        t.Fail("the test's own mark is no JSON: " + mark);
        return false;
    }
    return OWRecord::MarkOf(els.Items()[0]).has_value();
}

std::string Played(const std::string& at) { return R"({"tool": "subs", "played_at": ")" + at + "\"}"; }

}  // namespace

OW_TEST(record_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the record fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.123's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/record.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("record"), "the record profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::set<std::string> names;
    size_t marks = 0, records = 0;
    for (const OWJsonValue& c : cases->Items()) {
        names.insert(Str(c, "name"));
        if (Str(c, "kind") == "mark") ++marks;
        if (Str(c, "kind") == "records") ++records;
    }
    t.Eq(cases->Size(), size_t(46), "46 cases");
    t.Eq(names.size(), size_t(46), "each name its own");
    t.Eq(marks, size_t(34), "34 mark cases");
    t.Eq(records, size_t(12), "12 records cases");
    const OWJsonValue* mark = doc->Find("mark");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string(OWRecord::Mark), "the mark's field");
    std::vector<std::string> members;
    if (const OWJsonValue* list = mark ? mark->Find("members") : nullptr)
        for (const OWJsonValue& v : list->Items()) members.push_back(Str(&v));
    t.Ok(members == OWRecord::Members(), "the mark's members, in order");
}

OW_CASES(record_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("record_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("record_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// The instant is milliseconds from 0001-01-01T00:00Z on the proleptic Gregorian calendar, its offset applied, a
// fraction truncated past three digits and padded short of them. Values from JavaScript's Date.parse plus
// 62135596800000. Kills: a day count off by a century rule, a fraction rounded or read as a number, an offset added
// where it is taken away.
OW_TEST(record_the_instant_is_milliseconds_on_the_gregorian_calendar) {
    auto at = [](const char* s) { return OWRecord::InstantOf(s).value_or(-1); };
    t.Eq(at("0001-01-01T00:00:00Z"), int64_t(0), "the first instant of year 0001");
    t.Eq(at("1970-01-01T00:00:00Z"), int64_t(62135596800000), "the Unix epoch");
    t.Eq(at("2000-02-29T12:00:00Z"), int64_t(63087422400000), "a leap day of a century divisible by 400");
    t.Eq(at("2026-11-01T00:30:00.1239Z"), int64_t(63929089800123), "a fraction truncated, never rounded");
    t.Eq(at("2026-11-01T00:30:00.1Z"), int64_t(63929089800100), "a short fraction padded: .1 is 100 ms");
    t.Eq(at("2026-11-01T00:30:00.000000001Z"), int64_t(63929089800000), "a long fraction of zeros");
    t.Eq(at("2026-11-01T02:30:00+02:00"), int64_t(63929089800000), "an offset taken away");
    t.Eq(at("9999-12-31T23:59:59.999Z"), int64_t(315537897599999), "the last instant of year 9999");
    std::optional<int64_t> zero = OWRecord::InstantOf("0000-12-31T23:59:59Z");
    t.Ok(zero && *zero == -1000, "year 0000 reads, before the first instant of 0001");
}

// played_at's form at its edges, the message page's grammar: ASCII digits only, the seconds required and before a
// fraction, a fraction of at least one digit, four-digit years, an offset with its colon and in range, nothing after;
// 1900 is a common year and 2000 a leap year. Kills: a digit test that takes any Unicode digit (the Python package's
// \d), a time without seconds taken, a fraction with no seconds, a missing century rule, a search instead of a whole
// match.
OW_TEST(record_played_at_is_the_profiles_form_and_nothing_else) {
    const char* const bad[] = {
        "\xEF\xBC\x92\xEF\xBC\x90\xEF\xBC\x92\xEF\xBC\x96-11-01T00:30:00Z",  // 2026 in fullwidth digits (U+FF12 ...)
        // The Unity SDK's three (OWRecordTests): a fraction with no seconds, an empty fraction, one fullwidth first digit.
        "2026-11-01T00:00.5Z",
        "2026-11-01T00:00:00.Z",
        "\xEF\xBC\x92" "026-11-01T00:00:00Z",
        "2026-11-01T00:30Z",
        "2026-11-01T00:30z",
        "2026-11-01t00:30Z",
        "2026-11-01T00:30.5Z",
        "2026-11-01T00:30:00Z ",
        "2026-11-01T00:30:00Z\\n",  // a JSON escape: a newline after the Z
        "2026-11-01 00:30:00Z",
        "2026-11-01T00:30:00+0100",
        "2026-11-01T00:30:00+24:00",
        "2026-11-01T00:30:00+01:60",
        "2026-11-01T00Z",
        "2026-11-01",
        "10000-01-01T00:00:00Z",
        "2026-1-01T00:00:00Z",
        "1900-02-29T00:00:00Z",
        "2026-00-10T00:00:00Z",
        "2026-13-10T00:00:00Z",
        "2026-04-31T00:00:00Z",
        "2026-04-00T00:00:00Z",
        "2026-11-01T24:00:00Z",
        "2026-11-01T00:60:00Z",
        "2026-11-01T00:00:60Z",
        "",
    };
    for (const char* at : bad) t.Ok(!Marked(t, Played(at)), std::string("no mark: \"") + at + "\"");
    for (const char* at : {"2000-02-29T00:00:00Z", "2026-12-31T23:59:59Z", "0001-01-01T00:00:00Z", "2026-04-30T00:00:00Z",
                           "0000-02-29T00:00:00Z", "2026-11-01T00:30:00-23:59", "2026-11-01T00:30:00.5z"})
        t.Ok(Marked(t, Played(at)), std::string("a mark: ") + at);
    t.Ok(!Marked(t, R"({"tool": "subs", "played_at": 1793493000000})"), "played_at a number");
    t.Ok(!Marked(t, R"({"tool": "subs"})"), "no played_at");
}

// One time grammar: played_at is read by the message page's grammar (OWMessage::TimeKey), so a lowercase t and z, an
// offset and a time with no offset (read as UTC) are marks, each the instant it names, and a time without its seconds
// is no mark. Records order by that instant. Kills: the record keeping a grammar of its own, an offset read and not
// applied.
OW_TEST(record_played_at_is_read_by_the_message_grammar) {
    for (const char* at : {"2026-11-01t00:30:00z", "2026-11-01T01:30:00+01:00", "2026-11-01T00:30:00",
                           "2026-10-31T23:30:00-01:00", "2026-11-01T00:30:00.000Z"}) {
        t.Ok(Marked(t, Played(at)), std::string("a mark: ") + at);
        std::optional<int64_t> instant = OWRecord::InstantOf(at);
        t.Ok(instant && *instant == OWRecord::InstantOf("2026-11-01T00:30:00Z").value_or(-1),
             std::string("the instant of 2026-11-01T00:30:00Z: ") + at);
    }
    for (const char* at : {"2026-11-01T00:30Z", "2028-02-29T00:00Z", "2026-11-01t00:30z", "2026-11-01T00:30+01:00"})
        t.Ok(!Marked(t, Played(at)), std::string("no mark, no seconds: ") + at);
    const std::string c = Fluffington;
    OWJsonValue els = Parse(R"([
        {"id": "e1", "type": "event", "characters": [")" + c + R"("], "x_record": {"tool": "subs", "played_at": "2026-11-01T01:30:00+02:00"}},
        {"id": "e2", "type": "event", "characters": [")" + c + R"("], "x_record": {"tool": "subs", "played_at": "2026-11-01T00:00:00Z"}}])");
    OWCharacterRecords got = OWRecord::RecordsOf(c, els);
    std::string order;
    for (const OWCharacterRecord& r : got.records) order += r.event_id + " ";
    t.Eq(order, std::string("e2 e1 "), "e1 is 2026-10-31T23:30Z, older than e2, though its text sorts after");
}

// tool is [a-z0-9-], 1 to 32; outcome [a-z], 1 to 24; version and seed strings; seats an object of strings, null
// reading as none. Kills: a length bound off by one, a seat null taken as unset, a version that takes a number.
OW_TEST(record_the_marks_members_at_their_bounds) {
    std::string at = R"("played_at": "2026-11-01T00:30:00Z")";
    auto with = [&](const std::string& members) { return Marked(t, "{" + at + ", " + members + "}"); };
    t.Ok(with(R"("tool": ")" + std::string(32, 'a') + "\""), "a tool of 32");
    t.Ok(!with(R"("tool": ")" + std::string(33, 'a') + "\""), "a tool of 33");
    t.Ok(with(R"("tool": "sub-9")"), "a digit and a hyphen");
    t.Ok(!with(R"("tool": "")"), "an empty tool");
    t.Ok(!with(R"("tool": "sub_s")"), "an underscore");
    t.Ok(!with(R"("tool": 7)"), "a number tool");
    t.Ok(!with(R"("tool": "subs", "outcome": "w0n")"), "a digit in the outcome");
    t.Ok(!with(R"("tool": "subs", "version": 1)"), "a number version");
    t.Ok(!with(R"("tool": "subs", "version": {})"), "an object version");
    t.Ok(with(R"("tool": "subs", "version": "", "seed": "")"), "an empty version and seed are strings");
    t.Ok(!with(R"("tool": "subs", "seats": {"a": null})"), "a seat null");
    t.Ok(!with(R"("tool": "subs", "seats": "General")"), "seats a string");
    std::optional<OWRecordMark> none = OWRecord::MarkOf(OneEvent("{" + at + R"(, "tool": "subs", "seats": null})").Items()[0]);
    t.Ok(none && none->seats.empty(), "seats null reads as none");
    t.Ok(!OWRecord::MarkOf(Parse(R"({"id": "e", "type": "Event", "x_record": {"tool": "subs", "played_at": "2026-11-01T00:30:00Z"}})")),
         "the type is the slug \"event\"");
    t.Ok(!OWRecord::MarkOf(Parse(R"({"id": "e", "x_record": {"tool": "subs", "played_at": "2026-11-01T00:30:00Z"}})")),
         "an element with no type");
    t.Ok(!OWRecord::MarkOf(Parse(R"(["event"])")), "not an element");
}

// An Event or a story whose id is not a string cannot be named, so it is never read; a name that is not text reads "";
// a protagonist that is not a string names no one; a characters entry that is not the id lists nothing.
OW_TEST(record_what_cannot_be_named_is_never_read) {
    std::string mark = R"("x_record": {"tool": "subs", "played_at": "2026-11-01T00:30:00Z"})";
    std::string f = Fluffington;
    OWJsonValue els = Parse(R"([{"type": "event", "name": "no id", "characters": [")" + f + R"("], )" + mark + R"(},
        {"id": 7, "type": "event", "characters": [")" + f + R"("], )" + mark + R"(},
        {"id": "e2", "type": "event", "name": 7, "characters": [7, ")" + f + R"("], )" + mark + R"(},
        {"id": "e3", "type": "event", "characters": [[")" + f + R"("]], )" + mark + R"(},
        {"type": "narrative", "name": "no id", "protagonist": ")" + f + R"(", "events": ["e2"]},
        {"id": "n2", "type": "narrative", "name": null, "narrator": ")" + f + R"(", "events": ["e2"]},
        {"id": "n3", "type": "narrative", "protagonist": [")" + f + R"("], "events": ["e2"]},
        {"id": "n4", "type": "Narrative", "protagonist": ")" + f + R"(", "events": ["e2"]}])");
    const OWJsonValue want = Parse(R"([{"event": {"id": "e2", "name": ""}, "tool": "subs",
        "played_at": "2026-11-01T00:30:00Z", "version": null, "seed": null, "outcome": null, "seat": null,
        "stories": [{"id": "n2", "name": "", "as": "narrator"}]}])");
    Same(t, OWRecord::RecordsOf(f, els).ToJson(), &want, "only e2, its one story");
}

// The stories in code point order of their ids, whatever the order served; ties on the instant by the Event's id in
// code point order (a UTF-8 id's bytes), so an id past U+FFFF sorts after U+FFFD (UTF-16 order puts it first).
OW_TEST(record_ids_order_by_code_point) {
    std::string f = Fluffington, mark = R"("x_record": {"tool": "subs", "played_at": "2026-11-01T00:30:00Z"})";
    // U+FFFD then U+1F600: code point order; UTF-16 compares the surrogate 0xD83D below 0xFFFD.
    std::string hi = "\xEF\xBF\xBD", astral = "\xF0\x9F\x98\x80";
    OWJsonValue els = Parse(R"([{"id": ")" + astral + R"(", "type": "event", "characters": [")" + f + R"("], )" + mark +
                            R"(}, {"id": ")" + hi + R"(", "type": "event", "characters": [")" + f + R"("], )" + mark +
                            R"(}, {"id": "b", "type": "narrative", "narrator": ")" + f + R"(", "events": [")" + hi +
                            R"("]}, {"id": "a", "type": "narrative", "protagonist": ")" + f + R"(", "events": [")" + hi +
                            R"("]}])");
    OWCharacterRecords got = OWRecord::RecordsOf(f, els);
    t.Eq(got.records.size(), size_t(2), "two records");
    if (got.records.size() != 2) return;
    t.Eq(got.records[0].event_id, hi, "U+FFFD first");
    t.Eq(got.records[1].event_id, astral, "U+1F600 second");
    t.Eq(got.records[0].stories.size(), size_t(2), "two stories");
    if (got.records[0].stories.size() == 2) {
        t.Eq(got.records[0].stories[0].id, std::string("a"), "a first");
        t.Eq(got.records[0].stories[1].id, std::string("b"), "b second");
    }
}

// The npm SDK 4.124.2's vector (test/record.test.mjs): ties order by code point, never UTF-16. U+FF00 is below
// U+1F600 by code point and above it in UTF-16 (the surrogate pair starts at 0xD83D); the ids' UTF-8 bytes compare in
// code point order. Both the Events' tie and the stories.
OW_TEST(record_ties_order_by_code_point_npm_vector) {
    const std::string at = R"("x_record": {"tool": "tangle", "played_at": "2026-11-01T00:00:00Z"})";
    const std::string emoji = "\xF0\x9F\x98\x80", bmp = "\xEF\xBC\x80";   // U+1F600, U+FF00
    OWJsonValue els = Parse(R"([{"id": "e)" + emoji + R"(", "type": "event", "name": "hi", "characters": ["c"], )" + at +
                            R"(}, {"id": "e)" + bmp + R"(", "type": "event", "name": "bmp", "characters": ["c"], )" + at +
                            R"(}, {"id": "n)" + emoji + R"(", "type": "narrative", "name": "a", "protagonist": "c", "events": ["e)" +
                            emoji + R"("]}, {"id": "n)" + bmp + R"(", "type": "narrative", "name": "b", "protagonist": "c", "events": ["e)" +
                            emoji + R"("]}])");
    OWCharacterRecords got = OWRecord::RecordsOf("c", els);
    t.Eq(got.records.size(), size_t(2), "two records");
    if (got.records.size() != 2) return;
    t.Eq(got.records[0].event_id, "e" + bmp, "e U+FF00 first");
    t.Eq(got.records[1].event_id, "e" + emoji, "e U+1F600 second");
    t.Eq(got.records[1].stories.size(), size_t(2), "two stories");
    if (got.records[1].stories.size() == 2) {
        t.Eq(got.records[1].stories[0].id, "n" + bmp, "n U+FF00 first");
        t.Eq(got.records[1].stories[1].id, "n" + emoji, "n U+1F600 second");
    }
}

// npm 4.124.1's test (test/record.test.mjs): a Narrative naming an Event twice is one story of it, and other values in
// events name none. (The npm clause "no two records share a story" is moot here: stories are values, copied into each
// record; the check below still holds a record's story apart from the next record's.)
OW_TEST(record_a_narrative_naming_an_event_twice_is_one_story) {
    const std::string c = Fluffington;
    OWJsonValue els = Parse(R"([
        {"id": "e1", "type": "event", "name": "e1", "characters": [")" + c + R"("], "x_record": {"tool": "tangle", "played_at": "2026-11-02T00:00:00Z"}},
        {"id": "e2", "type": "event", "name": "e2", "characters": [")" + c + R"("], "x_record": {"tool": "tangle", "played_at": "2026-11-01T00:00:00Z"}},
        {"id": "n1", "type": "narrative", "name": "One", "protagonist": ")" + c + R"(", "events": ["e1", "e1", "e2"]},
        {"id": "n2", "type": "narrative", "name": "Two", "narrator": ")" + c + R"(", "events": [7, null, {}, ["e1"], "e1"]}])");
    OWCharacterRecords got = OWRecord::RecordsOf(c, els);
    t.Eq(got.records.size(), size_t(2), "two records");
    if (got.records.size() != 2) return;
    t.Eq(got.records[0].event_id, std::string("e1"), "e1 first");
    t.Eq(got.records[0].stories.size(), size_t(2), "e1: two stories, not one per mention");
    if (got.records[0].stories.size() == 2) {
        t.Eq(got.records[0].stories[0].id + ":" + got.records[0].stories[0].as, std::string("n1:protagonist"), "e1 first story");
        t.Eq(got.records[0].stories[1].id + ":" + got.records[0].stories[1].as, std::string("n2:narrator"), "e1 second story");
    }
    t.Eq(got.records[1].stories.size(), size_t(1), "e2: n1 alone");
    if (got.records[0].stories.size() == 2 && got.records[1].stories.size() == 1) {
        got.records[0].stories[0].name = "planted";
        t.Eq(got.records[1].stories[0].name, std::string("One"), "no two records share a story");
    }
}

// Record rule 1 at serial 103: proposed is what the review profile's proposedMarkOf reads, an x_review_proposed object
// with a key. The fixture cases an object with keys, null, {}, "" and []; the page's words decide the values it does
// not case, and the npm SDK's recordsOf and the Python package's records_of read them the same: a string with text, a
// number, a boolean and a list with an entry are no proposal, so the run and the story are read.
OW_TEST(record_proposed_is_an_object_with_a_key) {
    const std::string c = Fluffington;
    auto read = [&](const std::string& proposed) {
        OWJsonValue els = Parse(R"([
            {"id": "e1", "type": "event", "characters": [")" + c + R"("], "x_review_proposed": )" + proposed +
                                R"(, "x_record": {"tool": "subs", "played_at": "2026-11-01T00:00:00Z"}},
            {"id": "e2", "type": "event", "characters": [")" + c + R"("], "x_record": {"tool": "subs", "played_at": "2026-11-02T00:00:00Z"}},
            {"id": "n1", "type": "narrative", "protagonist": ")" + c + R"(", "x_review_proposed": )" + proposed +
                                R"(, "events": ["e2"]}])");
        OWCharacterRecords got = OWRecord::RecordsOf(c, els);
        std::string out;
        for (const OWCharacterRecord& r : got.records) {
            out += r.event_id + "(";
            for (const OWRecordStory& s : r.stories) out += s.id;
            out += ")";
        }
        return out;
    };
    t.Eq(read(R"({"by": "m1"})"), std::string("e2()"), "an object with a key: the run and the story wait");
    t.Eq(read(R"({"expires": null})"), std::string("e2()"), "any key: an object with a null member is still a mark");
    for (const char* settled : {"null", "{}", "\"\"", "[]", "\"a seat's proposal\"", "\"  \"", "1", "0", "true", "false",
                                "[\"m1\"]", "[{}]"})
        t.Eq(read(settled), std::string("e2(n1)e1()"), std::string(settled) + ": settled, both read");
}

// A caller's mistake is no reading: elements that are not a list.
OW_TEST(record_a_callers_mistake_is_named) {
    OWCharacterRecords got = OWRecord::RecordsOf(Fluffington, Parse(R"({"elements": []})"));
    t.Ok(got.error.find("records_of") != std::string::npos, "an object for the list: " + got.error);
    t.Ok(got.records.empty(), "nothing read");
    t.Ok(OWRecord::RecordsOf(Fluffington, Parse("[]")).error.empty(), "an empty list is no mistake");
    t.Ok(OWRecord::RecordsOf(Fluffington, Parse(R"([7, "x", null])")).records.empty(), "items that are no element");
}

// Pure: no reading moves the elements; a seat is copied out, and the mark as read is the mark as written.
OW_TEST(record_a_reading_never_moves_the_world) {
    OWJsonValue elements = Elements("harbour");
    std::string before = OWJson::Stringify(elements);
    for (const char* who : {Fluffington, "019a8000-0000-7000-8000-000000000102", "019a8000-0000-7000-8000-000000000104"})
        OWRecord::RecordsOf(who, elements);
    for (const OWJsonValue& el : elements.Items()) OWRecord::MarkOf(el);
    t.Eq(OWJson::Stringify(elements), before, "harbour unchanged");
    const OWJsonValue* ford = ById(elements, Ford);
    t.Ok(ford && MarkJson(*ford) == *ford->Find(OWRecord::Mark), "the Ford's mark reads as written, key order kept");
}
