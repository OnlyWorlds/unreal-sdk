#pragma once
// Two layout methods for a world nobody laid out, read from its graph as OWGraph::WorldGraph answers it (the npm SDK's
// layoutByKind and layoutAround, 4.167.0, and the Python package's layout_by_kind and layout_around, 0.127.0).
//
// by-kind: each kind of element gets a point on a circle, links act as springs, neighbours push apart, for a fixed
// number of seeded steps; the answer is fitted into a square. around: one element at the centre, its neighbours on rings
// round it, and at two steps their neighbours further out, near the element that reached them.
//
// Both are exact: the same graph gives the same points as the npm SDK and the Python package, by-kind to the last bit,
// around within about 1e-12 (its cosines and sines are the C library's). The kinds' points on the circle are a table of
// bit patterns (OWLayoutAnchors.generated.cpp, from the shared fixture), never computed; everything else is addition,
// subtraction, multiplication, division, square root and floor, in the npm SDK's order, with JavaScript's Math.min and
// Math.max, so even the sign of a zero matches. A compiler that fuses a multiply and an add into one rounding, or
// reorders them (MSVC's /fp:fast, clang's -ffp-contract=on or -ffast-math), breaks that: the core builds with MSVC's
// default /fp:precise, and the plugin's module asks for precise semantics on every target (OnlyWorlds.Build.cs).
//
// A graph is {nodes, edges} in the npm SDK's words: each node {id, type, name?} (the type a slug), each edge {source,
// target, field?, index?}. The graph is first put in one order (nodes by id; links by source, field, index, then target;
// every word by code point, a UTF-8 string's bytes), so the order the nodes and links are handed in moves nothing. A
// graph it cannot read answers an `error`, naming what (the npm SDK's TypeError): no nodes or edges list, a node that is
// not an object or has no id, a type that is not one of the 22, a name neither text nor null, an id twice, an edge
// without text source and target, a field there and not text (null too), an index there and not a finite number (null
// too). The kinds' colour groups are the presentation sidecar's defaults (OWPresentation::FamilyOf, Order).

#include "OWGraph.h"
#include "OWJson.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// One element's point, in a frame of -1 to 1 on the picture's longer axis, centred.
struct OWLayoutPoint {
    std::string id;
    double x = 0;
    double y = 0;
};

// One element's point round the focus: the focus at 0, 0, and how many links away from it the element is (0, 1, 2).
struct OWLayoutAroundPoint {
    std::string id;
    int depth = 0;
    double x = 0;
    double y = 0;
};

// What LayoutByKind answers: one point per node, in the order the nodes were handed in, and the method that made them;
// or, for a graph it cannot read, an error naming what (and no points).
struct OWLayoutByKind {
    std::string method = "by-kind";
    int version = 1;
    std::vector<OWLayoutPoint> points;
    std::string error;
};

// What LayoutAround answers: the focus and every element within the steps, in the order the nodes were handed in; or an
// error naming what.
struct OWLayoutAround {
    std::string method = "around";
    int version = 1;
    std::vector<OWLayoutAroundPoint> points;
    std::string error;
};

// What an Unplaced reading answers: the ids of the nodes a saved layout Map of the method does not place, by id in code
// point order (the default layouts page's `unplaced`), and the method's word; or an error naming what (and no ids).
struct OWLayoutUnplaced {
    std::string method;
    std::vector<std::string> unplaced;
    std::string error;
};

namespace OWLayout {

// Lay out a whole graph by kind. Each kind present gets an anchor on a circle, in colour group order and then by type
// word; every element starts near its kind's anchor, at a seeded jitter. Then, for 320 steps (160 past 2,000 elements):
// elements closer than a few rest lengths push each other apart (a cell holding more than 12 pushes with a rotating
// sample of 12, scaled up), every link pulls its two ends towards a rest length, every element is drawn back towards its
// kind's anchor, and no element moves further in one step than a cap that shrinks as the steps run out. Last, the points
// are fitted into a frame of -1 to 1 on the longer axis, centred, proportions kept. An empty graph answers no points, one
// element answers 0, 0. Self-links pull nothing; a link to an id that is not a node is passed over.
OWLayoutByKind LayoutByKind(const OWJsonValue& graph);
// The same on a graph WorldGraph answered, read in its own words (OWWorldGraph::ToJson).
OWLayoutByKind LayoutByKind(const OWWorldGraph& graph);

// Lay out one element and what is linked to it. The focus stands at 0, 0. Its neighbours (one link away, either
// direction) stand on a ring round it, in colour group order, then by type word, then by name (code point order), up to
// 60 on the first ring and further rings beyond it. With steps 2, each neighbour's own neighbours stand on an outer ring,
// fanned out near the neighbour that reached them. An error on steps that is not 1 or 2 (checked first), a graph it
// cannot read, or a focus that is not one of the graph's nodes.
OWLayoutAround LayoutAround(const OWJsonValue& graph, std::string_view focus, int steps = 1);
OWLayoutAround LayoutAround(const OWWorldGraph& graph, std::string_view focus, int steps = 1);

// What a saved layout Map does not place (the page's `unplaced`): the ids of the nodes the method's reading counts that
// are not in `placed`, by id in code point order. `placed` is the element ids of the Map's Pins (OWMapSpace's pins on
// that Map, their element ids), a JSON array: a member that is not a non-empty string names no element and is passed
// over (the number 7 is not the id "7"), and an id the method would not place is no part of the answer. `placed` null, a
// string or anything but an array is an error. The answer does not say why an element has no Pin.
//
// by-kind places every node, so every node counts; the graph is read as LayoutByKind reads it.
OWLayoutUnplaced UnplacedByKind(const OWJsonValue& graph, const OWJsonValue& placed);
OWLayoutUnplaced UnplacedByKind(const OWWorldGraph& graph, const OWJsonValue& placed);
// around counts the focus and every node within its steps; an error as LayoutAround's (steps, graph, focus).
OWLayoutUnplaced UnplacedAround(const OWJsonValue& graph, std::string_view focus, const OWJsonValue& placed, int steps = 1);
OWLayoutUnplaced UnplacedAround(const OWWorldGraph& graph, std::string_view focus, const OWJsonValue& placed, int steps = 1);
// by-links-3d counts every node of the graph, of any type, also one it would not place (linked to no other and pinned on
// no map), so for the Map an answer makes this is that answer's own `unplaced`; its Maps, Pins and held points are not
// read. An error when the graph is not {nodes, edges}, a node has no text id, or two nodes share an id.
OWLayoutUnplaced UnplacedByLinks3d(const OWJsonValue& graph, const OWJsonValue& placed);
OWLayoutUnplaced UnplacedByLinks3d(const OWWorldGraph& graph, const OWJsonValue& placed);

// The anchor table, bit patterns of float64s (generated from the shared fixture; its test holds it to the fixture).
constexpr std::size_t AnchorCount = 506;
extern const uint64_t AnchorBits[AnchorCount];

// JavaScript's Math.min and Math.max of two: NaN if either is, -0 below +0 (std::min and std::max answer the first on a
// tie).
double MinJs(double a, double b);
double MaxJs(double a, double b);

}  // namespace OWLayout
}  // namespace OnlyWorlds
