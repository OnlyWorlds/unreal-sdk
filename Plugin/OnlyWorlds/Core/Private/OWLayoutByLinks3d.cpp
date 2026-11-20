// OWLayout::LayoutByLinks3d: a whole world in three dimensions by its links, with the world's Maps and Pins as a plate
// its pinned elements stand on, and points an earlier answer gave held still. Written from the default layouts page
// (schema-dist profiles/layout.md, `by-links-3d`, serial 137), step by step in the page's order and numbering, with the
// Unity SDK's reader of the same page beside it, and held to the page's fixture (profiles/layout-by-links-3d.fixture.json):
// tests/test_layout_by_links_3d.cpp.
//
// Every number is a double and every expression is worked as the page writes it, left to right, nothing regrouped and no
// two operations fused (the core builds with MSVC's default /fp:precise; the plugin's module asks for precise semantics
// on every target, OnlyWorlds.Build.cs). Steps 1 to 8 use + - * /, std::sqrt, std::floor, the greater and the lesser of
// two, and the generator's whole numbers; step 9 std::cos and std::sin, and step 11 std::asin, std::cos and std::sin,
// which is why the points they place are held to 6e-8 and every other point to the last bit. The directions table is
// generated data (OWLayoutDirections.generated.cpp), never computed. No exceptions: a refusal is an answer's `code`.

#include "OWLayout.h"

