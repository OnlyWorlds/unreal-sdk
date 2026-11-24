#include "OWLanding.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

// The landing profile's reader, as the npm SDK's landingOf (src/landing.ts) and the Python package's landing_of read
// it: degree from OWGraph::DegreeOf, each head's own walk to its reach, and each node's least (hops, rank) over every
// walk, so no queue order ever decides a tie. Ids compare as UTF-8 bytes, which is code point order.

namespace OnlyWorlds {

namespace {

OWJsonValue Int(int64_t n) { return OWJsonValue::Number(n); }
OWJsonValue Text(const std::string& s) { return OWJsonValue::String(s); }

// A whole number as the packages read one: an integral number (2.0 as 2, as JavaScript cannot tell them apart); one
// past 64 bits reads as the largest (every option is a count or a reach, and the packages take 1e30 too); anything
// else is none.
std::optional<int64_t> Whole(const OWJsonValue& v) {
    if (!v.IsNumber()) return std::nullopt;
    double d = v.AsDouble();
    if (!std::isfinite(d) || std::floor(d) != d) return std::nullopt;
    if (d >= 9223372036854775808.0) return INT64_MAX;
    if (d < -9223372036854775808.0) return INT64_MIN;
    return int64_t(d);
}

}  // namespace

OWJsonValue OWWorldLanding::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    if (!refused.empty()) {
        out.Set("refused", Text(refused));
        return out;
    }
    out.Set("mode", Text(mode));
    out.Set("size", Int(size));
    OWJsonValue cs = OWJsonValue::Array();
    for (const OWLandingCluster& c : clusters) {
        OWJsonValue members = OWJsonValue::Array();
        for (const OWLandingMember& m : c.members) {
            OWJsonValue member = OWJsonValue::Object();
            member.Set("id", Text(m.id));
            member.Set("hops", Int(m.hops));
            member.Set("degree", Int(m.degree));
            members.Push(std::move(member));
        }
        OWJsonValue cluster = OWJsonValue::Object();
        cluster.Set("head", Text(c.head));
        cluster.Set("members", std::move(members));
        cs.Push(std::move(cluster));
    }
    out.Set("clusters", std::move(cs));
    OWJsonValue un = OWJsonValue::Array();
    for (const OWLandingNode& n : unclustered) {
        OWJsonValue node = OWJsonValue::Object();
        node.Set("id", Text(n.id));
        node.Set("degree", Int(n.degree));
        un.Push(std::move(node));
    }
    out.Set("unclustered", std::move(un));
    OWJsonValue gs = OWJsonValue::Array();
    for (const OWLandingGap& g : gaps) {
        OWJsonValue gap = OWJsonValue::Object();
        gap.Set("kind", Text(g.kind));
        gap.Set("id", Text(g.id));
        gs.Push(std::move(gap));
    }
    out.Set("gaps", std::move(gs));
    return out;
}

