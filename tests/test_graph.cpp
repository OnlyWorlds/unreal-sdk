// OWGraph beside the fixture's graph cases (run in test_lens.cpp, through the pinned fixture test): the graph's tables
// held to the fixture's, the profile's invariants over every graph case (one edge per place, edges between nodes only,
// the same bytes read twice and from the elements reversed, the world unmoved, a stub with no edge out, a lens's graph
// exactly its kept, a public reader with no room, hidden or proposed node and no count of one), the degrees, the
// refusal before any read, and a caller's mistakes.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWLens.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>

using namespace OnlyWorlds;
using owtest::P;

namespace {

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(P().root + "/codegen/schema-dist/profiles/lens.fixture.json", bytes);
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

std::vector<const OWJsonValue*> GraphCases() {
    std::vector<const OWJsonValue*> out;
    if (const OWJsonValue* cases = Fixture()->Find("cases"))
        for (const OWJsonValue& c : cases->Items())
            if (Str(c.Find("kind")) == "graph") out.push_back(&c);
    return out;
}

OWGraphOptions OptionsOf(const OWJsonValue& c) {
    OWGraphOptions o;
    const OWJsonValue* opt = c.Find("options");
    if (!opt || !opt->IsObject()) return o;
    if (const OWJsonValue* lens = opt->Find("lens")) o.lens = *lens;
    if (const OWJsonValue* k = opt->Find("clock"); k && k->IsString()) o.clock = k->Text();
    for (auto [key, flag] : {std::pair<const char*, bool*>{"rooms", &o.rooms}, {"hidden", &o.hidden},
                             {"proposed", &o.proposed}, {"map_space", &o.map_space}})
        if (const OWJsonValue* v = opt->Find(key); v && v->IsBool()) *flag = v->AsBool();
    return o;
}

std::string Bytes(const OWJsonValue& v) { return OWJson::Stringify(v, true); }

std::vector<std::string> List(const char* key) {
    std::vector<std::string> out;
    if (const OWJsonValue* v = Fixture()->Find(key))
        for (const OWJsonValue& s : v->Items()) out.push_back(s.Text());
    return out;
}

OWWorldGraph Read(const char* world, const char* reader, const OWGraphOptions& options = {}) {
    return OWGraph::WorldGraph(Parse(world), Parse(reader), options);
}

}  // namespace

OW_TEST(graph_tables_are_the_fixtures) {
    t.Eq(GraphCases().size(), size_t(47), "the fixture's 47 graph cases");
    t.Ok(List("graph_links") == OWGraph::DeclaredLinks(), "the declared links");
    t.Ok(List("graph_flags") == OWGraph::Flags(), "the flags");
    t.Ok(List("owner_only_flags") == OWGraph::OwnerOnlyFlags(), "the owner's flags");
    t.Ok(List("edges_cut_reasons") == OWGraph::EdgeReasons(), "the reasons a link is cut, in the fixture's order");
    t.Ok(List("owner_only_edges_cut_reasons") == OWGraph::OwnerOnlyEdgeReasons(), "the owner's reasons");
}

