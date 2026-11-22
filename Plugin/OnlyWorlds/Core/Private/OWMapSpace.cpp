#include "OWMapSpace.h"

#include "OWText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <string_view>
#include <utility>

// The map-space profile's reader, as the npm SDK's mapSpace (src/mapspace.ts) and the Python package's map_space read
// it. Ids compare as UTF-8 bytes, which is code point order.

namespace OnlyWorlds {

namespace {

OWJsonValue Int(int64_t n) { return OWJsonValue::Number(n); }
OWJsonValue Text(const std::string& s) { return OWJsonValue::String(s); }
OWJsonValue IntOrNull(const std::optional<int64_t>& n) { return n ? Int(*n) : OWJsonValue::Null(); }
OWJsonValue TextOrNull(const std::optional<std::string>& s) { return s ? Text(*s) : OWJsonValue::Null(); }

// Ids in code point order, the profiles' one order (OWText::ByCodePoint).
using ByCodePoint = OWText::ByCodePoint;

struct ByZoneThenMap {
    bool operator()(const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) const {
        if (a.first != b.first) return ByCodePoint()(a.first, b.first);
        return ByCodePoint()(a.second, b.second);
    }
};

// An id: a string that is not empty; anything else reads as none.
std::optional<std::string> IdOf(const OWJsonValue* v) {
    if (!v || !v->IsString() || v->Text().empty()) return std::nullopt;
    return v->Text();
}

OWJsonValue PointJson(const OWMapSpacePoint& p) {
    OWJsonValue a = OWJsonValue::Array();
    a.Push(Int(p.x));
    a.Push(Int(p.y));
    a.Push(Int(p.z));
    return a;
}

}  // namespace

OWJsonValue OWMapLayout::ToJson() const {
    OWJsonValue answer = OWJsonValue::Object();
    answer.Set("layout", TextOrNull(layout));
    answer.Set("ground", TextOrNull(ground));
    OWJsonValue ids = OWJsonValue::Array();
    for (const std::string& h : hand) ids.Push(Text(h));
    answer.Set("hand", std::move(ids));
    return answer;
}

OWJsonValue OWWorldMapSpace::ToJson() const {
    OWJsonValue answer = OWJsonValue::Object();
    OWJsonValue ms = OWJsonValue::Array();
    for (const OWMapSpaceMap& m : maps) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("id", Text(m.id));
        row.Set("width", IntOrNull(m.width));
        row.Set("height", IntOrNull(m.height));
        row.Set("depth", IntOrNull(m.depth));
        row.Set("z_max", IntOrNull(m.z_max));
        row.Set("scale", m.scale.IsNumber() ? m.scale : OWJsonValue::Null());
        row.Set("location", TextOrNull(m.location));
        row.Set("parent_map", TextOrNull(m.parent_map));
        if (m.measured) row.Set("extent", Text("points"));
        ms.Push(std::move(row));
    }
    answer.Set("maps", std::move(ms));
    OWJsonValue zs = OWJsonValue::Array();
    for (const OWMapSpaceOutline& o : zones) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("zone", Text(o.zone));
        row.Set("map", Text(o.map));
        row.Set("shape", Text(o.shape));
        OWJsonValue points = OWJsonValue::Array();
        for (const OWMapSpacePoint& p : o.points) points.Push(PointJson(p));
        row.Set("points", std::move(points));
        zs.Push(std::move(row));
    }
    answer.Set("zones", std::move(zs));
    OWJsonValue ps = OWJsonValue::Array();
    for (const OWMapSpacePin& p : pins) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("pin", Text(p.pin));
        row.Set("map", Text(p.map));
        row.Set("element_type", TextOrNull(p.element_type));
        row.Set("element_id", TextOrNull(p.element_id));
        row.Set("x", Int(p.point.x));
        row.Set("y", Int(p.point.y));
        row.Set("z", Int(p.point.z));
        ps.Push(std::move(row));
    }
    answer.Set("pins", std::move(ps));
    OWJsonValue counted = OWJsonValue::Object();
    const int64_t n[] = {out.no_point, out.no_markers, out.dangling, out.no_map};
    for (size_t i = 0; i < 4; ++i)
        if (n[i] > 0) counted.Set(OWMapSpace::OutReasons()[i], Int(n[i]));
    answer.Set("out", std::move(counted));
    return answer;
}

