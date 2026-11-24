// OWMapSpace::PinsKnownTo on the npm SDK's and the Python package's shared cases (tests/data/pins-known-to.cases.json,
// the same bytes, held by its sha256): each case's whole answer, key order included, and each refusal by the argument
// it names; then what the cases cannot hold: the inputs left as they were, and the answer beside MapSpaceOf's.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWMapSpace.h"

#include <memory>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/pins-known-to.cases.json at 4.200.0 (8ae9f6e), version 1; the Python package holds the
// same bytes.
const char* const CasesSha256 = "80f66dba8aa1a009770bdcffbf457ec84b78f7e4270a2fb9cf8614a4b76e39f6";

const OWBytes& CasesBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/pins-known-to.cases.json", bytes);
        read = true;
    }
    return bytes;
}

std::shared_ptr<OWJsonValue> Doc() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWJsonParseResult r = OWJson::Parse(CasesBytes());
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

// JSON text in one line: two answers are the same, key order and every value, exactly when their texts are.
std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

const OWJsonValue& Get(const OWJsonValue& v, const char* key) {
    static const OWJsonValue none;
    const OWJsonValue* x = v.Find(key);
    return x ? *x : none;
}

}  // namespace

OW_TEST(pins_known_to_cases_file_is_the_pinned_one) {
    t.Eq(owtest::Sha256Hex(CasesBytes()), std::string(CasesSha256), "the cases file's sha256 (the npm SDK's bytes)");
    const OWJsonValue& doc = *Doc();
    const OWJsonValue* version = doc.Find("version");
    t.Ok(version && version->IsNumber() && version->Text() == "1", "version 1");
    t.Eq(Get(doc, "cases").Size(), size_t(31), "31 cases");
    t.Eq(Get(doc, "refusals").Size(), size_t(8), "8 refusals");
}

OW_CASES(pins_known_to_cases) {
    owtest::Cases out;
    auto doc = Doc();
    for (const OWJsonValue& c : Get(*doc, "cases").Items()) {
        const OWJsonValue* cp = &c;
        out.push_back({"pins_known_to: " + Str(c, "name"), [doc, cp](owtest::T& t) {
                           const OWJsonValue& pins = Get(*cp, "pins");
                           const OWJsonValue& served = Get(*cp, "served");
                           const std::string pinsBefore = Compact(pins), servedBefore = Compact(served);
                           OWPinsKnownTo answer = OWMapSpace::PinsKnownTo(pins, served);
                           t.Eq(answer.error, std::string(), "no mistake");
                           t.Eq(Compact(answer.ToJson()), Compact(Get(*cp, "want")), "the whole answer");
                           t.Eq(Compact(pins), pinsBefore, "pins left as they were");
                           t.Eq(Compact(served), servedBefore, "served left as it was");
                       }});
    }
    for (const OWJsonValue& r : Get(*doc, "refusals").Items()) {
        const OWJsonValue* rp = &r;
        out.push_back({"pins_known_to refuses: " + Str(r, "name"), [doc, rp](owtest::T& t) {
                           const std::string refuses = Str(*rp, "refuses");
                           const std::string other = refuses == "pins" ? "served" : "pins";
                           OWPinsKnownTo answer = OWMapSpace::PinsKnownTo(Get(*rp, "pins"), Get(*rp, "served"));
                           t.Ok(answer.error.find(refuses + " is a list") != std::string::npos,
                                "the mistake names " + refuses + ": \"" + answer.error + "\"");
                           t.Ok(answer.error.find(other + " is a list") == std::string::npos,
                                "and not " + other + ": \"" + answer.error + "\"");
                           t.Ok(answer.known.empty() && answer.unknown == 0 && answer.out.no_link == 0, "nothing answered");
                       }});
    }
    return out;
}

OW_TEST(pins_known_to_the_answers_keys_and_words) {
    OWJsonParseResult pins = OWJson::Parse(std::string_view(
        R"([{"type": "pin", "id": "p1", "map": "m1", "element_type": "location", "element_id": "l1"},
            {"type": "pin", "id": "p2", "element_type": "location"}])"));
    OWJsonParseResult served = OWJson::Parse(std::string_view(R"([{"type": "location", "id": "l1", "known_of": true}])"));
    OWPinsKnownTo answer = OWMapSpace::PinsKnownTo(pins.value, served.value);
    t.Eq(Compact(answer.ToJson()),
         std::string(R"({"known": [{"pin": "p1","map": "m1","element_type": "location","element_id": "l1","how": "known_of"}],"unknown": 0,"out": {"no_link": 1}})"),
         "known, unknown, out in that order; each known pin, map, element_type, element_id, how");
    t.Eq(std::string(OWMapSpace::PinWhole), std::string("whole"), "the word for an element served whole");
    t.Eq(std::string(OWMapSpace::PinKnownOf), std::string("known_of"), "the word for a stub");
    OWPinsKnownTo none = OWMapSpace::PinsKnownTo(OWJsonValue::Array(), OWJsonValue::Array());
    t.Eq(Compact(none.ToJson()), std::string(R"({"known": [],"unknown": 0,"out": {}})"), "all three keys always present");
}

OW_TEST(pins_known_to_beside_map_space_a_drawing_keeps_the_pins_both_name) {
    OWJsonParseResult world = OWJson::Parse(std::string_view(R"([
        {"type": "map", "id": "m1", "width": 100, "height": 100},
        {"type": "pin", "id": "p1", "map": "m1", "x": 1, "y": 2, "element_type": "location", "element_id": "l1"},
        {"type": "pin", "id": "p2", "map": "m1", "x": 3, "y": 4, "element_type": "location", "element_id": "l2"},
        {"type": "pin", "id": "p3", "map": "m1", "element_type": "location", "element_id": "l1"},
        {"type": "pin", "id": "p4", "map": "gone", "x": 5, "y": 6, "element_type": "location", "element_id": "l1"},
        {"type": "location", "id": "l1"},
        {"type": "location", "id": "l2"}
    ])"));
    OWJsonParseResult served = OWJson::Parse(std::string_view(R"([{"type": "location", "id": "l1", "known_of": true}])"));
    if (!world.ok || !served.ok) return t.Fail("the inputs parse");
    OWPinsKnownTo answer = OWMapSpace::PinsKnownTo(world.value, served.value);
    std::string pins;
    for (const OWPinKnown& k : answer.known) pins += k.pin + " ";
    t.Eq(pins, std::string("p1 p3 p4 "), "known: p3 with no point and p4 on no Map are known all the same");
    t.Eq(answer.unknown, int64_t(1), "p2 unknown");
    std::string drawn;
    for (const OWMapSpacePin& p : OWMapSpace::MapSpaceOf(world.value).pins)
        for (const OWPinKnown& k : answer.known)
            if (k.pin == p.pin)
                drawn += p.pin + " " + std::to_string(p.point.x) + " " + std::to_string(p.point.y) + " " + k.how + ";";
    t.Eq(drawn, std::string("p1 1 2 known_of;"), "a drawing keeps the Pins both readings name");
}