OW_TEST(graph_every_case_holds_the_profiles_invariants) {
    for (const OWJsonValue* c : GraphCases()) {
        std::string name = Str(c->Find("name"));
        const OWJsonValue& world = *c->Find("world");
        const OWJsonValue& reader = *c->Find("reader");
        OWGraphOptions options = OptionsOf(*c);
        std::string before = Bytes(world);
        OWWorldGraph got = OWGraph::WorldGraph(world, reader, options);
        std::string once = Bytes(got.ToJson());
        t.Eq(Bytes(OWGraph::WorldGraph(world, reader, options).ToJson()), once, name + ": read twice, the same bytes");
        OWJsonValue flipped = world;
        OWJsonValue reversed = OWJsonValue::Array();
        const std::vector<OWJsonValue>& els = world.Find("elements")->Items();
        for (size_t i = els.size(); i-- > 0;) reversed.Push(els[i]);
        flipped.Set("elements", std::move(reversed));
        t.Eq(Bytes(OWGraph::WorldGraph(flipped, reader, options).ToJson()), once,
             name + ": from its elements reversed, the same bytes");
        t.Eq(Bytes(world), before, name + ": the world is unmoved");
        if (!got.refused.empty()) {
            t.Ok(got.nodes.empty() && got.edges.empty() && got.cut.empty() && got.edges_cut.empty(),
                 name + ": a refusal reads nothing");
            continue;
        }
        std::set<std::string> ids, stubs;
        for (const OWGraphNode& n : got.nodes) {
            ids.insert(n.id);
            if (n.known_of) stubs.insert(n.id);
        }
        std::set<std::tuple<std::string, std::string, std::string, int64_t>> places;
        for (const OWGraphEdge& e : got.edges) {
            t.Ok(places.emplace(e.source, e.field, e.target, e.index).second, name + ": one edge per place");
            t.Ok(ids.count(e.source) && ids.count(e.target), name + ": an edge between nodes");
            t.Ok(!stubs.count(e.source), name + ": no edge out of a known-of node");
        }
        if (!options.lens.IsNull() && !got.unreadable) {
            OWLensOptions lo;
            lo.clock = options.clock;
            OWLensReading lens = OWLens::Resolve(world, options.lens, reader, lo);
            std::set<std::string> kept(lens.kept.begin(), lens.kept.end());
            t.Ok(kept == ids && kept.size() == lens.kept.size(), name + ": a lens's graph is exactly its kept");
            OWJsonValue mine = got.ToJson(), its = lens.ToJson();
            t.Eq(Bytes(*mine.Find("cut")), Bytes(*its.Find("cut")), name + ": the lens's cut rides along unchanged");
        }
        const OWJsonValue* rung = reader.Find("rung");
        if (Str(rung) == "public") {
            for (const OWGraphNode& n : got.nodes)
                t.Ok(n.marks.empty(), name + ": a public reader's node carries no mark");
            for (const auto* counts : {&got.cut, &got.edges_cut})
                for (const auto& [why, n] : *counts)
                    t.Ok(why != "room" && why != "hidden" && why != "proposed" && why != "not_visible",
                         name + ": a public reader counts no " + why);
        }
    }
}

OW_TEST(graph_degrees) {
    OWWorldGraph g = Read(R"({"elements": [
        {"id": "a", "type": "character", "name": "A", "friends": ["b", "a"], "rivals": ["b"]},
        {"id": "b", "type": "character", "name": "B", "friends": ["a"]},
        {"id": "c", "type": "character", "name": "C"}]})", R"({"rung": "owner"})");
    t.Eq(g.edges.size(), size_t(4), "four edges: a-b twice, a-a, b-a");
    using Counts = std::vector<std::pair<std::string, int64_t>>;
    // a: a->b (friends), a->a, a->b (rivals), b->a = 4 (the link to itself once); b: three; c: none.
    t.Ok(OWGraph::DegreeOf(g) == Counts{{"a", 4}, {"b", 3}, {"c", 0}}, "degree: each edge once for each node it touches");
    t.Ok(OWGraph::OutDegreeOf(g) == Counts{{"a", 1}, {"b", 1}, {"c", 0}},
         "out-degree: distinct targets, a target in two fields once, itself never");
    t.Ok(OWGraph::DegreeOf(OWWorldGraph{}).empty() && OWGraph::OutDegreeOf(OWWorldGraph{}).empty(), "an empty graph");
}

