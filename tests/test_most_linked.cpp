// OWLanding::MostLinkedOf on the npm SDK's and the Python package's shared fixture (tests/data/most-linked.fixture.json,
// the same bytes, held by its sha256): every case's whole answer, key order included, or the option it refuses; then
// what the fixture cannot hold: no options, the caller's mistake, the typed options, purity, and the graph WorldGraph
// answers.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWLanding.h"

#include <memory>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/most-linked.fixture.json at 4.203.0 (fc4d560); the Python package holds the same bytes.
const char* const FixtureSha256 = "9a7b27d849cd1c268d86b754bcdb7954cb23b3ede85178023332d61e497489c4";

const OWBytes& FixtureBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/most-linked.fixture.json", bytes);
        read = true;
    }
    return bytes;
}

std::shared_ptr<OWJsonValue> Doc() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWJsonParseResult r = OWJson::Parse(FixtureBytes());
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

const OWJsonValue& Get(const OWJsonValue& v, const char* key) {
    static const OWJsonValue none;
    const OWJsonValue* x = v.Find(key);
    return x ? *x : none;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

OWJsonValue Parse(std::string_view text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? r.value : OWJsonValue();
}

std::string Compact(const OWJsonValue& v) { return OWJson::Stringify(v, true); }

// A case's graph in WorldGraph's shape, with the keys the reader reads: each node's id, type, name and known_of, the
// edges.
OWWorldGraph GraphOf(const OWJsonValue& graph) {
    OWWorldGraph g;
    for (const OWJsonValue& n : Get(graph, "nodes").Items()) {
        OWGraphNode node;
        node.id = Str(n, "id");
        node.type = Str(n, "type");
        node.name = Get(n, "name");
        const OWJsonValue* k = n.Find("known_of");
        node.known_of = k && k->IsBool() && k->AsBool();
        g.nodes.push_back(std::move(node));
    }
    for (const OWJsonValue& e : Get(graph, "edges").Items()) {
        const OWJsonValue* index = e.Find("index");
        g.edges.push_back({Str(e, "source"), Str(e, "field"), Str(e, "target"),
                           index && index->IsNumber() ? int64_t(index->AsDouble()) : 0});
    }
    return g;
}

}  // namespace

OW_TEST(most_linked_fixture_is_the_shared_file) {
    t.Eq(owtest::Sha256Hex(FixtureBytes()), std::string(FixtureSha256), "the fixture's sha256 (the npm SDK's bytes)");
    t.Eq(Str(*Doc(), "profile"), std::string("most-linked"), "the profile");
    t.Eq(Get(*Doc(), "cases").Size(), size_t(18), "18 cases");
}

OW_CASES(most_linked_cases) {
    owtest::Cases out;
    auto doc = Doc();
    for (const OWJsonValue& c : Get(*doc, "cases").Items()) {
        const OWJsonValue* cp = &c;
        out.push_back({"most_linked: " + Str(c, "name"), [doc, cp](owtest::T& t) {
                           OWMostLinked got = OWLanding::MostLinkedOf(GraphOf(Get(*cp, "graph")), Get(*cp, "options"));
                           t.Eq(got.error, std::string(), "no caller's mistake");
                           // Byte for byte, key order included; a refusal is {refused} with the option's name.
                           t.Eq(Compact(got.ToJson()), Compact(Get(*cp, "want")), "the whole answer");
                       }});
    }
    return out;
}

OW_TEST(most_linked_no_options_null_options_and_empty_options_are_the_default) {
    const OWJsonValue& c = Get(*Doc(), "cases").Items()[0];
    const OWWorldGraph g = GraphOf(Get(c, "graph"));
    const std::string want = Compact(Get(c, "want"));
    t.Eq(Compact(OWLanding::MostLinkedOf(g).ToJson()), want, "no options");
    t.Eq(Compact(OWLanding::MostLinkedOf(g, OWJsonValue::Null()).ToJson()), want, "null options");
    t.Eq(Compact(OWLanding::MostLinkedOf(g, OWJsonValue::Object()).ToJson()), want, "empty options");
}