#include "OWMapSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace OnlyWorlds {
namespace OWLayout {

namespace {

constexpr double Pi = 3.141592653589793;
constexpr double Rest = 60.0;
constexpr int Steps3d = 300;
constexpr int MaxCentres = 12;
constexpr int MinGroup = 3;
constexpr double WaysCap = 4503599627370496.0;
constexpr double Spread = 2;
constexpr double Tile = 0.4;
constexpr double Lobe = 1.1;
constexpr double Gap = 1.25;
constexpr double Clear = 1.8;
constexpr int PackSteps = 600;
constexpr double Cross = 0.25;
constexpr double PlateWeight = 0.02;
constexpr double Hold = 0.12;
constexpr double Cap0 = 0.1;
constexpr double Cutoff = 3;
constexpr double Line = Rest / 8;
constexpr double Snap = Rest / 1000000000;
constexpr double HeldBox = 768;
constexpr int FrameAgree = 3;
constexpr double Fit = 0.000001;
constexpr double RingGap = Clear * Rest;
const double Gold = Pi * (3 - std::sqrt(5.0));

constexpr int HowCentre = 0, HowWith = 1, HowArm = 2, HowLink = 3, HowOff = 4, HowMap = 5;
const char* const HowWords[] = {"centre", "with", "arm", "link", "off", "map"};

bool Opposing(const std::optional<std::string>& field) {
    return field && (*field == "rivals" || *field == "adversaries" || *field == "rival" || *field == "antagonist");
}

// A call that answers nothing: a refusal the page names (`code`), or a caller's mistake (no code).
struct Fail {
    bool failed = false;
    std::string code;
    std::string message;
    void Set(std::string c, std::string m) {
        if (failed) return;
        failed = true;
        code = std::move(c);
        message = "LayoutByLinks3d: " + std::move(m);
    }
};

using P3 = std::array<double, 3>;
using HeldList = std::vector<std::pair<std::string, P3>>;

struct Link3 {
    std::string source;
    std::string target;
    std::optional<std::string> field;
};

struct SpaceMap {
    std::string id;
    std::optional<int64_t> width;
    std::optional<int64_t> height;
    std::optional<std::string> location;
};

struct SpacePin {
    std::string pin;
    std::string map;
    std::optional<std::string> element;
    double x = 0;
    double y = 0;
};

struct Space {
    std::vector<SpaceMap> maps;
    std::vector<SpacePin> pins;
};

std::string NumberText(double v) { return OWJson::NumberToString(v); }

// ---- What is read besides the graph --------------------------------------------------------------------------------

// The world's Maps and Pins as the map-space reading answers them, a layout map left out with its Pins (its rule 11).
std::optional<Space> SpaceOf(const OWJsonValue* elements, Fail& fail) {
    if (!elements || elements->IsNull()) return std::nullopt;
    if (!elements->IsArray()) {
        fail.Set("", "elements is a list of the world's elements");
        return std::nullopt;
    }
    OWWorldMapSpace space = OWMapSpace::MapSpaceOf(*elements);
    if (!space.error.empty()) {
        fail.Set("", space.error);
        return std::nullopt;
    }
    std::unordered_map<std::string, const OWJsonValue*> rawMaps;
    OWJsonValue mapRows = OWJsonValue::Array(), pinRows = OWJsonValue::Array();
    for (const OWJsonValue& e : elements->Items()) {
        if (!e.IsObject()) continue;
        const OWJsonValue* type = e.Find("type");
        const OWJsonValue* id = e.Find("id");
        if (!type || !type->IsString() || !id || !id->IsString() || id->Text().empty()) continue;
        if (type->Text() == "map") {
            rawMaps[id->Text()] = &e;
            mapRows.Push(e);
        } else if (type->Text() == "pin") {
            pinRows.Push(e);
        }
    }
    Space answer;
    for (const OWMapSpaceMap& m : space.maps) {
        auto raw = rawMaps.find(m.id);
        if (raw != rawMaps.end() && OWMapSpace::LayoutOf(*raw->second, mapRows, pinRows).layout) continue;
        answer.maps.push_back(SpaceMap{m.id, m.width, m.height, m.location});
    }
    for (const OWMapSpacePin& p : space.pins)
        answer.pins.push_back(SpacePin{p.pin, p.map, p.element_id, static_cast<double>(p.point.x), static_cast<double>(p.point.y)});
    return answer;
}

// ---- Held points: the refusals before anything runs ----------------------------------------------------------------

std::optional<HeldList> ReadHeld(const OWJsonValue* held, Fail& fail) {
    if (!held || held->IsNull()) return std::nullopt;
    if (!held->IsObject()) {
        fail.Set("held-not-object", "held is not a mapping of ids to points");
        return std::nullopt;
    }
    HeldList entries;
    for (size_t i = 0; i < held->Size(); ++i) {
        const std::string& name = held->Keys()[i];
        const OWJsonValue& a = held->Items()[i];
        if (!a.IsArray() || a.Size() != 3) {
            fail.Set("held-not-point", "held[" + name + "] is not three finite numbers");
            return std::nullopt;
        }
        P3 q{};
        for (int c = 0; c < 3; ++c) {
            const OWJsonValue& v = a.Items()[c];
            q[c] = v.IsNumber() ? v.AsDouble() : NAN;
            if (!v.IsNumber() || !std::isfinite(q[c])) {
                fail.Set("held-not-point", "held[" + name + "] is not three finite numbers");
                return std::nullopt;
            }
        }
        entries.emplace_back(name, q);
    }
    return entries;
}

double ReadUnit(const OWJsonValue* unit, Fail& fail) {
    if (!unit || unit->IsNull()) return 0;
    if (unit->IsNumber()) {
        const double u = unit->AsDouble();
        if (std::isfinite(u) && u >= 0 && u <= Line) return u;
    }
    fail.Set("held-unit", "heldUnit " + (unit->IsString() ? OWJson::Quote(unit->Text()) : unit->IsBool() ? std::string(unit->AsBool() ? "true" : "false") : unit->Text()) +
                              " is not a finite number from 0 to " + NumberText(Line));
    return 0;
}

// The box test of the held points one run holds (Held points, Refused).
void TestBox(const std::optional<HeldList>& held, Fail& fail) {
    if (!held || held->empty()) return;
    for (int a = 0; a < 3; ++a) {
        double lo = INFINITY, hi = -INFINITY;
        for (const auto& p : *held) {
            if (p.second[a] < lo) lo = p.second[a];
            if (p.second[a] > hi) hi = p.second[a];
        }
        if (hi - lo > HeldBox * Rest) {
            fail.Set("held-box", "the held points' box is " + NumberText((hi - lo) / Rest) + " rests on " + std::string(1, "xyz"[a]) +
                                     ", wider than " + NumberText(HeldBox));
            return;
        }
    }
}

// ---- Step 2: the body ----------------------------------------------------------------------------------------------

struct BodyAnswer {
    std::vector<char> body;
    std::vector<int> hangCut;
    std::vector<int> hangDepth;
    std::vector<char> off;
};

BodyAnswer Body(int n, const std::vector<std::vector<int>>& adj) {
    // The pieces; the main piece the largest, a tie to the one that holds the lowest node.
    std::vector<int> piece(n, -1), queue(n);
    int main = -1, mainSize = 0, pieces = 0;
    for (int s = 0; s < n; ++s) {
        if (piece[s] != -1) continue;
        int head = 0, tail = 0;
        queue[tail++] = s;
        piece[s] = pieces;
        while (head < tail) {
            const int v = queue[head++];
            for (int w : adj[v]) {
                if (piece[w] != -1) continue;
                piece[w] = pieces;
                queue[tail++] = w;
            }
        }
        if (tail > mainSize) {
            mainSize = tail;
            main = pieces;
        }
        ++pieces;
    }

    // The blocks (Hopcroft and Tarjan, without recursion); the body is the block with the most members, a tie to the one
    // whose members in rising order are the higher at the first place they differ.
    BodyAnswer out;
    out.body.assign(n, 0);
    if (mainSize == 1) {
        for (int i = 0; i < n; ++i)
            if (piece[i] == main) out.body[i] = 1;
    } else if (mainSize > 1) {
        std::vector<int> disc(n, -1), low(n, 0), parent(n, -1), next(n, 0), stamp(n, -1), stackV(n);
        std::vector<int> edgeA, edgeB, best;
        int blocks = 0, clock = 0, top = 0;
        int root = 0;
        while (piece[root] != main) ++root;
        disc[root] = low[root] = clock++;
        stackV[top++] = root;
        while (top > 0) {
            const int v = stackV[top - 1];
            if (next[v] < static_cast<int>(adj[v].size())) {
                const int w = adj[v][next[v]++];
                if (disc[w] == -1) {
                    parent[w] = v;
                    disc[w] = low[w] = clock++;
                    edgeA.push_back(v);
                    edgeB.push_back(w);
                    stackV[top++] = w;
                } else if (w != parent[v] && disc[w] < disc[v]) {
                    edgeA.push_back(v);
                    edgeB.push_back(w);
                    if (disc[w] < low[v]) low[v] = disc[w];
                }
            } else {
                --top;
                const int u = parent[v];
                if (u == -1) continue;
                if (low[v] < low[u]) low[u] = low[v];
                if (low[v] < disc[u]) continue;
                std::vector<int> members;
                for (;;) {
                    const int a = edgeA.back(), b = edgeB.back();
                    edgeA.pop_back();
                    edgeB.pop_back();
                    if (stamp[a] != blocks) {
                        stamp[a] = blocks;
                        members.push_back(a);
                    }
                    if (stamp[b] != blocks) {
                        stamp[b] = blocks;
                        members.push_back(b);
                    }
                    if (a == u && b == v) break;
                }
                if (members.size() >= best.size()) {
                    std::sort(members.begin(), members.end());
                    bool wins = members.size() > best.size();
                    if (!wins)
                        for (size_t i = 0; i < members.size(); ++i)
                            if (members[i] != best[i]) {
                                wins = members[i] > best[i];
                                break;
                            }
                    if (wins) best = members;
                }
                ++blocks;
            }
        }
        for (int v : best) out.body[v] = 1;
    }

    // What hangs: outward from the whole body at once, each node's cut and its depth from it.
    out.hangCut.assign(n, -1);
    out.hangDepth.assign(n, 0);
    int qh = 0, qt = 0;
    for (int i = 0; i < n; ++i)
        if (out.body[i]) queue[qt++] = i;
    while (qh < qt) {
        const int v = queue[qh++];
        for (int w : adj[v]) {
            if (out.body[w] || out.hangCut[w] != -1) continue;
            out.hangCut[w] = out.body[v] ? v : out.hangCut[v];
            out.hangDepth[w] = out.hangDepth[v] + 1;
            queue[qt++] = w;
        }
    }
    out.off.assign(n, 0);
    for (int i = 0; i < n; ++i) out.off[i] = piece[i] != main;
    return out;
}

// ---- Step 3: the centres and their groups --------------------------------------------------------------------------

void ReachGroups(int n, const std::vector<std::vector<int>>& adj, const std::vector<int>& members, const std::vector<char>& inSet,
                 const std::vector<int>& taken, std::vector<int>& group, std::vector<int>& dist) {
    const int k = static_cast<int>(taken.size());
    dist.assign(n, -1);
    group.assign(n, -1);
    std::vector<double> ways(static_cast<size_t>(n) * k, 0.0);
    std::vector<int> front;
    for (int r = 0; r < k; ++r) {
        dist[taken[r]] = 0;
        ways[static_cast<size_t>(taken[r]) * k + r] = 1;
        front.push_back(taken[r]);
    }
    int d = 0;
    while (!front.empty()) {
        ++d;
        std::vector<int> next;
        for (int v : front)
            for (int w : adj[v]) {
                if (!inSet[w]) continue;
                if (dist[w] == -1) {
                    dist[w] = d;
                    next.push_back(w);
                }
                if (dist[w] != d) continue;
                for (int r = 0; r < k; ++r) {
                    double& to = ways[static_cast<size_t>(w) * k + r];
                    to = std::min(WaysCap, to + ways[static_cast<size_t>(v) * k + r]);
                }
            }
        front = std::move(next);
    }
    for (int r = 0; r < k; ++r) group[taken[r]] = r;
    for (int v : members) {
        if (dist[v] <= 0) continue;
        int best = 0;
        for (int r = 1; r < k; ++r)
            if (ways[static_cast<size_t>(v) * k + r] > ways[static_cast<size_t>(v) * k + best]) best = r;
        group[v] = best;
    }
}

std::vector<int> Pick(int n, const std::vector<std::vector<int>>& adj, const std::vector<int>& members, const std::vector<char>& inSet,
                      std::vector<int>& group, std::vector<int>& dist) {
    const int size = static_cast<int>(members.size());
    std::vector<int> count(n, 0);
    for (int v : members) {
        int c = 0;
        for (int w : adj[v])
            if (inSet[w]) ++c;
        count[v] = c;
    }
    std::vector<int> ranked = members;
    std::sort(ranked.begin(), ranked.end(), [&count](int a, int b) { return count[a] != count[b] ? count[a] > count[b] : a < b; });
    int f = 0;
    while (f * f < size) ++f;
    const int least = std::max(3, f);
    std::vector<int> mark(n, 0);
    std::vector<int> taken;
    int hit[MaxCentres];
    for (int v : ranked) {
        if (count[v] < least || static_cast<int>(taken.size()) == MaxCentres) break;
        std::fill(hit, hit + MaxCentres, 0);
        for (int w : adj[v]) {
            if (!inSet[w]) continue;
            for (size_t r = 0; r < taken.size(); ++r) hit[r] += (mark[w] >> r) & 1;
        }
        bool ok = true;
        for (size_t r = 0; r < taken.size(); ++r)
            if (2 * hit[r] >= count[v]) {
                ok = false;
                break;
            }
        if (!ok) continue;
        const int rank = static_cast<int>(taken.size());
        taken.push_back(v);
        mark[v] |= 1 << rank;
        for (int w : adj[v])
            if (inSet[w]) mark[w] |= 1 << rank;
    }
    if (taken.empty() && !ranked.empty()) taken.push_back(ranked[0]);
    for (;;) {
        ReachGroups(n, adj, members, inSet, taken, group, dist);
        std::vector<int> sizes(taken.size(), 0);
        for (int v : members)
            if (group[v] != -1) ++sizes[group[v]];
        int last = -1;
        for (size_t r = 0; r < taken.size(); ++r)
            if (sizes[r] < MinGroup) last = static_cast<int>(r);
        if (last == -1 || taken.size() == 1) return taken;
        taken.erase(taken.begin() + last);
    }
}

std::vector<int> Centres(int n, const std::vector<std::vector<int>>& adj, const std::vector<char>& body, std::vector<int>& group) {
    std::vector<int> members;
    for (int i = 0; i < n; ++i)
        if (body[i]) members.push_back(i);
    std::vector<int> dist;
    std::vector<int> taken = Pick(n, adj, members, body, group, dist);
    for (int round = 0; round < 2; ++round) {
        if (taken.empty()) break;
        std::vector<int> sizes(taken.size(), 0);
        for (int v : members) ++sizes[group[v]];
        size_t big = 0;
        for (size_t r = 1; r < taken.size(); ++r)
            if (sizes[r] > sizes[big]) big = r;
        if (2 * sizes[big] <= static_cast<int>(members.size())) break;
        std::vector<int> inside;
        std::vector<char> inSet(n, 0);
        for (int v : members)
            if (group[v] == static_cast<int>(big) && v != taken[big]) {
                inside.push_back(v);
                inSet[v] = 1;
            }
        std::vector<int> subGroup, subDist;
        std::vector<int> sub = Pick(n, adj, inside, inSet, subGroup, subDist);
        if (sub.size() < 2) break;
        const int bas = static_cast<int>(taken.size());
        taken.insert(taken.end(), sub.begin(), sub.end());
        for (int v : inside)
            if (subDist[v] != -1) group[v] = bas + subGroup[v];
    }
    return taken;
}

// ---- Step 4: the plate, and H1's frame -----------------------------------------------------------------------------

struct PlateAnswer {
    // The pinned elements in the order they were placed: the first map's, then each small plate's.
    std::vector<std::string> order;
    std::unordered_map<std::string, P3> at;
    // The first map's elements and each one's map place, (x - W / 2, y - H / 2), in the order of `order`.
    std::vector<std::pair<std::string, std::array<double, 2>>> first;
};

bool Sized(const std::optional<int64_t>& v) { return v && *v > 0; }

std::optional<PlateAnswer> PlateOf(const Space& space, const std::unordered_map<std::string, int>& nodes, std::optional<double> scaleHeld) {
    std::unordered_map<std::string, const SpaceMap*> maps;
    for (const SpaceMap& m : space.maps) maps[m.id] = &m;
    // A stable sort by pin id in code point order.
    std::vector<const SpacePin*> sorted;
    for (const SpacePin& p : space.pins) sorted.push_back(&p);
    std::stable_sort(sorted.begin(), sorted.end(), [](const SpacePin* a, const SpacePin* b) { return a->pin < b->pin; });

    using Row = std::vector<std::pair<std::string, std::array<double, 2>>>;
    std::vector<std::string> mapOrder;
    std::unordered_map<std::string, Row> on;
    for (const SpacePin* pin : sorted) {
        if (!maps.count(pin->map) || !pin->element || !nodes.count(*pin->element)) continue;
        auto found = on.find(pin->map);
        if (found == on.end()) {
            found = on.emplace(pin->map, Row()).first;
            mapOrder.push_back(pin->map);
        }
        Row& row = found->second;
        if (std::none_of(row.begin(), row.end(), [&pin](const auto& x) { return x.first == *pin->element; }))
            row.emplace_back(*pin->element, std::array<double, 2>{pin->x, pin->y});
    }
    if (mapOrder.empty()) return std::nullopt;
    std::string first;
    int best = -1;
    for (const std::string& c : mapOrder) {
        const int size = static_cast<int>(on[c].size());
        if (size > best || (size == best && c > first)) {
            first = c;
            best = size;
        }
    }
    const SpaceMap& map = *maps[first];
    const Row& mine = on[first];
    if (mine.size() < 3 || !Sized(map.width) || !Sized(map.height)) return std::nullopt;
    const double W = static_cast<double>(*map.width), H = static_cast<double>(*map.height);
    const double many = static_cast<double>(mine.size());
    const double scale = scaleHeld ? *scaleHeld : Spread * Rest / std::sqrt(W * H / many);
    PlateAnswer answer;
    for (const auto& e : mine) {
        const double mx = e.second[0] - W / 2, my = e.second[1] - H / 2;
        answer.first.emplace_back(e.first, std::array<double, 2>{mx, my});
        answer.order.push_back(e.first);
        answer.at[e.first] = P3{mx * scale, my * scale, 0.0};
    }

    std::vector<std::string> todo;
    for (const std::string& c : mapOrder)
        if (c != first) todo.push_back(c);
    std::sort(todo.begin(), todo.end());
    bool takenAny = true;
    while (takenAny) {
        takenAny = false;
        const std::vector<std::string> pass = todo;
        for (const std::string& c : pass) {
            const SpaceMap& m = *maps[c];
            const Row& row = on[c];
            Row fresh;
            for (const auto& x : row)
                if (!answer.at.count(x.first)) fresh.push_back(x);
            if (!m.location || !answer.at.count(*m.location) || !Sized(m.width) || !Sized(m.height) || row.size() < 3 || fresh.empty()) continue;
            const P3 l = answer.at[*m.location];
            const double cw = static_cast<double>(*m.width), ch = static_cast<double>(*m.height);
            const double count = static_cast<double>(row.size());
            const double sc = Tile * Rest / std::sqrt(cw * ch / count);
            const double rise = row.size() >= 20 ? Rest * 4 : Rest * 2;
            for (const auto& e : fresh) {
                answer.order.push_back(e.first);
                answer.at[e.first] = P3{l[0] + (e.second[0] - cw / 2) * sc, l[1] + (e.second[1] - ch / 2) * sc, l[2] + rise};
            }
            todo.erase(std::find(todo.begin(), todo.end(), c));
            takenAny = true;
        }
    }
    return answer;
}

// H1: the frame, where the plate stood when the held points were answered; false when there is none.
bool FrameOf(const PlateAnswer& plate, const std::unordered_map<std::string, int>& at, const std::vector<char>& hflag, const std::vector<double>& hat,
             double unit, double& s, P3& F) {
    std::vector<int> cv;
    std::vector<double> cx, cy;
    for (const auto& e : plate.first) {
        const int v = at.at(e.first);
        if (!hflag[v]) continue;
        cv.push_back(v);
        cx.push_back(e.second[0]);
        cy.push_back(e.second[1]);
    }
    std::vector<int> order(cv.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&cv](int p, int q) { return cv[p] < cv[q]; });
    const int m = static_cast<int>(order.size());
    const double tolZ = unit + Snap;
    const double band = 3 * unit + Snap;
    for (int ia = 0; ia < m; ++ia) {
        const int a = order[ia];
        const int va = cv[a];
        const double ax = hat[3 * va], ay = hat[3 * va + 1], az = hat[3 * va + 2];
        std::vector<int> partners;
        std::vector<double> bdist;
        for (int ib = 0; ib < m; ++ib) {
            const int c = order[ib];
            if (c == a) continue;
            const double dz = hat[3 * cv[c] + 2] - az;
            if (!(dz <= tolZ && -dz <= tolZ)) continue;
            if (cx[c] == cx[a] && cy[c] == cy[a]) continue;
            const double mx = cx[c] - cx[a], my = cy[c] - cy[a];
            partners.push_back(c);
            bdist.push_back(std::sqrt(mx * mx + my * my));
        }
        std::vector<int> byFar(partners.size());
        for (size_t i = 0; i < byFar.size(); ++i) byFar[i] = static_cast<int>(i);
        std::sort(byFar.begin(), byFar.end(), [&bdist](int p, int q) { return bdist[p] != bdist[q] ? bdist[p] > bdist[q] : p < q; });
        for (int ib : byFar) {
            const int b = partners[ib];
            const int vb = cv[b];
            const double hx = hat[3 * vb] - ax, hy = hat[3 * vb + 1] - ay;
            if (hx == 0 && hy == 0) continue;
            const double sc = std::sqrt(hx * hx + hy * hy) / bdist[ib];
            const P3 Pt{ax - sc * cx[a], ay - sc * cy[a], az};
            int agree = 0;
            for (int ic = 0; ic < m; ++ic) {
                const int c = order[ic];
                const int vc = cv[c];
                const double ex = hat[3 * vc] - (Pt[0] + sc * cx[c]), ey = hat[3 * vc + 1] - (Pt[1] + sc * cy[c]), ez = hat[3 * vc + 2] - Pt[2];
                if (ex <= band && -ex <= band && ey <= band && -ey <= band && ez <= band && -ez <= band) ++agree;
            }
            if (agree < FrameAgree) continue;
            s = sc;
            F = Pt;
            return true;
        }
    }
    return false;
}

// ---- Step 5: where each group's point stands -----------------------------------------------------------------------

struct GroupPointsAnswer {
    std::vector<double> cpt;
    std::vector<double> lobe;
    std::vector<char> pinned;
    std::vector<double> plateAt;  // empty: no plate
};

double Dir(int K, int s, int axis) {
    double d;
    std::memcpy(&d, &DirectionBits[static_cast<size_t>(3 * K * (K - 1) / 2 + 3 * s + axis)], sizeof d);
    return d;
}

// The page's cube root: 0 for v not over 0; else from sqrt(v), or 1 when that is under 1, forty Newton steps.
double Root(double v) {
    if (!(v > 0)) return 0;
    double x = std::sqrt(v);
    if (x < 1) x = 1;
    for (int i = 0; i < 40; ++i) x = (2 * x + v / (x * x)) / 3;
    return x;
}

using Pairs = std::vector<std::array<int, 2>>;

void Counts(int n, int K, const std::vector<int>& part, const Pairs& pairs, const Pairs& opps, std::vector<int>& size, std::vector<int>& link,
            std::vector<int>& opp) {
    size.assign(K, 0);
    link.assign(static_cast<size_t>(K) * K, 0);
    opp.assign(static_cast<size_t>(K) * K, 0);
    for (int v = 0; v < n; ++v)
        if (part[v] != -1) ++size[part[v]];
    for (const auto& pr : pairs) {
        const int p = part[pr[0]], q = part[pr[1]];
        if (p == -1 || q == -1 || p == q) continue;
        if (p < q) ++link[p * K + q];
        else ++link[q * K + p];
    }
    for (const auto& e : opps) {
        const int p = part[e[0]], q = part[e[1]];
        if (p == -1 || q == -1 || p == q) continue;
        if (p < q) ++opp[p * K + q];
        else ++opp[q * K + p];
    }
}

std::vector<int> BySize(int K, const std::vector<int>& size) {
    std::vector<int> order(K);
    for (int i = 0; i < K; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&size](int a, int b) { return size[a] != size[b] ? size[a] > size[b] : a < b; });
    return order;
}

