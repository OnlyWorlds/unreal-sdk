#pragma once
// The map-space profile (schema-dist profiles/map-space.md, serials 105 and 106): how a reader turns a world's four
// map-space types into geometry, a Map's frame, a Zone's outline from its Markers, a Pin's point. Structure and
// coordinates in the Map's own pixels, counted from its bottom left, z upward (an elevation, never a height); looks,
// cameras, walls, doors and units are a drawing tool's. It adds nothing to the world and stores nothing. The npm
// SDK's mapSpace and the Python package's map_space give the same answer; held to the profile's conformance fixture
// (profiles/map-space.fixture.json).

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OnlyWorlds {

// A point, whole numbers in the Map's pixels; z reads 0 when the row has none or one that is not whole.
struct OWMapSpacePoint {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
};

// One Map's frame.
struct OWMapSpaceMap {
    std::string id;
    // The Map's own `width` and `height` when whole numbers, else the largest x (or y) among its own placed points, its
    // outlines' and its Pins' (a dangling or unplaced point measures nothing; none with no point), and `measured` is
    // then true.
    std::optional<int64_t> width;
    std::optional<int64_t> height;
    // The Map's `depth` when a whole number, the z extent.
    std::optional<int64_t> depth;
    // The largest z among the Map's own outline and Pin points, kept as written (so it can pass `depth`).
    std::optional<int64_t> z_max;
    // Metres per pixel: the Map's `x_metres_per_pixel` as served when it is a positive number, else null.
    OWJsonValue scale;
    std::optional<std::string> location;
    std::optional<std::string> parent_map;
    // `extent: "points"` in the answer: a slot of `width` and `height` was measured, not written.
    bool measured = false;
};

// One Zone's outline on one Map: its Markers' points in order, a repeated point kept.
struct OWMapSpaceOutline {
    std::string zone;
    std::string map;
    // `polygon` or `line`: the Zone's own `shape` when it is one of the two, else `polygon`.
    std::string shape;
    std::vector<OWMapSpacePoint> points;
};

// One Pin's point. The element is the wire's two keys, both or neither: a Pin with only one reads both none.
struct OWMapSpacePin {
    std::string pin;
    std::string map;
    std::optional<std::string> element_type;
    std::optional<std::string> element_id;
    OWMapSpacePoint point;
};

// What the reading left aside, by reason.
struct OWMapSpaceOut {
    int64_t no_point = 0;
    int64_t no_markers = 0;
    int64_t dangling = 0;
    int64_t no_map = 0;
};

// What MapSpaceOf answers.
struct OWWorldMapSpace {
    // By id in code point order.
    std::vector<OWMapSpaceMap> maps;
    // By Zone id, then Map id.
    std::vector<OWMapSpaceOutline> zones;
    // By id in code point order.
    std::vector<OWMapSpacePin> pins;
    OWMapSpaceOut out;
    // Empty unless the call was a mistake (elements that are not a list): it names the mistake and nothing is read.
    std::string error;

    // The answer in the fixture's shape, {maps, zones, pins, out} (all four, in that order): a Map as {id, width,
    // height, depth, z_max, scale, location, parent_map} with `extent: "points"` after them when measured; an outline
    // as {zone, map, shape, points} with each point [x, y, z]; a Pin as {pin, map, element_type, element_id, x, y, z};
    // `out` with a reason only when something was counted, in the order OutReasons() lists them.
    OWJsonValue ToJson() const;
};

namespace OWMapSpace {

// The reasons `out` counts, in the answer's order: no_point, no_markers, dangling, no_map.
const std::vector<std::string>& OutReasons();

// The two shapes an outline closes by: polygon, line.
const std::vector<std::string>& Shapes();

// A whole number as the profile reads one, by value: a JSON number with no fractional part (2.0 is 2, 1e2 is 100),
// never a string or a boolean. This reader's integer range is int64: an integer literal within it reads exactly (past
// 2^53 too), and a value past it reads as not whole, as the page says a reader does (the fixture stays within 2^53).
std::optional<int64_t> WholeOf(const OWJsonValue* v);

// mapSpace(elements), over the elements as the reader is served them (a JSON list: a folder's rows or the API's, so
// visibility is already applied and a Zone the reader may not see is not brought back). Rows are read by their
// `type` as written, case-exactly (`map`, `zone`, `marker`, `pin`; `Map` is none of them), and `shape` too (`Line` is
// a polygon); a row that is not an object, has no string `type`, or no non-empty string `id` is no element, ignored
// and never counted. Two rows of one type with one id read as the last one served, for every type, even when the last
// reads less (an element is its id).
// A Zone's outline is the Markers whose `zone` names it and whose `map` names one Map, by `order` ascending; a Marker
// without a whole `order` sorts after every ordered one, ties by id in code point order; one outline per Map the Zone
// has Markers on. A Marker whose Zone or Map is not served is counted `dangling` (checked first, so it counts once),
// one without a whole `x` and `y` `no_point`; a Zone with no placed Marker `no_markers`; a Pin whose Map is not served
// `no_map`, one without a whole `x` and `y` `no_point`. A `z` past a Map's `depth` is kept and shows in its `z_max`.
// Ids compare by code point (a UTF-8 id's bytes), never by UTF-16 unit: the same input gives the same answer, byte for
// byte. Pure.
OWWorldMapSpace MapSpaceOf(const OWJsonValue& elements);

}  // namespace OWMapSpace
}  // namespace OnlyWorlds
