// OWLens on the lens profile's conformance fixture (schema-dist profiles/lens.fixture.json, dist.103's copy, its sha256
// held to MANIFEST.json): the pair and the template, the legal values of x_lens, where a seat's choice lives, every
// resolution over the fixture's nine worlds and their readers, the invariants, the graph kind's 49 cases (each its
// own world, through OWGraph::WorldGraph, the want compared whole) and the degree kind's 5 (OWGraph::DegreeOf and
// OutDegreeOf over each case's graph, every node a key in the graph's node order). The profile's tables are held
// equal to the fixture's, the field table to schema.json and the stub's public fields to presentation.json; then
// readings the fixture leaves open. The graph's own tests are in test_graph.cpp.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWGraph.h"
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

// The fixture's sha256 as dist.103's MANIFEST.json records it.
const char* const DistSha256 = "2950d16b7a660428a78fd0c383a7608135c94f86c5c4b46402a4b9744cf3d07c";

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

const OWJsonValue& World(const std::string& name) {
    static const OWJsonValue none;
    const OWJsonValue* worlds = Fixture()->Find("worlds");
    const OWJsonValue* w = worlds ? worlds->Find(name) : nullptr;
    return w ? *w : none;
}

// A fixture reader by name: its rung, its character, a guest's lists; `as` (a reader's name in the fixture) is the
// owner reading as that reader.
OWJsonValue ReaderOf(const OWJsonValue& world, const std::string& name) {
    const OWJsonValue* readers = world.Find("readers");
    const OWJsonValue* r = readers ? readers->Find(name) : nullptr;
    if (!r) return OWJsonValue::Null();
    OWJsonValue out = *r;
    if (const OWJsonValue* as = r->Find("as"); as && as->IsString()) out.Set("as", ReaderOf(world, as->Text()));
    return out;
}

OWLensOptions OptionsOf(const OWJsonValue& c) {
    OWLensOptions o;
    if (const OWJsonValue* a = c.Find("as_of"); a && a->IsNumber()) o.as_of = int64_t(std::llround(a->AsDouble()));
    if (const OWJsonValue* k = c.Find("clock"); k && k->IsString()) o.clock = k->Text();
    return o;
}

OWLensReading ResolveCase(const OWJsonValue& world, const OWJsonValue& c, const OWJsonValue& reader) {
    static const OWJsonValue none;
    const OWJsonValue* lens = c.Find("lens");
    return OWLens::Resolve(world, lens ? *lens : none, reader, OptionsOf(c));
}

std::string Names(const OWJsonValue& world, const OWJsonValue& ids) {
    std::map<std::string, std::string> by;
    if (const OWJsonValue* els = world.Find("elements"))
        for (const OWJsonValue& e : els->Items()) by[Str(e, "id")] = Str(e, "name");
    std::string out;
    for (const OWJsonValue& i : ids.Items()) {
        if (!out.empty()) out += ", ";
        auto it = by.find(i.Text());
        out += it != by.end() ? it->second : i.Text();
    }
    return out;
}

// The reading's keys the case names, compared as JSON values (mail: only its keys the case names).
void Check(owtest::T& t, const OWLensReading& got, const OWJsonValue& c, const OWJsonValue& world) {
    t.Eq(got.error, std::string(), "no caller's mistake");
    t.Ok(!got.unreadable, "readable");
    OWJsonValue all = got.ToJson();
    if (const OWJsonValue* want = c.Find("kept")) {
        OWJsonValue kept = *all.Find("kept");
        t.Ok(Equal(kept, *want), "kept [" + Names(world, kept) + "], the case says [" + Names(world, *want) + "]");
    }
    for (const char* k : {"cut", "rows", "notes_ignored"})
        if (const OWJsonValue* want = c.Find(k)) Same(t, *all.Find(k), want, k);
    if (const OWJsonValue* want = c.Find("mail")) {
        const OWJsonValue* mail = all.Find("mail");
        if (want->IsNull() || !mail || mail->IsNull()) {
            Same(t, mail ? *mail : OWJsonValue::Null(), want, "mail");
        } else {
            for (size_t i = 0; i < want->Size(); ++i) {
                const OWJsonValue* part = mail->Find(want->Keys()[i]);
                Same(t, part ? *part : OWJsonValue::Null(), &want->Items()[i], "mail." + want->Keys()[i]);
            }
        }
    }
}

std::string Stringify(const OWJsonValue& v) { return OWJson::Stringify(v, true); }

