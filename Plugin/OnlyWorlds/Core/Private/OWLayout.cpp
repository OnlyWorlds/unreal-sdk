// OWLayout: the npm SDK's layout-by-kind.ts (4.167.0), operation for operation. Every product and sum is its own
// statement or its own parenthesised term, in the npm SDK's order; no reordering by hand, and none by the compiler (the
// core builds with /fp:precise; the plugin's module asks for precise semantics, OnlyWorlds.Build.cs).

#include "OWLayout.h"

#include "OWPresentation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace OnlyWorlds {
namespace OWLayout {

namespace {

constexpr int Steps = 320;
constexpr int CellMax = 12;
constexpr int RingOne = 60;
constexpr double Pi = 3.141592653589793;

struct Node {
    std::string id;
    std::string type;
    std::string name;  // "" for none
    int rank = 0;
};

struct Edge {
    std::string source;
    std::string target;
    std::string field;  // "" for none
    double index = 0;
};

struct Ordered {
    std::string error;
    std::vector<Node> handed;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::unordered_map<std::string, int> index;
};

int Rank(const std::string& type) {
    const std::string family = OWPresentation::FamilyOf(type);
    const auto& order = OWPresentation::Order();
    return static_cast<int>(std::find(order.begin(), order.end(), family) - order.begin());
}

bool IsType(const std::string& type) { return !OWPresentation::FamilyOf(type).empty(); }

// Check the graph, then put it in the method's one order, whatever order the caller handed: the nodes by id, the edges
// by source, then field (none as ""), then index (none as 0), then target, every word in code point order (a UTF-8
// string's bytes compare in code point order).
Ordered ReadGraph(const char* fn, const OWJsonValue& graph) {
    Ordered g;
    auto fail = [&g, fn](const std::string& msg) {
        g.error = std::string(fn) + ": " + msg;
        return g;
    };
    const OWJsonValue* nodes = graph.IsObject() ? graph.Find("nodes") : nullptr;
    const OWJsonValue* edges = graph.IsObject() ? graph.Find("edges") : nullptr;
    if (!nodes || !nodes->IsArray() || !edges || !edges->IsArray()) return fail("the graph is { nodes, edges }, as WorldGraph answers it");
    std::unordered_map<std::string, int> seen;
    for (size_t i = 0; i < nodes->Size(); ++i) {
        const OWJsonValue& node = nodes->Items()[i];
        // An array is an object to JavaScript's typeof, one with no id.
        if (node.IsArray()) return fail("node " + std::to_string(i) + " has no id");
        if (!node.IsObject()) return fail("node " + std::to_string(i) + " is not an object");
        const OWJsonValue* id = node.Find("id");
        if (!id || !id->IsString() || id->Text().empty()) return fail("node " + std::to_string(i) + " has no id");
        const OWJsonValue* type = node.Find("type");
        if (!type || !type->IsString() || !IsType(type->Text())) {
            std::string shown = !type ? "undefined" : type->IsString() ? type->Text() : OWJson::Stringify(*type);
            return fail("node " + id->Text() + ": '" + shown + "' is not one of the 22 element types");
        }
        const OWJsonValue* name = node.Find("name");
        if (name && !name->IsNull() && !name->IsString()) return fail("node " + id->Text() + ": name is a string or null");
        if (!seen.emplace(id->Text(), 0).second) return fail("the id " + id->Text() + " is a node twice");
        Node n;
        n.id = id->Text();
        n.type = type->Text();
        n.name = name && name->IsString() ? name->Text() : std::string();
        n.rank = Rank(n.type);
        g.handed.push_back(std::move(n));
    }
    for (size_t k = 0; k < edges->Size(); ++k) {
        const OWJsonValue& e = edges->Items()[k];
        const OWJsonValue* source = e.IsObject() ? e.Find("source") : nullptr;
        const OWJsonValue* target = e.IsObject() ? e.Find("target") : nullptr;
        if (!source || !source->IsString() || !target || !target->IsString()) return fail("edge " + std::to_string(k) + " is not { source, target }");
        const OWJsonValue* field = e.Find("field");
        if (field && !field->IsString()) return fail("edge " + std::to_string(k) + ": field is a string");
        const OWJsonValue* index = e.Find("index");
        double at = 0;
        if (index) {
            if (!index->IsNumber()) return fail("edge " + std::to_string(k) + ": index is a number");
            at = index->AsDouble();
            if (!std::isfinite(at)) return fail("edge " + std::to_string(k) + ": index is a number");
        }
        g.edges.push_back(Edge{source->Text(), target->Text(), field ? field->Text() : std::string(), at});
    }
    g.nodes = g.handed;
    std::sort(g.nodes.begin(), g.nodes.end(), [](const Node& a, const Node& b) { return a.id < b.id; });
    std::sort(g.edges.begin(), g.edges.end(), [](const Edge& a, const Edge& b) {
        if (a.source != b.source) return a.source < b.source;
        if (a.field != b.field) return a.field < b.field;
        if (a.index != b.index) return a.index < b.index;
        return a.target < b.target;
    });
    for (size_t i = 0; i < g.nodes.size(); ++i) g.index[g.nodes[i].id] = static_cast<int>(i);
    return g;
}

double Anchor(size_t i) {
    double d;
    std::memcpy(&d, &AnchorBits[i], sizeof d);
    return d;
}

// The 32-bit linear congruential generator, exact in float64 in every language.
double Next(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s / 4294967296.0;
}

int64_t CellKey(int64_t x, int64_t y) { return (x + 1048576) * 2097152 + (y + 1048576); }

int Find(const std::unordered_map<std::string, int>& index, const std::string& id) {
    auto it = index.find(id);
    return it == index.end() ? -1 : it->second;
}

}  // namespace

double MinJs(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return NAN;
    if (a < b) return a;
    if (b < a) return b;
    return a == 0 && std::signbit(a) ? a : b;
}

double MaxJs(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return NAN;
    if (a > b) return a;
    if (b > a) return b;
    return a == 0 && !std::signbit(a) ? a : b;
}

OWLayoutByKind LayoutByKind(const OWJsonValue& graph) {
    OWLayoutByKind out;
    Ordered g = ReadGraph("LayoutByKind", graph);
    if (!g.error.empty()) {
        out.error = g.error;
        return out;
    }
    const std::vector<Node>& nodes = g.nodes;
    const int n = static_cast<int>(nodes.size());
    if (n <= 1) {
        for (const Node& x : g.handed) out.points.push_back(OWLayoutPoint{x.id, 0, 0});
        return out;
    }

    std::vector<std::pair<int, int>> edges;
    for (const Edge& e : g.edges) {
        int a = Find(g.index, e.source), b = Find(g.index, e.target);
        if (a >= 0 && b >= 0 && a != b) edges.emplace_back(a, b);
    }
    std::vector<const Node*> kinds;
    for (const Node& x : nodes)
        if (std::none_of(kinds.begin(), kinds.end(), [&x](const Node* k) { return k->type == x.type; })) kinds.push_back(&x);
    std::sort(kinds.begin(), kinds.end(), [](const Node* a, const Node* b) { return a->rank != b->rank ? a->rank < b->rank : a->type < b->type; });
    const size_t k = kinds.size();
    std::unordered_map<std::string, std::pair<double, double>> anchor;
    for (size_t i = 0; i < k; ++i) anchor[kinds[i]->type] = {Anchor(k * (k - 1) + 2 * i), Anchor(k * (k - 1) + 2 * i + 1)};

    uint32_t seed = 7;
    std::vector<double> px(n), py(n), tx(n), ty(n);
    for (int i = 0; i < n; ++i) {
        const auto& a = anchor[nodes[i].type];
        const double ax = a.first, ay = a.second;
        const double jx = Next(seed) - 0.5;
        px[i] = ax * 0.8 + jx * 0.5;
        const double jy = Next(seed) - 0.5;
        py[i] = ay * 0.8 + jy * 0.5;
        tx[i] = ax * 0.85;
        ty[i] = ay * 0.85;
    }
    const double rest = MaxJs(0.002, 0.9 / std::sqrt(static_cast<double>(n)));
    const double push = rest * rest * 1.4;
    const double cell = rest * 2.2;
    const double cell2 = cell * cell;
    std::vector<double> fx(n), fy(n);
    std::vector<int64_t> cxs(n), cys(n);
    const int steps = n > 2000 ? Steps / 2 : Steps;
    std::unordered_map<int64_t, std::vector<int>> grid;

    for (int it = 0; it < steps; ++it) {
        const double t = 1 - static_cast<double>(it) / steps;
        std::fill(fx.begin(), fx.end(), 0.0);
        std::fill(fy.begin(), fy.end(), 0.0);
        grid.clear();
        for (int i = 0; i < n; ++i) {
            cxs[i] = static_cast<int64_t>(std::floor(px[i] / cell));
            cys[i] = static_cast<int64_t>(std::floor(py[i] / cell));
            grid[CellKey(cxs[i], cys[i])].push_back(i);
        }
        for (int i = 0; i < n; ++i) {
            const int64_t cx = cxs[i], cy = cys[i];
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    auto found = grid.find(CellKey(cx + dx, cy + dy));
                    if (found == grid.end()) continue;
                    const std::vector<int>& cellOf = found->second;
                    if (static_cast<int>(cellOf.size()) > CellMax) {
                        // A crowded cell: a sample of 12 that turns with the element and the step, each push scaled up to the whole.
                        const int len = static_cast<int>(cellOf.size());
                        const int off = (i * 7 + it * 13) % len;
                        const double scale = static_cast<double>(len) / CellMax;
                        for (int s = 0; s < CellMax; ++s) {
                            const int j = cellOf[(off + s) % len];
                            if (j == i) continue;
                            const double ux = px[i] - px[j];
                            const double uy = py[i] - py[j];
                            const double d2 = ux * ux + uy * uy + 1e-6;
                            if (d2 > cell2) continue;
                            const double d = std::sqrt(d2);
                            const double f = (push * scale) / d2;
                            fx[i] += (ux / d) * f;
                            fy[i] += (uy / d) * f;
                        }
                        continue;
                    }
                    for (int j : cellOf) {
                        if (j <= i) continue;
                        const double ux = px[i] - px[j];
                        const double uy = py[i] - py[j];
                        const double d2 = ux * ux + uy * uy + 1e-6;
                        if (d2 > cell2) continue;
                        const double d = std::sqrt(d2);
                        const double f = push / d2;
                        fx[i] += (ux / d) * f;
                        fy[i] += (uy / d) * f;
                        fx[j] -= (ux / d) * f;
                        fy[j] -= (uy / d) * f;
                    }
                }
            }
        }
        for (const auto& e : edges) {
            const int a = e.first, b = e.second;
            const double ux = px[b] - px[a];
            const double uy = py[b] - py[a];
            const double d = std::sqrt(ux * ux + uy * uy) + 1e-6;
            const double f = (d - rest) * 0.06;
            fx[a] += (ux / d) * f;
            fy[a] += (uy / d) * f;
            fx[b] -= (ux / d) * f;
            fy[b] -= (uy / d) * f;
        }
        for (int i = 0; i < n; ++i) {
            fx[i] += (tx[i] - px[i]) * 0.06;
            fy[i] += (ty[i] - py[i]) * 0.06;
            const double m = std::sqrt(fx[i] * fx[i] + fy[i] * fy[i]);
            const double cap = 0.03 * t + 0.002;
            const double s = m > cap ? cap / m : 1;
            px[i] += fx[i] * s;
            py[i] += fy[i] * s;
        }
    }

    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (int i = 0; i < n; ++i) {
        x0 = MinJs(x0, px[i]);
        x1 = MaxJs(x1, px[i]);
        y0 = MinJs(y0, py[i]);
        y1 = MaxJs(y1, py[i]);
    }
    const double span = MaxJs(x1 - x0, y1 - y0);
    const double sc = span > 1e-9 ? 2 / span : 1;
    const double mx = (x0 + x1) / 2;
    const double my = (y0 + y1) / 2;
    for (const Node& x : g.handed) {
        const int i = g.index.at(x.id);
        out.points.push_back(OWLayoutPoint{x.id, (px[i] - mx) * sc, (py[i] - my) * sc});
    }
    return out;
}

