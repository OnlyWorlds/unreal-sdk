// OWLayout::LayoutByKind and LayoutAround on the shared fixture: the pinned dist's profiles/layout.fixture.json (the
// default layouts page, profiles/layout.md, serial 132), read from the vendored folder through OWJson, whose from_chars
// reads every double exactly. by-kind compares exactly, every x and y bit for bit: the method is chaotic, so two runtimes
// agree bit for bit or not at all. around compares within 1e-12: each of its points is one cosine and one sine, and
// runtimes' cosines may differ in the last place. The fixture's third suite, unplaced, is read whole: each case's want,
// the list of ids, equal exactly and in its order (UnplacedByKind, UnplacedAround). Then the npm SDK's other vectors
// (test/layout-by-kind.test.mjs). When the pin moves the fixture: run codegen/generate_layout.py and update the hash.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWLayout.h"
#include "OWPresentation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const FixtureSha256 = "392b5b19b21d5beb1994692f51a8d8df3ef431f93bc279a7fc42ab0b0db508a5";
const double AroundTolerance = 1e-12;

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/layout.fixture.json"; }

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

uint64_t Bits(double v) {
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

const OWJsonValue* Named(const char* list, const std::string& name) {
    const OWJsonValue* l = Fixture()->Find(list);
    if (!l) return nullptr;
    for (const OWJsonValue& c : l->Items())
        if (c.Find("name") && c.Find("name")->Text() == name) return &c;
    return nullptr;
}

std::vector<std::string> Names(const char* list) {
    std::vector<std::string> out;
    if (const OWJsonValue* l = Fixture()->Find(list))
        for (const OWJsonValue& c : l->Items()) out.push_back(c.Find("name")->Text());
    return out;
}

// The number of points that differ from the case's want, bit for bit (-1 when the counts differ or an id is out of order).
int Off(const OWLayoutByKind& got, const OWJsonValue& c) {
    const auto& want = c.Find("want")->Items();
    const auto& nodes = c.Find("graph")->Find("nodes")->Items();
    if (got.points.size() != want.size()) return -1;
    int off = 0;
    for (size_t k = 0; k < want.size(); ++k) {
        if (got.points[k].id != nodes[k].Find("id")->Text()) return -1;
        if (Bits(got.points[k].x) != Bits(want[k].Items()[0].AsDouble()) || Bits(got.points[k].y) != Bits(want[k].Items()[1].AsDouble()))
            ++off;
    }
    return off;
}

void ByKindCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    OWLayoutByKind got = OWLayout::LayoutByKind(*c.Find("graph"));
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, std::string("by-kind"), name + ": the method");
    OW_DEPRECATED_BEGIN
    t.Eq(got.version, 1, name + ": the version (deprecated since 0.7.0)");
    OW_DEPRECATED_END
    t.Eq(Off(got, c), 0, name + ": points that differ from the fixture, bit for bit (-1: counts or order)");
}

void AroundCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    OWLayoutAround got = OWLayout::LayoutAround(*c.Find("graph"), c.Find("focus")->Text(), static_cast<int>(c.Find("steps")->AsDouble()));
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, std::string("around"), name + ": the method");
    OW_DEPRECATED_BEGIN
    t.Eq(got.version, 1, name + ": the version (deprecated since 0.7.0)");
    OW_DEPRECATED_END
    const auto& want = c.Find("want")->Items();
    std::string ids, want_ids;
    for (const auto& p : got.points) ids += p.id + ":" + std::to_string(p.depth) + " ";
    for (const auto& w : want) want_ids += w.Items()[0].Text() + ":" + std::to_string(static_cast<int>(w.Items()[1].AsDouble())) + " ";
    t.Eq(ids, want_ids, name + ": the ids and depths, in the order handed");
    if (ids != want_ids) return;
    for (size_t k = 0; k < want.size(); ++k) {
        const auto& p = got.points[k];
        double wx = want[k].Items()[2].AsDouble(), wy = want[k].Items()[3].AsDouble();
        t.Ok(std::fabs(p.x - wx) <= AroundTolerance && std::fabs(p.y - wy) <= AroundTolerance, name + ": " + p.id + " within 1e-12");
    }
}