// The invariants: subset, same_without, unchanged.
void Holds(owtest::T& t, const OWJsonValue& c) {
    const OWJsonValue& world = World(Str(c, "world"));
    std::string claim = Str(c, "claim");
    const OWJsonValue* readers = c.Find("readers");
    if (!readers || !readers->IsArray() || readers->Size() == 0) return t.Fail("no readers");
    if (claim == "subset") {
        OWLensReading owner = ResolveCase(world, c, ReaderOf(world, "owner"));
        std::set<std::string> owner_kept(owner.kept.begin(), owner.kept.end());
        std::vector<std::string> secrets;
        for (const OWJsonValue& s : c.Find("secrets")->Items()) secrets.push_back(s.Text());
        for (const OWJsonValue& e : world.Find("elements")->Items())
            for (const OWJsonValue& s : c.Find("secrets")->Items())
                if (Str(e, "id") == s.Text()) secrets.push_back(Str(e, "name"));
        for (const OWJsonValue& r : readers->Items()) {
            OWJsonValue reader = ReaderOf(world, r.Text());
            OWLensReading got = ResolveCase(world, c, reader);
            t.Eq(got.error, std::string(), r.Text() + ": no caller's mistake");
            for (const std::string& i : got.kept)
                t.Ok(owner_kept.count(i) > 0, r.Text() + " keeps " + i + ", which the owner does not");
            if (Str(reader, "rung") == "owner") continue;
            std::string text = Stringify(got.ToJson());
            for (const std::string& s : secrets)
                t.Ok(text.find(s) == std::string::npos, r.Text() + "'s reading names the secret " + s);
            t.Ok(owner.cut.empty() || owner.cut != got.cut, r.Text() + "'s counts are the owner's");
        }
    } else if (claim == "same_without") {
        std::set<std::string> without;
        for (const OWJsonValue& s : c.Find("without")->Items()) without.insert(s.Text());
        OWJsonValue less = world;
        OWJsonValue kept = OWJsonValue::Array();
        for (const OWJsonValue& e : world.Find("elements")->Items())
            if (!without.count(Str(e, "id"))) kept.Push(e);
        less.Set("elements", kept);
        for (const OWJsonValue& r : readers->Items()) {
            std::string a = Stringify(ResolveCase(world, c, ReaderOf(world, r.Text())).ToJson());
            std::string b = Stringify(ResolveCase(less, c, ReaderOf(less, r.Text())).ToJson());
            t.Eq(b, a, r.Text() + " reads the same without the ids");
        }
    } else if (claim == "unchanged") {
        std::string before = Stringify(world);
        for (const OWJsonValue& r : readers->Items()) ResolveCase(world, c, ReaderOf(world, r.Text()));
        t.Eq(Stringify(world), before, "no reading moved the world");
    } else {
        t.Fail("an invariant of unknown claim " + claim);
    }
}

// A graph case's options: the lens (a body or an id), the reading's as_of and clock, and the four flags.
OWGraphOptions GraphOptionsOf(const OWJsonValue& c) {
    OWGraphOptions o;
    const OWJsonValue* opt = c.Find("options");
    if (!opt || !opt->IsObject()) return o;
    if (const OWJsonValue* lens = opt->Find("lens")) o.lens = *lens;
    if (const OWJsonValue* a = opt->Find("as_of"); a && a->IsNumber()) o.as_of = int64_t(std::llround(a->AsDouble()));
    if (const OWJsonValue* k = opt->Find("clock"); k && k->IsString()) o.clock = k->Text();
    for (auto [key, flag] : {std::pair<const char*, bool*>{"rooms", &o.rooms}, {"hidden", &o.hidden},
                             {"proposed", &o.proposed}, {"map_space", &o.map_space}})
        if (const OWJsonValue* v = opt->Find(key); v && v->IsBool()) *flag = v->AsBool();
    return o;
}

// A degree case's graph, in WorldGraph's shape with the keys the counts read: the nodes' ids and types, the edges.
OWWorldGraph GraphOf(const OWJsonValue& graph) {
    OWWorldGraph g;
    if (const OWJsonValue* nodes = graph.Find("nodes"))
        for (const OWJsonValue& n : nodes->Items()) {
            OWGraphNode node;
            node.id = Str(n, "id");
            node.type = Str(n, "type");
            g.nodes.push_back(std::move(node));
        }
    if (const OWJsonValue* edges = graph.Find("edges"))
        for (const OWJsonValue& e : edges->Items()) {
            const OWJsonValue* index = e.Find("index");
            g.edges.push_back({Str(e, "source"), Str(e, "field"), Str(e, "target"),
                               index && index->IsNumber() ? int64_t(index->AsDouble()) : 0});
        }
    return g;
}

// One case, by its kind.
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
    } else if (kind == "resolve") {
        const OWJsonValue& world = World(Str(c, "world"));
        OWJsonValue reader = ReaderOf(world, Str(c, "reader"));
        if (reader.IsNull()) return t.Fail("no reader " + Str(c, "reader"));
        Check(t, ResolveCase(world, c, reader), c, world);
    } else if (kind == "invariant") {
        Holds(t, c);
    } else if (kind == "graph") {
        OWWorldGraph got = OWGraph::WorldGraph(at("world"), at("reader"), GraphOptionsOf(c));
        t.Eq(got.error, std::string(), "no caller's mistake");
        Same(t, got.ToJson(), c.Find("want"), "the graph");
    } else if (kind == "degree") {
        OWWorldGraph g = GraphOf(at("graph"));
        const OWJsonValue& want = at("want");
        // Every node a key in the graph's node order: the keys compared in order, beside the values.
        auto counted = [&](const std::vector<std::pair<std::string, int64_t>>& got, const char* key) {
            OWJsonValue out = OWJsonValue::Object();
            for (const auto& [id, n] : got) out.Set(id, OWJsonValue::Number(n));
            const OWJsonValue* w = want.Find(key);
            Same(t, out, w, key);
            t.Ok(w && w->IsObject() && out.Keys() == w->Keys(), std::string(key) + ": the keys in the graph's node order");
        };
        counted(OWGraph::DegreeOf(g), "degree");
        counted(OWGraph::OutDegreeOf(g), "out_degree");
    } else {
        t.Fail("a case of unknown kind " + kind);
    }
}