OW_TEST(graph_a_refusal_comes_before_any_read) {
    OWGraphOptions rooms;
    rooms.rooms = true;
    OWWorldGraph g = Read(R"({"no": "elements"})", R"({"rung": "public"})", rooms);
    t.Eq(g.refused, std::string("rooms"), "a public reader asking rooms, with no world to read");
    t.Eq(g.error, std::string(), "and no caller's mistake: the world was never read");
    OWGraphOptions all;
    all.rooms = all.hidden = all.proposed = all.map_space = true;
    t.Eq(Read(R"({"elements": []})", R"({"rung": "public"})", all).refused, std::string("hidden"),
         "every flag from a public reader: hidden is named first");
    t.Eq(Read(R"({"elements": []})", R"({"rung": "guest", "whole": []})", all).refused, std::string("hidden"), "a guest");
    OWGraphOptions proposed;
    proposed.proposed = proposed.rooms = true;
    t.Eq(Read(R"({"elements": []})", R"({"rung": "contributor"})", proposed).refused, std::string("proposed"),
         "proposed from a contributor, before rooms");
    t.Eq(Read(R"({"elements": []})", R"({"rung": "owner", "as": {"rung": "public"}})", rooms).refused,
         std::string("rooms"), "the owner reading as the public is judged by that rung");
    t.Eq(Read(R"({"elements": []})", R"({"rung": "owner"})", all).refused, std::string(), "the owner may ask all four");
    t.Eq(Read(R"({"elements": []})", R"({"rung": "contributor"})", rooms).refused, std::string(), "rooms from a contributor");
}

OW_TEST(graph_a_callers_mistake_is_named) {
    t.Ok(!Read(R"({"no": 1})", R"({"rung": "owner"})").error.empty(), "no elements");
    t.Ok(!Read(R"({"elements": []})", R"({"rung": "king"})").error.empty(), "a rung outside the four");
    t.Ok(!Read(R"({"elements": []})", R"({"rung": "owner", "as": {"rung": "x"}})").error.empty(), "an as outside the four");
    OWGraphOptions clock;
    clock.lens = OWJsonValue::String("L");
    clock.clock = "yesterday";
    t.Ok(!Read(R"({"elements": []})", R"({"rung": "owner"})", clock).error.empty(), "a clock");
    OWGraphOptions number;
    number.lens = OWJsonValue::Number(int64_t(3));
    t.Ok(!Read(R"({"elements": []})", R"({"rung": "owner"})", number).error.empty(), "a lens neither body nor id");
    OWWorldGraph good = Read(R"({"elements": [{"id": "c", "type": "character", "name": "C"}]})", R"({"rung": "owner"})");
    t.Eq(good.error, std::string(), "a good call");
    t.Eq(good.nodes.size(), size_t(1), "one node");
}

OW_TEST(graph_a_link_names_an_element_of_its_fields_type) {
    // keel keys an element by its type and id, and a link by its field's target type (elements/lens.py _links_of), as
    // the core's lens reader does: a Character's `friends` naming a Location's id names nothing, and is not counted.
    OWWorldGraph g = Read(R"({"elements": [
        {"id": "c", "type": "character", "name": "C", "friends": ["p"], "location": "p"},
        {"id": "p", "type": "location", "name": "P"}]})", R"({"rung": "owner"})");
    t.Eq(g.edges.size(), size_t(1), "one edge");
    t.Ok(g.edges.size() == 1 && g.edges[0].field == "location", "the location, not the friend");
    t.Ok(g.edges_cut.empty(), "the wrong-type link is not counted");
    // A Pin's generic pair is no edge: keel's link registry lists only the single and list links (pin.element apart).
    OWGraphOptions map;
    map.map_space = true;
    OWWorldGraph pin = Read(R"({"elements": [
        {"id": "m", "type": "map", "name": "M"},
        {"id": "n", "type": "pin", "name": "N", "map": "m", "element_type": "character", "element_id": "c"},
        {"id": "c", "type": "character", "name": "C"}]})", R"({"rung": "owner"})", map);
    t.Eq(pin.edges.size(), size_t(1), "the pin's map only");
}

