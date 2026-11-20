#include "OWGraph.h"

#include "OWLensView.h"
#include "OWPresentation.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

// keel's graph reader for the profile (tests/test_lens_profile.py, world_graph) over the lens reader's own view
// (OWLensView, from OWLens.cpp). Where a line mirrors one of the profile's rules or one of keel's readings, the comment
// says which. The Godot SDK's ow_graph.gd is the closest port in shape.

namespace OnlyWorlds {
namespace OWGraph {

namespace {

const std::vector<std::string> Declared = {"x_lens_leads_to", "x_spine_depends_on"};
const std::vector<std::string> FlagWords = {"rooms", "hidden", "proposed", "map_space", "reading_aids"};
const std::vector<std::string> OwnerFlags = {"hidden", "proposed"};
const std::vector<std::string> Reasons = {"room",   "hidden", "proposed", "map_space",   "reading_aid",
                                          "not_visible", "outside_lens", "budget", "types", "window",
                                          "stale",  "undated", "after_as_of", "anchor_not_visible", "hops_not_served"};
// The owner's two counts (the owner reading as another keeps them), and a keeper's two: the owner's or a co-builder's
// (serial 140; the owner's alone before).
const std::vector<std::string> OwnerReasons = {"not_visible", "anchor_not_visible"};
const std::vector<std::string> KeeperReasons = {"hidden", "proposed"};
// Map space: out by default for size, not for standing, so a node it brings back carries no mark. A Zone is content
// that gains spatial presence through its Markers, so it is a node by default (serial 92).
const std::vector<std::string> MapSpace = {"map", "pin", "marker"};

bool Has(const std::vector<std::string>& list, std::string_view s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

OWJsonValue Counts(const std::vector<std::pair<std::string, int64_t>>& cut) {
    OWJsonValue out = OWJsonValue::Object();
    for (const auto& [reason, n] : cut) out.Set(reason, OWJsonValue::Number(n));
    return out;
}

// Counts in the reasons' one order, a reason with no count left out, the owner's reasons kept for the owner only and
// the hidden and proposed counts for a keeper.
std::vector<std::pair<std::string, int64_t>> Tally(const std::map<std::string, int64_t>& counts, bool owner, bool keeper) {
    std::vector<std::pair<std::string, int64_t>> out;
    for (const std::string& why : Reasons) {
        auto it = counts.find(why);
        if (it == counts.end() || it->second <= 0) continue;
        if (!owner && Has(OwnerReasons, why)) continue;
        if (!keeper && Has(KeeperReasons, why)) continue;
        out.emplace_back(why, it->second);
    }
    return out;
}

}  // namespace

const std::vector<std::string>& DeclaredLinks() { return Declared; }
const std::vector<std::string>& MapSpaceTypes() { return MapSpace; }
const std::vector<std::string>& Flags() { return FlagWords; }
const std::vector<std::string>& OwnerOnlyFlags() { return OwnerFlags; }
const std::vector<std::string>& EdgeReasons() { return Reasons; }
const std::vector<std::string>& OwnerOnlyEdgeReasons() { return OwnerReasons; }
const std::vector<std::string>& KeeperOnlyEdgeReasons() { return KeeperReasons; }

OWWorldGraph WorldGraph(const OWJsonValue& world, const OWJsonValue& reader, const OWGraphOptions& options) {
    OWWorldGraph g;
    if (std::string bad = OWLensView::CheckReader(reader, "world_graph: reader"); !bad.empty()) {
        g.error = bad;
        return g;
    }
    // Refused by name before anything is read, judged by the rung read as (the owner as another is that rung): a
    // keeper's two flags (the owner's or a co-builder's, serial 140) from any rung below, then any flag from a public
    // reading; the first unmet in the order hidden,
    // proposed, rooms, map_space, reading_aids (any signed reader may ask the last, serial 104).
    const OWJsonValue* as = reader.Find("as");
    const OWJsonValue& seen = reader.Find("rung")->Text() == "owner" && as && as->IsObject() ? *as : reader;
    const std::string rung = seen.Find("rung")->Text();
    const std::map<std::string, bool> asked = {{"rooms", options.rooms},         {"hidden", options.hidden},
                                               {"proposed", options.proposed},   {"map_space", options.map_space},
                                               {"reading_aids", options.reading_aids}};
    for (const char* flag : {"hidden", "proposed", "rooms", "map_space", "reading_aids"}) {
        bool owners = Has(OwnerFlags, flag);
        const bool keeper = rung == "owner" || rung == "co-builder";
        if (asked.at(flag) && ((owners && !keeper) || rung == "public")) {
            g.refused = flag;
            return g;
        }
    }

    const bool lensed = !options.lens.IsNull();
    OWLensOptions lo;
    lo.as_of = options.as_of;
    lo.clock = options.clock;
    OWLensView::Sight r;
    if (std::string bad = OWLensView::See(world, reader, lensed ? &options.lens : nullptr, lo, "world_graph", r);
        !bad.empty()) {
        g.error = bad;
        return g;
    }
    // A keeper (the owner, or a co-builder; serial 140) is served the lists as stored, and its unlensed view holds the
    // proposed elements, out by default. The hidden and proposed counts are a keeper's, and the counts of a reader that
    // keeps the owner's (the owner reading as another).
    const bool seen_keeper = r.seen_rung == "owner" || r.seen_rung == "co-builder";
    const bool keeper_counts = seen_keeper || r.owner_counts;
    const size_t n = r.els.size();

    // The kinds out by default that apply to an element, each by its reason word (a flag's word but rooms -> room and
    // reading_aids -> reading_aid).
    auto kinds = [&](size_t i) {
        std::vector<std::string> out;
        if (r.room[i]) out.push_back("room");
        if (r.hidden[i]) out.push_back("hidden");
        if (r.proposed[i]) out.push_back("proposed");
        if (Has(MapSpace, r.types[i])) out.push_back("map_space");
        if (r.aid[i]) out.push_back("reading_aid");
        return out;
    };
    // An unlensed graph's candidates: the reader's view, and for a keeper (by the rung read as) what is proposed.
    auto candidate = [&](size_t i) { return r.view[i] || (seen_keeper && r.proposed[i]); };
    // Out by the first kind its flag does not bring back.
    auto flag_of = [](const std::string& k) -> std::string {
        if (k == "room") return "rooms";
        if (k == "reading_aid") return "reading_aids";
        return k;
    };
    auto out_by = [&](size_t i) -> std::string {
        for (const std::string& k : kinds(i))
            if (!asked.at(flag_of(k))) return k;
        return std::string();
    };

    std::vector<char> node(n, 0);
    std::function<std::string(size_t)> reason_of;
    if (lensed) {
        // The nodes are exactly the lens's kept, its cut rides along unchanged, and no flag adds to them.
        for (const std::string& id : r.kept) node[r.by_id.at(id)] = 1;
        g.cut = r.cut;
        g.unreadable = r.unreadable;
        // A target the lens cut is counted by the lens's own reason (it wins over not_visible: the profile's "Edges
        // cut"); one the reader sees that the lens never reached is outside_lens; else not_visible.
        reason_of = [&](size_t j) -> std::string {
            auto it = r.cut_by.find({r.types[j], r.ids[j]});
            if (it != r.cut_by.end()) return it->second;
            return r.view[j] ? "outside_lens" : "not_visible";
        };
    } else {
        std::map<std::string, int64_t> counts;
        for (size_t i = 0; i < n; ++i) {
            if (!candidate(i)) continue;
            std::string why = out_by(i);
            if (why.empty()) node[i] = 1;
            else ++counts[why];
        }
        g.cut = Tally(counts, r.owner_counts, keeper_counts);
        reason_of = [&](size_t j) { return candidate(j) ? out_by(j) : std::string("not_visible"); };
    }

    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i)
        if (node[i]) order.push_back(i);
    // By id, by code point: a UTF-8 id's bytes compare in code point order.
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return r.ids[a] < r.ids[b]; });
    for (size_t i : order) {
        OWGraphNode out;
        out.id = r.ids[i];
        // The type as stored, never folded (a non-slug type is the lint's to flag); the folded slug finds its colour.
        const OWJsonValue* stored = r.els[i]->Find("type");
        out.type = stored && stored->IsString() ? stored->Text() : r.types[i];
        const OWJsonValue* name = r.els[i]->Find("name");
        out.name = name ? *name : OWJsonValue::Null();
        out.family = OWPresentation::FamilyOf(r.types[i]);
        out.icon = OWPresentation::IconOf(r.types[i]);
        // Every mark that applies, lens-kept or not: a reading aid a lens keeps carries reading_aid as a room element
        // a lens keeps carries room (serial 104).
        for (const std::string& k : kinds(i))
            if (k != "map_space") out.marks.push_back(k);
        out.known_of = r.stub[i] != 0;
        g.nodes.push_back(std::move(out));
    }