// Each part's direction: the two ends, then the rest by size, each the free direction of least cost.
std::vector<double> Directions(int K, const std::vector<int>& size, const std::vector<int>& link, const std::vector<int>& opp, Fail& fail) {
    std::vector<double> dirs(3 * static_cast<size_t>(K), 0.0);
    if (K == 1) return dirs;
    if (K > DirectionsMax) {
        fail.Set("", "the directions table has no row of " + std::to_string(K) + " directions");
        return dirs;
    }
    bool anyOpp = false;
    for (int i = 0; i < K; ++i)
        for (int j = i + 1; j < K; ++j)
            if (opp[i * K + j] > 0) anyOpp = true;
    int b0 = -1, b1 = -1;
    if (anyOpp) {
        int bo = -1, bs = -1;
        for (int i = 0; i < K; ++i)
            for (int j = i + 1; j < K; ++j) {
                const int o = opp[i * K + j], s = size[i] + size[j];
                if (o > bo || (o == bo && s > bs)) {
                    bo = o;
                    bs = s;
                    b0 = i;
                    b1 = j;
                }
            }
    } else {
        const std::vector<int> bySize = BySize(K, size);
        const int m = std::min(K, 4);
        std::vector<int> big(bySize.begin(), bySize.begin() + m);
        std::sort(big.begin(), big.end());
        for (int x = 0; x < m; ++x)
            for (int y = x + 1; y < m; ++y) {
                const int i = big[x], j = big[y];
                if (b0 == -1) {
                    b0 = i;
                    b1 = j;
                    continue;
                }
                const int64_t left = static_cast<int64_t>(link[i * K + j]) * size[b0] * size[b1];
                const int64_t right = static_cast<int64_t>(link[b0 * K + b1]) * size[i] * size[j];
                if (left < right) {
                    b0 = i;
                    b1 = j;
                }
            }
    }

    std::vector<int> place(K, -1);
    std::vector<char> used(K, 0);
    std::vector<int> placed{b0, b1};
    place[b0] = 0;
    used[0] = 1;
    place[b1] = K - 1;
    used[K - 1] = 1;
    for (int g : BySize(K, size)) {
        if (place[g] != -1) continue;
        int bestS = -1;
        double bestC = 0;
        for (int s = 0; s < K; ++s) {
            if (used[s]) continue;
            double c = 0;
            for (int h : placed) {
                const int t = place[h];
                const int lo = g < h ? g : h, hi = g < h ? h : g;
                const int w = link[lo * K + hi] - 4 * opp[lo * K + hi];
                const double dx = Dir(K, s, 0) - Dir(K, t, 0), dy = Dir(K, s, 1) - Dir(K, t, 1), dz = Dir(K, s, 2) - Dir(K, t, 2);
                c += w * std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            if (bestS == -1 || c < bestC) {
                bestS = s;
                bestC = c;
            }
        }
        place[g] = bestS;
        used[bestS] = 1;
        placed.push_back(g);
    }
    for (int g = 0; g < K; ++g)
        for (int a = 0; a < 3; ++a) dirs[3 * g + a] = Dir(K, place[g], a);
    return dirs;
}

// The packing (step 5), and H3's with fixed parts: fixedFlag empty when no part is fixed.
std::vector<double> Pack(int K, const std::vector<double>& radius, const std::vector<int>& link, const std::vector<int>& opp, const std::vector<double>& dirs,
                         const std::vector<char>& fixedFlag, const std::vector<double>& fixedAt) {
    std::vector<double> P(3 * static_cast<size_t>(K), 0.0);
    int nf = 0;
    if (!fixedFlag.empty())
        for (int i = 0; i < K; ++i)
            if (fixedFlag[i]) ++nf;
    if (nf == 0 && K == 1) return P;
    double sum = 0;
    for (int i = 0; i < K; ++i) sum += radius[i];
    if (nf == 0) {
        for (int c = 0; c < 3 * K; ++c) P[c] = dirs[c] * sum;
    } else {
        double ox = 0, oy = 0, oz = 0;
        for (int i = 0; i < K; ++i) {
            if (!fixedFlag[i]) continue;
            P[3 * i] = fixedAt[3 * i];
            P[3 * i + 1] = fixedAt[3 * i + 1];
            P[3 * i + 2] = fixedAt[3 * i + 2];
            ox += fixedAt[3 * i];
            oy += fixedAt[3 * i + 1];
            oz += fixedAt[3 * i + 2];
        }
        if (nf == K) return P;
        ox /= nf;
        oy /= nf;
        oz /= nf;
        for (int i = 0; i < K; ++i) {
            if (fixedFlag[i]) continue;
            P[3 * i] = ox + dirs[3 * i] * sum;
            P[3 * i + 1] = oy + dirs[3 * i + 1] * sum;
            P[3 * i + 2] = oz + dirs[3 * i + 2] * sum;
        }
    }

    double total = 0;
    for (int i = 0; i < K; ++i)
        for (int j = i + 1; j < K; ++j) total += link[i * K + j];
    if (total == 0) total = 1;
    std::vector<double> D(3 * static_cast<size_t>(K), 0.0);
    for (int step = 0; step < PackSteps; ++step) {
        std::fill(D.begin(), D.end(), 0.0);
        for (int i = 0; i < K; ++i)
            for (int j = i + 1; j < K; ++j) {
                const double dx = P[3 * i] - P[3 * j], dy = P[3 * i + 1] - P[3 * j + 1], dz = P[3 * i + 2] - P[3 * j + 2];
                const double dist = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 0.000001);
                double f = dist - Gap * (radius[i] + radius[j]);
                if (f > 0) f = f * ((0.02 + 0.5 * link[i * K + j] / total) * (opp[i * K + j] > 0 ? 0.25 : 1));
                const double ux = dx / dist * f * 0.5, uy = dy / dist * f * 0.5, uz = dz / dist * f * 0.5;
                D[3 * i] = D[3 * i] - ux;
                D[3 * i + 1] = D[3 * i + 1] - uy;
                D[3 * i + 2] = D[3 * i + 2] - uz;
                D[3 * j] = D[3 * j] + ux;
                D[3 * j + 1] = D[3 * j + 1] + uy;
                D[3 * j + 2] = D[3 * j + 2] + uz;
            }
        for (int i = 0; i < K; ++i) {
            if (nf > 0 && fixedFlag[i]) continue;
            for (int c = 3 * i; c < 3 * i + 3; ++c) P[c] = P[c] + D[c] * 0.5;
        }
    }
    if (nf > 0) return P;
    double mx = 0, my = 0, mz = 0;
    for (int i = 0; i < K; ++i) {
        mx += P[3 * i];
        my += P[3 * i + 1];
        mz += P[3 * i + 2];
    }
    mx /= K;
    my /= K;
    mz /= K;
    for (int i = 0; i < K; ++i) {
        P[3 * i] -= mx;
        P[3 * i + 1] -= my;
        P[3 * i + 2] -= mz;
    }
    return P;
}