namespace OWMapSpace {

const std::vector<std::string>& OutReasons() {
    static const std::vector<std::string> reasons = {"no_point", "no_markers", "dangling", "no_map"};
    return reasons;
}

const std::vector<std::string>& Shapes() {
    static const std::vector<std::string> shapes = {"polygon", "line"};
    return shapes;
}

std::optional<int64_t> WholeOf(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    const std::string& text = v->Text();
    int64_t n = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (end == text.data() + text.size()) {
        if (ec == std::errc()) return n;
        // An integer literal past int64 is past this reader's range, read as not whole (rule 1, serial 106), never as
        // the double it rounds to (-9223372036854775809 rounds to int64's least).
        if (ec == std::errc::result_out_of_range) return std::nullopt;
    }
    // A literal with a fraction or an exponent is read as a double, which holds a whole number exactly only to 2^53 - 1:
    // past it a value the parse had to round cannot be told from an exact one, so it is not whole (the reader's range
    // is the range it holds exactly; the npm SDK's isSafeInteger).
    double d = v->AsDouble();
    if (!std::isfinite(d) || std::floor(d) != d || std::fabs(d) > 9007199254740991.0) return std::nullopt;
    return int64_t(d);
}

namespace {

// A Marker's or Pin's point, its x and y already read whole; a z that is missing or not whole reads 0.
OWMapSpacePoint PointOf(const OWJsonValue& e) {
    return {*WholeOf(e.Find("x")), *WholeOf(e.Find("y")), WholeOf(e.Find("z")).value_or(0)};
}

}  // namespace

OWWorldMapSpace MapSpaceOf(const OWJsonValue& elements) {
    OWWorldMapSpace answer;
    if (!elements.IsArray()) {
        answer.error = "mapSpace: elements is a list of elements.";
        return answer;
    }
    // The rows by type, the type read as written (reading K: `Map` is no Map), one row per id: the last served takes
    // the place of an earlier one (reading I, serial 106: an element is its id), in the first one's position, as npm's
    // Map.set keeps it. A row without a non-empty string id is no element: ignored, never counted.
    struct Rows {
        std::vector<const OWJsonValue*> list;
        std::map<std::string, size_t> at;
    };
    std::map<std::string, Rows> by_type;
    for (const OWJsonValue& e : elements.Items()) {
        if (!e.IsObject()) continue;
        const OWJsonValue* type = e.Find("type");
        if (!type || !type->IsString()) continue;
        std::optional<std::string> id = IdOf(e.Find("id"));
        if (!id) continue;
        Rows& rows = by_type[type->Text()];
        auto [it, fresh] = rows.at.emplace(*id, rows.list.size());
        if (fresh) rows.list.push_back(&e);
        else rows.list[it->second] = &e;
    }
    auto rows_of = [&](const char* type) -> const std::vector<const OWJsonValue*>& {
        static const std::vector<const OWJsonValue*> none;
        auto it = by_type.find(type);
        return it == by_type.end() ? none : it->second.list;
    };
    // The Maps and Zones served, by id.
    auto indexed = [&](const char* type) {
        std::map<std::string, const OWJsonValue*, ByCodePoint> m;
        for (const OWJsonValue* r : rows_of(type)) m[*IdOf(r->Find("id"))] = r;
        return m;
    };
    const auto maps = indexed("map");
    const auto zones = indexed("zone");
    OWMapSpaceOut& out = answer.out;

    // Markers: dangling first, then no_point; a Marker counts once. Grouped by (Zone, Map), in the order served.
    std::map<std::pair<std::string, std::string>, std::vector<const OWJsonValue*>, ByZoneThenMap> placed;
    for (const OWJsonValue* mk : rows_of("marker")) {
        std::optional<std::string> z = IdOf(mk->Find("zone"));
        std::optional<std::string> m = IdOf(mk->Find("map"));
        if (!z || !m || !zones.count(*z) || !maps.count(*m)) {
            ++out.dangling;
            continue;
        }
        if (!WholeOf(mk->Find("x")) || !WholeOf(mk->Find("y"))) {
            ++out.no_point;
            continue;
        }
        placed[{*z, *m}].push_back(mk);
    }

    // An outline's order: a whole `order` ascending, a Marker without one after every ordered one, ties by id.
    auto marker_order = [](const OWJsonValue* a, const OWJsonValue* b) {
        std::optional<int64_t> oa = WholeOf(a->Find("order"));
        std::optional<int64_t> ob = WholeOf(b->Find("order"));
        if (oa && ob && *oa != *ob) return *oa < *ob;
        if (oa.has_value() != ob.has_value()) return oa.has_value();
        return ByCodePoint()(*IdOf(a->Find("id")), *IdOf(b->Find("id")));
    };
    for (const auto& [zid, zone] : zones) {
        bool any = false;
        const OWJsonValue* s = zone->Find("shape");
        const std::string shape = s && s->IsString() && s->Text() == "line" ? "line" : "polygon";
        for (const auto& [key, markers] : placed) {
            if (key.first != zid) continue;
            any = true;
            std::vector<const OWJsonValue*> sorted = markers;
            std::stable_sort(sorted.begin(), sorted.end(), marker_order);
            OWMapSpaceOutline o{zid, key.second, shape, {}};
            for (const OWJsonValue* mk : sorted) o.points.push_back(PointOf(*mk));
            answer.zones.push_back(std::move(o));
        }
        if (!any) ++out.no_markers;
    }

    // Pins, by id: no_map first, then no_point. The element needs both keys; one alone reads as neither.
    std::vector<const OWJsonValue*> pin_rows = rows_of("pin");
    std::stable_sort(pin_rows.begin(), pin_rows.end(), [](const OWJsonValue* a, const OWJsonValue* b) {
        return ByCodePoint()(*IdOf(a->Find("id")), *IdOf(b->Find("id")));
    });
    for (const OWJsonValue* p : pin_rows) {
        std::optional<std::string> m = IdOf(p->Find("map"));
        if (!m || !maps.count(*m)) {
            ++out.no_map;
            continue;
        }
        if (!WholeOf(p->Find("x")) || !WholeOf(p->Find("y"))) {
            ++out.no_point;
            continue;
        }
        OWMapSpacePin pin{*IdOf(p->Find("id")), *m, IdOf(p->Find("element_type")), IdOf(p->Find("element_id")),
                          PointOf(*p)};
        if (!pin.element_type || !pin.element_id) pin.element_type = pin.element_id = std::nullopt;
        answer.pins.push_back(std::move(pin));
    }

    // Maps, by id: a slot of width and height measured from the Map's own placed points (outlines and Pins; a dangling
    // or unplaced point measures nothing) when it is not written whole.
    for (const auto& [mid, mp] : maps) {
        std::vector<OWMapSpacePoint> own;
        for (const OWMapSpaceOutline& o : answer.zones)
            if (o.map == mid) own.insert(own.end(), o.points.begin(), o.points.end());
        for (const OWMapSpacePin& q : answer.pins)
            if (q.map == mid) own.push_back(q.point);
        auto largest = [&own](int64_t OWMapSpacePoint::*axis) {
            std::optional<int64_t> best;
            for (const OWMapSpacePoint& p : own)
                if (!best || p.*axis > *best) best = p.*axis;
            return best;
        };
        OWMapSpaceMap row;
        row.id = mid;
        row.width = WholeOf(mp->Find("width"));
        if (!row.width) {
            row.width = largest(&OWMapSpacePoint::x);
            row.measured = true;
        }
        row.height = WholeOf(mp->Find("height"));
        if (!row.height) {
            row.height = largest(&OWMapSpacePoint::y);
            row.measured = true;
        }
        row.depth = WholeOf(mp->Find("depth"));
        row.z_max = largest(&OWMapSpacePoint::z);
        const OWJsonValue* sc = mp->Find("x_metres_per_pixel");
        if (sc && sc->IsNumber() && sc->AsDouble() > 0) row.scale = *sc;
        row.location = IdOf(mp->Find("location"));
        row.parent_map = IdOf(mp->Find("parent_map"));
        answer.maps.push_back(std::move(row));
    }
    return answer;
}

OWMapLayout LayoutOf(const OWJsonValue& map, const OWJsonValue& maps, const OWJsonValue& pins) {
    OWMapLayout answer;
    if (!map.IsObject()) {
        answer.error = "layoutOf: map is one Map element.";
        return answer;
    }
    if (!maps.IsArray()) {
        answer.error = "layoutOf: maps is a list of Map elements.";
        return answer;
    }
    if (!pins.IsArray()) {
        answer.error = "layoutOf: pins is a list of Pin elements.";
        return answer;
    }
    const std::optional<std::string> id = IdOf(map.Find("id"));
    if (!id) return answer;

    // Rule 8: the mark is x_layout alone, an object with a non-empty string method.
    const OWJsonValue* recipe = map.Find("x_layout");
    if (!recipe || !recipe->IsObject()) return answer;
    const std::optional<std::string> method = IdOf(recipe->Find("method"));
    if (!method) return answer;

    // Rule 9: the ground is a served Map other than this one, named by settings.ground.map.
    const OWJsonValue* settings = recipe->Find("settings");
    const OWJsonValue* ground = settings && settings->IsObject() ? settings->Find("ground") : nullptr;
    const std::optional<std::string> named = ground && ground->IsObject() ? IdOf(ground->Find("map")) : std::nullopt;
    if (named && *named != *id) {
        for (const OWJsonValue& m : maps.Items())
            if (m.IsObject() && IdOf(m.Find("id")) == named) {
                answer.ground = named;
                break;
            }
    }

    // Rule 10: one row per Pin id, the last served; this Map's Pins, every one on a board, else the marked ones.
    std::map<std::string, const OWJsonValue*, ByCodePoint> held;
    for (const OWJsonValue& p : pins.Items()) {
        if (!p.IsObject()) continue;
        if (std::optional<std::string> pid = IdOf(p.Find("id"))) held[*pid] = &p;
    }
    const bool board = *method == Board;
    for (const auto& [pid, p] : held) {
        if (IdOf(p->Find("map")) != id) continue;
        const OWJsonValue* mark = p->Find("x_hand");
        if (board || (mark && mark->IsBool() && mark->AsBool())) answer.hand.push_back(pid);
    }
    answer.layout = method;
    return answer;
}

}  // namespace OWMapSpace