    std::map<std::string, int64_t> cut_edges;
    auto visit = [&](size_t source, const std::string& field, std::optional<size_t> target, int64_t index,
                     std::set<size_t>& met) {
        if (!target) return;  // a dangling link, not a visibility fact: not counted
        // A multi-link names each element once: a repeat is read once, in its first place, an edge or a cut alike
        // (schema-dist serial 128). An element is found by its id as written, so two spellings are two elements.
        if (!met.insert(*target).second) return;
        if (node[*target]) {
            g.edges.push_back({r.ids[source], field, r.ids[*target], index});
            return;
        }
        std::string why = reason_of(*target);
        if (!why.empty()) ++cut_edges[why];
    };
    for (size_t source : order) {
        if (r.stub[source]) continue;  // a stub carries no links
        const OWJsonValue& el = *r.els[source];
        // A schema link (keel's link registry: the single and list links, a Pin's generic pair apart), typed by its
        // field's target as the lens reader keys it. A keeper reading as itself is served the list as stored; any
        // other reader the list as keel serves it (less what it hides; a proposed id kept below the public rung), so a
        // hidden target is never a gap.
        for (const OWLens::OWLensField& f : OWLens::FieldsOf(r.types[source])) {
            if (f.kind != OWLens::OWFieldKind::Single && f.kind != OWLens::OWFieldKind::Multi) continue;
            const OWJsonValue* v = el.Find(f.name);
            std::vector<const OWJsonValue*> list;
            if (f.kind == OWLens::OWFieldKind::Multi) {
                if (v && v->IsArray())
                    for (const OWJsonValue& t : v->Items()) list.push_back(&t);
            } else if (v) {
                list.push_back(v);
            }
            int64_t served = 0;
            std::set<size_t> met;
            for (size_t i = 0; i < list.size(); ++i) {
                std::optional<size_t> t = list[i]->IsString() ? r.Find(f.target, list[i]->Text()) : std::nullopt;
                visit(source, f.name, t, seen_keeper ? int64_t(i) : served, met);
                // As served: a proposed id below the public rung too, and a repeat, so an index points into the list
                // as served.
                if (seen_keeper || (t && r.scoped[*t])) ++served;
            }
        }
        // A declared extension link names any element by id, is served as stored to every reader, and names each
        // element once as a multi-link does.
        for (const std::string& field : Declared) {
            const OWJsonValue* v = el.Find(field);
            if (!v || !v->IsArray()) continue;
            std::set<size_t> met;
            for (size_t i = 0; i < v->Size(); ++i) {
                const OWJsonValue& t = v->Items()[i];
                std::optional<size_t> j;
                if (t.IsString())
                    if (auto it = r.by_id.find(t.Text()); it != r.by_id.end()) j = it->second;
                visit(source, field, j, int64_t(i), met);
            }
        }
    }
    std::sort(g.edges.begin(), g.edges.end(), [](const OWGraphEdge& a, const OWGraphEdge& b) {
        if (a.source != b.source) return a.source < b.source;
        if (a.field != b.field) return a.field < b.field;
        return a.index < b.index;
    });
    g.edges_cut = Tally(cut_edges, r.owner_counts, keeper_counts);
    return g;
}

