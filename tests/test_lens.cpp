// OWLens on the lens profile's conformance fixture (schema-dist profiles/lens.fixture.json, dist.91's copy, its sha256
// held to MANIFEST.json): the pair and the template, the legal values of x_lens, where a seat's choice lives, the
// resolve and invariant kinds' 79 cases and the graph kind's 39 are counted, not run: resolution and the graph section
// are not read here yet. The profile's tables are held equal to the fixture's, the
// field table to schema.json and the stub's public fields to presentation.json; then readings the fixture leaves open.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.91's MANIFEST.json records it.
const char* const DistSha256 = "2ce92de4f07ccd4b289a881f749579c653358a37fc4ec9e219d84d13addb0cdd";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/lens.fixture.json"; }

std::shared_ptr<OWJsonValue> Load(const std::string& path) {
    OWBytes bytes;
    OWFiles::Read(path, bytes);
    OWJsonParseResult r = OWJson::Parse(bytes);
    return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
}

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = Load(FixturePath());
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

OWJsonValue Strings(const std::vector<std::string>& list) {
    OWJsonValue out = OWJsonValue::Array();
    for (const std::string& s : list) out.Push(OWJsonValue::String(s));
    return out;
}

// One case, by its kind. A text_only case holds for a reader that can see a number's text: this one can, so it runs.
void Case(owtest::T& t, const OWJsonValue& c) {
    std::string kind = Str(c, "kind");
    static const OWJsonValue none;
    auto at = [&](const char* key) -> const OWJsonValue& {
        const OWJsonValue* v = c.Find(key);
        if (!v) t.Fail(std::string("no ") + key);
        return v ? *v : none;
    };
    if (kind == "shape") {
        Same(t, OWJsonValue::Bool(OWLens::IsLens(at("element"))), c.Find("is_lens"), "is_lens");
        Same(t, OWJsonValue::Bool(OWLens::IsTemplate(at("element"))), c.Find("is_template"), "is_template");
    } else if (kind == "keys") {
        std::optional<std::string> bad = OWLens::CheckKeys(c.Find("x_lens"));
        Same(t, bad ? OWJsonValue::String(*bad) : OWJsonValue::Null(), c.Find("bad"), "the key refused");
        Same(t, OWJsonValue::Bool(!bad), c.Find("legal"), "legal");
    } else if (kind == "uses") {
        Same(t, Strings(OWLens::LensesInUse(c.Find("membership"), c.Find("x_lens_uses"))), c.Find("lenses"),
             "the lenses read");
    } else {
        t.Fail("a case of unknown kind " + kind);
    }
}

// The kinds this file runs, and the one it counts only.
const char* const Run[] = {"shape", "keys", "uses"};

bool Runs(const std::string& kind) {
    for (const char* k : Run)
        if (kind == k) return true;
    return false;
}

}  // namespace

OW_TEST(lens_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the lens fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.91's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/lens.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("lens"), "the lens profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::map<std::string, size_t> kinds;
    std::set<std::string> names;
    size_t run = 0;
    for (const OWJsonValue& c : cases->Items()) {
        ++kinds[Str(c, "kind")];
        names.insert(Str(c, "name"));
        if (Runs(Str(c, "kind"))) ++run;
    }
    t.Eq(cases->Size(), size_t(153), "153 cases");
    t.Eq(names.size(), size_t(153), "each name its own");
    const std::pair<const char*, size_t> want[] = {{"shape", 6},   {"keys", 25},      {"uses", 4},
                                                   {"resolve", 75}, {"invariant", 4}, {"graph", 39}};
    size_t sum = 0;
    for (const auto& [kind, n] : want) {
        t.Eq(kinds[kind], n, std::string("kind ") + kind);
        sum += n;
    }
    t.Eq(sum, size_t(153), "the six kinds are every case");
    t.Eq(kinds.size(), size_t(6), "and no other kind");
    t.Eq(run, size_t(35), "35 run here, the 79 resolve and invariant and the 39 graph cases not yet");
}

