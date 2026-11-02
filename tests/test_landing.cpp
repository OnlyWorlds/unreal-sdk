// OWLanding on the landing profile's conformance fixture (schema-dist profiles/landing.fixture.json, dist.103's copy,
// its sha256 held to MANIFEST.json): every case by name, its graph built as OWGraph::WorldGraph answers it, the
// options passed as the packages take them (JSON), each key the want holds compared in order and a case marked whole
// compared whole; then readings the fixture leaves open (the typed options beside the JSON ones, a whole float, a
// caller's mistake, the order of the refusals) and the profile's invariants over every case (every node once, a
// known-of node never a head or a gap, the same bytes from the graph read in any order).

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWLanding.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.103's MANIFEST.json records it.
const char* const DistSha256 = "bf6512dea464946e6fa3855d3a942446ecc97aabc564258d5ba801a6f7e01475";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/landing.fixture.json"; }

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

// Equal as written: the same kinds, every object's keys in the same order, lists in order, numbers by their text (the
// fixture's and the answer's are integers). The profile's order is part of the answer, so nothing is compared loosely.
void Same(owtest::T& t, const OWJsonValue& got, const OWJsonValue* want, const std::string& what) {
    t.Ok(want && got == *want, what + ": gave " + Compact(got) + ", the case says " + (want ? Compact(*want) : "nothing"));
}

