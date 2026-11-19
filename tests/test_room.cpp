// OWLens::IsRoomElement on the room profile's conformance fixture (schema-dist profiles/room.fixture.json, dist.136's
// copy, its sha256 held to MANIFEST.json): each case's element is a room element exactly when in_room says, the pair
// AND the mark; and each talk case's element is in the talks it says (OWLens::TalksOf, serial 135), as a set. Every
// case runs, counted, so a dropped case fails.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"

#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.136's MANIFEST.json records it.
const char* const DistSha256 = "43d84e0012537a9899f67bd1950b7223b0c2b4ff87e57cfab395a232e44a85c7";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/room.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* f = v.Find(key);
    return f && f->IsString() ? f->Text() : std::string();
}

}  // namespace

OW_TEST(room_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the room fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.136's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/room.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("room"), "the room profile's fixture");
    const OWJsonValue* mark = doc->Find("mark");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string("x_room_of"), "the mark");
}

OW_TEST(room_cases_answer_in_room) {
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::set<std::string> names;
    size_t run = 0, in = 0;
    for (const OWJsonValue& c : cases->Items()) {
        const std::string name = Str(c, "name");
        names.insert(name);
        const OWJsonValue* element = c.Find("element");
        const OWJsonValue* want = c.Find("in_room");
        if (!element || !want || !want->IsBool()) {
            t.Ok(false, name + ": a case with an element and in_room");
            continue;
        }
        t.Eq(OWLens::IsRoomElement(*element), want->AsBool(), name + ": in_room");
        ++run;
        if (want->AsBool()) ++in;
    }
    t.Eq(cases->Size(), size_t(48), "48 cases");
    t.Eq(names.size(), size_t(48), "each name its own");
    t.Eq(run, size_t(48), "every case runs here");
    t.Eq(in, size_t(21), "21 of them in the room");
}

namespace {

// A list of {type, id} as a set of "type id" strings, and whether any came twice.
std::set<std::string> TalkSet(const std::vector<OWLens::OWTalk>& talks, bool& twice) {
    std::set<std::string> out;
    twice = false;
    for (const OWLens::OWTalk& k : talks)
        if (!out.insert(k.type + " " + k.id).second) twice = true;
    return out;
}

std::set<std::string> WantSet(const OWJsonValue* talks) {
    std::set<std::string> out;
    if (talks && talks->IsArray())
        for (const OWJsonValue& k : talks->Items()) out.insert(Str(k, "type") + " " + Str(k, "id"));
    return out;
}

}  // namespace

// The talk list is read whole (serial 135): a case added later must not pass unseen.
OW_TEST(room_talk_cases_are_serial_135s) {
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("talk_cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its talk_cases");
    if (!cases) return;
    std::set<std::string> names;
    for (const OWJsonValue& c : cases->Items()) names.insert(Str(c, "name"));
    t.Eq(cases->Size(), size_t(13), "13 talk cases");
    t.Eq(names.size(), size_t(13), "each name its own");
    const std::vector<std::pair<std::string, std::string>> fields = {{"objects", "object"}, {"constructs", "construct"}};
    t.Ok(OWLens::TalkFields() == fields, "the two talk fields");
}

OW_CASES(room_talk_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("talk_cases");
    if (!cases || !cases->IsArray()) {
        out.emplace_back("room_talk:not-found", [](owtest::T& t) { t.Fail("the talk cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("room_talk:" + Str(c, "name"), [doc, &c](owtest::T& t) {
            const OWJsonValue* element = c.Find("element");
            if (!element) return t.Fail("no element");
            bool twice = false;
            std::set<std::string> got = TalkSet(OWLens::TalksOf(*element), twice);
            t.Ok(!twice, "each talk once");
            t.Ok(got == WantSet(c.Find("talks")), "the talks, as a set");
        });
    return out;
}

// Beside the fixture: one presence for both marks (serial 126). x_room_of and x_charter_of each count by presence, a
// string only when something is left after trimming White_Space, a list or an object only when it holds something,
// any other value but null (a number, false) a mark; the value is never compared.
OW_TEST(room_both_marks_one_presence) {
    const std::pair<const char*, bool> shapes[] = {
        {R"("w1")", true},   {"7", true},     {"0", true},          {"false", true},
        {R"(["w1"])", true}, {R"({"a": 1})", true}, {R"("")", false}, {R"("  ")", false},
        {"null", false},     {"[]", false},   {"{}", false},
    };
    for (const auto& [mark, marks] : shapes) {
        const std::string room = std::string(R"({"type": "narrative", "supertype": "Message", "x_room_of": )") + mark + "}";
        const std::string aid = std::string(R"({"type": "law", "supertype": "Charter", "x_charter_of": )") + mark + "}";
        t.Eq(OWLens::IsRoomElement(OWJson::Parse(room).value), marks, std::string("x_room_of ") + mark);
        t.Eq(OWLens::IsReadingAid(OWJson::Parse(aid).value), marks, std::string("x_charter_of ") + mark);
    }
}