// The kinds this file runs: every kind of the fixture.
const char* const Run[] = {"shape", "keys", "uses", "resolve", "invariant", "graph", "degree"};

bool Runs(const std::string& kind) {
    for (const char* k : Run)
        if (kind == k) return true;
    return false;
}

}  // namespace

OW_TEST(lens_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the lens fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.103's");
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
    t.Eq(cases->Size(), size_t(196), "196 cases");
    t.Eq(names.size(), size_t(196), "each name its own");
    const std::pair<const char*, size_t> want[] = {{"shape", 6},   {"keys", 25},      {"uses", 5},
                                                   {"resolve", 102}, {"invariant", 4}, {"graph", 49},
                                                   {"degree", 5}};
    size_t sum = 0;
    for (const auto& [kind, n] : want) {
        t.Eq(kinds[kind], n, std::string("kind ") + kind);
        sum += n;
    }
    t.Eq(sum, size_t(196), "the seven kinds are every case");
    t.Eq(kinds.size(), size_t(7), "and no other kind");
    t.Eq(run, size_t(196), "every case runs here, the 49 graph and the 5 degree cases among them");
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

OW_TEST(lens_x_lens_reads_a_whole_float_whole) {
    // The profile's owner, 2026-10-30 (serial 83's text-only refusal retired; fixture cases with serial 93): a whole
    // float reads whole in every reader, so depth 2.0 is 2; only a fraction stays refused.
    auto bad = [](const char* json) {
        OWJsonValue v = Parse(json);
        std::optional<std::string> b = OWLens::CheckKeys(&v);
        return b ? *b : std::string();
    };
    t.Eq(bad(R"({"depth": 2})"), std::string(), "an integer");
    t.Eq(bad(R"({"depth": -0})"), std::string(), "-0 is an integer (0)");
    t.Eq(bad(R"({"depth": 2.0})"), std::string(), "a whole float reads whole");
    t.Eq(bad(R"({"depth": 2e0})"), std::string(), "an exponent that is whole");
    t.Eq(bad(R"({"depth": 1e1})"), std::string("depth"), "a whole 10 is past depth's table");
    t.Eq(bad(R"({"budget": 7.0, "window": 0.0, "changed_within": 3.0})"), std::string(), "budget, window, changed_within");
    t.Eq(bad(R"({"from": -5.0, "to": 4.0})"), std::string(), "from and to");
    t.Eq(bad(R"({"budget": 7.5})"), std::string("budget"), "a fraction");
    t.Eq(bad(R"({"depth": 1.5})"), std::string("depth"), "a fraction of depth");
    t.Eq(bad(R"({"window": 9223372036854775807})"), std::string(), "the largest 64-bit window");
    t.Eq(bad(R"({"window": 9223372036854775808})"), std::string("window"), "past 64 bits: a platform limit");
    OWJsonValue world = Parse(R"({"elements": [{"id": "a", "type": "character", "name": "A", "friends": ["b"]},
        {"id": "b", "type": "character", "name": "B"}]})");
    OWLensReading r = OWLens::Resolve(world, Parse(R"({"id": "L", "type": "construct", "supertype": "Lens",
        "characters": ["a"], "x_lens": {"depth": 0.0, "budget": 5.0}})"), Parse(R"({"rung": "owner"})"));
    t.Ok(!r.unreadable && r.kept == std::vector<std::string>{"a"}, "a lens of depth 0.0 reads as depth 0");
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

OW_TEST(lens_a_callers_mistake_is_named) {
    OWJsonValue world = Parse(R"({"elements": []})");
    OWJsonValue lens = Parse(R"({"type": "construct", "supertype": "Lens"})");
    t.Ok(!OWLens::Resolve(Parse(R"({"no": 1})"), lens, Parse(R"({"rung": "owner"})")).error.empty(), "no elements");
    t.Ok(!OWLens::Resolve(world, lens, Parse(R"({"rung": "king"})")).error.empty(), "a rung outside the four");
    t.Ok(!OWLens::Resolve(world, lens, Parse(R"({"rung": "owner", "as": {"rung": "x"}})")).error.empty(),
         "an as outside the four");
    OWLensOptions bad_clock;
    bad_clock.clock = "yesterday";
    t.Ok(!OWLens::Resolve(world, lens, Parse(R"({"rung": "owner"})"), bad_clock).error.empty(), "a clock");
    t.Ok(!OWLens::Resolve(world, Parse("3"), Parse(R"({"rung": "owner"})")).error.empty(), "a lens neither body nor id");
    t.Eq(OWLens::Resolve(world, lens, Parse(R"({"rung": "owner"})")).error, std::string(), "a good call");
}