// One case of the unplaced suite, by its method word: the want, the list of ids, equal exactly and in its order.
void UnplacedCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    const std::string method = c.Find("method")->Text();
    OWLayoutUnplaced got;
    if (method == "by-kind")
        got = OWLayout::UnplacedByKind(*c.Find("graph"), *c.Find("placed"));
    else if (method == "around")
        got = OWLayout::UnplacedAround(*c.Find("graph"), c.Find("focus")->Text(), *c.Find("placed"), static_cast<int>(c.Find("steps")->AsDouble()));
    else {
        t.Fail(name + ": a method this suite does not hold: " + method);
        return;
    }
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, method, name + ": the method");
    std::string ids, want;
    for (const auto& id : got.unplaced) ids += id + " ";
    for (const auto& w : c.Find("want")->Items()) want += w.Text() + " ";
    t.Eq(ids, want, name + ": the unplaced ids, in their order");
}

OWJsonValue Copy(const OWJsonValue& v) { return v; }

// The npm test's seeded shuffle (its generator, seed 99), so a failure reruns the same.
struct Shuffler {
    uint32_t s = 99;
    double Next() {
        s = s * 1664525u + 1013904223u;
        return s / 4294967296.0;
    }
    OWJsonValue Shuffle(const OWJsonValue& list) {
        OWJsonValue a = list;
        auto& items = a.Items();
        for (size_t i = items.size() - 1; i > 0 && items.size() > 1; --i) {
            size_t j = static_cast<size_t>(std::floor(Next() * static_cast<double>(i + 1)));
            std::swap(items[i], items[j]);
        }
        return a;
    }
};

std::map<std::string, std::string> Keys(const std::vector<OWLayoutPoint>& points) {
    std::map<std::string, std::string> out;
    for (const auto& p : points) out[p.id] = std::to_string(Bits(p.x)) + ":" + std::to_string(Bits(p.y));
    return out;
}

std::map<std::string, std::string> Keys(const std::vector<OWLayoutAroundPoint>& points) {
    std::map<std::string, std::string> out;
    for (const auto& p : points) out[p.id] = std::to_string(p.depth) + ":" + std::to_string(Bits(p.x)) + ":" + std::to_string(Bits(p.y));
    return out;
}

std::vector<std::string> RingByAngle(const OWLayoutAround& around, const std::map<std::string, std::string>& names) {
    const double pi = 3.141592653589793;
    std::vector<std::pair<double, std::string>> ring;
    for (const auto& p : around.points)
        if (p.depth == 1) ring.emplace_back(std::fmod(std::atan2(p.y, p.x) + pi / 2 + 4 * pi, 2 * pi), names.at(p.id));
    std::sort(ring.begin(), ring.end());
    std::vector<std::string> out;
    for (const auto& r : ring) out.push_back(r.second);
    return out;
}

}  // namespace

OW_TEST(layout_fixture_is_the_pinned_dists_bytes_with_seventeen_exact_cases_and_ten_rings) {
    OWBytes bytes;
    t.Ok(OWFiles::Read(FixturePath(), bytes), "the fixture is vendored");
    t.Eq(owtest::Sha256Hex(bytes), std::string(FixtureSha256), "the fixture's bytes are the ones this test pins");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/layout.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == FixtureSha256, "MANIFEST.json records the same sha256");
    const OWJsonValue* compare = Fixture()->Find("compare");
    t.Ok(compare && compare->Keys() == std::vector<std::string>{"cases", "around", "unplaced"}, "the compare block");
    t.Eq(Names("cases").size(), static_cast<size_t>(17), "seventeen exact cases");
    t.Eq(Names("around").size(), static_cast<size_t>(10), "ten rings");
}

OW_TEST(layout_the_anchor_table_is_the_fixtures_anchors_bit_for_bit) {
    const OWJsonValue* anchors = Fixture()->Find("anchors");
    t.Ok(anchors && anchors->Size() == 22, "k = 1 to 22");
    if (!anchors) return;
    std::vector<uint64_t> flat;
    for (int k = 1; k <= 22; ++k) {
        const OWJsonValue* row = anchors->Find(std::to_string(k));
        t.Ok(row && row->Size() == static_cast<size_t>(k), "k = " + std::to_string(k));
        if (!row) return;
        for (const auto& p : row->Items()) {
            flat.push_back(Bits(p.Items()[0].AsDouble()));
            flat.push_back(Bits(p.Items()[1].AsDouble()));
        }
    }
    t.Eq(flat.size(), OWLayout::AnchorCount, "253 points");
    size_t off = 0;
    for (size_t i = 0; i < flat.size() && i < OWLayout::AnchorCount; ++i) off += flat[i] != OWLayout::AnchorBits[i];
    t.Eq(off, static_cast<size_t>(0), "anchors that differ from the fixture's");
}

