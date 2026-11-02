#include "OWMapSpace.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <utility>

// The map-space profile's reader, as the npm SDK's mapSpace (src/mapspace.ts) and the Python package's map_space read
// it. Ids compare as UTF-8 bytes, which is code point order.

namespace OnlyWorlds {

namespace {

OWJsonValue Int(int64_t n) { return OWJsonValue::Number(n); }
OWJsonValue Text(const std::string& s) { return OWJsonValue::String(s); }
OWJsonValue IntOrNull(const std::optional<int64_t>& n) { return n ? Int(*n) : OWJsonValue::Null(); }
OWJsonValue TextOrNull(const std::optional<std::string>& s) { return s ? Text(*s) : OWJsonValue::Null(); }

// Ids in code point order: a UTF-8 string's bytes compared unsigned (std::string's own order) are its code points'.
struct ByCodePoint {
    bool operator()(const std::string& a, const std::string& b) const { return a < b; }
};

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
    double d = v->AsDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d >= 9223372036854775808.0 || d < -9223372036854775808.0)
        return std::nullopt;
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

}  // namespace OWMapSpace
}  // namespace OnlyWorlds
