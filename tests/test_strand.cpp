// OWStrand on the strand profile's conformance fixture (schema-dist profiles/strand.fixture.json, dist.123's copy, its
// sha256 held to MANIFEST.json): the six slots over the fixture's 24 worlds and their readers, every case by name,
// and the mark held equal to the fixture's; then readings the fixture leaves open (the public rung on a closed and an
// open world, the mark shapes that are no mark with an empty-string member among them, a story alone, a non-list
// multi-link, a nameless slot, a caller's mistake, a reading that never moves the world).

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWStrand.h"

#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.123's MANIFEST.json records it.
const char* const DistSha256 = "d0f9bb23e9f538c99165b3e590d4da33dc329fa9ee565e0875d2fa4a09e2979d";

const char* const Story = "019a7f00-0000-7000-8000-000000000301";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/strand.fixture.json"; }

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

// Equal as JSON values: an object's keys in any order, a number by its value (2 and 2.0 alike).
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

// A fixture world by name; a case's world is always a name here.
OWJsonValue World(const std::string& name) {
    const OWJsonValue* worlds = Fixture()->Find("worlds");
    const OWJsonValue* w = worlds ? worlds->Find(name) : nullptr;
    return w ? *w : OWJsonValue::Null();
}

// A fixture reader by name, from its world's readers.
OWJsonValue ReaderOf(const OWJsonValue& world, const std::string& name) {
    const OWJsonValue* readers = world.Find("readers");
    const OWJsonValue* r = readers ? readers->Find(name) : nullptr;
    return r ? *r : OWJsonValue::Null();
}

const OWJsonValue& Empty() {
    static const OWJsonValue empty =
        Parse(R"({"narrative": null, "event": null, "location": null, "character": null, "now": null, "same": null})");
    return empty;
}

void Case(owtest::T& t, const OWJsonValue& c) {
    if (Str(c, "kind") != "strand") return t.Fail("a case of unknown kind " + Str(c, "kind"));
    OWJsonValue world = World(Str(c, "world"));
    if (!world.IsObject()) return t.Fail("no world " + Str(c, "world"));
    OWJsonValue reader = ReaderOf(world, Str(c, "reader"));
    if (reader.IsNull()) return t.Fail("no reader " + Str(c, "reader"));
    OWWorldStrand got = OWStrand::StrandOf(world, reader);
    t.Eq(got.error, std::string(), "no caller's mistake");
    Same(t, got.ToJson(), c.Find("want"), "the six slots");
}

OWJsonValue Owner() { return Parse(R"({"rung": "owner"})"); }

}  // namespace

OW_TEST(strand_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the strand fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.123's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/strand.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("strand"), "the strand profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::set<std::string> names;
    size_t strands = 0;
    for (const OWJsonValue& c : cases->Items()) {
        names.insert(Str(c, "name"));
        if (Str(c, "kind") == "strand") ++strands;
    }
    t.Eq(cases->Size(), size_t(27), "27 cases");
    t.Eq(names.size(), size_t(27), "each name its own");
    t.Eq(strands, size_t(27), "every case a strand case");
    const OWJsonValue* worlds = doc->Find("worlds");
    t.Eq(worlds ? worlds->Size() : size_t(0), size_t(24), "24 worlds");
    const OWJsonValue* mark = doc->Find("mark");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string(OWStrand::Mark), "the mark's field");
    std::vector<std::string> members;
    if (const OWJsonValue* list = mark ? mark->Find("members") : nullptr)
        for (const OWJsonValue& v : list->Items()) members.push_back(Str(&v));
    t.Ok(members == OWStrand::Members(), "the mark's members, in order");
}

