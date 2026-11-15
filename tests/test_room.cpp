// OWLens::IsRoomElement on the room profile's conformance fixture (schema-dist profiles/room.fixture.json, dist.131's
// copy, its sha256 held to MANIFEST.json): each case's element is a room element exactly when in_room says, the pair
// AND the mark. Every case runs, counted, so a dropped case fails.

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

// The fixture's sha256 as dist.131's MANIFEST.json records it.
const char* const DistSha256 = "a1700126b8025d3b5f31c5542c3e6a50e623e915086ef6834fe7905086bc41b3";

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
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.131's");
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