std::vector<std::pair<std::string, int64_t>> DegreeOf(const OWWorldGraph& graph) {
    std::vector<std::pair<std::string, int64_t>> out;
    std::map<std::string, size_t> at;
    for (const OWGraphNode& n : graph.nodes)
        if (at.emplace(n.id, out.size()).second) out.emplace_back(n.id, 0);
    for (const OWGraphEdge& e : graph.edges) {
        if (auto s = at.find(e.source); s != at.end()) ++out[s->second].second;
        if (e.target == e.source) continue;  // a link to itself counts once
        if (auto t = at.find(e.target); t != at.end()) ++out[t->second].second;
    }
    return out;
}

std::vector<std::pair<std::string, int64_t>> OutDegreeOf(const OWWorldGraph& graph) {
    std::vector<std::pair<std::string, int64_t>> out;
    std::map<std::string, size_t> at;
    for (const OWGraphNode& n : graph.nodes)
        if (at.emplace(n.id, out.size()).second) out.emplace_back(n.id, 0);
    std::set<std::pair<std::string, std::string>> seen;
    for (const OWGraphEdge& e : graph.edges) {
        auto s = at.find(e.source);
        if (s == at.end() || e.target == e.source || !seen.emplace(e.source, e.target).second) continue;
        ++out[s->second].second;
    }
    return out;
}

}  // namespace OWGraph

OWJsonValue OWWorldGraph::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    if (!refused.empty()) {
        out.Set("refused", OWJsonValue::String(refused));
        return out;
    }
    OWJsonValue ns = OWJsonValue::Array();
    for (const OWGraphNode& n : nodes) {
        OWJsonValue node = OWJsonValue::Object();
        node.Set("id", OWJsonValue::String(n.id));
        node.Set("type", OWJsonValue::String(n.type));
        node.Set("name", n.name);
        node.Set("family", OWJsonValue::String(n.family));
        node.Set("icon", OWJsonValue::String(n.icon));
        if (!n.marks.empty()) {
            OWJsonValue marks = OWJsonValue::Array();
            for (const std::string& m : n.marks) marks.Push(OWJsonValue::String(m));
            node.Set("marks", std::move(marks));
        }
        if (n.known_of) node.Set("known_of", OWJsonValue::Bool(true));
        ns.Push(std::move(node));
    }
    out.Set("nodes", std::move(ns));
    OWJsonValue es = OWJsonValue::Array();
    for (const OWGraphEdge& e : edges) {
        OWJsonValue edge = OWJsonValue::Object();
        edge.Set("source", OWJsonValue::String(e.source));
        edge.Set("field", OWJsonValue::String(e.field));
        edge.Set("target", OWJsonValue::String(e.target));
        edge.Set("index", OWJsonValue::Number(e.index));
        es.Push(std::move(edge));
    }
    out.Set("edges", std::move(es));
    out.Set("edges_cut", OWGraph::Counts(edges_cut));
    out.Set("cut", OWGraph::Counts(cut));
    out.Set("unreadable", unreadable ? OWJsonValue::String(*unreadable) : OWJsonValue::Null());
    return out;
}

}  // namespace OnlyWorlds