OW_TEST(graph_the_lenss_reason_wins_over_not_visible) {
    // The profile's "Edges cut": for a target the lens cut, the lens's own reason wins over not_visible ("an anchor the
    // reader cannot see is the lens's anchor_not_visible"). keel's replay tests the view first; no fixture case holds
    // an edge into such an anchor.
    OWGraphOptions lens;
    lens.lens = OWJsonValue::String("L1");
    OWWorldGraph g = Read(R"({"elements": [
        {"id": "L1", "type": "construct", "supertype": "Lens", "name": "Pair", "characters": ["c1", "c2"]},
        {"id": "c1", "type": "character", "name": "Ada", "friends": ["c2"]},
        {"id": "c2", "type": "character", "name": "Ghost", "hidden": true}]})",
                          R"({"rung": "owner", "as": {"rung": "contributor"}})", lens);
    t.Eq(g.nodes.size(), size_t(1), "Ada alone");
    using Counts = std::vector<std::pair<std::string, int64_t>>;
    t.Ok(g.cut == Counts{{"anchor_not_visible", 1}}, "the lens counts the Ghost for the owner");
    t.Ok(g.edges_cut == Counts{{"anchor_not_visible", 1}}, "and the edge to it carries the lens's reason");
    OWWorldGraph c = Read(R"({"elements": [
        {"id": "L1", "type": "construct", "supertype": "Lens", "name": "Pair", "characters": ["c1", "c2"]},
        {"id": "c1", "type": "character", "name": "Ada", "friends": ["c2"]},
        {"id": "c2", "type": "character", "name": "Ghost", "hidden": true}]})", R"({"rung": "contributor"})", lens);
    t.Ok(c.cut.empty() && c.edges_cut.empty(), "a contributor carries neither count");
}

OW_TEST(graph_an_index_is_the_place_in_the_list_as_served) {
    // The profile's owner (G5): an edge's index is the target's place in the link list as that reader is served it.
    // keel serves a proposed id in every rung's lists but the public's (elements/visibility.py), so a contributor's
    // index counts it; the public's does not. A proposed element is still no node for either.
    const char* world = R"({"elements": [
        {"id": "c1", "type": "character", "name": "Ada", "friends": ["p", "v"]},
        {"id": "p", "type": "character", "name": "Pending", "x_review_proposed": "a seat's proposal"},
        {"id": "v", "type": "character", "name": "Vera"}]})";
    OWWorldGraph c = Read(world, R"({"rung": "contributor"})");
    t.Eq(c.edges.size(), size_t(1), "a contributor: one edge, to Vera");
    t.Ok(c.edges.size() == 1 && c.edges[0].target == "v" && c.edges[0].index == 1,
         "at index 1: the proposed id before her is served");
    OWWorldGraph pub = Read(world, R"({"rung": "public"})");
    t.Ok(pub.edges.size() == 1 && pub.edges[0].target == "v" && pub.edges[0].index == 0,
         "the public: index 0, the proposed id is not served");
}

