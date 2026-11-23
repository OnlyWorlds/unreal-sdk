#pragma once
// The map-space profile (schema-dist profiles/map-space.md, serials 105 and 106): how a reader turns a world's four
// map-space types into geometry, a Map's frame, a Zone's outline from its Markers, a Pin's point. Structure and
// coordinates in the Map's own pixels, counted from its bottom left, z upward (an elevation, never a height); looks,
// cameras, walls, doors and units are a drawing tool's. It adds nothing to the world and stores nothing. The npm
// SDK's mapSpace and the Python package's map_space give the same answer; held to the profile's conformance fixture
// (profiles/map-space.fixture.json). Since serial 110 also the profile's second, small reading, LayoutOf: one Map's
// layout keys (`x_layout` on the Map, `x_hand` on its Pins), held to the fixture's `layout_cases`. Since serial 139
// also its third, CarryOf: where a point on a child Map lies on its parent and on each Map above it (rules 13 and
// 14), held to the fixture's `frame_cases`.

#include "OWApi.h"
#include "OWJson.h"

#include <array>
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
struct OW_API OWWorldMapSpace {
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

// What LayoutOf answers: one Map's layout keys (the map-space profile's rules 8 to 10, serial 110). How a method places
// Pins, and its own settings, are a layout tool's and are not read: a tool that cannot run a method draws the Pins
// where they are and never lays them out again.
struct OW_API OWMapLayout {
    // The Map's `x_layout.method` when `x_layout` is an object whose `method` is a non-empty string ("hand" for a
    // board; a method no page names answers itself), else none: the Map is not a layout map.
    std::optional<std::string> layout;
    // When `layout` is set: the id in `x_layout.settings.ground.map` when it names a served Map other than this one,
    // else none. Its size is not returned: the served Map's own `width` and `height` win, and the caller holds it.
    std::optional<std::string> ground;
    // The ids of this Map's Pins a new lay-out keeps where they are, by id in code point order: every Pin on a board,
    // the Pins whose `x_hand` is exactly `true` on any other layout map, none on a Map that is not a layout map.
    std::vector<std::string> hand;
    // Empty unless the call was a mistake (a map that is not an object, maps or pins that are not a list): it names the
    // mistake and nothing is read.
    std::string error;

    // The answer in the fixture's shape, {layout, ground, hand}, all three, in that order.
    OWJsonValue ToJson() const;
};

// One point of a frame reading: a Map's id and the point in its pixels, from its bottom left, each coordinate the
// 64-bit float the carry gives.
struct OWMapCarryPoint {
    std::string map;
    double x = 0;
    double y = 0;
};

// What CarryOf answers: one Map's frame reading (the map-space profile's rules 13 and 14, serial 139).
struct OW_API OWMapCarry {
    // The Map's `x_parent_frame` as written, [x0, y0, x1, y1] each read as a 64-bit float, when it is a parent frame,
    // else none: also on a Map whose parent is not served or closes a ring. The Map's own: it does not depend on the
    // point.
    std::optional<std::array<double, 4>> parent_frame;
    // `no_parent`, `not_four_numbers` or `not_a_box` when this Map's own key is one (CarryLints()), else none. A Map
    // further up the chain is read for its own lint by its own reading.
    std::optional<std::string> lint;
    // The point on this Map as given, then the point on each Map it is carried to, up the `parent_map` chain.
    std::vector<OWMapCarryPoint> points;
    // Why the last point is carried no further (CarryStops()); none only for a row that is no Map.
    std::optional<std::string> stop;
    // Empty unless the call was a mistake (a map that is not an object, a point that is not two finite numbers, maps
    // that is not a list): it names the mistake and nothing is read.
    std::string error;