// -- the frame reading (rules 13 and 14) ----------------------------------------------------------------------------

namespace {

// Whether a number's magnitude is 1 or more, from its text alone (for a number past a double's range either way).
bool AtLeastOne(std::string_view text) {
    if (!text.empty() && text[0] == '-') text.remove_prefix(1);
    size_t e = text.find_first_of("eE");
    long long exponent = 0;
    if (e != std::string_view::npos) {
        std::string_view x = text.substr(e + 1);
        bool negative = !x.empty() && x[0] == '-';
        if (!x.empty() && (x[0] == '-' || x[0] == '+')) x.remove_prefix(1);
        for (char c : x) exponent = std::min(exponent * 10 + (c - '0'), 1000000000LL);
        if (negative) exponent = -exponent;
        text = text.substr(0, e);
    }
    size_t point = text.find('.');
    std::string_view whole = text.substr(0, point);
    std::string_view fraction = point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    long long place;
    if (size_t first = whole.find_first_not_of('0'); first != std::string_view::npos) {
        place = static_cast<long long>(whole.size() - first) - 1;
    } else if (size_t f = fraction.find_first_not_of('0'); f != std::string_view::npos) {
        place = -static_cast<long long>(f) - 1;
    } else {
        return false;
    }
    return place + exponent >= 0;
}

// A Map row: `type` exactly `map` (reading K) and a non-empty string id (reading H).
std::optional<std::string> MapIdOf(const OWJsonValue& v) {
    if (!v.IsObject()) return std::nullopt;
    const OWJsonValue* type = v.Find("type");
    if (!type || !type->IsString() || type->Text() != "map") return std::nullopt;
    return IdOf(v.Find("id"));
}

struct OwnFrame {
    std::optional<std::array<double, 4>> frame;
    std::optional<std::string> lint;
};

// A Map's own parent frame and lint (rule 13).
OwnFrame FrameOf(const OWJsonValue& map) {
    const OWJsonValue* key = map.Find("x_parent_frame");
    const bool present = key && !key->IsNull();
    // The key is read only on a Map with a parent_map, a link as reading H reads one.
    if (!IdOf(map.Find("parent_map"))) return {std::nullopt, present ? std::optional<std::string>("no_parent") : std::nullopt};
    if (!present) return {};
    if (!key->IsArray() || key->Size() != 4) return {std::nullopt, "not_four_numbers"};
    std::array<double, 4> box{};
    for (size_t i = 0; i < 4; ++i) {
        std::optional<double> n = OWMapSpace::NumberOf(&key->Items()[i]);
        if (!n) return {std::nullopt, "not_four_numbers"};
        box[i] = *n;
    }
    if (!(box[0] < box[2]) || !(box[1] < box[3])) return {std::nullopt, "not_a_box"};
    return {box, std::nullopt};
}

// A size a carry may divide by: a whole number above 0, by value (reading K: 100.0 reads as 100), with no bound: the
// npm SDK's isCarrySize (Number.isInteger and above 0).
std::optional<double> CarrySizeOf(const OWJsonValue* v) {
    std::optional<double> n = OWMapSpace::NumberOf(v);
    if (!n || std::floor(*n) != *n || !(*n > 0)) return std::nullopt;
    return n;
}

}  // namespace