GroupPointsAnswer GroupPoints(int n, const std::vector<char>& body, const std::vector<int>& taken, const std::vector<int>& group, const Pairs& pairs,
                              const Pairs& opps, const std::optional<PlateAnswer>& plate, const std::unordered_map<std::string, int>& at,
                              const std::vector<char>& gflag, const std::vector<double>& gat, const std::optional<P3>& frameP,
                              const std::vector<char>& hflag, const std::vector<double>& hat, double nearBand, Fail& fail) {
    const int k = static_cast<int>(taken.size());
    std::vector<int> part(n);
    for (int v = 0; v < n; ++v) part[v] = body[v] ? group[v] : -1;
    std::vector<int> size, link, opp;
    Counts(n, k, part, pairs, opps, size, link, opp);
    GroupPointsAnswer answer;
    answer.lobe.resize(k);
    for (int g = 0; g < k; ++g) answer.lobe[g] = Rest * Lobe * Root(size[g]);
    answer.pinned.assign(n, 0);

    if (!plate) {
        const std::vector<double> dirs = Directions(k, size, link, opp, fail);
        answer.cpt = Pack(k, answer.lobe, link, opp, dirs, gflag, gat);
        return answer;
    }

    std::vector<char>& pinned = answer.pinned;
    std::vector<double> q(3 * static_cast<size_t>(n), 0.0);
    int outside = 0;
    double half = 0;
    for (const std::string& id : plate->order) {
        const int v = at.at(id);
        const P3& xyz = plate->at.at(id);
        pinned[v] = 1;
        q[3 * v] = xyz[0];
        q[3 * v + 1] = xyz[1];
        q[3 * v + 2] = xyz[2];
        if (!body[v]) ++outside;
        half = std::max(half, std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1]));
    }

    std::vector<char> over(k, 0);
    std::vector<int> unit(k, -1);
    int pu = 0;
    for (int g = 0; g < k; ++g) {
        if (pinned[taken[g]]) over[g] = 1;
        else unit[g] = pu++;
    }
    const int K = pu + 1;
    std::vector<int> upart(n);
    for (int v = 0; v < n; ++v) upart[v] = !body[v] ? -1 : pinned[v] || over[group[v]] ? pu : unit[group[v]];
    std::vector<int> usize, ulink, uopp;
    Counts(n, K, upart, pairs, opps, usize, ulink, uopp);
    usize[pu] += outside;
    const std::vector<double> udirs = Directions(K, usize, ulink, uopp, fail);
    std::vector<double> radius(K, 0.0);
    for (int p = 0; p < pu; ++p) radius[p] = Rest * Lobe * Root(usize[p]);
    radius[pu] = half * Clear;
    std::vector<char> fflag;
    std::vector<double> fat;
    if (!gflag.empty()) {
        fflag.assign(K, 0);
        fat.assign(3 * static_cast<size_t>(K), 0.0);
        for (int g = 0; g < k; ++g) {
            if (over[g] || !gflag[g]) continue;
            const int p = unit[g];
            fflag[p] = 1;
            fat[3 * p] = gat[3 * g];
            fat[3 * p + 1] = gat[3 * g + 1];
            fat[3 * p + 2] = gat[3 * g + 2];
        }
        if (frameP) {
            fflag[pu] = 1;
            fat[3 * pu] = (*frameP)[0];
            fat[3 * pu + 1] = (*frameP)[1];
            fat[3 * pu + 2] = (*frameP)[2];
        }
    }
    const std::vector<double> upt = Pack(K, radius, ulink, uopp, udirs, fflag, fat);
    std::vector<double> plateAt(3 * static_cast<size_t>(n), 0.0);
    for (int v = 0; v < n; ++v) {
        if (!pinned[v]) continue;
        for (int c = 0; c < 3; ++c) plateAt[3 * v + c] = upt[3 * pu + c] + q[3 * v + c];
    }

    // H4: the snap, before any group reads its centre's standing place.
    if (!hflag.empty())
        for (int v = 0; v < n; ++v) {
            if (!pinned[v] || !hflag[v]) continue;
            bool close = true;
            for (int c = 0; c < 3; ++c) {
                const double d = plateAt[3 * v + c] - hat[3 * v + c];
                if (!(d <= nearBand && -d <= nearBand)) close = false;
            }
            if (close)
                for (int c = 0; c < 3; ++c) plateAt[3 * v + c] = hat[3 * v + c];
        }

    std::vector<double> cpt(3 * static_cast<size_t>(k), 0.0);
    for (int g = 0; g < k; ++g) {
        if (over[g]) {
            const int c = taken[g];
            cpt[3 * g] = plateAt[3 * c] + 0;
            cpt[3 * g + 1] = plateAt[3 * c + 1] + 0;
            cpt[3 * g + 2] = plateAt[3 * c + 2] + (answer.lobe[g] + Rest * 6);
        } else {
            const int p = unit[g];
            cpt[3 * g] = upt[3 * p];
            cpt[3 * g + 1] = upt[3 * p + 1];
            cpt[3 * g + 2] = upt[3 * p + 2];
        }
    }
    answer.cpt = std::move(cpt);
    answer.plateAt = std::move(plateAt);
    return answer;
}

// ---- Steps 6 to 8: the feed, the start, the steps ------------------------------------------------------------------

// FNV-1a, 32 bits, over the id's UTF-8 bytes (the product taken exactly, then mod 2^32: unsigned arithmetic).
uint32_t Fnv1a(const std::string& id) {
    uint32_t h = 2166136261u;
    for (unsigned char b : id) h = (h ^ b) * 16777619u;
    return h;
}

// by-kind's generator (its rule 4), exact in float64.
double Next(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s / 4294967296.0;
}