    // The answer in the fixture's shape, {parent_frame, lint, points, stop} (all four, in that order), each point as
    // {map, x, y}; a number spelled as JSON.stringify writes the double.
    OWJsonValue ToJson() const;
};

namespace OWMapSpace {

// The reasons `out` counts, in the answer's order: no_point, no_markers, dangling, no_map.
OW_API const std::vector<std::string>& OutReasons();

// The two shapes an outline closes by: polygon, line.
OW_API const std::vector<std::string>& Shapes();

// A whole number as the profile reads one, by value: a JSON number with no fractional part (2.0 is 2, 1e2 is 100),
// never a string or a boolean. A reader's integer range is the range it holds exactly: an integer literal is read as
// int64, exactly (past 2^53 too), and one past int64 reads as not whole; a literal with a fraction or an exponent is
// read as a double, whole only to 2^53 - 1 (past it the parse may have rounded it), as the npm SDK's isSafeInteger.
// The fixture stays within 2^53.
OW_API std::optional<int64_t> WholeOf(const OWJsonValue* v);

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
OW_API OWWorldMapSpace MapSpaceOf(const OWJsonValue& elements);

// The method word of a board, a Map whose Pins are all placed by hand.
inline constexpr const char* Board = "hand";

// layoutOf(map, maps, pins), the profile's layout reading of one Map: `map` the Map read, `maps` the Maps the reader is
// served, `pins` every Pin the caller holds, of any Map, as served (two JSON lists). The reader keeps the Pins whose
// `map` names this Map's id, so a caller passes every Pin it has. Rows are read as MapSpaceOf reads them: a row that is
// not an object or has no non-empty string `id` is no element (a Map without one is no layout map), and of two Pin
// rows with one id the last served is read. The Map is a layout map when its `x_layout` is an object whose `method` is
// a non-empty string; anything else (absent, null, a string, an object without a string `method`, "") is no layout,
// and its ground and Pins are not read. On a board (`method` "hand") every Pin of the Map is kept, marked or not; on
// any other layout map only the Pins whose `x_hand` is the JSON `true` (never "true" or 1); on a Map that is not a
// layout map none, whatever its Pins carry. A Pin whose element the reader cannot see is still a Pin. Ids in code
// point order. Pure; MapSpaceOf reads neither key. The npm SDK's layoutOf and the Python package's layout_of.
OW_API OWMapLayout LayoutOf(const OWJsonValue& map, const OWJsonValue& maps, const OWJsonValue& pins);

// Why the walk up `parent_map` ended (rule 14), in the page's order: top, dangling, cycle, no_frame, no_carry.
OW_API const std::vector<std::string>& CarryStops();

// What is wrong with a Map's own `x_parent_frame` (rule 13), in the page's order: no_parent, not_four_numbers,
// not_a_box.
OW_API const std::vector<std::string>& CarryLints();

// A number as rule 13 reads one: a JSON number held as a finite 64-bit float, the double nearest its text (as
// JSON.parse reads it, so a magnitude below a double's least reads as 0); one too large for a double (`1E400`, which
// JSON.parse reads as Infinity) is none, and so is a string, a boolean, null, a list and an object.
OW_API std::optional<double> NumberOf(const OWJsonValue* v);

// carryOf(map, point, maps), the profile's frame reading of one Map: where the point (x, y) on `map` lies on the Map
// its `parent_map` names, and on each Map above it. `maps` is the Maps the reader is served (a JSON list): rows of any
// other type are skipped, a row whose `type` is not exactly `map` or that has no non-empty string id is no Map, and of
// two rows with one id the last served is read. A `map` that is no Map answers {null, null, [], null}.
// The Map's own `x_parent_frame` is read only when its `parent_map` is a link (a non-empty string; else a key present
// and not null is the lint `no_parent`); it is a parent frame when it is a list of exactly four numbers (NumberOf) with
// x0 < x1 and y0 < y1 (else `not_four_numbers`, or `not_a_box`, never normalised); absent or null is none, with no
// lint. The point is carried one level at a time, by each level's own frame, as `x0 + ((x * (x1 - x0)) / width)` and
// `y0 + ((y * (y1 - y0)) / height)` in 64-bit floats in that order, never through one product of the levels' ratios.
// At each Map the walk ends on the first of: no `parent_map` (`top`), a parent not served (`dangling`), a parent the
// walk already reached, this Map included (`cycle`), no parent frame (`no_frame`), a written `width` or `height` that
// is not a whole number above 0 by value (`no_carry`). A size is any whole double, with no bound at 2^53, as the npm
// SDK's isCarrySize reads it (WholeOf, which MapSpaceOf reads sizes by, stops at 2^53). A point outside the child is
// carried as written; z is not carried. Pure. The npm SDK's carryOf (4.183.0), held to the fixture's `frame_cases`.
OW_API OWMapCarry CarryOf(const OWJsonValue& map, double x, double y, const OWJsonValue& maps);

}  // namespace OWMapSpace
}  // namespace OnlyWorlds
