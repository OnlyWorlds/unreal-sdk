// OWCharter on the charter profile's conformance fixture (schema-dist profiles/charter.fixture.json, dist.115's copy, its
// sha256 held to MANIFEST.json): the pair-and-mark test, and the packet over the fixture's nine worlds, their readers
// and the cases that bring a world of their own. The profile's tables are held equal to the fixture's, the hub fields
// to the Construct's link fields; then readings the fixture leaves open.

#include "ow_test.h"
#include "sha256.h"

#include "OWCharter.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"

#include <map>
#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.115's MANIFEST.json records it.
const char* const DistSha256 = "3f1c3c4bd41b4332f7dfc0f7034b0a61d29dba18445aa99e1a482a69927e5ba7";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/charter.fixture.json"; }

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

// A case's world: a name in the fixture's worlds, or a world of its own.
const OWJsonValue& WorldOf(const OWJsonValue& c) {
    static const OWJsonValue none;
    const OWJsonValue* w = c.Find("world");
    if (w && w->IsObject()) return *w;
    const OWJsonValue* worlds = Fixture()->Find("worlds");
    const OWJsonValue* named = worlds && w && w->IsString() ? worlds->Find(w->Text()) : nullptr;
    return named ? *named : none;
}

// A fixture reader by name, from its world's readers.
OWJsonValue ReaderOf(const OWJsonValue& world, const std::string& name) {
    const OWJsonValue* readers = world.Find("readers");
    const OWJsonValue* r = readers ? readers->Find(name) : nullptr;
    return r ? *r : OWJsonValue::Null();
}

OWJsonValue DomainsJson(const std::vector<OWCharterDomain>& domains) {
    OWReadingAids aids;
    aids.domains = domains;
    return *aids.ToJson().Find("domains");
}

void Case(owtest::T& t, const OWJsonValue& c) {
    std::string kind = Str(c, "kind");
    if (kind == "element") {
        const OWJsonValue* el = c.Find("element");
        if (!el) return t.Fail("no element");
        Same(t, OWJsonValue::Bool(OWCharter::IsCharterElement(*el)), c.Find("is_charter_element"), "is_charter_element");
    } else if (kind == "charter") {
        const OWJsonValue& world = WorldOf(c);
        if (!world.IsObject()) return t.Fail("no world");
        OWJsonValue reader = ReaderOf(world, Str(c, "reader"));
        if (reader.IsNull()) return t.Fail("no reader " + Str(c, "reader"));
        OWReadingAids got = OWCharter::CharterOf(world, reader);
        t.Eq(got.error, std::string(), "no caller's mistake");
        const OWJsonValue* want = c.Find("want");
        Same(t, got.ToJson(), want, "the packet");
        Same(t, DomainsJson(OWCharter::DomainsOf(world, reader)), want ? want->Find("domains") : nullptr, "domainsOf");
    } else {
        t.Fail("a case of unknown kind " + kind);
    }
}

// The kinds this file runs: every kind of the fixture.
const char* const Run[] = {"charter", "element"};

bool Runs(const std::string& kind) {
    for (const char* k : Run)
        if (kind == k) return true;
    return false;
}

// A world of `elements` (a JSON list) with every reader the tests below use.
OWJsonValue WorldWith(std::string_view elements, std::string_view extra = "") {
    std::string text = "{\"id\": \"w\", \"elements\": " + std::string(elements) + std::string(extra) + "}";
    return Parse(text);
}

}  // namespace

OW_TEST(charter_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the charter fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.115's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/charter.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("charter"), "the charter profile's fixture");
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
    t.Eq(cases->Size(), size_t(31), "31 cases");
    t.Eq(names.size(), size_t(31), "each name its own");
    t.Eq(kinds["charter"], size_t(26), "kind charter");
    t.Eq(kinds["element"], size_t(5), "kind element");
    t.Eq(kinds.size(), size_t(2), "and no other kind");
    t.Eq(run, size_t(31), "every case runs here");
    const OWJsonValue* worlds = doc->Find("worlds");
    t.Eq(worlds ? worlds->Size() : size_t(0), size_t(9), "nine worlds");
}