OW_TEST(lens_an_unreadable_lens_names_its_key_and_resolves_nothing) {
    const OWJsonValue& world = World("vale");
    OWJsonValue lens = Parse(R"({"id": "L", "type": "construct", "supertype": "Lens",
        "characters": ["019a7f00-0000-7000-8000-000000000001"], "x_lens": {"depth": 1.5}})");
    OWLensReading r = OWLens::Resolve(world, lens, ReaderOf(world, "owner"));
    t.Ok(r.unreadable && *r.unreadable == "depth", "unreadable names depth");
    t.Ok(r.kept.empty() && r.cut.empty() && r.rows.empty(), "and nothing is resolved");
}

OW_TEST(lens_first_line_cuts_at_cr_or_lf_and_writes_no_description_it_was_not_given) {
    // The profile's owner, 2026-10-30 (fixture cases with serial 93): first_line cuts at the first CR or LF, and a row
    // whose body has no description (or one that is not text) carries none, as the npm and Python packages read it.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "a", "type": "character", "name": "A", "description": "One\r\nTwo"},
        {"id": "b", "type": "character", "name": "B", "description": "Up\rDown"},
        {"id": "c", "type": "character", "name": "C", "description": "Left\nRight"},
        {"id": "d", "type": "character", "name": "D"},
        {"id": "e", "type": "character", "name": "E", "description": null}]})");
    OWJsonValue lens = Parse(R"({"id": "L", "type": "construct", "supertype": "Lens",
        "characters": ["a", "b", "c", "d", "e"], "x_lens": {"depth": 0, "show": "first_line"}})");
    OWLensReading r = OWLens::Resolve(world, lens, Parse(R"({"rung": "owner"})"));
    OWJsonValue want = Parse(R"([
        {"id": "a", "type": "character", "name": "A", "description": "One"},
        {"id": "b", "type": "character", "name": "B", "description": "Up"},
        {"id": "c", "type": "character", "name": "C", "description": "Left"},
        {"id": "d", "type": "character", "name": "D"},
        {"id": "e", "type": "character", "name": "E"}])");
    Same(t, *r.ToJson().Find("rows"), &want, "the rows");
}

OW_TEST(lens_an_element_with_no_updated_at_sorts_last) {
    // The profile's owner, 2026-10-30 (fixture cases with serial 93): under rule 5 an element with no updated_at sorts
    // after every element of its type that has one, never as now; two without one by id. The npm and Python packages'
    // reading.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "c0", "type": "character", "name": "Anchor", "friends": ["c1", "c2", "c3", "c4"]},
        {"id": "c1", "type": "character", "name": "Old", "updated_at": "2026-01-01T00:00:00Z"},
        {"id": "c4", "type": "character", "name": "None, by id after c2"},
        {"id": "c2", "type": "character", "name": "None"},
        {"id": "c3", "type": "character", "name": "New", "updated_at": "2026-06-01T00:00:00Z"}]})");
    OWJsonValue lens = Parse(R"({"id": "L", "type": "construct", "supertype": "Lens", "characters": ["c0"]})");
    OWLensOptions o;
    o.clock = "2026-10-30T00:00:00Z";
    OWLensReading r = OWLens::Resolve(world, lens, Parse(R"({"rung": "owner"})"), o);
    t.Ok(r.kept == std::vector<std::string>{"c0", "c3", "c1", "c2", "c4"},
         "kept: the anchor, the newest, the older, then the two with no updated_at by id");
    lens = Parse(R"({"id": "L", "type": "construct", "supertype": "Lens", "characters": ["c0"],
        "x_lens": {"budget": 3}})");
    r = OWLens::Resolve(world, lens, Parse(R"({"rung": "owner"})"), o);
    t.Ok(r.kept == std::vector<std::string>{"c0", "c3", "c1"}, "a budget cuts the undated-by-change first");
}

OW_TEST(lens_lenses_in_use_reads_each_id_once_and_drops_the_empty_one) {
    // The profile's owner, 2026-10-30 (fixture cases with serial 93): the choice's strings each once, in the order
    // given, "" dropped, as the npm and Python packages read it; the membership still wins whole.
    auto uses = [](const char* membership, const char* x_lens_uses) {
        OWJsonValue m = Parse(membership), x = Parse(x_lens_uses);
        return OWLens::LensesInUse(&m, &x);
    };
    using Ids = std::vector<std::string>;
    t.Ok(uses("null", R"(["a", "", "b", "a", 7, "b"])") == Ids{"a", "b"}, "x_lens_uses: each once, no \"\", no number");
    t.Ok(uses(R"(["x", "x", ""])", R"(["a"])") == Ids{"x"}, "the membership: each once, and it wins");
    t.Ok(uses(R"([""])", R"(["a"])").empty(), "a membership of only \"\" is a choice of nothing");
    t.Ok(uses("null", R"(["b", "a", "b"])") == Ids{"b", "a"}, "the order given, the first place kept");
}