std::vector<double> Relax(int m, const std::vector<double>& p0, const std::vector<double>& anchor, const std::vector<double>& pull,
                          const std::vector<char>& isFixed, const std::vector<int>& la, const std::vector<int>& lb, const std::vector<double>& lw) {
    const double R = Rest;
    const double cut = Cutoff * R;
    const double cut2 = cut * cut;
    std::vector<double> px(m), py(m), pz(m);
    for (int i = 0; i < m; ++i) {
        px[i] = p0[3 * i];
        py[i] = p0[3 * i + 1];
        pz[i] = p0[3 * i + 2];
    }
    std::vector<double> scale(m, 0.0);
    for (size_t e = 0; e < la.size(); ++e) {
        scale[la[e]] += lw[e];
        scale[lb[e]] += lw[e];
    }
    for (int i = 0; i < m; ++i) scale[i] = 1 / std::max(1.0, scale[i] / 4);
    int movers = 0;
    for (int i = 0; i < m; ++i)
        if (!isFixed[i]) ++movers;
    const int run = movers == 0 ? std::min(Steps3d, 1) : Steps3d;

    std::vector<double> dx(m), dy(m), dz(m), lx(m), ly(m), lz(m);
    std::vector<int64_t> key(m);
    std::vector<int> order(m);
    std::unordered_map<int64_t, int> cells;
    std::vector<int> cellStart;
    std::vector<int64_t> cellKey;
    std::vector<char> cellFree;
    for (int step = 0; step < run; ++step) {
        // The cells: each mover's cell, the cells in rising order, each listing its movers in rising order.
        double x0 = INFINITY, y0 = INFINITY, z0 = INFINITY;
        double x1 = -INFINITY, y1 = -INFINITY, z1 = -INFINITY;
        for (int i = 0; i < m; ++i) {
            if (px[i] < x0) x0 = px[i];
            if (px[i] > x1) x1 = px[i];
            if (py[i] < y0) y0 = py[i];
            if (py[i] > y1) y1 = py[i];
            if (pz[i] < z0) z0 = pz[i];
            if (pz[i] > z1) z1 = pz[i];
        }
        const int64_t nx = static_cast<int64_t>(std::floor((x1 - x0) / cut)) + 1;
        const int64_t ny = static_cast<int64_t>(std::floor((y1 - y0) / cut)) + 1;
        const int64_t nz = static_cast<int64_t>(std::floor((z1 - z0) / cut)) + 1;
        for (int i = 0; i < m; ++i) {
            const int64_t cx = static_cast<int64_t>(std::floor((px[i] - x0) / cut));
            const int64_t cy = static_cast<int64_t>(std::floor((py[i] - y0) / cut));
            const int64_t cz = static_cast<int64_t>(std::floor((pz[i] - z0) / cut));
            key[i] = (cx * ny + cy) * nz + cz;
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&key](int a, int b) { return key[a] != key[b] ? key[a] < key[b] : a < b; });
        cells.clear();
        cellStart.clear();
        cellKey.clear();
        cellFree.clear();
        for (int u = 0; u < m; ++u) {
            const int i = order[u];
            if (cellKey.empty() || cellKey.back() != key[i]) {
                cells[key[i]] = static_cast<int>(cellKey.size());
                cellKey.push_back(key[i]);
                cellStart.push_back(u);
                cellFree.push_back(0);
            }
            if (!isFixed[i]) cellFree.back() = 1;
        }
        cellStart.push_back(m);

        // The push.
        std::fill(dx.begin(), dx.end(), 0.0);
        std::fill(dy.begin(), dy.end(), 0.0);
        std::fill(dz.begin(), dz.end(), 0.0);
        for (size_t ci = 0; ci < cellKey.size(); ++ci) {
            const int64_t ck = cellKey[ci];
            const int64_t cx = ck / (ny * nz);
            const int64_t cy = ck / nz % ny;
            const int64_t cz = ck % nz;
            const int s0 = cellStart[ci], s1 = cellStart[ci + 1];
            for (int ox = 0; ox <= 1; ++ox)
                for (int oy = ox == 1 ? -1 : 0; oy <= 1; ++oy)
                    for (int oz = ox == 1 || oy == 1 ? -1 : 0; oz <= 1; ++oz) {
                        const int64_t qx = cx + ox, qy = cy + oy, qz = cz + oz;
                        if (qx >= nx || qy < 0 || qy >= ny || qz < 0 || qz >= nz) continue;
                        auto found = cells.find((qx * ny + qy) * nz + qz);
                        if (found == cells.end()) continue;
                        const int qi = found->second;
                        if (!cellFree[ci] && !cellFree[qi]) continue;
                        const bool same = qi == static_cast<int>(ci);
                        const int t0 = cellStart[qi], t1 = cellStart[qi + 1];
                        for (int u = s0; u < s1; ++u) {
                            const int i = order[u];
                            for (int w = same ? u + 1 : t0; w < t1; ++w) {
                                const int j = order[w];
                                if (isFixed[i] && isFixed[j]) continue;
                                const double ux = px[i] - px[j], uy = py[i] - py[j], uz = pz[i] - pz[j];
                                const double d2 = ux * ux + uy * uy + uz * uz;
                                if (d2 > cut2) continue;
                                const double d = std::max(std::sqrt(d2), 0.000001);
                                const double f = ((R * R) / d - R / 3) / d;
                                dx[i] = dx[i] + ux * f;
                                dy[i] = dy[i] + uy * f;
                                dz[i] = dz[i] + uz * f;
                                dx[j] = dx[j] - ux * f;
                                dy[j] = dy[j] - uy * f;
                                dz[j] = dz[j] - uz * f;
                            }
                        }
                    }
        }

        // The links.
        std::fill(lx.begin(), lx.end(), 0.0);
        std::fill(ly.begin(), ly.end(), 0.0);
        std::fill(lz.begin(), lz.end(), 0.0);
        for (size_t e = 0; e < la.size(); ++e) {
            const int a = la[e], b = lb[e];
            const double ux = px[a] - px[b], uy = py[a] - py[b], uz = pz[a] - pz[b];
            const double d = std::max(std::sqrt(ux * ux + uy * uy + uz * uz), 0.000001);
            const double f = ((d - R) * 0.5 * lw[e]) / d;
            lx[b] = lx[b] + ux * f;
            ly[b] = ly[b] + uy * f;
            lz[b] = lz[b] + uz * f;
            lx[a] = lx[a] - ux * f;
            ly[a] = ly[a] - uy * f;
            lz[a] = lz[a] - uz * f;
        }

        // The move.
        const double cap = Cap0 * R * (1 - static_cast<double>(step) / Steps3d) + R / 20;
        for (int i = 0; i < m; ++i) {
            if (isFixed[i]) {
                px[i] = anchor[3 * i];
                py[i] = anchor[3 * i + 1];
                pz[i] = anchor[3 * i + 2];
                continue;
            }
            const double vx = dx[i] + lx[i] * scale[i] + (anchor[3 * i] - px[i]) * pull[i];
            const double vy = dy[i] + ly[i] * scale[i] + (anchor[3 * i + 1] - py[i]) * pull[i];
            const double vz = dz[i] + lz[i] * scale[i] + (anchor[3 * i + 2] - pz[i]) * pull[i];
            const double len = std::max(std::sqrt(vx * vx + vy * vy + vz * vz), 0.000000001);
            const double shrink = std::min(1.0, cap / len);
            px[i] = px[i] + vx * shrink;
            py[i] = py[i] + vy * shrink;
            pz[i] = pz[i] + vz * shrink;
        }
    }
    std::vector<double> pos(3 * static_cast<size_t>(m));
    for (int i = 0; i < m; ++i) {
        pos[3 * i] = px[i];
        pos[3 * i + 1] = py[i];
        pos[3 * i + 2] = pz[i];
    }
    return pos;
}

// Steps 6, 7 and 8: each body node's point after the force steps, row v at 3v (rows outside the body unread).
std::vector<double> ForceSteps(const std::vector<std::string>& ids, const std::vector<char>& body, const Pairs& pairs, const std::vector<int>& taken,
                               const std::vector<int>& group, const GroupPointsAnswer& pts, const std::vector<char>& keep, const std::vector<double>& hat,
                               const std::vector<char>& freeGroup, const std::vector<int>& extra) {
    const int n = static_cast<int>(ids.size());
    std::vector<int> blist, bi(n, -1);
    for (int v = 0; v < n; ++v) {
        if (!body[v]) continue;
        bi[v] = static_cast<int>(blist.size());
        blist.push_back(v);
    }
    const int nb = static_cast<int>(blist.size());
    std::vector<char> centre(n, 0);
    for (int t : taken) centre[t] = 1;
    const bool plate = !pts.plateAt.empty();
    auto pinnedAt = [&](int v) { return plate && pts.pinned[v]; };
    auto kept = [&](int v) { return !keep.empty() && keep[v]; };

    // 6. The movers, their anchors, fixed or not, their links and weights.
    std::vector<int> la, lb;
    std::vector<double> lw;
    for (const auto& p : pairs) {
        const int i = bi[p[0]], j = bi[p[1]];
        if (i < 0 || j < 0) continue;
        la.push_back(i);
        lb.push_back(j);
        const int u = blist[i], w = blist[j];
        double x = group[u] == group[w] ? 1.0 : Cross;
        if (pinnedAt(u) || pinnedAt(w)) x = PlateWeight;
        lw.push_back(x);
    }
    const int m = nb + static_cast<int>(extra.size());
    std::vector<double> anchor(3 * static_cast<size_t>(m), 0.0), pull(m, 0.0);
    std::vector<char> isFixed(m, 0);
    std::vector<int> mgroup(m, -1);
    std::vector<const std::string*> mid(m, nullptr);
    for (int i = 0; i < nb; ++i) {
        const int v = blist[i];
        if (pinnedAt(v))
            for (int c = 0; c < 3; ++c) anchor[3 * i + c] = pts.plateAt[3 * v + c];
        else if (kept(v))
            for (int c = 0; c < 3; ++c) anchor[3 * i + c] = hat[3 * v + c];
        else
            for (int c = 0; c < 3; ++c) anchor[3 * i + c] = pts.cpt[3 * group[v] + c];
        pull[i] = Hold;
        isFixed[i] = (centre[v] && !(!freeGroup.empty() && freeGroup[group[v]])) || pinnedAt(v) || kept(v);
        mgroup[i] = group[v];
        mid[i] = &ids[v];
    }
    for (size_t e = 0; e < extra.size(); ++e) {
        const int i = nb + static_cast<int>(e);
        const int v = extra[e];
        for (int c = 0; c < 3; ++c) anchor[3 * i + c] = hat[3 * v + c];
        pull[i] = 0;
        isFixed[i] = 1;
        mgroup[i] = -1;
        mid[i] = &ids[v];
    }

    // 7. The start: a fixed mover at its anchor; any other in its group's ball, by its own seeded generator.
    std::vector<double> p0(3 * static_cast<size_t>(m), 0.0);
    std::unordered_map<int, int> freeCount;
    for (int i = 0; i < m; ++i)
        if (!isFixed[i]) ++freeCount[mgroup[i]];
    for (int i = 0; i < m; ++i) {
        if (isFixed[i]) {
            for (int c = 0; c < 3; ++c) p0[3 * i + c] = anchor[3 * i + c];
            continue;
        }
        const double ball = Rest * Lobe * Root(freeCount[mgroup[i]]);
        uint32_t g = Fnv1a(*mid[i]);
        Next(g);
        Next(g);
        double x = 0, y = 0, z = 0;
        for (int tries = 0; tries < 64; ++tries) {
            const double r1 = 2 * Next(g) - 1;
            const double r2 = 2 * Next(g) - 1;
            const double r3 = 2 * Next(g) - 1;
            if (r1 * r1 + r2 * r2 + r3 * r3 <= 1) {
                x = r1;
                y = r2;
                z = r3;
                break;
            }
        }
        p0[3 * i] = anchor[3 * i] + (0 + x * ball);
        p0[3 * i + 1] = anchor[3 * i + 1] + (0 + y * ball);
        p0[3 * i + 2] = anchor[3 * i + 2] + (0 + z * ball);
    }

    // 8. The steps.
    const std::vector<double> pos = Relax(m, p0, anchor, pull, isFixed, la, lb, lw);
    std::vector<double> relaxed(3 * static_cast<size_t>(n), 0.0);
    for (int i = 0; i < nb; ++i)
        for (int c = 0; c < 3; ++c) relaxed[3 * blist[i] + c] = pos[3 * i + c];
    return relaxed;
}