OW_TEST(most_linked_options_that_are_not_an_object_are_the_callers_mistake) {
    OWMostLinked a = OWLanding::MostLinkedOf(OWWorldGraph{}, OWJsonValue::Number(int64_t(3)));
    t.Ok(!a.error.empty() && a.refused.empty(), "options 3: an error, no option refused");
    OWMostLinked b = OWLanding::MostLinkedOf(OWWorldGraph{}, Parse("[]"));
    t.Ok(!b.error.empty() && b.refused.empty(), "options []: an error, no option refused");
}

OW_TEST(most_linked_typed_options_refuse_as_the_json_ones_do) {
    const OWWorldGraph g = GraphOf(Get(Get(*Doc(), "cases").Items()[0], "graph"));
    for (int64_t n : {int64_t(0), int64_t(-1), int64_t(9007199254740992)}) {
        OWMostLinkedOptions o;
        o.per_type = n;
        t.Eq(OWLanding::MostLinkedOf(g, o).refused, std::string("per_type"), "per_type " + std::to_string(n));
    }
    OWMostLinkedOptions most;
    most.per_type = 9007199254740991;
    OWMostLinked a = OWLanding::MostLinkedOf(g, most);
    t.Eq(a.per_type, int64_t(9007199254740991), "2^53 - 1 taken and echoed");
    t.Eq(a.by_type.size() > 0 ? a.by_type[0].second.size() : size_t(0), size_t(4), "every character with an edge");
}

OW_TEST(most_linked_reads_the_graph_it_is_given_and_changes_nothing_in_it) {
    const OWJsonValue& graph = Get(Get(*Doc(), "cases").Items()[6], "graph");
    const OWWorldGraph g = GraphOf(graph);
    const OWWorldGraph before = g;
    OWLanding::MostLinkedOf(g);
    t.Eq(g.nodes.size(), before.nodes.size(), "the nodes");
    t.Eq(g.edges.size(), before.edges.size(), "the edges");
    bool same = true;
    for (size_t i = 0; i < g.nodes.size(); ++i) same = same && g.nodes[i].id == before.nodes[i].id;
    t.Ok(same, "each node in its place");
}

OW_TEST(most_linked_through_world_graph_a_hidden_element_and_map_space_are_not_counted) {
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "c1", "type": "character", "name": "Ada", "friends": ["c2"]},
        {"id": "c2", "type": "character", "name": "Brin"},
        {"id": "sec", "type": "event", "name": "The Secret", "characters": ["c1", "c2"], "hidden": true},
        {"id": "m1", "type": "map", "name": "Chart"},
        {"id": "p1", "type": "pin", "name": "Pin", "map": "m1", "element_type": "character", "element_id": "c1"}]})");
    OWJsonValue owner = Parse(R"({"rung": "owner"})");
    OWMostLinked got = OWLanding::MostLinkedOf(OWGraph::WorldGraph(world, owner));
    t.Eq(Compact(got.ToJson()),
         Compact(Parse(R"({"per_type": 3, "by_type": {"character": [{"id": "c1", "name": "Ada", "degree": 1},
                                                                  {"id": "c2", "name": "Brin", "degree": 1}]}})")),
         "the owner's graph");
    // The control: a graph that brings the hidden element in counts it, so the case above can see it.
    OWGraphOptions hidden;
    hidden.hidden = true;
    OWMostLinked shown = OWLanding::MostLinkedOf(OWGraph::WorldGraph(world, owner, hidden));
    const OWJsonValue answer = shown.ToJson();
    t.Eq(Compact(Get(Get(answer, "by_type"), "event")),
         Compact(Parse(R"([{"id": "sec", "name": "The Secret", "degree": 2}])")), "the hidden event, brought in");
    t.Eq(Compact(Get(Get(answer, "by_type"), "character").Items()[0]),
         Compact(Parse(R"({"id": "c1", "name": "Ada", "degree": 2})")), "and counted at Ada");
}