OW_TEST(graph_a_public_readers_served_list_leaves_a_proposed_id_out_and_its_index_has_no_gap) {
    // A public reader's served link lists leave a proposed id out, as keel's wire serves the public rung, so the index
    // has no gap: a visible target after a proposed one is at 0. The served list is read off the lens's row for Ada.
    OWJsonValue world = Parse(R"({"elements": [
        {"id": "a", "type": "character", "name": "Ada", "friends": ["px", "b"]},
        {"id": "px", "type": "character", "name": "Pax", "x_review_proposed": {"by": "m1"}},
        {"id": "b", "type": "character", "name": "Brin"},
        {"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "characters": ["a"], "x_lens": {"depth": 0}}]})");
    OWLensReading r = OWLens::Resolve(world, OWJsonValue::String("L"), Parse(R"({"rung": "public"})"));
    const OWJsonValue* friends = r.rows.size() == 1 ? r.rows[0].Find("friends") : nullptr;
    t.Ok(friends && friends->IsArray() && friends->Size() == 1 && friends->Items()[0].IsString() &&
             friends->Items()[0].Text() == "b",
         "the public rung is served [Brin]");
    OWWorldGraph g = OWGraph::WorldGraph(world, Parse(R"({"rung": "public"})"));
    std::vector<const OWGraphEdge*> from_ada;  // the lens's own edge to Ada aside
    for (const OWGraphEdge& e : g.edges)
        if (e.source == "a") from_ada.push_back(&e);
    t.Ok(from_ada.size() == 1 && from_ada[0]->target == "b" && from_ada[0]->index == 0, "Brin at 0");
}

OW_TEST(graph_a_nodes_type_is_as_stored) {
    // The profile's owner (G4): a node's type is the element's type as stored, never folded; a non-slug type is a data
    // fault for the lint. Its colour word and icon still come from the type it names.
    OWWorldGraph g = Read(R"({"elements": [{"id": "c1", "type": "Character", "name": "Ada"}]})", R"({"rung": "owner"})");
    t.Eq(g.nodes.size(), size_t(1), "one node");
    t.Ok(g.nodes.size() == 1 && g.nodes[0].type == "Character", "its type as stored");
    t.Ok(g.nodes.size() == 1 && g.nodes[0].family == "blue", "its colour word from the character's");
    OWWorldGraph padded = Read(R"({"elements": [{"id": "c1", "type": " character ", "name": "Ada"}]})", R"({"rung": "public"})");
    t.Ok(padded.nodes.size() == 1 && padded.nodes[0].type == " character ", "a public reader's node, padding kept");
}

OW_TEST(graph_an_edge_to_an_element_the_lens_cut_as_a_wrongly_typed_anchor_reads_the_lenss_reason) {
    // The cut counts each element once, so an edge to an element the lens cut only as a wrongly typed anchor (a Location
    // named under characters) reads the lens's reason, never outside_lens. Kills: the edge's target looked up by its
    // own type and id.
    OWGraphOptions o;
    o.lens = Parse(R"({"id": "L", "type": "construct", "name": "L", "supertype": "Lens", "characters": ["x"], "objects": ["o"]})");
    OWWorldGraph g = Read(R"({"elements": [
        {"id": "o", "type": "object", "name": "Ledger", "location": "x"},
        {"id": "x", "type": "location", "name": "Harbour"}]})", R"({"rung": "owner"})", o);
    using Counts = std::vector<std::pair<std::string, int64_t>>;
    t.Ok(g.nodes.size() == 1 && g.nodes[0].id == "o", "the Ledger alone");
    t.Ok(g.cut == Counts{{"anchor_not_visible", 1}}, "the lens counts the Harbour once");
    t.Ok(g.edges_cut == Counts{{"anchor_not_visible", 1}}, "and the edge to it carries the lens's reason");
}

OW_TEST(graph_below_the_owner_an_edge_to_an_unseen_anchor_a_cut_child_reaches_is_cut_by_the_childs_reason) {
    // Counts are per reader, and an edge with them: below the owner an element whose owner-side reason was
    // anchor_not_visible and which a child cuts by types is cut by the child's reason, so the edge to it is cut by that
    // reason, never left uncounted. Guards what this reader already does. Kills: the unseen anchor recorded for every
    // reader and dropped at the count.
    OWGraphOptions o;
    o.lens = OWJsonValue::String("U");
    OWWorldGraph g = Read(R"({"elements": [
        {"id": "U", "type": "construct", "name": "U", "supertype": "Lens", "characters": ["x"], "constructs": ["C"], "x_lens": {"depth": 0}},
        {"id": "C", "type": "construct", "name": "C", "supertype": "Lens", "characters": ["y"], "x_lens": {"types": ["character"]}},
        {"id": "y", "type": "character", "name": "Ysolde", "birthplace": "x"},
        {"id": "x", "type": "location", "name": "Harbour"}]})", R"({"rung": "contributor"})", o);
    using Counts = std::vector<std::pair<std::string, int64_t>>;
    t.Ok(g.nodes.size() == 1 && g.nodes[0].id == "y", "Ysolde alone");
    t.Ok(g.cut == Counts{{"types", 1}}, "the child's reason");
    t.Ok(g.edges_cut == Counts{{"types", 1}}, "and the edge to the Harbour carries it");
}