OW_TEST(charter_tables_are_the_fixtures) {
    auto doc = Fixture();
    const OWJsonValue* pairs = doc->Find("pairs");
    t.Ok(pairs && pairs->Size() == OWCharter::Pairs().size(), "three pairs");
    std::vector<std::string> folded;
    if (pairs)
        for (size_t i = 0; i < pairs->Size() && i < OWCharter::Pairs().size(); ++i) {
            const OWJsonValue& p = pairs->Items()[i];
            const OWCharter::OWCharterPair& mine = OWCharter::Pairs()[i];
            t.Eq(Str(p, "type"), std::string(mine.type), "pair " + std::to_string(i) + "'s type");
            t.Eq(Str(p, "supertype"), std::string(mine.supertype), "pair " + std::to_string(i) + "'s supertype");
            t.Eq(Str(p, "aid"), std::string(mine.aid), "pair " + std::to_string(i) + "'s word");
            std::string f = Str(p, "type") + "/" + Str(p, "supertype");
            for (char& ch : f) ch = ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch;
            folded.push_back(f);
        }
    t.Ok(folded == OWLens::ReadingAidPairs(), "the pairs, folded, are the lens reader's");
    const OWJsonValue* mark = doc->Find("mark");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string(OWCharter::Mark), "the mark");
    t.Eq(std::string(OWCharter::Mark), std::string(OWLens::ReadingAidMark), "the lens reader's mark");
    t.Eq(Str(*doc, "rules_field"), std::string(OWCharter::RulesField), "the rules field");
    const OWJsonValue* max = doc->Find("skill_max");
    t.Eq(max ? max->Text() : std::string(), std::to_string(OWCharter::SkillMax), "the skill's cap");
    std::vector<std::pair<std::string, std::string>> hubs;
    if (const OWJsonValue* h = doc->Find("hub_fields"))
        for (const OWJsonValue& f : h->Items()) hubs.emplace_back(Str(f, "field"), Str(f, "type"));
    t.Eq(hubs.size(), size_t(20), "twenty hub fields");
    t.Ok(hubs == OWCharter::HubFields(), "the hub fields");
    std::vector<std::pair<std::string, std::string>> links;
    for (const OWLens::OWLensField& f : OWLens::FieldsOf("construct"))
        if (f.kind == OWLens::OWFieldKind::Single || f.kind == OWLens::OWFieldKind::Multi)
            links.emplace_back(f.name, f.target);
    t.Ok(!links.empty() && links == OWCharter::HubFields(), "the hub fields are the Construct's link fields");
}