OW_TEST(lens_a_known_of_stub_is_shaped_as_keel_serves_it) {
    // Ruling 7 (keel's known_of, elements/visibility.py): a stub keeps the base and its type's public fields, every
    // other field at its unset value, and no key that is not a field: `world` is none of keel's field kinds, so it
    // goes with the extensions rather than reading "".
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "a", "type": "character", "name": "A", "friends": ["b"]},
        {"id": "b", "type": "character", "name": "B", "world": "w1", "physicality": "tall", "background": "a long story",
         "x_note": "kept back"}]})");
    OWLensReading r = OWLens::Resolve(world, Parse(R"({"id": "L", "type": "construct", "supertype": "Lens",
        "characters": ["a"]})"), Parse(R"({"rung": "guest", "whole": ["a"], "known_of": ["b"]})"));
    const OWJsonValue* stub = nullptr;
    for (const OWJsonValue& row : r.rows)
        if (const OWJsonValue* id = row.Find("id"); id && id->IsString() && id->Text() == "b") stub = &row;
    t.Ok(stub != nullptr, "the stub is kept");
    if (!stub) return;
    const OWJsonValue* known = stub->Find("known_of");
    t.Ok(known && known->IsBool() && known->AsBool(), "known_of: true");
    t.Ok(stub->Find("world") == nullptr, "no world key");
    t.Ok(stub->Find("x_note") == nullptr, "no extension key");
    const OWJsonValue* phys = stub->Find("physicality");
    t.Ok(phys && phys->IsString() && phys->Text() == "tall", "a public field is kept (a Character's physicality)");
    const OWJsonValue* background = stub->Find("background");
    t.Ok(background && background->IsString() && background->Text().empty(), "a field outside the keeps reads unset");
}

OW_TEST(lens_an_empty_single_link_is_served_as_stored_and_never_marked_trimmed) {
    // A single link stored as "" is served as stored to every reader below the owner: "", never null, and never named
    // in x_served_trimmed, since no id was left out (keel's redact strips only an id the reader cannot see). A Message
    // whose narrator is "" claims no hidden sender.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "o1", "type": "object", "name": "Cup", "language": ""},
        {"id": "c1", "type": "character", "name": "Ada"},
        {"id": "m1", "type": "narrative", "supertype": "Message", "name": "Hi", "narrator": "", "characters": ["c1"]},
        {"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "objects": ["o1"], "narratives": ["m1"],
         "x_lens": {"depth": 0}}]})");
    for (const char* reader : {R"({"rung": "contributor"})", R"({"rung": "guest", "whole": ["o1", "c1", "m1", "L"]})",
                               R"({"rung": "public"})"}) {
        OWLensReading r = OWLens::Resolve(world, OWJsonValue::String("L"), Parse(reader));
        for (const auto& [id, field] : {std::pair<std::string, std::string>{"m1", "narrator"}, {"o1", "language"}}) {
            const OWJsonValue* row = nullptr;
            for (const OWJsonValue& each : r.rows)
                if (const OWJsonValue* rid = each.Find("id"); rid && rid->IsString() && rid->Text() == id) row = &each;
            std::string what = std::string(reader) + ": " + id;
            t.Ok(row != nullptr, what + " is kept");
            if (!row) continue;
            const OWJsonValue* v = row->Find(field);
            t.Ok(v && v->IsString() && v->Text().empty(), what + "." + field + " served as \"\"");
            t.Ok(row->Find("x_served_trimmed") == nullptr, what + " carries no trimmed mark");
        }
    }
}

OW_TEST(lens_a_mail_tie_keeps_the_lists_order) {
    // Ruling 11, as the profile's owner reversed it (2026-10-30): two messages with one created_at keep the order of
    // the list they came in, as the message profile rules, never by id. A tie listed in reverse id order stays as listed.
    auto unread = [&](const char* first, const char* second) {
        std::string letters;
        for (const char* id : {first, second})
            letters += std::string(R"(, {"id": ")") + id + R"(", "type": "narrative", "name": ")" + id +
                       R"(", "supertype": "Message", "narrator": "a", "characters": ["me"],
                       "created_at": "2026-10-20T12:00:00Z"})";
        OWJsonValue world = Parse((std::string(R"({"elements": [{"id": "me", "type": "character", "name": "Me"},
            {"id": "a", "type": "character", "name": "A"})") + letters + "]}").c_str());
        OWJsonValue lens = Parse(R"({"id": "L", "type": "construct", "supertype": "Lens",
            "x_lens": {"types": ["narrative/message"], "mail": "all"}})");
        OWLensOptions o;
        o.clock = "2026-10-28T00:00:00Z";
        OWLensReading r = OWLens::Resolve(world, lens, Parse(R"({"rung": "owner", "character": "me"})"), o);
        return r.mail ? r.mail->unread : std::vector<std::string>{};
    };
    using Ids = std::vector<std::string>;
    t.Ok(unread("m2", "m1") == Ids{"m2", "m1"}, "m2 before m1, as listed");
    t.Ok(unread("m1", "m2") == Ids{"m1", "m2"}, "listed the other way, read the other way");
}