OW_CASES(strand_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("strand_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("strand_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// The public reader reads no strand of a world that is not open, and the owner's strand of one that is, as the charter
// reader reads the public rung (the npm SDK's test/strand.test.mjs); the owner reading as the public reads a closed
// world as the public does.
OW_TEST(strand_the_public_reads_nothing_of_a_closed_world) {
    OWJsonValue world = World("harbour");
    OWJsonValue owner = OWStrand::StrandOf(world, Owner()).ToJson();
    Same(t, OWStrand::StrandOf(world, Parse(R"({"rung": "public"})")).ToJson(), &Empty(), "closed: nothing");
    Same(t, OWStrand::StrandOf(world, Parse(R"({"rung": "owner", "as": {"rung": "public"}})")).ToJson(), &Empty(),
         "the owner as the public");
    world.Set("open", OWJsonValue::Bool(true));
    Same(t, OWStrand::StrandOf(world, Parse(R"({"rung": "public"})")).ToJson(), &owner, "open: the owner's strand");
}

// A value that is not the profile's shape is no mark (serial 99): the story's member missing, null or not a string; the
// event's or the place's an empty string, a number or an object; the mark a list, a bare id, null, no key. Kills: a mark
// read from whichever members are strings, and one that refuses only the empty string.
OW_TEST(strand_a_mark_that_is_not_the_profiles_shape_is_no_mark) {
    OWJsonValue harbour = World("harbour");
    std::string n = "\"019a7f00-0000-7000-8000-000000000301\"", e = "\"019a7f00-0000-7000-8000-000000000311\"",
                l = "\"019a7f00-0000-7000-8000-000000000321\"";
    const std::string bad[] = {
        "{\"event\": " + e + ", \"location\": " + l + "}",
        "{\"narrative\": null, \"event\": " + e + ", \"location\": " + l + "}",
        "{\"narrative\": 7, \"event\": " + e + ", \"location\": " + l + "}",
        "{\"narrative\": " + n + ", \"event\": 7, \"location\": " + l + "}",
        "{\"narrative\": " + n + ", \"event\": " + e + ", \"location\": 7}",
        "{\"narrative\": " + n + ", \"event\": {}, \"location\": " + l + "}",
        "{\"narrative\": " + n + ", \"event\": " + e + ", \"location\": []}",
        "[" + n + ", " + e + ", " + l + "]",
        n,
        "null",
    };
    for (const std::string& mark : bad) {
        OWJsonValue world = harbour;
        world.Set(OWStrand::Mark, Parse(mark));
        Same(t, OWStrand::StrandOf(world, Owner()).ToJson(), &Empty(), mark);
    }
    OWJsonValue none = harbour;
    none.Erase(OWStrand::Mark);
    Same(t, OWStrand::StrandOf(none, Owner()).ToJson(), &Empty(), "no key");
}

// An empty string in the event's or the place's member is neither an id nor unset, so the whole value is no mark
// (ruled for serial 99, the npm and Python readers' reading). Kills: an empty string read as an empty slot.
OW_TEST(strand_an_empty_string_member_is_no_mark) {
    for (const char* member : {"event", "location"}) {
        OWJsonValue world = World("harbour");
        if (OWJsonValue* mark = world.Find(OWStrand::Mark)) mark->Set(member, OWJsonValue::String(""));
        Same(t, OWStrand::StrandOf(world, Owner()).ToJson(), &Empty(), member);
    }
}

// Only the story is required (serial 99): the event and the place null or absent read as unset, so a story alone is a
// strand begun, its hero and now read; a place named with no event is an empty place. Kills: an unset member read as
// no mark, and a place read without its event.
OW_TEST(strand_a_story_alone_is_a_strand_begun) {
    std::string n = "\"019a7f00-0000-7000-8000-000000000301\"", l = "\"019a7f00-0000-7000-8000-000000000321\"";
    const std::string begun[] = {
        "{\"narrative\": " + n + ", \"event\": null, \"location\": null}",
        "{\"narrative\": " + n + "}",
        "{\"narrative\": " + n + ", \"event\": null, \"location\": " + l + "}",
        "{\"narrative\": " + n + ", \"location\": " + l + "}",
    };
    const OWJsonValue want = Parse(R"({"narrative": {"id": "019a7f00-0000-7000-8000-000000000301",
        "name": "The Night the Lighthouse Went Dark"}, "event": null, "location": null,
        "character": {"id": "019a7f00-0000-7000-8000-000000000331", "name": "Ilse the Keeper"},
        "now": {"id": "019a7f00-0000-7000-8000-000000000322", "name": "The Docks"}, "same": null})");
    for (const std::string& mark : begun) {
        OWJsonValue world = World("harbour");
        world.Set(OWStrand::Mark, Parse(mark));
        Same(t, OWStrand::StrandOf(world, Owner()).ToJson(), &want, mark);
    }
}