OWLayoutByKind LayoutByKind(const OWWorldGraph& graph) { return LayoutByKind(graph.ToJson()); }

OWLayoutAround LayoutAround(const OWJsonValue& graph, std::string_view focus, int steps) {
    OWLayoutAround out;
    if (steps != 1 && steps != 2) {
        out.error = "LayoutAround: steps is 1 or 2, not " + std::to_string(steps);
        return out;
    }
    Ordered g = ReadGraph("LayoutAround", graph);
    if (!g.error.empty()) {
        out.error = g.error;
        return out;
    }
    const std::vector<Node>& nodes = g.nodes;
    const int centre = Find(g.index, std::string(focus));
    if (centre < 0) {
        out.error = "LayoutAround: the focus " + std::string(focus) + " is not a node of the graph";
        return out;
    }
    const int n = static_cast<int>(nodes.size());
    std::vector<std::vector<int>> adj(n);
    for (const Edge& e : g.edges) {
        int a = Find(g.index, e.source), b = Find(g.index, e.target);
        if (a < 0 || b < 0) continue;
        adj[a].push_back(b);
        if (a != b) adj[b].push_back(a);
    }
    std::vector<int> depth(n, -1), via(n, -1);
    depth[centre] = 0;
    // Each step's newly reached elements in the order they were reached: the order the npm SDK's depth map holds them
    // in, which orders each neighbour's fan.
    std::vector<std::vector<int>> reached(steps + 1);
    std::vector<int> frontier{centre};
    for (int s = 1; s <= steps; ++s) {
        std::vector<int> next;
        for (int a : frontier) {
            for (int to : adj[a]) {
                if (depth[to] >= 0) continue;
                depth[to] = s;
                via[to] = a;
                next.push_back(to);
            }
        }
        reached[s] = next;
        frontier = std::move(next);
    }
    std::vector<int> ring1 = reached[1];
    std::sort(ring1.begin(), ring1.end(), [&nodes](int a, int b) {
        if (nodes[a].rank != nodes[b].rank) return nodes[a].rank < nodes[b].rank;
        if (nodes[a].type != nodes[b].type) return nodes[a].type < nodes[b].type;
        if (nodes[a].name != nodes[b].name) return nodes[a].name < nodes[b].name;
        return a < b;
    });

    std::vector<char> placedAt(n, 0);
    std::vector<double> atX(n, 0), atY(n, 0), angle(n, 0);
    placedAt[centre] = 1;
    if (!ring1.empty()) {
        const int count1 = static_cast<int>(ring1.size());
        const int first = std::min(count1, RingOne);
        const double r1 = MaxJs(0.34, first * 0.017);
        const double gap = (2 * Pi * r1) / first;
        int placed = 0;
        double r = r1;
        while (placed < count1) {
            const int room = static_cast<int>(MaxJs(1, std::floor((2 * Pi * r) / gap)));
            const int count = std::min(room, count1 - placed);
            for (int k = 0; k < count; ++k) {
                const int j = ring1[placed + k];
                const double a = -Pi / 2 + (2 * Pi * (k + 0.5)) / count;
                angle[j] = a;
                atX[j] = std::cos(a) * r;
                atY[j] = std::sin(a) * r;
                placedAt[j] = 1;
            }
            placed += count;
            r += gap * 1.2;
        }
        const double outer = r - gap * 1.2;
        if (steps == 2) {
            std::vector<int> parents;
            std::unordered_map<int, std::vector<int>> kids;
            for (int j : reached[2]) {
                auto& l = kids[via[j]];
                if (l.empty()) parents.push_back(via[j]);
                l.push_back(j);
            }
            const double spread = MinJs(0.5, ((2 * Pi) / std::max(1, count1)) * 0.9);
            for (int p : parents) {
                const double a0 = angle[p];
                const std::vector<int>& ks = kids[p];
                const int size = static_cast<int>(ks.size());
                for (int k = 0; k < size; ++k) {
                    const int j = ks[k];
                    const double a = a0 + (size == 1 ? 0 : (static_cast<double>(k) / (size - 1) - 0.5) * spread);
                    atX[j] = std::cos(a) * outer * 1.8;
                    atY[j] = std::sin(a) * outer * 1.8;
                    placedAt[j] = 1;
                }
            }
        }
    }
    for (const Node& x : g.handed) {
        const int j = g.index.at(x.id);
        if (placedAt[j]) out.points.push_back(OWLayoutAroundPoint{x.id, depth[j], atX[j], atY[j]});
    }
    return out;
}

OWLayoutAround LayoutAround(const OWWorldGraph& graph, std::string_view focus, int steps) {
    if (steps != 1 && steps != 2) {
        OWLayoutAround out;
        out.error = "LayoutAround: steps is 1 or 2, not " + std::to_string(steps);
        return out;
    }
    return LayoutAround(graph.ToJson(), focus, steps);
}

}  // namespace OWLayout
}  // namespace OnlyWorlds