namespace {
using Counts = std::vector<std::pair<std::string, int64_t>>;
using Ids = std::vector<std::string>;

OWLensReading ReadAs(const OWJsonValue& world, const OWJsonValue& lens, const char* reader) {
    return OWLens::Resolve(world, lens, Parse(reader));
}

std::string RowsOf(const OWLensReading& r) {
    OWJsonValue rows = OWJsonValue::Array();
    for (const OWJsonValue& row : r.rows) rows.Push(row);
    return Compact(rows);
}
}  // namespace

OW_TEST(lens_a_lens_whose_anchor_fields_hold_only_empty_strings_names_no_anchor_and_is_a_filter) {
    // An empty string is no id, so a lens whose anchor fields hold only empty strings names no anchor and is a filter,
    // as rule 7 reads a lens with empty lists. One real id beside them anchors it, and a served lens whose
    // x_served_trimmed names an anchor field stays anchored. Kills: [""] read as naming an anchor.
    OWJsonValue world = Parse(R"({"elements": [{"id": "e03", "type": "law", "name": "Law"},
        {"id": "a", "type": "character", "name": "Ada"}]})");
    auto lens = [](const char* anchors) {
        return Parse((std::string(R"({"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "x_lens": {}, )") +
                      anchors + "}").c_str());
    };
    t.Ok(ReadAs(world, lens(R"("narratives": [""])"), R"({"rung": "owner"})").kept == Ids{"a", "e03"}, "a filter");
    t.Ok(ReadAs(world, lens(R"("characters": ["", ""], "laws": [""])"), R"({"rung": "public"})").kept == Ids{"a", "e03"},
         "a filter, for any reader");
    t.Ok(ReadAs(world, lens(R"("characters": ["", "a"])"), R"({"rung": "owner"})").kept == Ids{"a"}, "one id anchors it");
    t.Ok(ReadAs(world, lens(R"("narratives": [""], "x_served_trimmed": ["narratives"])"), R"({"rung": "owner"})").kept.empty(),
         "an anchor field the server trimmed still names an anchor");
}