OWJsonValue OWMapCarry::ToJson() const {
    OWJsonValue answer = OWJsonValue::Object();
    if (parent_frame) {
        OWJsonValue box = OWJsonValue::Array();
        for (double d : *parent_frame) box.Push(OWJsonValue::Number(d));
        answer.Set("parent_frame", std::move(box));
    } else {
        answer.Set("parent_frame", OWJsonValue::Null());
    }
    answer.Set("lint", TextOrNull(lint));
    OWJsonValue ps = OWJsonValue::Array();
    for (const OWMapCarryPoint& p : points) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("map", Text(p.map));
        row.Set("x", OWJsonValue::Number(p.x));
        row.Set("y", OWJsonValue::Number(p.y));
        ps.Push(std::move(row));
    }
    answer.Set("points", std::move(ps));
    answer.Set("stop", TextOrNull(stop));
    return answer;
}

namespace OWMapSpace {

const std::vector<std::string>& CarryStops() {
    static const std::vector<std::string> stops = {"top", "dangling", "cycle", "no_frame", "no_carry"};
    return stops;
}

const std::vector<std::string>& CarryLints() {
    static const std::vector<std::string> lints = {"no_parent", "not_four_numbers", "not_a_box"};
    return lints;
}

std::optional<double> NumberOf(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    const std::string& text = v->Text();
    double d = 0;
    std::from_chars_result r = std::from_chars(text.data(), text.data() + text.size(), d);
    if (r.ec == std::errc::result_out_of_range) {
        // Past a double: one too large is no number (JSON.parse reads Infinity), one too small reads as 0, as it does.
        if (AtLeastOne(text)) return std::nullopt;
        return !text.empty() && text[0] == '-' ? -0.0 : 0.0;
    }
    if (r.ec != std::errc() || !std::isfinite(d)) return std::nullopt;
    return d;
}

OWMapCarry CarryOf(const OWJsonValue& map, double x, double y, const OWJsonValue& maps) {
    OWMapCarry answer;
    if (!map.IsObject()) {
        answer.error = "CarryOf: map is one Map element";
        return answer;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        answer.error = "CarryOf: the point is two finite numbers";
        return answer;
    }
    if (!maps.IsArray()) {
        answer.error = "CarryOf: maps is a list of Map elements";
        return answer;
    }
    const std::optional<std::string> id = MapIdOf(map);
    if (!id) return answer;

    // The Maps served, one row per id, the last served (reading I).
    std::map<std::string, const OWJsonValue*> served;
    for (const OWJsonValue& m : maps.Items())
        if (std::optional<std::string> mid = MapIdOf(m)) served[*mid] = &m;

    const OwnFrame own = FrameOf(map);
    answer.parent_frame = own.frame;
    answer.lint = own.lint;
    answer.points.push_back({*id, x, y});
    std::set<std::string> reached = {*id};
    const OWJsonValue* at = &map;
    std::optional<std::array<double, 4>> frame = own.frame;
    for (;;) {
        // Served-ness before shape (reading A): top, dangling, cycle, then no_frame and no_carry.
        const std::optional<std::string> parent_id = IdOf(at->Find("parent_map"));
        if (!parent_id) { answer.stop = "top"; break; }
        auto parent = served.find(*parent_id);
        if (parent == served.end()) { answer.stop = "dangling"; break; }
        if (reached.count(*parent_id)) { answer.stop = "cycle"; break; }
        if (!frame) { answer.stop = "no_frame"; break; }
        const std::optional<double> width = CarrySizeOf(at->Find("width"));
        const std::optional<double> height = CarrySizeOf(at->Find("height"));
        if (!width || !height) { answer.stop = "no_carry"; break; }
        const auto [x0, y0, x1, y1] = *frame;
        // Rule 14's order: the difference, then the product, then the quotient, then the sum.
        x = x0 + ((x * (x1 - x0)) / *width);
        y = y0 + ((y * (y1 - y0)) / *height);
        answer.points.push_back({*parent_id, x, y});
        reached.insert(*parent_id);
        at = parent->second;
        frame = FrameOf(*at).frame;
    }
    return answer;
}

}  // namespace OWMapSpace
}  // namespace OnlyWorlds
