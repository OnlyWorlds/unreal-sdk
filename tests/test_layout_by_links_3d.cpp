// by-links-3d on its shared fixture: the pinned dist's profiles/layout-by-links-3d.fixture.json and the directions table
// beside it (the default layouts page, profiles/layout.md, serial 137), read from the vendored folder through OWJson,
// whose from_chars reads every double exactly. Each of the 116 cases is run whole, as the fixture's `call` says, and its
// want compared as its `compare` says: every ids, how, group, part, held, centres and unplaced entry exactly; each x, y and
// z exactly (a zero by its value), save an element of a ring piece (found from the graph and the want) and one placed arm
// or link whose held is false, each within 6e-8 (`compare.within`); an element whose held is true at its input point; a
// refusal by its code. A coordinate off prints the case, the id, the axis, its how and ring, the step and the math call
// it comes from, both values and both bit patterns. OW_LAYOUT3D_WITHIN=0 in the environment compares every coordinate
// exactly, to list the last bits a build's cosine, sine and arcsine move. The fixture's unplaced suite is read whole
// (UnplacedByLinks3d): each case's want, the list of ids, equal exactly and in its order. When the pin moves the fixture:
// run codegen/generate_layout_3d.py and update the hashes.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLayout.h"
#include "OWMapSpace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const FixtureSha256 = "acd6da7252493485cafc24356a84c53656d078872a28c695c0f7554009b6fb2a";
const char* const TableSha256 = "37dc2bbb4a1769e4ddafc2d0a27c9889229c3ef483e8f7cd6c9ba66a0d1afb87";

std::string Profiles() { return P().root + "/codegen/schema-dist/profiles/"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(Profiles() + "layout-by-links-3d.fixture.json", bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

std::vector<std::string> Names(const char* list) {
    std::vector<std::string> out;
    if (const OWJsonValue* l = Fixture()->Find(list))
        for (const OWJsonValue& c : l->Items()) out.push_back(c.Find("name")->Text());
    return out;
}

const OWJsonValue* Named(const char* list, const std::string& name) {
    const OWJsonValue* l = Fixture()->Find(list);
    if (!l) return nullptr;
    for (const OWJsonValue& c : l->Items())
        if (c.Find("name") && c.Find("name")->Text() == name) return &c;
    return nullptr;
}

std::string Joined(const std::vector<std::string>& ids) {
    std::string out;
    for (const auto& id : ids) out += id + " ";
    return out;
}

void UnplacedCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    OWLayoutUnplaced got = OWLayout::UnplacedByLinks3d(*c.Find("graph"), *c.Find("placed"));
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, std::string("by-links-3d"), name + ": the method");
    std::vector<std::string> want;
    for (const auto& w : c.Find("want")->Items()) want.push_back(w.Text());
    t.Eq(Joined(got.unplaced), Joined(want), name + ": the unplaced ids, in their order");
}

uint64_t Bits(double v) {
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

std::string Hex(double v) {
    char s[24];
    std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(Bits(v)));
    return s;
}

std::string Num(double v) {
    char s[40];
    std::snprintf(s, sizeof s, "%.17g", v);
    return s;
}

// compare.within, 1e-9 of a rest; OW_LAYOUT3D_WITHIN in the environment overrides it (0: every coordinate exact).
double Within() {
    static const double within = [] {
        double w = 6e-8;
#ifdef _WIN32
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, "OW_LAYOUT3D_WITHIN") == 0 && v && *v) w = std::atof(v);
        std::free(v);
#else
        if (const char* v = std::getenv("OW_LAYOUT3D_WITHIN"); v && *v) w = std::atof(v);
#endif
        return w;
    }();
    return within;
}

std::string Text(const OWJsonValue& v) { return v.IsNull() ? std::string("null") : v.Text(); }
std::string Opt(const std::optional<std::string>& v) { return v ? *v : std::string("null"); }

std::string List(const OWJsonValue* l) {
    std::string out;
    if (l)
        for (const auto& w : l->Items()) out += (w.IsBool() ? std::string(w.AsBool() ? "true" : "false") : Text(w)) + " ";
    return out;
}