OW_TEST(lens_an_owners_full_row_of_a_hidden_element_carries_hidden_true_as_served) {
    // A row is the body as the wire serves it to that reader, and a served body of an element under the hidden mark
    // carries hidden: true: the owner's full row of a hidden element, by the world's list or by its own key; below the
    // owner only a guest's row of its own hidden creation. A name row never carries it. Kills: the row as stored, a
    // guest's row unmarked.
    const char* elements = R"("elements": [
        {"id": "e05", "type": "collective", "name": "Guild"},
        {"id": "e06", "type": "collective", "name": "Choir", "hidden": true},
        {"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "x_lens": %s}])";
    auto world = [&](const char* x_lens) {
        std::string body = elements;
        body.replace(body.find("%s"), 2, x_lens);
        return Parse((std::string(R"({"hidden": ["e05"], )") + body + "}").c_str());
    };
    OWJsonValue full = world("{}");
    OWLensReading owner = ReadAs(full, OWJsonValue::String("L"), R"({"rung": "owner"})");
    t.Ok(owner.rows.size() == 2 &&
             Equal(owner.rows[0], Parse(R"({"id": "e05", "type": "collective", "name": "Guild", "hidden": true})")) &&
             Equal(owner.rows[1], Parse(R"({"id": "e06", "type": "collective", "name": "Choir", "hidden": true})")),
         "the world's list and the element's own key, one mark: " + RowsOf(owner));
    OWLensReading guest = ReadAs(full, OWJsonValue::String("L"), R"({"rung": "guest", "whole": ["L", "e05"]})");
    t.Ok(guest.rows.size() == 1 &&
             Equal(guest.rows[0], Parse(R"({"id": "e05", "type": "collective", "name": "Guild", "hidden": true})")),
         "a guest's own hidden creation: " + RowsOf(guest));
    OWLensReading named = ReadAs(world(R"({"show": "name"})"), OWJsonValue::String("L"), R"({"rung": "owner"})");
    t.Ok(named.rows.size() == 2 && named.rows[0].Find("hidden") == nullptr, "a name row: " + RowsOf(named));
}

OW_TEST(lens_a_stored_hidden_false_is_dropped_from_the_row) {
    // keel serves no hidden key on a body no mark hides, so a stored hidden: false is dropped from the row, for the
    // owner and every reader below it. Kills: the stored key carried onto the row.
    OWJsonValue world = Parse(R"({"elements": [{"id": "e05", "type": "collective", "name": "Guild", "hidden": false},
        {"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "x_lens": {}}]})");
    for (const char* reader : {R"({"rung": "owner"})", R"({"rung": "contributor"})",
                               R"({"rung": "guest", "whole": ["L", "e05"]})", R"({"rung": "public"})"}) {
        OWLensReading r = ReadAs(world, OWJsonValue::String("L"), reader);
        t.Ok(r.rows.size() == 1 && Equal(r.rows[0], Parse(R"({"id": "e05", "type": "collective", "name": "Guild"})")),
             std::string(reader) + ": " + RowsOf(r));
    }
}

OW_TEST(lens_the_cut_counts_each_element_once_and_a_kept_element_is_never_also_cut) {
    // The cut counts each element once under its first reason, and a kept element is never also counted as cut; an
    // anchor resolves by its field's type and the id. One id an umbrella and its child anchor under two wrong types (a
    // Location under characters, then objects) is one element, counted once; one the umbrella anchors under the wrong
    // type and the child under its own is kept and not counted, and so the other way round. Kills: the cut keyed by
    // (type, id).
    auto read = [&](const char* u, const char* k) {
        OWJsonValue world = Parse((std::string(R"({"elements": [{"id": "C", "type": "construct", "name": "C",
            "supertype": "Lens", "x_lens": {"depth": 0}, )") + k + R"(}, {"id": "x", "type": "location", "name": "Harbour"}]})").c_str());
        OWJsonValue umbrella = Parse((std::string(R"({"id": "U", "type": "construct", "name": "U", "supertype": "Lens",
            "constructs": ["C"], "x_lens": {"depth": 0}, )") + u + "}").c_str());
        return OWLens::Resolve(world, umbrella, Parse(R"({"rung": "owner"})"));
    };
    OWLensReading r = read(R"("characters": ["x"])", R"("objects": ["x"])");
    t.Ok(r.kept.empty() && r.cut == Counts{{"anchor_not_visible", 1}}, "one element, once");
    r = read(R"("characters": ["x"])", R"("locations": ["x"])");
    t.Ok(r.kept == Ids{"x"} && r.cut.empty(), "kept by the child, so never also cut");
    r = read(R"("locations": ["x"])", R"("characters": ["x"])");
    t.Ok(r.kept == Ids{"x"} && r.cut.empty(), "kept by the umbrella, the child's wrong type not counted");
}

OW_TEST(lens_below_the_owner_an_unseen_anchor_a_cut_child_reaches_is_counted_under_the_childs_reason) {
    // Counts are per reader: below the owner an element whose owner-side reason was anchor_not_visible (a Location the
    // umbrella names under characters) and which a child cuts by types is counted under the child's reason; for the
    // owner anchor_not_visible comes first and it is counted once there. Guards what this reader already does. Kills:
    // the unseen anchor recorded for every reader and dropped at the count.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "U", "type": "construct", "name": "U", "supertype": "Lens", "characters": ["x"], "constructs": ["C"], "x_lens": {"depth": 0}},
        {"id": "C", "type": "construct", "name": "C", "supertype": "Lens", "characters": ["y"], "x_lens": {"types": ["character"]}},
        {"id": "y", "type": "character", "name": "Ysolde", "birthplace": "x"},
        {"id": "x", "type": "location", "name": "Harbour"}]})");
    OWLensReading contributor = ReadAs(world, OWJsonValue::String("U"), R"({"rung": "contributor"})");
    t.Ok(contributor.kept == Ids{"y"}, "Ysolde");
    t.Ok(contributor.cut == Counts{{"types", 1}}, "the child's reason");
    t.Ok(ReadAs(world, OWJsonValue::String("U"), R"({"rung": "public"})").cut == Counts{{"types", 1}}, "the public reader's too");
    t.Ok(ReadAs(world, OWJsonValue::String("U"), R"({"rung": "owner"})").cut == Counts{{"anchor_not_visible", 1}},
         "the owner's first reason, once");
}

OW_TEST(lens_a_note_goes_with_its_anchor_an_anchor_that_reaches_nothing_carries_it_onto_no_row) {
    // A note goes with its anchor: an anchor that reaches nothing (a Language named under constructs) carries its note
    // onto no row, even where another path (a child filter) keeps the element, for the owner and below it; and the
    // note is not counted ignored. Guards what this reader already does. Kills: an unseen id among the anchors whose
    // notes ride.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "U", "type": "construct", "name": "U", "supertype": "Lens", "constructs": ["e08", "L1"], "x_lens": {"notes": {"e08": "the old tongue"}}},
        {"id": "L1", "type": "construct", "name": "L1", "supertype": "Lens", "x_lens": {}},
        {"id": "e08", "type": "language", "name": "Old"}]})");
    for (const char* reader : {R"({"rung": "owner"})", R"({"rung": "contributor"})"}) {
        OWLensReading r = ReadAs(world, OWJsonValue::String("U"), reader);
        t.Ok(r.kept == Ids{"e08"}, std::string(reader) + ": kept by the child");
        t.Ok(r.rows.size() == 1 && r.rows[0].Find("note") == nullptr, std::string(reader) + ": no note rides");
        t.Eq(r.notes_ignored, int64_t(0), std::string(reader) + ": a note on an anchor is never counted ignored");
    }
}