namespace OWLanding {

const std::vector<std::string>& OptionKeys() {
    static const std::vector<std::string> keys = {"heads", "hops", "size"};
    return keys;
}

const std::vector<std::string>& GapKinds() {
    static const std::vector<std::string> kinds = {"orphan", "near_orphan", "no_place", "nobody_here"};
    return kinds;
}

OWWorldLanding LandingOf(const OWWorldGraph& graph, const OWLandingOptions& options) {
    OWWorldLanding out;
    // Rule 0, the options: each a whole number of 1 or more, refused by name in the order heads, hops, size.
    const std::pair<const char*, const std::optional<int64_t>*> given[] = {
        {"heads", &options.heads}, {"hops", &options.hops}, {"size", &options.size}};
    for (const auto& [key, v] : given)
        if (*v && **v < 1) {
            out.refused = key;
            return out;
        }
    const int64_t heads = options.heads.value_or(DefaultHeads);
    const int64_t reach = options.hops.value_or(DefaultHops);
    const int64_t size = options.size.value_or(DefaultSize);

    // Degree (the lens page's): every edge once at each node it touches, a link to itself once.
    std::vector<std::pair<std::string, int64_t>> degrees = OWGraph::DegreeOf(graph);
    std::map<std::string, int64_t> degree(degrees.begin(), degrees.end());
    auto deg = [&](const std::string& id) {
        auto it = degree.find(id);
        return it == degree.end() ? int64_t(0) : it->second;
    };
    std::map<std::string, std::string> type;  // a node's type as stored, the first node of an id
    for (const OWGraphNode& n : graph.nodes) type.emplace(n.id, n.type);
    // The walk's steps: an edge either way between two nodes, a mirror pair one step, a link to itself none.
    std::map<std::string, std::set<std::string>> near;
    for (const OWGraphNode& n : graph.nodes) near[n.id];
    // A Character's `location` edge to a Location node: the Character placed, the Location someone's.
    std::set<std::string> placed, inhabited;
    for (const OWGraphEdge& e : graph.edges) {
        auto s = type.find(e.source), t = type.find(e.target);
        if (s == type.end() || t == type.end()) continue;
        if (e.source != e.target) {
            near[e.source].insert(e.target);
            near[e.target].insert(e.source);
        }
        if (e.field == "location" && s->second == "character" && t->second == "location") {
            placed.insert(e.source);
            inhabited.insert(e.target);
        }
    }

    // Rule 2, the heads: the Characters with an edge, never a known-of node, most linked first, ties by id.
    std::vector<std::string> ranked;
    std::set<std::string> taken;
    for (const OWGraphNode& n : graph.nodes)
        if (n.type == "character" && !n.known_of && deg(n.id) > 0 && taken.insert(n.id).second) ranked.push_back(n.id);
    std::sort(ranked.begin(), ranked.end(), [&](const std::string& a, const std::string& b) {
        if (deg(a) != deg(b)) return deg(a) > deg(b);
        return a < b;
    });
    if (int64_t(ranked.size()) > heads) ranked.resize(size_t(heads));

    // Rule 3, the clusters: each head's own walk to its reach, then each node's least (hops, rank) over every walk.
    std::map<std::string, std::pair<int64_t, size_t>> best;
    for (size_t rank = 0; rank < ranked.size(); ++rank) {
        std::map<std::string, int64_t> seen = {{ranked[rank], 0}};
        std::vector<std::string> frontier = {ranked[rank]};
        for (int64_t step = 0; step < reach && !frontier.empty(); ++step) {
            std::vector<std::string> next;
            for (const std::string& id : frontier)
                for (const std::string& nb : near[id])
                    if (seen.emplace(nb, step + 1).second) next.push_back(nb);
            frontier = std::move(next);
        }
        for (const auto& [id, hops] : seen) {
            auto b = best.find(id);
            if (b == best.end() || std::make_pair(hops, rank) < b->second) best[id] = {hops, rank};
        }
    }
    for (const std::string& head : ranked) out.clusters.push_back({head, {}});
    std::set<std::string> homed;
    for (const OWGraphNode& n : graph.nodes) {
        if (!homed.insert(n.id).second) continue;
        auto b = best.find(n.id);
        if (b != best.end())
            out.clusters[b->second.second].members.push_back({n.id, b->second.first, deg(n.id)});
        else
            out.unclustered.push_back({n.id, deg(n.id)});
    }
    for (OWLandingCluster& c : out.clusters)
        std::sort(c.members.begin(), c.members.end(), [](const OWLandingMember& a, const OWLandingMember& b) {
            if (a.hops != b.hops) return a.hops < b.hops;
            if (a.degree != b.degree) return a.degree > b.degree;
            return a.id < b.id;
        });
    // Rule 4, the rest.
    std::sort(out.unclustered.begin(), out.unclustered.end(), [](const OWLandingNode& a, const OWLandingNode& b) {
        if (a.degree != b.degree) return a.degree > b.degree;
        return a.id < b.id;
    });

    // Rule 5, the gaps: never a known-of node; orphan and near_orphan by degree 0 and 1, as the npm SDK and the Python
    // package read the links report's.
    std::set<std::string> gapped;
    for (const OWGraphNode& n : graph.nodes) {
        if (n.known_of || !gapped.insert(n.id).second) continue;
        const int64_t d = deg(n.id);
        if (d == 0) out.gaps.push_back({"orphan", n.id});
        if (d == 1) out.gaps.push_back({"near_orphan", n.id});
        if (n.type == "character" && !placed.count(n.id)) out.gaps.push_back({"no_place", n.id});
        if (n.type == "location" && !inhabited.count(n.id)) out.gaps.push_back({"nobody_here", n.id});
    }
    auto order = [](const std::string& kind) {
        const std::vector<std::string>& kinds = GapKinds();
        return std::find(kinds.begin(), kinds.end(), kind) - kinds.begin();
    };
    std::sort(out.gaps.begin(), out.gaps.end(), [&](const OWLandingGap& a, const OWLandingGap& b) {
        if (a.kind != b.kind) return order(a.kind) < order(b.kind);
        return a.id < b.id;
    });

    // Rule 1, the mode.
    out.size = size;
    out.mode = int64_t(graph.nodes.size()) < size ? "gaps" : "graph";
    return out;
}

OWWorldLanding LandingOf(const OWWorldGraph& graph, const OWJsonValue& options) {
    OWWorldLanding out;
    if (!options.IsNull() && !options.IsObject()) {
        out.error = "landing_of: options is an object, such as {\"heads\": 5}, or null";
        return out;
    }
    OWLandingOptions typed;
    if (options.IsObject()) {
        // An unknown key is refused by name before anything is read.
        for (const std::string& key : options.Keys())
            if (std::find(OptionKeys().begin(), OptionKeys().end(), key) == OptionKeys().end()) {
                out.refused = key;
                return out;
            }
        const std::pair<const char*, std::optional<int64_t>*> into[] = {
            {"heads", &typed.heads}, {"hops", &typed.hops}, {"size", &typed.size}};
        for (const auto& [key, slot] : into) {
            const OWJsonValue* v = options.Find(key);
            if (!v || v->IsNull()) continue;  // null takes the default, as in worldGraph
            std::optional<int64_t> n = Whole(*v);
            if (!n || *n < 1) {
                out.refused = key;
                return out;
            }
            *slot = n;
        }
    }
    return LandingOf(graph, typed);
}

namespace {
constexpr int64_t MaxSafe = 9007199254740991;  // 2^53 - 1: Number.MAX_SAFE_INTEGER
}  // namespace

OWMostLinked MostLinkedOf(const OWWorldGraph& graph, const OWMostLinkedOptions& options) {
    OWMostLinked out;
    if (options.per_type && (*options.per_type < 1 || *options.per_type > MaxSafe)) {
        out.refused = "per_type";
        return out;
    }
    const int64_t per = options.per_type.value_or(DefaultPerType);
    out.per_type = per;

    std::vector<std::pair<std::string, int64_t>> degrees = OWGraph::DegreeOf(graph);
    std::map<std::string, int64_t> degree(degrees.begin(), degrees.end());
    std::map<std::string, std::vector<OWLinked>> by_type;  // ordered by the type's bytes: code point order
    for (const OWGraphNode& n : graph.nodes) {
        auto d = degree.find(n.id);
        if (d == degree.end() || d->second == 0 || n.known_of) continue;
        std::optional<std::string> name;
        if (n.name.IsString()) name = n.name.Text();
        by_type[n.type].push_back({n.id, std::move(name), d->second});
    }
    for (auto& [type, list] : by_type) {
        // Stable, as JavaScript's sort is: two nodes of one id and degree keep the graph's order.
        std::stable_sort(list.begin(), list.end(), [](const OWLinked& a, const OWLinked& b) {
            if (a.degree != b.degree) return a.degree > b.degree;
            return a.id < b.id;
        });
        if (int64_t(list.size()) > per) list.resize(size_t(per));
        out.by_type.emplace_back(type, std::move(list));
    }
    return out;
}

OWMostLinked MostLinkedOf(const OWWorldGraph& graph, const OWJsonValue& options) {
    OWMostLinked out;
    if (!options.IsNull() && !options.IsObject()) {
        out.error = "most_linked_of: options is an object, such as {\"per_type\": 3}, or null";
        return out;
    }
    OWMostLinkedOptions typed;
    if (options.IsObject()) {
        for (const std::string& key : options.Keys())
            if (key != "per_type") {
                out.refused = key;
                return out;
            }
        const OWJsonValue* v = options.Find("per_type");
        if (v && !v->IsNull()) {
            const double d = v->IsNumber() ? v->AsDouble() : 0.0;
            if (!v->IsNumber() || !std::isfinite(d) || std::floor(d) != d || d < 1 || d > double(MaxSafe)) {
                out.refused = "per_type";
                return out;
            }
            typed.per_type = int64_t(d);
        }
    }
    return MostLinkedOf(graph, typed);
}

}  // namespace OWLanding

OWJsonValue OWMostLinked::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    if (!refused.empty()) {
        out.Set("refused", Text(refused));
        return out;
    }
    out.Set("per_type", Int(per_type));
    OWJsonValue types = OWJsonValue::Object();
    for (const auto& [type, list] : by_type) {
        OWJsonValue entries = OWJsonValue::Array();
        for (const OWLinked& e : list) {
            OWJsonValue entry = OWJsonValue::Object();
            entry.Set("id", Text(e.id));
            entry.Set("name", e.name ? Text(*e.name) : OWJsonValue::Null());
            entry.Set("degree", Int(e.degree));
            entries.Push(std::move(entry));
        }
        types.Set(type, std::move(entries));
    }
    out.Set("by_type", std::move(types));
    return out;
}
}  // namespace OnlyWorlds