OW_TEST(layout_the_groups_are_the_fixtures_and_the_presentation_defaults) {
    const OWJsonValue* order = Fixture()->Find("group_order");
    std::vector<std::string> o;
    if (order)
        for (const auto& w : order->Items()) o.push_back(w.Text());
    t.Ok(o == OWPresentation::Order(), "the group order");
    const OWJsonValue* groups = Fixture()->Find("groups");
    t.Ok(groups && groups->Size() == 22, "22 types");
    if (!groups) return;
    for (size_t i = 0; i < groups->Size(); ++i)
        t.Eq(OWPresentation::GroupOf(groups->Keys()[i]), groups->Items()[i].Text(), groups->Keys()[i]);
}

OW_CASES(layout_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Names("cases").empty()) {
        out.emplace_back("layout_fixture:not-found", [](owtest::T& t) { t.Fail("the fixture did not load"); });
        return out;
    }
    for (const std::string& name : Names("cases"))
        out.emplace_back("layout-by-kind:" + name, [doc, name](owtest::T& t) { ByKindCase(t, *Named("cases", name)); });
    for (const std::string& name : Names("around"))
        out.emplace_back("layout-around:" + name, [doc, name](owtest::T& t) { AroundCase(t, *Named("around", name)); });
    for (const std::string& name : Names("unplaced"))
        out.emplace_back("layout-unplaced:" + name, [doc, name](owtest::T& t) { UnplacedCase(t, *Named("unplaced", name)); });
    return out;
}

OW_TEST(layout_unplaced_seventeen_cases_by_kind_and_around_only) {
    const OWJsonValue* l = Fixture()->Find("unplaced");
    size_t byKind = 0, around = 0, other = 0;
    if (l)
        for (const OWJsonValue& c : l->Items()) {
            const std::string m = c.Find("method") ? c.Find("method")->Text() : "";
            (m == "by-kind" ? byKind : m == "around" ? around : other) += 1;
        }
    t.Eq(Names("unplaced").size(), static_cast<size_t>(17), "seventeen unplaced cases");
    t.Ok(byKind > 0 && around > 0 && other == 0, "methods by-kind and around only");
}

OW_TEST(layout_unplaced_refuses_a_placed_that_is_no_list) {
    OWJsonValue g = Parse(R"({"nodes": [{"id": "a", "type": "character"}], "edges": []})");
    for (const char* bad : {"null", "\"a\"", "{\"a\": true}", "7"}) {
        OWJsonValue p = Parse(bad);
        const std::string a = OWLayout::UnplacedByKind(g, p).error, b = OWLayout::UnplacedAround(g, "a", p).error,
                          c = OWLayout::UnplacedByLinks3d(g, p).error;
        t.Ok(a == "UnplacedByKind: placed is a list of element ids", std::string(bad) + ": by-kind: " + a);
        t.Ok(b == "UnplacedAround: placed is a list of element ids", std::string(bad) + ": around: " + b);
        t.Ok(c == "UnplacedByLinks3d: placed is a list of element ids", std::string(bad) + ": by-links-3d: " + c);
    }
    OWJsonValue empty = Parse("[]");
    t.Ok(OWLayout::UnplacedAround(g, "b", empty).error.find("the focus b is not a node of the graph") != std::string::npos, "around: no focus");
    t.Ok(OWLayout::UnplacedAround(g, "a", empty, 3).error.find("UnplacedAround: steps is 1 or 2") != std::string::npos, "around: steps");
    t.Ok(OWLayout::UnplacedByKind(Parse("null"), empty).error.rfind("UnplacedByKind: the graph is { nodes, edges }", 0) == 0, "by-kind: no graph");
    t.Ok(OWLayout::UnplacedByLinks3d(Parse(R"({"nodes": [{"id": "a"}, {"id": "a"}], "edges": []})"), empty).error == "UnplacedByLinks3d: two nodes share the id a", "by-links-3d: an id twice");
    t.Ok(OWLayout::UnplacedByLinks3d(Parse(R"({"nodes": [{"type": "map"}], "edges": []})"), empty).error == "UnplacedByLinks3d: node 0 has no id", "by-links-3d: no id");
    OWLayoutUnplaced ok = OWLayout::UnplacedByKind(g, empty);
    t.Ok(ok.error.empty() && ok.method == "by-kind" && ok.unplaced == std::vector<std::string>{"a"}, "an empty list places nothing");
}