OW_TEST(lens_an_unseen_anchor_is_counted_before_the_visited_check_the_lens_itself_included) {
    // Visited never comes before visibility, for any anchor: a hidden lens in its own constructs, read as a body through
    // a contributor's view, counts itself for the owner as anchor_not_visible; so does a child lens pointing back at its
    // hidden umbrella, its own objects kept. Guards what this reader already does. Kills: an anchor already visited
    // skipped before it is counted.
    const char* as_contributor = R"({"rung": "owner", "as": {"rung": "contributor"}})";
    OWJsonValue self = Parse(R"({"id": "L0", "type": "construct", "name": "L0", "supertype": "Lens", "hidden": true,
        "constructs": ["L0"], "x_lens": {}})");
    OWJsonValue alone = OWJsonValue::Object();
    OWJsonValue list = OWJsonValue::Array();
    list.Push(self);
    alone.Set("elements", list);
    t.Ok(ReadAs(alone, self, as_contributor).cut == Counts{{"anchor_not_visible", 1}}, "the lens's own id");
    OWJsonValue umbrella = Parse(R"({"id": "U", "type": "construct", "name": "U", "supertype": "Lens", "hidden": true,
        "constructs": ["C"], "x_lens": {"depth": 0}})");
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "U", "type": "construct", "name": "U", "supertype": "Lens", "hidden": true, "constructs": ["C"], "x_lens": {"depth": 0}},
        {"id": "C", "type": "construct", "name": "C", "supertype": "Lens", "constructs": ["U"], "objects": ["o1"], "x_lens": {"depth": 0}},
        {"id": "o1", "type": "object", "name": "Cup"}]})");
    OWLensReading r = ReadAs(world, umbrella, as_contributor);
    t.Ok(r.kept == Ids{"o1"}, "the child's own objects");
    t.Ok(r.cut == Counts{{"anchor_not_visible", 1}}, "the hidden umbrella, once");
}

OW_TEST(lens_a_hidden_element_in_a_guests_known_of_is_not_in_its_view_no_stub_no_row_no_count_no_mark) {
    // Hidden beats known_of: a hidden element named in a guest's known_of that is not its own creation is not in that
    // guest's view, so no stub, no row, no count and no mark; a hidden id in whole stays its own creation. A filter
    // keeps nothing of it; an anchor on it is one the guest cannot see, counted only for the owner reading as that
    // guest; a link to it is left out of the guest's served list (no gap). Kills: the hidden element kept as a stub.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "e05", "type": "character", "name": "Heir", "hidden": true},
        {"id": "c1", "type": "character", "name": "Ada", "friends": ["e05", "e06"]},
        {"id": "e06", "type": "character", "name": "Brin"},
        {"id": "F", "type": "construct", "name": "F", "supertype": "Lens", "x_lens": {"types": ["character"]}},
        {"id": "A", "type": "construct", "name": "A", "supertype": "Lens", "characters": ["e05"], "x_lens": {"depth": 0}}]})");
    const char* guest = R"({"rung": "guest", "whole": ["F", "A", "c1", "e06"], "known_of": ["e05"]})";
    OWLensReading filter = ReadAs(world, OWJsonValue::String("F"), guest);
    t.Ok(filter.kept == Ids{"c1", "e06"} && filter.cut.empty(), "a filter keeps no stub of it: " + RowsOf(filter));
    const OWJsonValue* friends = nullptr;
    for (const OWJsonValue& row : filter.rows)
        if (Str(row, "id") == "c1") friends = row.Find("friends");
    t.Ok(friends && friends->IsArray() && friends->Size() == 1 && Str(&friends->Items()[0]) == "e06",
         "left out of the guest's served list");
    OWLensReading anchored = ReadAs(world, OWJsonValue::String("A"), guest);
    t.Ok(anchored.kept.empty() && anchored.cut.empty(), "no stub row, nothing counted for the guest");
    OWLensReading as_guest = ReadAs(world, OWJsonValue::String("A"),
                                    R"({"rung": "owner", "as": {"rung": "guest", "whole": ["F", "A", "c1", "e06"], "known_of": ["e05"]}})");
    t.Ok(as_guest.cut == Counts{{"anchor_not_visible", 1}}, "the owner reading as the guest counts the anchor");
    OWLensReading own = ReadAs(world, OWJsonValue::String("F"), R"({"rung": "guest", "whole": ["F", "e05"]})");
    const OWJsonValue* mark = own.rows.size() == 1 ? own.rows[0].Find("hidden") : nullptr;
    t.Ok(mark && mark->IsBool() && mark->AsBool(), "a hidden id in whole stays its own creation, marked");
}