OW_TEST(lens_tables_are_the_fixtures) {
    auto doc = Fixture();
    auto list = [&](const char* key) {
        std::vector<std::string> out;
        if (const OWJsonValue* v = doc->Find(key))
            for (const OWJsonValue& s : v->Items()) out.push_back(s.Text());
        return out;
    };
    const OWJsonValue* pair = doc->Find("pair");
    t.Ok(pair != nullptr, "the fixture names the pair");
    if (!pair) return;
    t.Eq(Str(*pair, "type"), std::string(OWLens::Type), "the pair's type");
    t.Eq(Str(*pair, "supertype"), std::string(OWLens::Supertype), "the pair's supertype");
    t.Eq(Str(*doc, "template_subtype"), std::string(OWLens::TemplateSubtype), "the template's subtype");
    t.Eq(Str(*doc, "field"), std::string(OWLens::Field), "the recipe's field");
    t.Eq(Str(*doc, "uses_field"), std::string(OWLens::UsesField), "the uses field");
    t.Ok(list("anchor_fields") == OWLens::AnchorFields(), "the anchor fields");
    t.Ok(list("type_order") == OWLens::TypeOrder(), "the type order");
    t.Ok(list("room_pairs") == OWLens::RoomPairs(), "the room pairs");
    t.Ok(list("reading_aid_pairs") == OWLens::ReadingAidPairs(), "the reading aids' pairs");
    t.Ok(list("cut_reasons") == OWLens::CutReasons(), "the cut reasons");
    t.Ok(list("owner_only_reasons") == OWLens::OwnerOnlyReasons(), "the owner's two");
    const OWJsonValue* mark = doc->Find("reading_aid_mark");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string(OWLens::ReadingAidMark), "the reading aid's mark");
    std::vector<std::pair<std::string, std::string>> dates;
    if (const OWJsonValue* d = doc->Find("date_table"))
        for (size_t i = 0; i < d->Size(); ++i) dates.emplace_back(d->Keys()[i], d->Items()[i].Text());
    t.Ok(dates == OWLens::DateTable(), "the date table");
    const OWJsonValue* d = doc->Find("defaults");
    t.Ok(d != nullptr, "the defaults");
    if (!d) return;
    t.Eq(d->Find("depth")->Text(), std::to_string(OWLens::DefaultDepth), "depth");
    t.Eq(d->Find("budget")->Text(), std::to_string(OWLens::DefaultBudget), "budget");
    t.Eq(Str(*d, "undated"), std::string(OWLens::DefaultUndated), "undated");
    t.Eq(Str(*d, "show"), std::string(OWLens::DefaultShow), "show");
    t.Eq(Str(*d, "mail"), std::string(OWLens::DefaultMail), "mail");
    t.Eq(d->Find("mail_days")->Text(), std::to_string(OWLens::MailDays), "mail_days");
    // The room pairs are the room profile's own.
    std::shared_ptr<OWJsonValue> room = Load(P().root + "/codegen/schema-dist/profiles/room.fixture.json");
    std::vector<std::string> words;
    if (const OWJsonValue* pairs = room->Find("pairs"))
        for (const OWJsonValue& p : pairs->Items())
            words.push_back(Str(p, "type") + "/" + Str(p, "supertype"));
    std::vector<std::string> folded;
    for (const std::string& w : words) {
        std::string f;
        for (char ch : w) f.push_back(ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch);
        folded.push_back(f);
    }
    t.Ok(!folded.empty() && folded == OWLens::RoomPairs(), "the room fixture's pairs, folded");
}

OW_TEST(lens_fields_are_schema_jsons) {
    std::shared_ptr<OWJsonValue> schema = Load(P().root + "/codegen/schema-dist/schema.json");
    const OWJsonValue* types = schema->Find("types");
    t.Ok(types && types->Size() == 22, "schema.json's 22 types");
    if (!types) return;
    std::vector<std::string> order(types->Keys().begin(), types->Keys().end());
    t.Ok(order == OWLens::TypeOrder(), "the type order is schema.json's");
    auto kind_of = [](OWLens::OWFieldKind k) {
        switch (k) {
            case OWLens::OWFieldKind::Text: return "text";
            case OWLens::OWFieldKind::Int: return "int";
            case OWLens::OWFieldKind::Single: return "single";
            case OWLens::OWFieldKind::Multi: return "multi";
            case OWLens::OWFieldKind::GenericType: return "generic_type";
            case OWLens::OWFieldKind::GenericId: return "generic_id";
        }
        return "?";
    };
    for (size_t i = 0; i < types->Size(); ++i) {
        const std::string& type = types->Keys()[i];
        std::vector<std::string> want, got;
        for (const OWJsonValue& f : types->Items()[i].Items()) {
            std::string k = Str(f, "kind");
            if (k == "generic") {
                want.push_back(Str(f, "type_key") + ":generic_type:");
                want.push_back(Str(f, "id_key") + ":generic_id:" + Str(f, "type_key"));
                continue;
            }
            std::string kk = k == "scalar_int" ? "int" : k == "single" ? "single" : k == "multi" ? "multi" : "text";
            want.push_back(Str(f, "name") + ":" + kk + ":" + Str(f, "target"));
        }
        for (const OWLens::OWLensField& f : OWLens::FieldsOf(type))
            got.push_back(std::string(f.name) + ":" + kind_of(f.kind) + ":" + f.target);
        t.Ok(!want.empty() && got == want, type + "'s fields are schema.json's");
    }
    t.Ok(OWLens::FieldsOf("Character").empty() && OWLens::FieldsOf("people").empty(), "a word not a slug has none");
    // The stub's public fields are presentation.json's.
    std::shared_ptr<OWJsonValue> pres = Load(P().root + "/codegen/schema-dist/presentation.json");
    const OWJsonValue* ptypes = pres->Find("types");
    t.Ok(ptypes != nullptr, "presentation.json's types");
    if (!ptypes) return;
    for (const std::string& type : OWLens::TypeOrder()) {
        std::vector<std::string> want;
        const OWJsonValue* row = ptypes->Find(type);
        if (const OWJsonValue* pub = row ? row->Find("public") : nullptr)
            for (const OWJsonValue& f : pub->Items()) want.push_back(f.Text());
        t.Ok(OWLens::PublicFieldsOf(type) == want, type + "'s public fields are presentation.json's");
    }
}