// A case's graph in WorldGraph's shape, with the keys the reader reads: each node's id, type and known_of, the edges.
OWWorldGraph GraphOf(const OWJsonValue& graph) {
    OWWorldGraph g;
    if (const OWJsonValue* nodes = graph.Find("nodes"))
        for (const OWJsonValue& n : nodes->Items()) {
            OWGraphNode node;
            node.id = Str(n, "id");
            node.type = Str(n, "type");
            const OWJsonValue* k = n.Find("known_of");
            node.known_of = k && k->IsBool() && k->AsBool();
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

const OWJsonValue& OptionsOf(const OWJsonValue& c) {
    static const OWJsonValue none;
    const OWJsonValue* o = c.Find("options");
    return o ? *o : none;
}

std::vector<const OWJsonValue*> Cases() {
    std::vector<const OWJsonValue*> out;
    if (const OWJsonValue* cases = Fixture()->Find("cases"))
        for (const OWJsonValue& c : cases->Items()) out.push_back(&c);
    return out;
}

std::string Bytes(const OWJsonValue& v) { return OWJson::Stringify(v, true); }

void Case(owtest::T& t, const OWJsonValue& c) {
    const OWJsonValue* graph = c.Find("graph");
    const OWJsonValue* want = c.Find("want");
    if (!graph || !want || !want->IsObject()) return t.Fail("the case has no graph or no want");
    OWWorldLanding got = OWLanding::LandingOf(GraphOf(*graph), OptionsOf(c));
    t.Eq(got.error, std::string(), "no caller's mistake");
    OWJsonValue answer = got.ToJson();
    const OWJsonValue* whole = c.Find("whole");
    if ((whole && whole->IsBool() && whole->AsBool()) || want->Find("refused")) {
        Same(t, answer, want, "the whole answer");
        return;
    }
    const std::vector<std::string> parts = {"mode", "size", "clusters", "unclustered", "gaps"};
    t.Ok(answer.Keys() == parts, "the answer's five keys, in order: " + Compact(answer));
    for (size_t i = 0; i < want->Size(); ++i) {
        const std::string& key = want->Keys()[i];
        const OWJsonValue* mine = answer.Find(key);
        if (!mine) {
            t.Fail("no " + key + " in the answer");
            continue;
        }
        Same(t, *mine, &want->Items()[i], key);
    }
}

OWWorldGraph Graph(const char* json) { return GraphOf(Parse(json)); }

}  // namespace

OW_TEST(landing_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the landing fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.103's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/landing.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("landing"), "the landing profile's fixture");
    std::set<std::string> names;
    size_t refusals = 0, wholes = 0;
    for (const OWJsonValue* c : Cases()) {
        names.insert(Str(*c, "name"));
        if (const OWJsonValue* w = c->Find("want"); w && w->Find("refused")) ++refusals;
        if (const OWJsonValue* w = c->Find("whole"); w && w->IsBool() && w->AsBool()) ++wholes;
    }
    t.Eq(Cases().size(), size_t(30), "30 cases");
    t.Eq(names.size(), size_t(30), "each name its own");
    t.Eq(refusals, size_t(7), "7 refusals");
    t.Eq(wholes, size_t(1), "1 case whole");
}

OW_CASES(landing_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Cases().empty()) {
        out.emplace_back("landing_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue* c : Cases())
        out.emplace_back("landing_fixture:" + Str(*c, "name"), [doc, c](owtest::T& t) { Case(t, *c); });
    return out;
}

// -- the profile's invariants over every case ----------------------------------------------------------------------

OW_TEST(landing_every_case_holds_the_profiles_invariants) {
    for (const OWJsonValue* c : Cases()) {
        std::string name = Str(*c, "name");
        OWWorldGraph g = GraphOf(*c->Find("graph"));
        OWWorldLanding got = OWLanding::LandingOf(g, OptionsOf(*c));
        std::string once = Bytes(got.ToJson());
        t.Eq(Bytes(OWLanding::LandingOf(g, OptionsOf(*c)).ToJson()), once, name + ": read twice, the same bytes");
        // No queue order decides anything: the nodes and the edges read backwards give the same answer.
        OWWorldGraph back = g;
        std::reverse(back.nodes.begin(), back.nodes.end());
        std::reverse(back.edges.begin(), back.edges.end());
        t.Eq(Bytes(OWLanding::LandingOf(back, OptionsOf(*c)).ToJson()), once, name + ": from the graph reversed");
        if (!got.refused.empty()) {
            t.Ok(got.clusters.empty() && got.unclustered.empty() && got.gaps.empty() && got.mode.empty(),
                 name + ": a refusal reads nothing");
            continue;
        }
        std::set<std::string> stubs, all, seen;
        for (const OWGraphNode& n : g.nodes) {
            all.insert(n.id);
            if (n.known_of) stubs.insert(n.id);
        }
        for (const OWLandingCluster& cl : got.clusters) {
            t.Ok(!stubs.count(cl.head), name + ": a known-of node is never a head");
            t.Ok(!cl.members.empty() && cl.members[0].id == cl.head && cl.members[0].hops == 0,
                 name + ": the head first, at 0");
            for (const OWLandingMember& m : cl.members) t.Ok(seen.insert(m.id).second, name + ": one home per node");
        }
        for (const OWLandingNode& n : got.unclustered) t.Ok(seen.insert(n.id).second, name + ": one home per node");
        t.Ok(seen == all, name + ": every node is in a cluster or unclustered");
        for (const OWLandingGap& gap : got.gaps) t.Ok(!stubs.count(gap.id), name + ": a known-of node is never a gap");
        t.Eq(got.mode, std::string(int64_t(g.nodes.size()) < got.size ? "gaps" : "graph"), name + ": the mode");
    }
}

// -- readings the fixture leaves open -------------------------------------------------------------------------------

OW_TEST(landing_the_tables) {
    t.Ok(OWLanding::OptionKeys() == std::vector<std::string>{"heads", "hops", "size"}, "the three options, in order");
    t.Ok(OWLanding::GapKinds() == std::vector<std::string>{"orphan", "near_orphan", "no_place", "nobody_here"},
         "the gap kinds, in the page's order");
    std::set<std::string> kinds;
    for (const OWJsonValue* c : Cases())
        if (const OWJsonValue* gaps = c->Find("want")->Find("gaps"))
            for (const OWJsonValue& g : gaps->Items()) kinds.insert(Str(g, "kind"));
    t.Eq(kinds.size(), size_t(4), "the fixture's gaps name all four kinds");
    for (const std::string& k : kinds)
        t.Ok(std::find(OWLanding::GapKinds().begin(), OWLanding::GapKinds().end(), k) != OWLanding::GapKinds().end(),
             k + " is a kind");
}

OW_TEST(landing_the_typed_options_answer_as_the_json_ones) {
    // Every case read again through OWLandingOptions, its options set from the JSON: the same bytes.
    for (const OWJsonValue* c : Cases()) {
        const OWJsonValue& o = OptionsOf(*c);
        if (c->Find("want")->Find("refused")) continue;  // the typed form cannot carry a string or a fraction
        OWLandingOptions typed;
        for (auto [key, into] : {std::pair<const char*, std::optional<int64_t>*>{"heads", &typed.heads},
                                 {"hops", &typed.hops}, {"size", &typed.size}})
            if (const OWJsonValue* v = o.IsObject() ? o.Find(key) : nullptr; v && v->IsNumber())
                *into = int64_t(v->AsDouble());
        OWWorldGraph g = GraphOf(*c->Find("graph"));
        t.Eq(Bytes(OWLanding::LandingOf(g, typed).ToJson()), Bytes(OWLanding::LandingOf(g, o).ToJson()),
             Str(*c, "name"));
    }
    OWWorldGraph g = Graph(R"({"nodes": [{"id": "a", "type": "character"}], "edges": []})");
    OWLandingOptions zero;
    zero.hops = 0;
    t.Eq(OWLanding::LandingOf(g, zero).refused, std::string("hops"), "a typed 0 is refused by name");
    OWLandingOptions both;
    both.size = -1;
    both.heads = 0;
    t.Eq(OWLanding::LandingOf(g, both).refused, std::string("heads"), "heads named before size");
    OWWorldLanding d = OWLanding::LandingOf(g);
    t.Ok(d.refused.empty() && d.size == 24 && d.mode == "gaps", "no options: the defaults");
}

OW_TEST(landing_options_as_json_at_their_edges) {
    OWWorldGraph g = Graph(R"({"nodes": [{"id": "a", "type": "character"}, {"id": "b", "type": "character"},
        {"id": "e", "type": "event"}], "edges": [{"source": "e", "field": "characters", "target": "a", "index": 0},
        {"source": "e", "field": "characters", "target": "b", "index": 1}]})");
    auto refused = [&](const char* json) { return OWLanding::LandingOf(g, Parse(json)).refused; };
    t.Eq(refused(R"({"heads": 1.0})"), std::string(), "a whole float reads whole (JavaScript cannot tell 1.0 from 1)");
    t.Eq(OWLanding::LandingOf(g, Parse(R"({"heads": 1.0})")).clusters.size(), size_t(1), "and caps at one head");
    t.Eq(refused(R"({"size": 2e1})"), std::string(), "2e1 is 20");
    t.Eq(OWLanding::LandingOf(g, Parse(R"({"size": 2e1})")).size, int64_t(20), "read at 20");
    t.Eq(refused(R"({"heads": 1e30, "hops": 1e30})"), std::string(), "past 64 bits is still a whole number");
    t.Eq(OWLanding::LandingOf(g, Parse(R"({"heads": 1e30, "hops": 1e30})")).clusters.size(), size_t(2),
         "every head, each reaching all it can");
    t.Eq(refused(R"({"size": 1e400})"), std::string("size"), "a number past every double is none (JavaScript's Infinity)");
    t.Eq(refused(R"({"heads": true})"), std::string("heads"), "a boolean");
    t.Eq(refused(R"({"hops": -1})"), std::string("hops"), "below 1");
    t.Eq(refused(R"({"size": [3]})"), std::string("size"), "a list");
    t.Eq(refused(R"({"heads": 0, "zone": 1})"), std::string("zone"), "an unknown key before a bad value");
    t.Eq(refused(R"({"size": 0, "heads": 0})"), std::string("heads"), "heads before size, whatever the key order");
    t.Eq(refused(R"({})"), std::string(), "an empty object: the defaults");
    t.Eq(refused("null"), std::string(), "null: the defaults");
    OWWorldLanding bad = OWLanding::LandingOf(g, Parse("[]"));
    t.Ok(!bad.error.empty() && bad.refused.empty() && bad.mode.empty(),
         "a list for the options is a caller's mistake: " + bad.error);
    t.Ok(!OWLanding::LandingOf(g, Parse("5")).error.empty(), "a number for the options");
}