// The ids of the case's ring pieces (compare.pieces): its graph read into pieces, the main piece the largest (a tie to the
// smaller least id); every other piece of two nodes or more with no node placed map and no node held.
std::set<std::string> RingIds(const OWJsonValue& c) {
    const OWJsonValue& input = *c.Find("input");
    const OWJsonValue& want = *c.Find("want");
    std::vector<std::string> ids;
    for (const auto& x : input.Find("nodes")->Items()) ids.push_back(x.Find("id")->Text());
    std::sort(ids.begin(), ids.end());
    std::map<std::string, int> at;
    for (size_t i = 0; i < ids.size(); ++i) at[ids[i]] = static_cast<int>(i);
    std::vector<int> parent(ids.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    auto find = [&parent](int a) {
        while (parent[a] != a) a = parent[a] = parent[parent[a]];
        return a;
    };
    for (const auto& e : input.Find("edges")->Items()) {
        const OWJsonValue* s = e.Find("source");
        const OWJsonValue* t = e.Find("target");
        if (!s || !t || !at.count(s->Text()) || !at.count(t->Text())) continue;
        const int a = find(at[s->Text()]), b = find(at[t->Text()]);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
    std::map<int, std::vector<std::string>> pieces;  // by root, the least node's place
    for (size_t i = 0; i < ids.size(); ++i) pieces[find(static_cast<int>(i))].push_back(ids[i]);
    int mainRoot = -1;
    size_t mainSize = 0;
    for (const auto& p : pieces)
        if (p.second.size() > mainSize) mainSize = p.second.size(), mainRoot = p.first;
    std::set<std::string> map, held;
    const auto& wids = want.Find("ids")->Items();
    const auto& how = want.Find("how")->Items();
    for (size_t i = 0; i < wids.size(); ++i)
        if (how[i].Text() == "map") map.insert(wids[i].Text());
    if (const OWJsonValue* h = input.Find("held"); h && h->IsObject())
        for (const auto& k : h->Keys()) held.insert(k);
    std::set<std::string> ring;
    for (const auto& p : pieces) {
        if (p.first == mainRoot || p.second.size() < 2) continue;
        if (std::any_of(p.second.begin(), p.second.end(), [&](const std::string& id) { return map.count(id) || held.count(id); })) continue;
        ring.insert(p.second.begin(), p.second.end());
    }
    return ring;
}

OWLayoutByLinks3d Call(const OWJsonValue& input) {
    OWJsonValue graph = OWJsonValue::Object();
    graph.Set("nodes", *input.Find("nodes"));
    graph.Set("edges", *input.Find("edges"));
    return OWLayout::LayoutByLinks3d(graph, input.Find("rows"), input.Find("held"), input.Find("heldUnit"));
}

void ByLinks3dCase(owtest::T& t, const OWJsonValue& c) {
    const std::string name = c.Find("name")->Text();
    const OWJsonValue& input = *c.Find("input");
    const OWJsonValue& want = *c.Find("want");
    t.Eq(static_cast<int>(c.Find("steps")->AsDouble()), 300, name + ": steps, the method's own");
    OWLayoutByLinks3d got = Call(input);
    if (const OWJsonValue* refused = want.Find("refused")) {
        t.Eq(got.code, refused->Text(), name + ": the refusal's code (" + got.error + ")");
        t.Ok(!got.error.empty() && got.points.empty() && got.centres.empty() && got.unplaced.empty(), name + ": a refused call answers nothing");
        return;
    }
    t.Eq(got.error, std::string(), name + ": an answer");
    t.Eq(got.method, std::string("by-links-3d"), name + ": the method");
    std::string ids, how, group, part, held, centres, unplaced;
    for (const auto& p : got.points) {
        ids += p.id + " ";
        how += p.how + " ";
        group += Opt(p.group) + " ";
        part += Opt(p.part) + " ";
        held += std::string(p.held ? "true" : "false") + " ";
    }
    for (const auto& id : got.centres) centres += id + " ";
    for (const auto& id : got.unplaced) unplaced += id + " ";
    t.Eq(ids, List(want.Find("ids")), name + ": ids");
    t.Eq(how, List(want.Find("how")), name + ": how");
    t.Eq(group, List(want.Find("group")), name + ": group");
    t.Eq(part, List(want.Find("part")), name + ": part");
    t.Eq(held, List(want.Find("held")), name + ": held");
    t.Eq(centres, List(want.Find("centres")), name + ": centres");
    t.Eq(unplaced, List(want.Find("unplaced")), name + ": unplaced");
    if (ids != List(want.Find("ids"))) return;

    const std::set<std::string> ring = RingIds(c);
    const auto& points = want.Find("points")->Items();
    const auto& whow = want.Find("how")->Items();
    const auto& wheld = want.Find("held")->Items();
    const OWJsonValue* inHeld = input.Find("held");
    int exactOff = 0, looseOff = 0;
    std::string shown;
    int lines = 0;
    for (size_t i = 0; i < got.points.size(); ++i) {
        const OWLayout3dPoint& p = got.points[i];
        const std::string h = whow[i].Text();
        const bool isRing = ring.count(p.id) > 0;
        const bool heldTrue = wheld[i].AsBool();
        const bool loose = isRing || ((h == "arm" || h == "link") && !heldTrue);
        const double g3[3] = {p.x, p.y, p.z};
        for (int a = 0; a < 3; ++a) {
            const double w = points[3 * i + a].AsDouble();
            const bool ok = loose ? std::fabs(g3[a] - w) <= Within() : g3[a] == w;
            if (heldTrue && inHeld && inHeld->Find(p.id)) {
                const double at = inHeld->Find(p.id)->Items()[a].AsDouble();
                t.Ok(g3[a] == at, name + ": " + p.id + " stands at its held point on " + "xyz"[a]);
            }
            if (ok) continue;
            (loose ? looseOff : exactOff) += 1;
            if (lines++ < 12) {
                const char* ring3[] = {"step 11's ring, M.x + R * std::cos(phi), phi from std::asin", "step 11's ring, M.y + R * std::sin(phi), phi from std::asin",
                                       "step 11's ring, M.z - c.z, no math call of its own"};
                const char* fan3[] = {"step 9's fan, (t.x * std::cos(ang) + w.x * std::sin(ang)) * fan", "step 9's fan, (t.y * std::cos(ang) + w.y * std::sin(ang)) * fan",
                                      "step 9's fan, (t.z * std::cos(ang) + w.z * std::sin(ang)) * fan"};
                const char* from = isRing ? ring3[a] : h == "arm" || h == "link" ? fan3[a] : "exact arithmetic, no math call";
                shown += "\n         " + p.id + " " + std::string(1, "xyz"[a]) + " (" + h + (isRing ? ", ring" : "") + "; " + from + "): " + Num(g3[a]) + " against " +
                         Num(w) + " (bits " + Hex(g3[a]) + " against " + Hex(w) + ")";
            }
        }
    }
    t.Ok(exactOff == 0 && looseOff == 0, name + ": " + std::to_string(exactOff) + " exact and " + std::to_string(looseOff) +
                                             " loose coordinates differ from the fixture:" + shown);
}

}  // namespace

OW_TEST(layout_by_links_3d_the_directions_table_is_the_vendored_table_bit_for_bit) {
    OWBytes bytes;
    OWFiles::Read(Profiles() + "layout-by-links-3d.directions.json", bytes);
    OWJsonParseResult r = OWJson::Parse(bytes);
    const OWJsonValue* table = r.ok ? r.value.Find("directions") : nullptr;
    const OWJsonValue* kmax = r.ok ? r.value.Find("k_max") : nullptr;
    t.Ok(kmax && kmax->AsDouble() == OWLayout::DirectionsMax, "k_max is 37");
    t.Ok(table && table->Size() == 37, "K = 1 to 37");
    if (!table) return;
    std::vector<uint64_t> flat;
    int negativeZeros = 0;
    for (int k = 1; k <= 37; ++k) {
        const OWJsonValue* row = table->Find(std::to_string(k));
        t.Ok(row && row->Size() == static_cast<size_t>(k), "K = " + std::to_string(k));
        if (!row) return;
        for (const auto& v : row->Items())
            for (const auto& x : v.Items()) {
                const double d = x.AsDouble();
                negativeZeros += d == 0 && std::signbit(d);
                flat.push_back(Bits(d));
            }
    }
    t.Eq(flat.size(), OWLayout::DirectionCount, "2,109 doubles");
    size_t off = 0;
    for (size_t i = 0; i < flat.size() && i < OWLayout::DirectionCount; ++i) off += flat[i] != OWLayout::DirectionBits[i];
    t.Eq(off, static_cast<size_t>(0), "directions that differ from the table's");
    t.Eq(negativeZeros, 35, "the table's -0.0, read as -0");
}

OW_TEST(layout_by_links_3d_the_golden_angle_is_the_pages_expression_and_the_packages_literal) {
    const double gold = 3.141592653589793 * (3 - std::sqrt(5.0));
    t.Eq(Hex(gold), std::string("4003331febfa4bfb"), "PI * (3 - sqrt(5))");
    t.Ok(gold == 2.399963229728653, "the npm package's literal");
}

OW_CASES(layout_by_links_3d_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const std::vector<std::string> names = Names("cases");
    if (names.empty()) {
        out.emplace_back("layout-by-links-3d:not-found", [](owtest::T& t) { t.Fail("the fixture did not load"); });
        return out;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        char num[8];
        std::snprintf(num, sizeof num, "%03zu", i);
        const std::string name = names[i];
        out.emplace_back("layout-by-links-3d:" + std::string(num) + " " + name, [doc, i](owtest::T& t) { ByLinks3dCase(t, Fixture()->Find("cases")->Items()[i]); });
    }
    return out;
}

OW_TEST(layout_by_links_3d_a_graph_it_cannot_read_is_an_error_without_a_code) {
    auto bad = [&t](const char* graph, const std::string& words) {
        const OWLayoutByLinks3d got = OWLayout::LayoutByLinks3d(OWJson::Parse(graph).value);
        t.Ok(got.code.empty() && got.error.find(words) != std::string::npos && got.points.empty(), words + ": " + got.error);
    };
    bad("null", "the graph is { nodes, edges }");
    bad(R"({"nodes": [{"type": "character"}], "edges": []})", "node 0 has no id");
    bad(R"({"nodes": [{"id": "a"}, {"id": "a"}], "edges": []})", "two nodes share the id a");
    const OWJsonValue graph = OWJson::Parse(R"({"nodes": [{"id": "a"}, {"id": "b"}], "edges": [{"source": "a", "target": "b"}]})").value;
    const OWJsonValue notList = OWJson::Parse(R"({"rows": []})").value;
    const OWLayoutByLinks3d got = OWLayout::LayoutByLinks3d(graph, &notList);
    t.Ok(got.code.empty() && got.error.find("elements is a list") != std::string::npos, "elements that are not a list: " + got.error);
    const OWLayoutByLinks3d two = OWLayout::LayoutByLinks3d(graph);
    t.Ok(two.error.empty() && two.points.size() == 2 && two.method == "by-links-3d" && two.version == 2, "two linked, placed");
}

OW_TEST(layout_by_links_3d_fixture_and_table_are_the_pinned_dists_bytes) {
    OWBytes fixture, table, manifest;
    t.Ok(OWFiles::Read(Profiles() + "layout-by-links-3d.fixture.json", fixture), "the fixture is vendored");
    t.Ok(OWFiles::Read(Profiles() + "layout-by-links-3d.directions.json", table), "the directions table is vendored");
    t.Eq(owtest::Sha256Hex(fixture), std::string(FixtureSha256), "the fixture's bytes are the ones this test pins");
    t.Eq(owtest::Sha256Hex(table), std::string(TableSha256), "the table's bytes are the ones this test pins");
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* f = files ? files->Find("profiles/layout-by-links-3d.fixture.json") : nullptr;
    const OWJsonValue* d = files ? files->Find("profiles/layout-by-links-3d.directions.json") : nullptr;
    t.Ok(f && f->Text() == FixtureSha256, "MANIFEST.json records the fixture's sha256");
    t.Ok(d && d->Text() == TableSha256, "MANIFEST.json records the table's sha256");
    const OWJsonValue* named = Fixture()->Find("table_sha256");
    t.Ok(named && named->Text() == TableSha256, "the fixture names the table by its sha256");
    t.Eq(Names("cases").size(), static_cast<size_t>(116), "116 cases");
    t.Eq(Names("unplaced").size(), static_cast<size_t>(9), "nine unplaced cases");
}

OW_CASES(layout_by_links_3d_unplaced_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Names("unplaced").empty()) {
        out.emplace_back("layout-by-links-3d-unplaced:not-found", [](owtest::T& t) { t.Fail("the fixture did not load"); });
        return out;
    }
    for (const std::string& name : Names("unplaced"))
        out.emplace_back("layout-by-links-3d-unplaced:" + name, [doc, name](owtest::T& t) { UnplacedCase(t, *Named("unplaced", name)); });
    return out;
}