// ---- Step 9: what is placed after the steps, with H6 ---------------------------------------------------------------

double Len3(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }

double Skin(std::vector<double> a) {
    const int m = static_cast<int>(a.size());
    if (m == 0) return 0;
    std::sort(a.begin(), a.end());
    const double vi = (m - 1) * 0.9;
    int lo, hi;
    if (vi >= m - 1) lo = hi = m - 1;
    else {
        lo = static_cast<int>(std::floor(vi));
        hi = lo + 1;
    }
    const double t = vi - lo;
    const double d = a[hi] - a[lo];
    return t >= 0.5 ? a[hi] - d * (1 - t) : a[lo] + d * t;
}

std::vector<int> Hang(int n, const BodyAnswer& b, const std::vector<int>& taken, const std::vector<int>& group, const GroupPointsAnswer& pts,
                      const std::vector<int>& degree, const std::vector<double>& relaxed, const std::vector<char>& keep, const std::vector<double>& hat,
                      std::vector<double>& pos, Fail& fail) {
    const int k = static_cast<int>(taken.size());
    const std::vector<double>& cpt = pts.cpt;
    const std::vector<double>& plateAt = pts.plateAt;
    const std::vector<char>& pinned = pts.pinned;
    const std::vector<char>& body = b.body;
    pos.assign(3 * static_cast<size_t>(n), 0.0);
    std::vector<int> how(n, -1);

    // The kept nodes (held, not pinned) and the near test against every one of them.
    std::vector<int> kept;
    if (!keep.empty())
        for (int v = 0; v < n; ++v)
            if (keep[v] && !pinned[v]) kept.push_back(v);
    auto isNear = [&](double x, double y, double z) {
        for (int u : kept)
            if (Len3(x - hat[3 * u], y - hat[3 * u + 1], z - hat[3 * u + 2]) < Line) return true;
        return false;
    };

    // The body.
    int nbody = 0;
    for (int v = 0; v < n; ++v) {
        if (!body[v]) continue;
        for (int c = 0; c < 3; ++c) pos[3 * v + c] = relaxed[3 * v + c];
        how[v] = taken[group[v]] == v ? HowCentre : HowWith;
        ++nbody;
    }

    // The middle.
    double midX = 0, midY = 0, midZ = 0;
    for (int v = 0; v < n; ++v) {
        if (!body[v]) continue;
        midX += relaxed[3 * v];
        midY += relaxed[3 * v + 1];
        midZ += relaxed[3 * v + 2];
    }
    midX /= nbody;
    midY /= nbody;
    midZ /= nbody;

    // The skin of each group.
    std::vector<double> skin(k, 0.0);
    for (int g = 0; g < k; ++g) {
        std::vector<double> vals;
        for (int v = 0; v < n; ++v) {
            if (!body[v] || group[v] != g || pinned[v]) continue;
            vals.push_back(Len3(relaxed[3 * v] - cpt[3 * g], relaxed[3 * v + 1] - cpt[3 * g + 1], relaxed[3 * v + 2] - cpt[3 * g + 2]));
        }
        skin[g] = Skin(std::move(vals));
    }

    // What hangs: each cut in the one order, its nodes by depth, then the one order.
    std::vector<std::vector<int>> lists(n);
    for (int v = 0; v < n; ++v)
        if (b.hangCut[v] != -1) lists[b.hangCut[v]].push_back(v);
    double fp[3] = {0, 0, 0};
    for (int c = 0; c < n; ++c) {
        std::vector<int>& row = lists[c];
        if (row.empty()) continue;
        std::sort(row.begin(), row.end(), [&b](int x, int y) { return b.hangDepth[x] != b.hangDepth[y] ? b.hangDepth[x] < b.hangDepth[y] : x < y; });
        const int g = group[c];

        // Outward.
        double ux = pos[3 * c] - cpt[3 * g], uy = pos[3 * c + 1] - cpt[3 * g + 1], uz = pos[3 * c + 2] - cpt[3 * g + 2];
        const double lu = Len3(ux, uy, uz);
        if (lu > 0.000001) {
            ux = ux / lu;
            uy = uy / lu;
            uz = uz / lu;
        } else {
            ux = uy = uz = 0.0;
        }
        double ox = cpt[3 * g] - midX, oy = cpt[3 * g + 1] - midY, oz = cpt[3 * g + 2] - midZ;
        const double lo = Len3(ox, oy, oz);
        if (lo > 0.000001) {
            ox = ox / lo;
            oy = oy / lo;
            oz = oz / lo;
        } else {
            ox = 0.0;
            oy = 0.0;
            oz = 1.0;
        }
        ux = ux + 0.75 * ox;
        uy = uy + 0.75 * oy;
        uz = uz + 0.75 * oz;
        const double l = Len3(ux, uy, uz);
        ux = ux / l;
        uy = uy / l;
        uz = uz / l;

        // t = u x (0, 0, 1), else u x (1, 0, 0); w = u x t.
        double tx = uy * 1.0 - uz * 0.0, ty = uz * 0.0 - ux * 1.0, tz = ux * 0.0 - uy * 0.0;
        if (Len3(tx, ty, tz) < 0.001) {
            tx = uy * 0.0 - uz * 0.0;
            ty = uz * 1.0 - ux * 0.0;
            tz = ux * 0.0 - uy * 1.0;
        }
        const double lt = Len3(tx, ty, tz);
        tx = tx / lt;
        ty = ty / lt;
        tz = tz / lt;
        const double wx = uy * tz - uz * ty, wy = uz * tx - ux * tz, wz = ux * ty - uy * tx;
        const bool overPlace = pinned[c] != 0;

        auto fanPoint = [&](int j, int d) {
            const double ang = Gold * j;
            const double fan = Rest * 0.5 * std::sqrt(static_cast<double>(j));
            const double ca = std::cos(ang), sa = std::sin(ang);
            if (overPlace) {
                fp[0] = plateAt[3 * c] + ca * fan * 0.5;
                fp[1] = plateAt[3 * c + 1] + sa * fan * 0.5;
                fp[2] = plateAt[3 * c + 2] + Rest * d;
            } else {
                const double reach = skin[g] + Rest * d;
                fp[0] = cpt[3 * g] + ux * reach + (tx * ca + wx * sa) * fan;
                fp[1] = cpt[3 * g + 1] + uy * reach + (ty * ca + wy * sa) * fan;
                fp[2] = cpt[3 * g + 2] + uz * reach + (tz * ca + wz * sa) * fan;
            }
        };

        const bool claims = !keep.empty();
        std::unordered_set<int> used;
        if (claims) {
            // H6: first the claims of the kept nodes of the list.
            for (int v : row) {
                if (!keep[v] || pinned[v]) continue;
                const int d = b.hangDepth[v];
                const double hx = hat[3 * v], hy = hat[3 * v + 1], hz = hat[3 * v + 2];
                double axx, axy, axz;
                if (overPlace) {
                    axx = plateAt[3 * c];
                    axy = plateAt[3 * c + 1];
                    axz = plateAt[3 * c + 2] + Rest * d;
                } else {
                    const double reach = skin[g] + Rest * d;
                    axx = cpt[3 * g] + ux * reach;
                    axy = cpt[3 * g + 1] + uy * reach;
                    axz = cpt[3 * g + 2] + uz * reach;
                }
                const double bound = Len3(hx - axx, hy - axy, hz - axz) + Line + Rest;
                for (int j = 0;; ++j) {
                    const double fan = Rest * 0.5 * std::sqrt(static_cast<double>(j));
                    if (!((overPlace ? fan * 0.5 : fan) < bound)) break;
                    fanPoint(j, d);
                    if (Len3(fp[0] - hx, fp[1] - hy, fp[2] - hz) < Line) used.insert(j);
                }
            }
        }
        for (size_t r = 0; r < row.size(); ++r) {
            const int v = row[r];
            const int d = b.hangDepth[v];
            how[v] = degree[v] == 1 && d == 1 ? HowLink : HowArm;
            int j = static_cast<int>(r);
            if (claims) {
                if (keep[v] && !pinned[v]) continue;
                for (j = 0;; ++j) {
                    if (used.count(j)) continue;
                    fanPoint(j, d);
                    if (!isNear(fp[0], fp[1], fp[2])) break;
                }
                used.insert(j);
            }
            fanPoint(j, d);
            pos[3 * v] = fp[0];
            pos[3 * v + 1] = fp[1];
            pos[3 * v + 2] = fp[2];
        }
    }

    // Off the main piece: placed by no rule of a run.
    for (int v = 0; v < n; ++v)
        if (b.off[v] && !pinned[v]) how[v] = HowOff;
    // Kept: every kept node at its held point.
    for (int v : kept)
        for (int c = 0; c < 3; ++c) pos[3 * v + c] = hat[3 * v + c];
    // The map, last: every pinned node at its standing place.
    for (int v = 0; v < n; ++v) {
        if (!pinned[v]) continue;
        for (int c = 0; c < 3; ++c) pos[3 * v + c] = plateAt[3 * v + c];
        how[v] = HowMap;
    }
    for (int v = 0; v < n; ++v)
        if (how[v] == -1) fail.Set("", "a node was placed by no rule");
    return how;
}