// A multi-link that is not a list is empty (serial 99): the owner's stored story with a bare string in its events lists
// nothing, so the event and the place are empty and the rest reads. Kills: keel's one-item reading.
OW_TEST(strand_a_multi_link_that_is_not_a_list_is_empty) {
    OWJsonValue world = World("harbour");
    if (OWJsonValue* elements = world.Find("elements"))
        for (OWJsonValue& el : elements->Items())
            if (Str(el, "id") == Story) el.Set("events", OWJsonValue::String("019a7f00-0000-7000-8000-000000000311"));
    OWWorldStrand got = OWStrand::StrandOf(world, Owner());
    t.Eq(got.narrative ? got.narrative->id : std::string(), std::string(Story), "the story");
    t.Ok(!got.event, "no event");
    t.Ok(!got.location, "no place");
    t.Eq(got.character ? got.character->id : std::string(), std::string("019a7f00-0000-7000-8000-000000000331"), "the hero");
}

// A slot whose element has no name, a null one or one that is not text reads an empty string, never null (serial 99).
OW_TEST(strand_a_slot_without_a_name_reads_an_empty_string) {
    const OWJsonValue want = Parse(R"({"id": "019a7f00-0000-7000-8000-000000000322", "name": ""})");
    for (int shape = 0; shape < 3; ++shape) {
        OWJsonValue world = World("harbour");
        if (OWJsonValue* elements = world.Find("elements"))
            for (OWJsonValue& el : elements->Items()) {
                if (Str(el, "id") != "019a7f00-0000-7000-8000-000000000322") continue;
                if (shape == 0) el.Erase("name");
                else el.Set("name", shape == 1 ? OWJsonValue::Null() : OWJsonValue::Number(int64_t(7)));
            }
        OWJsonValue got = OWStrand::StrandOf(world, Owner()).ToJson();
        const OWJsonValue* now = got.Find("now");
        t.Ok(now && Equal(*now, want), "name shape " + std::to_string(shape) + ": " + (now ? Compact(*now) : "none"));
    }
}

// A caller's mistake is no reading: the message names it, and the strand is empty.
OW_TEST(strand_a_callers_mistake_is_named) {
    t.Ok(OWStrand::StrandOf(Parse(R"({"no": "elements"})"), Owner()).error.find("strand_of") != std::string::npos,
         "a world without elements");
    t.Ok(!OWStrand::StrandOf(Parse(R"({"elements": []})"), Parse(R"({"rung": "king"})")).error.empty(),
         "a rung outside the four");
    Same(t, OWStrand::StrandOf(OWJsonValue::Number(int64_t(3)), Owner()).ToJson(), &Empty(), "nothing read");
}

// Pure: no reading moves the world.
OW_TEST(strand_a_reading_never_moves_the_world) {
    const OWJsonValue* worlds = Fixture()->Find("worlds");
    t.Ok(worlds && worlds->Size() == 24, "the 24 worlds");
    if (!worlds) return;
    for (size_t w = 0; w < worlds->Size(); ++w) {
        const OWJsonValue& world = worlds->Items()[w];
        std::string before = OWJson::Stringify(world);
        if (const OWJsonValue* readers = world.Find("readers"))
            for (const OWJsonValue& r : readers->Items()) OWStrand::StrandOf(world, r);
        t.Eq(OWJson::Stringify(world), before, worlds->Keys()[w] + " unchanged");
    }
}