OW_TEST(layout_the_order_nodes_and_links_are_handed_in_moves_no_point) {
    Shuffler sh;
    for (const char* name : {"hyperion", "past-324", "a-self-link-pulls-nothing"}) {
        const OWJsonValue& g = *Named("cases", name)->Find("graph");
        OWJsonValue mixed = OWJsonValue::Object();
        mixed.Set("nodes", sh.Shuffle(*g.Find("nodes")));
        mixed.Set("edges", sh.Shuffle(*g.Find("edges")));
        OWLayoutByKind got = OWLayout::LayoutByKind(mixed);
        std::vector<std::string> ids, handed;
        for (const auto& p : got.points) ids.push_back(p.id);
        for (const auto& x : mixed.Find("nodes")->Items()) handed.push_back(x.Find("id")->Text());
        t.Ok(ids == handed, std::string(name) + ": in the order handed");
        t.Ok(Keys(got.points) == Keys(OWLayout::LayoutByKind(g).points) && !got.points.empty(), std::string(name) + ": the same points");
    }
    const OWJsonValue& c = *Named("around", "two-steps");
    const OWJsonValue& g = *c.Find("graph");
    OWJsonValue mixed = OWJsonValue::Object();
    mixed.Set("nodes", sh.Shuffle(*g.Find("nodes")));
    mixed.Set("edges", sh.Shuffle(*g.Find("edges")));
    OWLayoutAround got = OWLayout::LayoutAround(mixed, c.Find("focus")->Text(), 2);
    std::map<std::string, size_t> order;
    for (size_t k = 0; k < mixed.Find("nodes")->Size(); ++k) order[mixed.Find("nodes")->Items()[k].Find("id")->Text()] = k;
    bool in_order = !got.points.empty();
    for (size_t k = 1; k < got.points.size(); ++k) in_order = in_order && order[got.points[k - 1].id] < order[got.points[k].id];
    t.Ok(in_order, "around: in the order handed");
    t.Ok(Keys(got.points) == Keys(OWLayout::LayoutAround(g, c.Find("focus")->Text(), 2).points), "around: the same points");
}

OW_TEST(layout_a_link_pulls_both_ends_alike) {
    const OWJsonValue& c = *Named("cases", "two-elements-linked");
    OWJsonValue g = *c.Find("graph");
    const OWJsonValue& e = g.Find("edges")->Items()[0];
    OWJsonValue flipped = OWJsonValue::Object();
    flipped.Set("nodes", *g.Find("nodes"));
    flipped.Set("edges", Parse("[{\"source\": \"" + e.Find("target")->Text() + "\", \"target\": \"" + e.Find("source")->Text() + "\", \"field\": \"x\", \"index\": 0}]"));
    OWJsonValue cc = c;
    cc.Set("graph", flipped);
    t.Eq(Off(OWLayout::LayoutByKind(flipped), cc), 0, "its direction moves nothing");
}

OW_TEST(layout_a_frame_of_minus_one_to_one_on_the_longer_axis_centred) {
    for (const char* name : {"hyperion", "moppetopia", "past-324"}) {
        OWLayoutByKind got = OWLayout::LayoutByKind(*Named("cases", name)->Find("graph"));
        if (got.points.empty()) {
            t.Fail(std::string(name) + ": no points");
            continue;
        }
        double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
        for (const auto& p : got.points) {
            x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
        }
        t.Ok(std::fabs(std::max(x1 - x0, y1 - y0) - 2) < 1e-12, std::string(name) + ": the span");
        t.Ok(std::fabs(x1 + x0) < 1e-12 && std::fabs(y1 + y0) < 1e-12, std::string(name) + ": centred");
    }
}

OW_TEST(layout_the_empty_graph_answers_no_points_and_one_element_zero) {
    OWLayoutByKind none = OWLayout::LayoutByKind(Parse(R"({"nodes": [], "edges": []})"));
    OW_DEPRECATED_BEGIN
    t.Ok(none.error.empty() && none.points.empty() && none.method == "by-kind" && none.version == 1, "empty");
    OW_DEPRECATED_END
    OWLayoutByKind one = OWLayout::LayoutByKind(Parse(R"({"nodes": [{"id": "a", "type": "zone"}], "edges": []})"));
    t.Ok(one.error.empty() && one.points.size() == 1 && one.points[0].id == "a" && Bits(one.points[0].x) == 0 && Bits(one.points[0].y) == 0, "one");
}

