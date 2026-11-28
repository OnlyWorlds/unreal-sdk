// The TypeScript SDK's and the Python package's shared unsafe-id cases (tests/data/unsafe-ids.cases.json, the same
// bytes, held by its sha256), each run through OWWorld::WriteRefusal and WriteFolder: a refused id is refused before any
// file lands, in their words; a written id lands at their path.
//
// Two cases part from the packages on purpose. An empty id meets the writer's own no-id refusal first, which names what
// a maker has to do; the packages' "it is empty" is never reached from a write. And "c-\u007f", which the packages
// write, is refused: the archive's name check behind their rules takes a control character as general category Cc
// (folder spec section 4.7, OW-22), U+007F among them.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <memory>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/unsafe-ids.cases.json at 4.224.0; the Python package holds the same bytes.
const char* const CasesSha256 = "cc6c0c016b441c0ebdc2840304f28c69e05a7ddf6eb0a333b343188717eb5581";

const OWBytes& CasesBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/unsafe-ids.cases.json", bytes);
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

const OWJsonValue& Get(const OWJsonValue& v, const char* key) {
    static const OWJsonValue none = OWJsonValue::Array();
    const OWJsonValue* x = v.Find(key);
    return x ? *x : none;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

void Under(const std::string& root, const std::string& rel, std::string& out) {
    std::string here = rel.empty() ? root : root + "/" + rel;
    for (const std::string& f : OWFiles::List(here, false)) out += (out.empty() ? "" : " ") + (rel.empty() ? f : rel + "/" + f);
    for (const std::string& d : OWFiles::List(here, true)) Under(root, rel.empty() ? d : rel + "/" + d, out);
}

struct Outcome {
    std::string refusal;  // WriteRefusal's line
    bool written = false;  // WriteFolder's answer
    std::string error;     // WriteFolder's line
    std::string files;     // every file under the temporary folder, relative to it, joined by spaces
};

// Writes one case's element into box/world two levels inside a fresh temporary folder.
Outcome Write(const OWJsonValue& c) {
    static const std::string world = "{\"id\": \"0191f3a2-7c4e-7b1a-9d2e-3f4a5b6c7d8e\", \"name\": \"W\"}";
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", OWBytes(world.begin(), world.end())}});
    OWJsonValue b = OWJsonValue::Object();
    b.Set("id", OWJsonValue::String(Str(c, "id")));
    b.Set("type", OWJsonValue::String("character"));
    b.Set("name", OWJsonValue::String(Str(c, "name")));
    w->Add("character", b);
    Outcome o;
    o.refusal = w->WriteRefusal();
    std::string root = P().tmp + "/unsafe-id-cases";
    OWFiles::RemoveTree(root);
    OWFiles::MakeDirs(root + "/box");
    o.written = w->WriteFolder(root + "/box/world", false, &o.error);
    Under(root, "", o.files);
    OWFiles::RemoveTree(root);
    return o;
}

std::string Label(const OWJsonValue& c) { return OWJson::Quote(Str(c, "id")) + ", name '" + Str(c, "name") + "'"; }

}  // namespace

OW_TEST(unsafe_id_cases_file_is_the_shared_bytes) {
    t.Eq(owtest::Sha256Hex(CasesBytes()), std::string(CasesSha256), "the cases file's sha256 (the npm SDK's bytes)");
    t.Eq(Get(*Doc(), "refused").Size(), size_t(19), "the refused cases' count");
    t.Eq(Get(*Doc(), "written").Size(), size_t(5), "the written cases' count");
}

OW_CASES(unsafe_id_cases) {
    owtest::Cases out;
    auto doc = Doc();
    for (const OWJsonValue& c : Get(*doc, "refused").Items()) {
        out.emplace_back("unsafe_id_cases: " + Label(c) + " is refused", [c](owtest::T& t) {
            const std::string id = Str(c, "id"), label = Label(c);
            Outcome o = Write(c);
            if (id.empty()) {
                // By design: an empty id keeps the writer's no-id refusal, which runs first and tells a maker what to
                // do; the packages' "it is empty" is their guard's word for the same refusal.
                t.Ok(o.refusal.rfind("a new or edited character has no id", 0) == 0, label + ": the no-id refusal: " + o.refusal);
            } else {
                t.Eq(o.refusal, "character element id " + OWJson::Quote(id) + " cannot be a file name: " + Str(c, "why") + ".",
                     label + ": in the packages' words");
            }
            t.Ok(!o.written, label + ": WriteFolder refuses");
            t.Eq(o.error, o.refusal, label + ": with WriteRefusal's line");
            t.Eq(o.files, std::string(), label + ": nothing written, inside the folder or beside it");
        });
    }
    for (const OWJsonValue& c : Get(*doc, "written").Items()) {
        out.emplace_back("unsafe_id_cases: " + Label(c) + " is written", [c](owtest::T& t) {
            const std::string id = Str(c, "id"), label = Label(c);
            Outcome o = Write(c);
            if (id == "c-\x7f") {
                // By design: the archive's name check, behind the packages' rules, refuses U+007F (general category Cc,
                // folder spec section 4.7, OW-22), where the packages refuse U+0000 to U+001F only.
                t.Ok(o.refusal.rfind("character element id " + OWJson::Quote(id) + " cannot be a file name: its file name ", 0) == 0,
                     label + ": the name check's refusal: " + o.refusal);
                t.Ok(!o.written, label + ": WriteFolder refuses");
                t.Eq(o.files, std::string(), label + ": nothing written");
                return;
            }
            t.Eq(o.refusal, std::string(), label + ": no refusal");
            t.Ok(o.written, label + ": written: " + o.error);
            t.Eq(o.files, "box/world/world.json box/world/" + Str(c, "path"), label + ": at the packages' path");
        });
    }
    return out;
}