// ---- Steps 1 to 9 and the held points' rules: one run --------------------------------------------------------------

struct RunAnswer {
    std::vector<double> points;
    std::vector<int> how;
    std::vector<std::optional<std::string>> group;
    std::vector<std::optional<std::string>> part;
    std::vector<char> held;
    std::vector<std::string> centres;
    std::unordered_map<std::string, int> row;
};

RunAnswer Run(const std::vector<std::string>& handed, const std::vector<Link3>& edgesIn, const std::optional<Space>& space,
              const std::optional<HeldList>& heldIn, double unit, Fail& fail) {
    RunAnswer answer;
    TestBox(heldIn, fail);
    if (fail.failed) return answer;
    const int n = static_cast<int>(handed.size());

    // 1. The one order, and the pairs.
    std::vector<std::string> ids = handed;
    std::sort(ids.begin(), ids.end());
    std::unordered_map<std::string, int> at;
    for (int i = 0; i < n; ++i) at[ids[i]] = i;
    Pairs opp, pairs;
    std::unordered_set<int64_t> seen;
    for (const Link3& e : edgesIn) {
        auto sa = at.find(e.source), sb = at.find(e.target);
        if (sa == at.end() || sb == at.end()) continue;
        const int a = sa->second, b = sb->second;
        if (Opposing(e.field)) opp.push_back({a, b});
        if (a == b) continue;
        const int lo = a < b ? a : b, hi = a < b ? b : a;
        if (seen.insert(static_cast<int64_t>(lo) * n + hi).second) pairs.push_back({lo, hi});
    }
    std::sort(pairs.begin(), pairs.end());
    std::vector<std::vector<int>> adj(n);
    for (const auto& p : pairs) {
        adj[p[0]].push_back(p[1]);
        adj[p[1]].push_back(p[0]);
    }
    for (auto& l : adj) std::sort(l.begin(), l.end());
    std::vector<int> degree(n);
    for (int i = 0; i < n; ++i) degree[i] = static_cast<int>(adj[i].size());

    // 2. The body. 3. The centres and their groups.
    const BodyAnswer body = Body(n, adj);
    std::vector<int> group;
    const std::vector<int> taken = Centres(n, adj, body.body, group);
    const int k = static_cast<int>(taken.size());

    // The held points that are nodes (a held id that is no node is passed over).
    std::vector<char> hflag;
    std::vector<double> hat;
    if (heldIn)
        for (const auto& p : *heldIn) {
            auto found = at.find(p.first);
            if (found == at.end()) continue;
            if (hflag.empty()) {
                hflag.assign(n, 0);
                hat.assign(3 * static_cast<size_t>(n), 0.0);
            }
            const int v = found->second;
            hflag[v] = 1;
            hat[3 * v] = p.second[0];
            hat[3 * v + 1] = p.second[1];
            hat[3 * v + 2] = p.second[2];
        }

    // 4. The plate; with held points, H1's frame, and the plate again at the frame's scale.
    std::optional<PlateAnswer> plate;
    std::optional<P3> frameP;
    if (space) {
        plate = PlateOf(*space, at, std::nullopt);
        double s = 0;
        P3 F{};
        if (plate && !hflag.empty() && FrameOf(*plate, at, hflag, hat, unit, s, F)) {
            frameP = F;
            plate = PlateOf(*space, at, s);
        }
    }

    // H2: each group's case; H3's fixed parts and H4's snap for step 5; H5's kept nodes.
    std::vector<char> keep, freeGroup, gflag;
    std::vector<double> gat;
    std::vector<int> extra;
    if (!hflag.empty()) {
        std::vector<char> pinnedV(n, 0);
        if (plate)
            for (const std::string& id : plate->order) pinnedV[at.at(id)] = 1;
        keep.assign(n, 0);
        for (int v = 0; v < n; ++v) keep[v] = hflag[v] && !pinnedV[v];
        gflag.assign(k, 0);
        gat.assign(3 * static_cast<size_t>(k), 0.0);
        freeGroup.assign(k, 0);
        std::vector<double> gs(3 * static_cast<size_t>(k), 0.0);
        std::vector<int> gc(k, 0);
        for (int v = 0; v < n; ++v) {
            if (!body.body[v] || !keep[v]) continue;
            const int g = group[v];
            gs[3 * g] += hat[3 * v];
            gs[3 * g + 1] += hat[3 * v + 1];
            gs[3 * g + 2] += hat[3 * v + 2];
            ++gc[g];
        }
        for (int g = 0; g < k; ++g) {
            const int t = taken[g];
            if (pinnedV[t]) continue;
            if (keep[t]) {
                gflag[g] = 1;
                gat[3 * g] = hat[3 * t];
                gat[3 * g + 1] = hat[3 * t + 1];
                gat[3 * g + 2] = hat[3 * t + 2];
            } else if (gc[g] > 0) {
                gflag[g] = 1;
                freeGroup[g] = 1;
                gat[3 * g] = gs[3 * g] / gc[g];
                gat[3 * g + 1] = gs[3 * g + 1] / gc[g];
                gat[3 * g + 2] = gs[3 * g + 2] / gc[g];
            } else {
                freeGroup[g] = 1;
            }
        }
        for (int v = 0; v < n; ++v)
            if (keep[v] && !body.body[v]) extra.push_back(v);
    }

    // 5. Where each group's point stands.
    const GroupPointsAnswer pts = GroupPoints(n, body.body, taken, group, pairs, opp, plate, at, gflag, gat, frameP, hflag, hat, 3 * unit + Snap, fail);
    if (fail.failed) return answer;

    // 6 to 8. The feed, the start, the steps.
    const std::vector<double> relaxed = ForceSteps(ids, body.body, pairs, taken, group, pts, keep, hat, freeGroup, extra);

    // 9. What is placed after the steps.
    std::vector<double> pos;
    const std::vector<int> how = Hang(n, body, taken, group, pts, degree, relaxed, keep, hat, pos, fail);
    if (fail.failed) return answer;

    // The run's answer, in the order handed in.
    answer.points.assign(3 * static_cast<size_t>(n), 0.0);
    answer.how.assign(n, 0);
    answer.group.assign(n, std::nullopt);
    answer.part.assign(n, std::nullopt);
    answer.held.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        const int v = at.at(handed[i]);
        answer.row[handed[i]] = i;
        answer.points[3 * i] = pos[3 * v];
        answer.points[3 * i + 1] = pos[3 * v + 1];
        answer.points[3 * i + 2] = pos[3 * v + 2];
        answer.held[i] = !keep.empty() && keep[v];
        const int w = how[v];
        answer.how[i] = w;
        const bool own = body.body[v] && taken[group[v]] == v;
        if (w == HowCentre || w == HowWith || (w == HowMap && own)) answer.group[i] = ids[taken[group[v]]];
        else if (w == HowMap) answer.part[i] = std::string("map");
        else if (w == HowArm || w == HowLink) answer.part[i] = std::string("one-link");
    }
    for (int t : taken) answer.centres.push_back(ids[t]);
    return answer;
}

// ---- Steps 10 to 12: the pieces, the ring and the answer -----------------------------------------------------------