OW_TEST(layout_a_link_to_an_id_that_is_not_a_node_is_passed_over) {
    const OWJsonValue& c = *Named("cases", "two-elements-linked");
    OWJsonValue g = *c.Find("graph");
    g.Find("edges")->Push(Parse("{\"source\": \"" + g.Find("nodes")->Items()[0].Find("id")->Text() + "\", \"target\": \"not-a-node\", \"field\": \"friends\", \"index\": 0}"));
    t.Eq(Off(OWLayout::LayoutByKind(g), c), 0, "the same points");
}

OW_TEST(layout_by_kind_runs_on_world_graphs_answer_as_it_comes) {
    OWWorldGraph g = OWGraph::WorldGraph(Parse(R"({"elements": [
        {"id": "01a1d4ef-0000-7000-8000-000000000002", "type": "location", "name": "Harbour"},
        {"id": "01a1d4ef-0000-7000-8000-000000000001", "type": "character", "name": "Mara", "birthplace": "01a1d4ef-0000-7000-8000-000000000002"},
        {"id": "01a1d4ef-0000-7000-8000-000000000003", "type": "creature", "name": "Gull", "location": "01a1d4ef-0000-7000-8000-000000000002"}]})"),
        Parse(R"({"rung": "owner"})"));
    t.Eq(g.error, std::string(), "the graph read");
    OWLayoutByKind got = OWLayout::LayoutByKind(g);
    std::vector<std::string> ids, want;
    bool finite = true;
    for (const auto& p : got.points) ids.push_back(p.id), finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
    for (const auto& n : g.nodes) want.push_back(n.id);
    t.Ok(ids == want && finite, "one finite point per node, in the graph's order");
    std::string depths;
    for (const auto& p : OWLayout::LayoutAround(g, "01a1d4ef-0000-7000-8000-000000000002").points) depths += std::to_string(p.depth);
    t.Eq(depths, std::string("101"), "around the harbour");
}

OW_TEST(layout_a_graph_it_cannot_read_is_refused_by_name) {
    auto bad = [&t](const char* graph, const std::string& words) {
        OWJsonValue g = Parse(graph);
        std::string a = OWLayout::LayoutByKind(g).error, b = OWLayout::LayoutAround(g, "a").error;
        t.Ok(a.rfind("LayoutByKind: ", 0) == 0 && a.find(words) != std::string::npos, "LayoutByKind refuses (" + words + "): " + a);
        t.Ok(b.rfind("LayoutAround: ", 0) == 0 && b.find(words) != std::string::npos, "LayoutAround refuses (" + words + "): " + b);
    };
    bad("null", "the graph is { nodes, edges }");
    bad(R"({"nodes": []})", "the graph is { nodes, edges }");
    bad(R"({"nodes": [null], "edges": []})", "node 0 is not an object");
    bad(R"({"nodes": [{"type": "character"}], "edges": []})", "node 0 has no id");
    bad(R"({"nodes": [{"id": "a", "type": "starship"}], "edges": []})", "'starship' is not one of the 22 element types");
    bad(R"({"nodes": [{"id": "a", "type": "toString"}], "edges": []})", "'toString' is not one of the 22 element types");
    bad(R"({"nodes": [{"id": "a", "type": "character", "name": 7}], "edges": []})", "name is a string or null");
    bad(R"({"nodes": [{"id": "a", "type": "character"}, {"id": "a", "type": "location"}], "edges": []})", "the id a is a node twice");
    bad(R"({"nodes": [{"id": "a", "type": "character"}], "edges": [{"source": "a"}]})", "edge 0 is not { source, target }");
    bad(R"({"nodes": [{"id": "a", "type": "character"}], "edges": [{"source": "a", "target": "a", "field": 3}]})", "edge 0: field is a string");
    bad(R"({"nodes": [{"id": "a", "type": "character"}], "edges": [{"source": "a", "target": "a", "field": null}]})", "edge 0: field is a string");
    bad(R"({"nodes": [{"id": "a", "type": "character"}], "edges": [{"source": "a", "target": "a", "index": "0"}]})", "edge 0: index is a number");
    bad(R"({"nodes": [{"id": "a", "type": "character"}], "edges": [{"source": "a", "target": "a", "index": null}]})", "edge 0: index is a number");
}