OW_TEST(landing_reads_the_graph_world_graph_answers) {
    // Over a world, not a hand-made graph: a Character in a hidden Location, read by a contributor, has no place,
    // since the graph the visitor is served holds no edge to it; the owner's graph with the hidden flag places her.
    const char* world = R"({"elements": [
        {"id": "c1", "type": "character", "name": "Mara", "location": "l1"},
        {"id": "l1", "type": "location", "name": "Hollow", "hidden": true},
        {"id": "l2", "type": "location", "name": "Moor"},
        {"id": "e1", "type": "event", "name": "Fair", "characters": ["c1"], "locations": ["l2"]}]})";
    OWJsonValue w = Parse(world);
    auto gaps = [&](const char* reader, const OWGraphOptions& options) {
        OWWorldGraph g = OWGraph::WorldGraph(w, Parse(reader), options);
        std::string out;
        for (const OWLandingGap& gap : OWLanding::LandingOf(g).gaps) out += gap.kind + ":" + gap.id + " ";
        return out;
    };
    OWGraphOptions hidden;
    hidden.hidden = true;
    t.Eq(gaps(R"({"rung": "owner"})", hidden), std::string("near_orphan:l1 near_orphan:l2 nobody_here:l2 "),
         "the owner asking for hidden: Mara is placed in the Hollow");
    t.Eq(gaps(R"({"rung": "owner"})", {}), std::string("near_orphan:c1 near_orphan:l2 no_place:c1 nobody_here:l2 "),
         "the owner's default graph leaves the Hollow out, so Mara has no place there");
    t.Eq(gaps(R"({"rung": "contributor"})", {}),
         std::string("near_orphan:c1 near_orphan:l2 no_place:c1 nobody_here:l2 "),
         "a contributor: the Hollow is no node, so Mara has no place");
}