void Pieces(const std::vector<std::string>& handed, const std::vector<Link3>& edges, const std::optional<Space>& space,
            const std::optional<HeldList>& held, double unit, OWLayoutByLinks3d& out, Fail& fail) {
    const int n = static_cast<int>(handed.size());
    if (n == 0) return;
    std::vector<std::string> ids = handed;
    std::sort(ids.begin(), ids.end());
    for (int i = 1; i < n; ++i)
        if (ids[i] == ids[i - 1]) {
            fail.Set("", "two nodes share the id " + ids[i]);
            return;
        }
    std::unordered_map<std::string, int> at;
    for (int i = 0; i < n; ++i) at[ids[i]] = i;

    // The pieces: a path of pairs joins two nodes of one piece; the main piece the largest, a tie to the lowest node.
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    auto find = [&parent](int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    for (const Link3& e : edges) {
        auto sa = at.find(e.source), sb = at.find(e.target);
        if (sa == at.end() || sb == at.end() || sa->second == sb->second) continue;
        const int ra = find(sa->second), rb = find(sb->second);
        if (ra != rb) {
            if (ra < rb) parent[rb] = ra;
            else parent[ra] = rb;
        }
    }
    std::vector<int> pieceOf(n), rootPiece(n, -1);
    std::vector<std::vector<int>> pieces;
    for (int v = 0; v < n; ++v) {
        const int r = find(v);
        if (rootPiece[r] == -1) {
            rootPiece[r] = static_cast<int>(pieces.size());
            pieces.emplace_back();
        }
        pieceOf[v] = rootPiece[r];
        pieces[pieceOf[v]].push_back(v);
    }
    int mainP = -1, mainSize = 0;
    for (size_t p = 0; p < pieces.size(); ++p) {
        if (static_cast<int>(pieces[p].size()) <= mainSize) continue;
        mainSize = static_cast<int>(pieces[p].size());
        mainP = static_cast<int>(p);
    }

    // The main run holds the held points of the main piece's nodes (when it holds a pair) and of every pinned node.
    std::unordered_map<std::string, P3> heldAll;
    if (held)
        for (const auto& p : *held) heldAll[p.first] = p.second;
    std::unordered_set<std::string> onMap;
    if (space) {
        const std::optional<PlateAnswer> pa = PlateOf(*space, at, std::nullopt);
        if (pa) onMap.insert(pa->order.begin(), pa->order.end());
    }
    std::optional<HeldList> mainHeld;
    if (held) {
        mainHeld.emplace();
        for (const auto& p : *held) {
            auto found = at.find(p.first);
            if (found != at.end() && ((mainSize >= 2 && pieceOf[found->second] == mainP) || onMap.count(p.first))) mainHeld->push_back(p);
        }
    }
    const RunAnswer main = Run(handed, edges, space, mainHeld, unit, fail);
    if (fail.failed) return;

    std::vector<char> pinned(n, 0), keep(n, 0);
    for (int v = 0; v < n; ++v) {
        if (main.how[main.row.at(ids[v])] == HowMap) pinned[v] = 1;
        if ((mainSize >= 2 && pieceOf[v] == mainP) || pinned[v]) keep[v] = 1;
    }

    // Step 11's order: every other piece that holds a pair, most nodes first, a tie to the lower least node.
    std::vector<int> others;
    for (size_t p = 0; p < pieces.size(); ++p)
        if (static_cast<int>(p) != mainP && pieces[p].size() >= 2) others.push_back(static_cast<int>(p));
    std::sort(others.begin(), others.end(), [&pieces](int p, int q) {
        return pieces[q].size() != pieces[p].size() ? pieces[q].size() < pieces[p].size() : pieces[p][0] < pieces[q][0];
    });

    // A piece's run: its nodes and its edges in the order handed in, no Maps, its pinned nodes held at their main-run points.
    std::unordered_map<int, RunAnswer> runs;
    std::unordered_map<int, bool> moved;
    for (int p : others) {
        std::unordered_set<std::string> inP;
        for (int v : pieces[p]) inP.insert(ids[v]);
        std::vector<std::string> pnodes;
        for (const std::string& id : handed)
            if (inP.count(id)) pnodes.push_back(id);
        std::vector<Link3> pedges;
        for (const Link3& e : edges)
            if (inP.count(e.source) && inP.count(e.target)) pedges.push_back(e);
        HeldList sub;
        for (int v : pieces[p]) {
            if (pinned[v]) {
                const int m = main.row.at(ids[v]);
                sub.emplace_back(ids[v], P3{main.points[3 * m], main.points[3 * m + 1], main.points[3 * m + 2]});
            } else {
                auto q = heldAll.find(ids[v]);
                if (q != heldAll.end()) sub.emplace_back(ids[v], q->second);
            }
        }
        const bool holds = !sub.empty();
        runs[p] = Run(pnodes, pedges, std::nullopt, holds ? std::optional<HeldList>(std::move(sub)) : std::nullopt, unit, fail);
        if (fail.failed) return;
        moved[p] = !holds;
    }

    // Step 11. The main shape: M over the nodes taken from the main run, in the one order; Rmain over those and every
    // node of a piece that is not moved.
    int cnt = 0;
    double sx = 0, sy = 0, sz = 0;
    for (int v = 0; v < n; ++v) {
        if (!keep[v]) continue;
        const int m = main.row.at(ids[v]);
        sx += main.points[3 * m];
        sy += main.points[3 * m + 1];
        sz += main.points[3 * m + 2];
        ++cnt;
    }
    const P3 M = cnt > 0 ? P3{sx / cnt, sy / cnt, sz / cnt} : P3{0, 0, 0};
    double rMain = 0;
    auto reach3 = [&M, &rMain](double x, double y, double z) {
        const double dx = x - M[0], dy = y - M[1], dz = z - M[2];
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > rMain) rMain = d;
    };
    for (int v = 0; v < n; ++v) {
        if (!keep[v]) continue;
        const int m = main.row.at(ids[v]);
        reach3(main.points[3 * m], main.points[3 * m + 1], main.points[3 * m + 2]);
    }
    for (int p : others) {
        if (moved[p]) continue;
        const RunAnswer& u = runs[p];
        for (int v : pieces[p]) {
            if (pinned[v]) continue;
            const int j = u.row.at(ids[v]);
            reach3(u.points[3 * j], u.points[3 * j + 1], u.points[3 * j + 2]);
        }
    }

    std::vector<int> ringList;
    for (int p : others)
        if (moved[p]) ringList.push_back(p);
    std::unordered_map<int, P3> c;
    std::unordered_map<int, double> rp;
    for (int p : ringList) {
        const RunAnswer& u = runs[p];
        double qx = 0, qy = 0, qz = 0;
        for (int v : pieces[p]) {
            const int i = u.row.at(ids[v]);
            qx += u.points[3 * i];
            qy += u.points[3 * i + 1];
            qz += u.points[3 * i + 2];
        }
        const int kk = static_cast<int>(pieces[p].size());
        const P3 cp{qx / kk, qy / kk, qz / kk};
        double r = 0;
        for (int v : pieces[p]) {
            const int i = u.row.at(ids[v]);
            const double dx = u.points[3 * i] - cp[0], dy = u.points[3 * i + 1] - cp[1], dz = u.points[3 * i + 2] - cp[2];
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > r) r = d;
        }
        c[p] = cp;
        rp[p] = r;
    }
    double rmax = 0;
    for (int p : ringList)
        if (rp[p] > rmax) rmax = rp[p];
    std::unordered_map<int, P3> shift;
    const double fitEdge = 2 * Pi - Fit;
    double ringR = rMain + RingGap + rmax;
    std::vector<std::pair<int, double>> on;
    double sum = 0;
    auto close = [&]() {
        const double share = (2 * Pi - sum) / static_cast<double>(on.size());
        double a = 0;
        for (const auto& q : on) {
            const double phi = a + q.second / 2;
            a = a + (q.second + share);
            const P3& cp = c[q.first];
            shift[q.first] = P3{M[0] + ringR * std::cos(phi) - cp[0], M[1] + ringR * std::sin(phi) - cp[1], M[2] - cp[2]};
        }
    };
    for (int p : ringList) {
        double th = 2 * std::asin((rp[p] + RingGap / 2) / ringR);
        if (!on.empty() && !(sum + th <= fitEdge)) {
            close();
            ringR = ringR + 2 * rmax + RingGap;
            on.clear();
            sum = 0;
            th = 2 * std::asin((rp[p] + RingGap / 2) / ringR);
        }
        on.emplace_back(p, th);
        sum = sum + th;
    }
    if (!on.empty()) close();

    // Step 12. The answer, in the order the nodes were handed in.
    std::vector<int> unplaced;
    std::unordered_set<std::string> placed;
    for (const std::string& id : handed) {
        const int v = at.at(id);
        if (keep[v]) {
            const int m = main.row.at(id);
            std::optional<std::string> group = main.group[m];
            std::optional<std::string> part = main.part[m];
            if (pinned[v] && pieceOf[v] != mainP) {
                auto pr = runs.find(pieceOf[v]);
                if (pr != runs.end() && std::find(pr->second.centres.begin(), pr->second.centres.end(), id) != pr->second.centres.end()) {
                    group = id;
                    part = std::nullopt;
                }
            }
            out.points.push_back(OWLayout3dPoint{id, main.points[3 * m], main.points[3 * m + 1], main.points[3 * m + 2], HowWords[main.how[m]], group, part,
                                                 main.held[m] != 0});
            placed.insert(id);
            continue;
        }
        const int p = pieceOf[v];
        if (pieces[p].size() < 2) {
            unplaced.push_back(v);
            continue;
        }
        const RunAnswer& u = runs[p];
        const int j = u.row.at(id);
        double x = u.points[3 * j], y = u.points[3 * j + 1], z = u.points[3 * j + 2];
        auto T = shift.find(p);
        if (T != shift.end()) {
            x = x + T->second[0];
            y = y + T->second[1];
            z = z + T->second[2];
        }
        out.points.push_back(OWLayout3dPoint{id, x, y, z, HowWords[u.how[j]], u.group[j], u.part[j], u.held[j] != 0});
        placed.insert(id);
    }
    std::sort(unplaced.begin(), unplaced.end());
    for (const std::string& id : main.centres)
        if (placed.count(id)) out.centres.push_back(id);
    for (int p : others) out.centres.insert(out.centres.end(), runs[p].centres.begin(), runs[p].centres.end());
    for (int v : unplaced) out.unplaced.push_back(ids[v]);
}

}  // namespace

OWLayoutByLinks3d LayoutByLinks3d(const OWJsonValue& graph, const OWJsonValue* elements, const OWJsonValue* held, const OWJsonValue* heldUnit) {
    OWLayoutByLinks3d out;
    Fail fail;
    const OWJsonValue* givenNodes = graph.IsObject() ? graph.Find("nodes") : nullptr;
    const OWJsonValue* givenEdges = graph.IsObject() ? graph.Find("edges") : nullptr;
    if (!givenNodes || !givenNodes->IsArray() || !givenEdges || !givenEdges->IsArray()) {
        out.error = "LayoutByLinks3d: the graph is { nodes, edges }, as WorldGraph answers it";
        return out;
    }
    const std::optional<HeldList> heldIn = ReadHeld(held, fail);
    const double unit = fail.failed ? 0 : ReadUnit(heldUnit, fail);
    std::vector<std::string> handed;
    if (!fail.failed)
        for (size_t i = 0; i < givenNodes->Size(); ++i) {
            const OWJsonValue& node = givenNodes->Items()[i];
            const OWJsonValue* id = node.IsObject() ? node.Find("id") : nullptr;
            if (!id || !id->IsString()) {
                fail.Set("", "node " + std::to_string(i) + " has no id");
                break;
            }
            handed.push_back(id->Text());
        }
    std::vector<Link3> edges;
    for (const OWJsonValue& e : givenEdges->Items()) {
        if (!e.IsObject()) continue;
        const OWJsonValue* s = e.Find("source");
        const OWJsonValue* g = e.Find("target");
        if (!s || !s->IsString() || !g || !g->IsString()) continue;
        const OWJsonValue* f = e.Find("field");
        edges.push_back(Link3{s->Text(), g->Text(), f && f->IsString() ? std::optional<std::string>(f->Text()) : std::nullopt});
    }
    std::optional<Space> space;
    if (!fail.failed) space = SpaceOf(elements, fail);
    if (!fail.failed) Pieces(handed, edges, space, heldIn, unit, out, fail);
    if (fail.failed) {
        out.points.clear();
        out.centres.clear();
        out.unplaced.clear();
        out.error = fail.message;
        out.code = fail.code;
    }
    return out;
}

OWLayoutByLinks3d LayoutByLinks3d(const OWWorldGraph& graph, const OWJsonValue* elements, const OWJsonValue* held, const OWJsonValue* heldUnit) {
    return LayoutByLinks3d(graph.ToJson(), elements, held, heldUnit);
}

}  // namespace OWLayout
}  // namespace OnlyWorlds