OW_TEST(layout_around_steps_is_one_or_two_and_the_focus_is_a_node) {
    OWJsonValue g = Parse(R"({"nodes": [{"id": "a", "type": "character"}], "edges": []})");
    for (int steps : {0, 3, -1})
        t.Ok(OWLayout::LayoutAround(g, "a", steps).error.find("LayoutAround: steps is 1 or 2") != std::string::npos, "steps " + std::to_string(steps));
    t.Ok(OWLayout::LayoutAround(Parse(R"({"nodes": [], "edges": []})"), "a").error.find("the focus a is not a node of the graph") != std::string::npos, "no node a");
    t.Ok(!OWLayout::LayoutAround(g, "b").error.empty(), "no node b");
    t.Eq(OWLayout::LayoutAround(g, "a").points.size(), static_cast<size_t>(1), "the focus alone");
}

OW_TEST(layout_around_one_step_answers_the_neighbours_only_two_add_theirs) {
    const OWJsonValue& one = *Named("around", "one-step-of-the-same");
    const OWJsonValue& two = *Named("around", "two-steps");
    auto d1 = OWLayout::LayoutAround(*one.Find("graph"), one.Find("focus")->Text()).points;
    auto d2 = OWLayout::LayoutAround(*two.Find("graph"), two.Find("focus")->Text(), 2).points;
    int focus = 0;
    bool shallow = true, deep = false;
    for (const auto& p : d1) shallow = shallow && p.depth <= 1, focus += p.depth == 0;
    t.Ok(shallow && focus == 1 && d1.size() > 1, "one step");
    std::vector<OWLayoutAroundPoint> ring;
    for (const auto& p : d2) {
        deep = deep || p.depth == 2;
        if (p.depth < 2) ring.push_back(p);
    }
    t.Ok(deep, "two steps reach further");
    t.Ok(Keys(ring) == Keys(d1), "the first ring is the same either way");
}

OW_TEST(layout_around_names_order_by_code_point_never_by_locale) {
    const OWJsonValue& c = *Named("around", "names-by-code-point-not-locale");
    std::map<std::string, std::string> names;
    for (const auto& x : c.Find("graph")->Find("nodes")->Items())
        names[x.Find("id")->Text()] = x.Find("name") && x.Find("name")->IsString() ? x.Find("name")->Text() : "";
    auto ring = RingByAngle(OWLayout::LayoutAround(*c.Find("graph"), c.Find("focus")->Text()), names);
    t.Ok(ring == std::vector<std::string>{"Bea", "Zed", "adam", "bea", "\xC3\x89mile", "\xC3\x98yvind", "\xF0\x9D\x90\x80lpha"}, "by code point");
}

// U+FF5A (one UTF-16 code unit, 0xFF5A) sorts before U+1D400 (a surrogate pair, 0xD835 0xDC00) by code point, after it by
// UTF-16 code unit. The fixture's names-by-code-point-not-utf-16 case sets the two orders apart as well (serial 132);
// this pair is the smallest that does.
OW_TEST(layout_around_names_order_by_code_point_not_by_utf16_code_unit) {
    const std::string astral = "\xF0\x9D\x90\x80", wide = "\xEF\xBD\x9A";
    OWJsonValue g = Parse("{\"nodes\": [{\"id\": \"f\", \"type\": \"character\"}, {\"id\": \"p\", \"type\": \"character\", \"name\": \"" + astral +
                          "\"}, {\"id\": \"q\", \"type\": \"character\", \"name\": \"" + wide +
                          "\"}], \"edges\": [{\"source\": \"f\", \"target\": \"p\", \"field\": \"friends\", \"index\": 0}, {\"source\": \"f\", \"target\": \"q\", \"field\": \"friends\", \"index\": 1}]}");
    auto ring = RingByAngle(OWLayout::LayoutAround(g, "f"), {{"p", astral}, {"q", wide}});
    t.Ok(ring == std::vector<std::string>{wide, astral}, "U+FF5A first");
}

OW_TEST(layout_min_and_max_are_javascripts) {
    t.Ok(std::signbit(OWLayout::MinJs(0.0, -0.0)) && std::signbit(OWLayout::MinJs(-0.0, 0.0)), "min(+0, -0) is -0, either way round");
    t.Ok(!std::signbit(OWLayout::MaxJs(0.0, -0.0)) && !std::signbit(OWLayout::MaxJs(-0.0, 0.0)), "max(+0, -0) is +0, either way round");
    t.Ok(std::isnan(OWLayout::MinJs(NAN, 1)) && std::isnan(OWLayout::MaxJs(1, NAN)), "NaN if either is");
    t.Eq(OWLayout::MinJs(1, 2), 1.0, "min");
    t.Eq(OWLayout::MaxJs(1, 2), 2.0, "max");
}