OW_CASES(charter_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("charter_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        if (Runs(Str(c, "kind")))
            out.emplace_back("charter_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

// -- readings the fixture leaves open ---------------------------------------------------------------------------------

OW_TEST(charter_first_line_trims_and_cuts_at_cr_or_lf) {
    t.Eq(OWCharter::FirstLine("One.\nTwo."), std::string("One."), "LF");
    t.Eq(OWCharter::FirstLine("One.\r\nTwo."), std::string("One."), "CRLF");
    t.Eq(OWCharter::FirstLine("One.\rTwo."), std::string("One."), "a lone CR");
    t.Eq(OWCharter::FirstLine("  \n\n  One. \nTwo."), std::string("One."), "trimmed first, so a leading break is no cut");
    t.Eq(OWCharter::FirstLine("\xC2\xA0One.\xC2\xA0"), std::string("One."), "White_Space, a no-break space among it");
    t.Eq(OWCharter::FirstLine("\x1F" "One."), std::string("\x1F" "One."), "U+001F is not White_Space");
    t.Eq(OWCharter::FirstLine(""), std::string(), "none");
    t.Eq(OWCharter::FirstLine("One.\xE2\x80\xA8Two."), std::string("One.\xE2\x80\xA8Two."),
         "only CR and LF cut: U+2028 is kept inside");
}

OW_TEST(charter_reading_aid_of_reads_the_pair_alone) {
    auto of = [](const char* json) {
        std::optional<std::string> w = OWCharter::ReadingAidOf(Parse(json));
        return w ? *w : std::string("(none)");
    };
    t.Eq(of(R"({"type": "law", "supertype": "Charter"})"), std::string("charter"), "an unmarked Charter Law");
    t.Eq(of(R"({"type": " Construct ", "supertype": " GUIDE ", "x_charter_of": "w"})"), std::string("guide"),
         "trimmed and folded, the mark ignored");
    t.Eq(of(R"({"type": "construct", "supertype": "domain"})"), std::string("domain"), "a Domain");
    t.Eq(of(R"({"type": "construct", "supertype": "Domains"})"), std::string("(none)"), "the whole word");
    t.Eq(of(R"({"type": "law", "supertype": "Guide"})"), std::string("(none)"), "the word on the wrong type");
    t.Eq(of(R"({"type": "law", "supertype": null})"), std::string("(none)"), "no supertype");
    t.Eq(of(R"({"type": 3, "supertype": "Charter"})"), std::string("(none)"), "a type that is not text");
    t.Eq(of(R"([1, 2])"), std::string("(none)"), "not a body");
    t.Ok(!OWCharter::IsCharterElement(Parse(R"({"type": "law", "supertype": "Charter"})")),
         "and the unmarked Charter Law is no reading aid");
}

OW_TEST(charter_a_callers_mistake_reads_nothing) {
    OWJsonValue owner = Parse(R"({"rung": "owner"})");
    OWReadingAids a = OWCharter::CharterOf(Parse(R"({"id": "w"})"), owner);
    t.Ok(!a.error.empty(), "a world with no elements list: " + a.error);
    OWReadingAids b = OWCharter::CharterOf(WorldWith("[]"), Parse(R"({"rung": "king"})"));
    t.Ok(!b.error.empty(), "a rung outside the four: " + b.error);
    t.Ok(!b.charter && !b.guide && b.domains.empty(), "nothing read");
    t.Ok(OWCharter::DomainsOf(Parse("[]"), owner).empty(), "domainsOf: empty");
}

namespace {
const char* const Rules = R"([
  {"id": "c1", "type": "law", "name": "Rules", "supertype": "Charter", "x_charter_of": "w",
   "created_at": "2026-10-01T12:00:00Z", "x_checks_rules": [{"rule": "r"}],
   "description": "Read ow://object/0A0A0A0A-0000-7000-8000-000000000001 and 0a0a0a0a-0000-7000-8000-000000000002."},
  {"id": "0a0a0a0a-0000-7000-8000-000000000001", "type": "object", "name": "Named in upper case"},
  {"id": "0a0a0a0a-0000-7000-8000-000000000002", "type": "object", "name": "Proposed",
   "x_review_proposed": {"by": "seat"}}
])";
}  // namespace

OW_TEST(charter_a_guest_reads_the_charter_whole_even_as_a_stub) {
    // keel reads the Charter's row whole (elements/charter.py, packet; D152): a guest that holds it only as a known-of
    // stub still reads its name, its rules and its skill.
    OWJsonValue world = WorldWith(Rules);
    OWReadingAids a = OWCharter::CharterOf(
        world, Parse(R"({"rung": "guest", "whole": ["0a0a0a0a-0000-7000-8000-000000000001"], "known_of": ["c1"]})"));
    t.Ok(a.charter.has_value(), "the Charter is there");
    if (!a.charter) return;
    t.Eq(a.charter->name, std::string("Rules"), "its name");
    t.Eq(Compact(a.charter->rules), std::string(R"([{"rule": "r"}])"), "its rules as stored");
    t.Eq(a.charter->skill.size(), size_t(1), "its skill");
}

OW_TEST(charter_skill_reads_the_texts_id_in_any_case) {
    // The text names the Object in upper case: taken in lowercase, it is the Object held in lowercase, its own id.
    OWReadingAids a = OWCharter::CharterOf(WorldWith(Rules), Parse(R"({"rung": "owner"})"));
    t.Ok(a.charter.has_value(), "the Charter is there");
    if (!a.charter) return;
    t.Ok(!a.charter->skill.empty() && a.charter->skill[0] == "0a0a0a0a-0000-7000-8000-000000000001",
         "the Object named in upper case, its own id");
}

OW_TEST(charter_skill_named_in_lowercase_is_not_an_object_held_in_upper_case) {
    // The address profile's rule for the same id, at the Charter's door: the id found in the text is taken in
    // lowercase, the Object's own id is read as written, so an Object held in upper case is not the one it names.
    OWReadingAids a = OWCharter::CharterOf(WorldWith(R"([
      {"id": "c1", "type": "law", "name": "Rules", "supertype": "Charter", "x_charter_of": "w",
       "description": "Read ow://object/0b0b0b0b-0000-7000-8000-0000000000bf."},
      {"id": "0B0B0B0B-0000-7000-8000-0000000000BF", "type": "object", "name": "Upper"}
    ])"), Parse(R"({"rung": "owner"})"));
    t.Ok(a.charter.has_value(), "the Charter is there");
    if (!a.charter) return;
    t.Eq(a.charter->skill.size(), size_t(0), "not kept");
}

OW_TEST(charter_skill_named_in_upper_case_is_the_object_held_in_lowercase) {
    // An id found in the text is read in either case and taken in lowercase: the Object held in lowercase, its own id.
    OWReadingAids a = OWCharter::CharterOf(WorldWith(R"([
      {"id": "c1", "type": "law", "name": "Rules", "supertype": "Charter", "x_charter_of": "w",
       "description": "Read 0B0B0B0B-0000-7000-8000-0000000000BE."},
      {"id": "0b0b0b0b-0000-7000-8000-0000000000be", "type": "object", "name": "Lower"}
    ])"), Parse(R"({"rung": "owner"})"));
    t.Ok(a.charter.has_value(), "the Charter is there");
    if (!a.charter) return;
    t.Ok(a.charter->skill.size() == 1 && a.charter->skill[0] == "0b0b0b0b-0000-7000-8000-0000000000be",
         "kept, as the Object's own id");
}

OW_TEST(charter_skill_leaves_a_proposed_object_out) {
    // Read through the lens view, where a proposed element is in no reader's view. keel's scoped serves one to every
    // rung but the public, and charter_skill would keep it: asked of the profile's owner.
    OWReadingAids a = OWCharter::CharterOf(WorldWith(Rules), Parse(R"({"rung": "owner"})"));
    t.Ok(a.charter.has_value(), "the Charter is there");
    if (!a.charter) return;
    t.Eq(a.charter->skill.size(), size_t(1), "the proposed Object is not kept");
}

OW_TEST(charter_an_owner_reading_as_the_public_on_a_closed_world_reads_nothing) {
    OWJsonValue world = WorldWith(Rules);
    OWReadingAids a = OWCharter::CharterOf(world, Parse(R"({"rung": "owner", "as": {"rung": "public"}})"));
    t.Eq(a.error, std::string(), "no mistake");
    t.Ok(!a.charter && !a.guide && a.domains.empty(), "nothing: the reading is the public's");
    OWJsonValue open = WorldWith(Rules, R"(, "open": true)");
    t.Ok(OWCharter::CharterOf(open, Parse(R"({"rung": "public"})")).charter.has_value(), "open: the Charter");
}

OW_TEST(charter_the_guides_order_is_as_served) {
    // The Guide names a Domain the contributor cannot see, a Domain twice, an unmarked Domain and a lore element: only
    // the marked Domains the reader sees, each once, then the rest by folded name.
    const char* els = R"([
      {"id": "g", "type": "construct", "name": "Guide", "supertype": "Guide", "x_charter_of": "w",
       "constructs": ["d3", "dh", "d3", "lore", "d1"]},
      {"id": "d1", "type": "construct", "name": "b", "supertype": "Domain", "x_charter_of": "w"},
      {"id": "d2", "type": "construct", "name": "A", "supertype": "Domain", "x_charter_of": "w",
       "description": "\n  First.  \r\nSecond.", "founder": "ch", "characters": ["ch", "ch2"], "constructs": ["d2", "d1"]},
      {"id": "d3", "type": "construct", "name": "c", "supertype": "Domain", "x_charter_of": "w"},
      {"id": "dh", "type": "construct", "name": "hidden", "supertype": "Domain", "x_charter_of": "w"},
      {"id": "lore", "type": "construct", "name": "lore", "supertype": "Domain"},
      {"id": "ch", "type": "character", "name": "Ch"},
      {"id": "ch2", "type": "character", "name": "Ch2"}
    ])";
    OWJsonValue world = WorldWith(els, R"(, "hidden": ["dh", "ch2"])");
    OWReadingAids a = OWCharter::CharterOf(world, Parse(R"({"rung": "contributor"})"));
    std::string order;
    for (const OWCharterDomain& d : a.domains) order += d.id + " ";
    t.Eq(order, std::string("d3 d1 d2 "), "the Guide's order, then the rest");
    if (a.domains.size() != 3) return;
    const OWCharterDomain& d2 = a.domains[2];
    t.Eq(d2.sentence, std::string("First."), "the sentence");
    std::string hubs;
    for (const OWCharterHub& h : d2.hubs) hubs += h.type + ":" + h.id + " ";
    t.Eq(hubs, std::string("character:ch construct:d1 "), "the hubs: each once, a hidden one not served, never itself");
}