OW_CASES(lens_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("lens_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        if (Runs(Str(c, "kind")))
            out.emplace_back("lens_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// -- readings the fixture leaves open ---------------------------------------------------------------------------------

OW_TEST(lens_x_lens_reads_a_numbers_text) {
    auto bad = [](const char* json) {
        OWJsonValue v = Parse(json);
        std::optional<std::string> b = OWLens::CheckKeys(&v);
        return b ? *b : std::string();
    };
    t.Eq(bad(R"({"depth": 2})"), std::string(), "an integer");
    t.Eq(bad(R"({"depth": -0})"), std::string(), "-0 is an integer (0)");
    t.Eq(bad(R"({"depth": 2.0})"), std::string("depth"), "an integral float");
    t.Eq(bad(R"({"depth": 2e0})"), std::string("depth"), "an exponent");
    t.Eq(bad(R"({"budget": 7.5})"), std::string("budget"), "a fraction");
    t.Eq(bad(R"({"window": 9223372036854775807})"), std::string(), "the largest 64-bit window");
    t.Eq(bad(R"({"window": 9223372036854775808})"), std::string("window"), "past 64 bits: a platform limit");
    t.Eq(bad(R"({"from": -5, "to": -4})"), std::string(), "a range below zero");
    t.Eq(bad(R"({"types": [" Narrative/Message "]})"), std::string(), "a pair word trimmed and folded");
    t.Eq(bad(R"({"types": ["narrative / message"]})"), std::string("types"), "a pair word with inner spaces");
    t.Eq(bad(R"({"types": ["law/charter", "Construct/Domain"]})"), std::string(), "the reading aids' pairs");
    t.Eq(bad(R"({"types": [""]})"), std::string("types"), "an empty word");
    t.Eq(bad(R"({"undated": "Include"})"), std::string("undated"), "a word is not folded");
    t.Eq(bad(R"({"notes": []})"), std::string("notes"), "notes not an object");
    t.Eq(bad(R"({"depth": null, "show": null})"), std::string(), "null is absent");
    t.Eq(bad(R"(null)"), std::string(), "a null x_lens");
    t.Eq(bad(R"("depth")"), std::string("x_lens"), "a string x_lens");
    t.Ok(!OWLens::CheckKeys(nullptr), "no x_lens");
}

OW_TEST(lens_date_of_reads_the_table) {
    auto date = [](const char* json) {
        std::optional<int64_t> d = OWLens::DateOf(Parse(json));
        return d ? std::to_string(*d) : std::string("none");
    };
    t.Eq(date(R"({"type": "character", "birth_date": 1170})"), std::string("1170"), "a Character's birth_date");
    t.Eq(date(R"({"type": " Event ", "start_date": -3})"), std::string("-3"), "a folded type, below zero");
    t.Eq(date(R"({"type": "event", "start_date": 7.0})"), std::string("7"), "an integral float reads whole");
    t.Eq(date(R"({"type": "event", "start_date": 7.5})"), std::string("none"), "a fraction is no date");
    t.Eq(date(R"({"type": "event", "start_date": "7"})"), std::string("none"), "a string is no date");
    t.Eq(date(R"({"type": "event", "start_date": null})"), std::string("none"), "null");
    t.Eq(date(R"({"type": "object", "start_date": 7})"), std::string("none"), "a timeless type");
    t.Eq(date(R"({"type": "event", "start_date": 7, "known_of": true})"), std::string("7"),
         "it reads the body it is given");
}
