#pragma once
// The landing profile (schema-dist profiles/landing.md, serial 103): a world's first picture at a door, read from the
// graph OWGraph::WorldGraph answers for the visitor. A real world shows its most-linked Characters, each with what it
// links; a small world shows its gaps, each an invitation to make the world bigger. It adds nothing to the world and
// stores nothing, and it sees exactly what the graph holds: every leave-out (room, hidden, proposed, map space, the
// reading aids) is the graph's, made before this reads. Structure only, never coordinates: a layout is the drawing
// tool's, keyed by id. The npm SDK's landingOf and the Python package's landing_of give the same answer; held to the
// profile's conformance fixture (profiles/landing.fixture.json).

#include "OWApi.h"
#include "OWGraph.h"
#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// A node a head reaches: its steps from the head (0 for the head itself) and its degree (OWGraph::DegreeOf's).
struct OWLandingMember {
    std::string id;
    int64_t hops = 0;
    int64_t degree = 0;
};

// One head and what joins it: the head first, then by hops, then by degree (highest first), then by id in code point
// order.
struct OWLandingCluster {
    std::string head;
    std::vector<OWLandingMember> members;
};

// A node no head reaches.
struct OWLandingNode {
    std::string id;
    int64_t degree = 0;
};

// One gap: `kind` is orphan (degree 0), near_orphan (degree 1), no_place (a Character whose `location` names no
// Location node) or nobody_here (a Location no Character's `location` names).
struct OWLandingGap {
    std::string kind;
    std::string id;
};

// What LandingOf answers.
struct OW_API OWWorldLanding {
    // "gaps" when the graph has fewer nodes than `size`, "graph" otherwise: which part a door draws first. Both parts
    // are always read.
    std::string mode;
    // The size the mode was read at.
    int64_t size = 0;
    // In the heads' order: the Characters with an edge, most linked first, ties by id in code point order.
    std::vector<OWLandingCluster> clusters;
    // By degree (highest first), then by id.
    std::vector<OWLandingNode> unclustered;
    // In the kinds' order (orphan, near_orphan, no_place, nobody_here), then by id; a node can carry several.
    std::vector<OWLandingGap> gaps;
    // An option refused, named (an unknown key, or a value that is not a whole number of 1 or more): nothing was read
    // and the answer is empty.
    std::string refused;
    // Empty unless the call was a mistake (options that are neither an object nor null): it names the mistake and
    // nothing is read.
    std::string error;

    // The answer in the fixture's shape, {mode, size, clusters, unclustered, gaps} (all five, in that order), or
    // {refused} when refused.
    OWJsonValue ToJson() const;
};

// The options, each a whole number of 1 or more; unset is the default. A value below 1 is refused by name, in the
// order heads, hops, size.
struct OWLandingOptions {
    std::optional<int64_t> heads;  // how many clusters at most; default 5
    std::optional<int64_t> hops;   // how far a head reaches, a step following an edge either way; default 1
    std::optional<int64_t> size;   // the node count at which the mode turns from gaps to graph; default 24
};

// One of the most linked: its id, its name as the graph holds it (none when the node holds no string), its degree.
struct OWLinked {
    std::string id;
    std::optional<std::string> name;
    int64_t degree = 0;
};

// What MostLinkedOf answers.
struct OW_API OWMostLinked {
    // The count per type it was read at.
    int64_t per_type = 0;
    // By type, the types in code point order, each its most linked, highest first, ties by id; a type with nothing to
    // pick is left out.
    std::vector<std::pair<std::string, std::vector<OWLinked>>> by_type;
    // An option refused, named (an unknown key, or a per_type that is not a whole number from 1 to 2^53 - 1): nothing
    // was read and the answer is empty.
    std::string refused;
    // Empty unless the call was a mistake (options that are neither an object nor null): it names the mistake.
    std::string error;

    // The answer in the fixture's shape, {per_type, by_type} (each entry {id, name, degree}, a missing name null), or
    // {refused} when refused.
    OWJsonValue ToJson() const;
};

// mostLinkedOf's option: per_type, a whole number from 1 to 2^53 - 1; unset is the default, 3.
struct OWMostLinkedOptions {
    std::optional<int64_t> per_type;
};

namespace OWLanding {

// mostLinkedOf's default count per type.
constexpr int64_t DefaultPerType = 3;

// mostLinkedOf(graph, options), the npm SDK's (4.203.0) and the Python package's most_linked_of: the most linked
// elements of each type, over the graph WorldGraph answered for the asker, so it holds exactly what that reader may
// see. For each type (as stored), its nodes by OWGraph::DegreeOf, highest first, ties by id in code point order, at
// most per_type of each: a few of every type, never one ranking. It is "the most linked", never "where to start":
// where a world has a Guide, the Guide is the start. A node of degree 0 and a known-of node are never picked; a node's
// type and name are echoed as written. Pure. A draft reading beside the landing profile, held to the packages' shared
// fixture (most-linked.fixture.json). A per_type below 1 or past 2^53 - 1 is refused by name, as the npm SDK refuses
// one that is not a safe integer.
OW_API OWMostLinked MostLinkedOf(const OWWorldGraph& graph, const OWMostLinkedOptions& options = {});

// The same with the options as JSON, as the packages take them: an object or null. An unknown key is refused by name
// (the first in the object's order); per_type null takes the default; any other value that is not a whole number from
// 1 to 2^53 - 1 (a fraction, a string, a boolean) is refused by name. A whole float reads whole (2.0 as 2).
OW_API OWMostLinked MostLinkedOf(const OWWorldGraph& graph, const OWJsonValue& options);

// The option keys, in the profile's order, and their defaults.
OW_API const std::vector<std::string>& OptionKeys();
constexpr int64_t DefaultHeads = 5;
constexpr int64_t DefaultHops = 1;
constexpr int64_t DefaultSize = 24;

// The gap kinds, in the order the answer lists them.
OW_API const std::vector<std::string>& GapKinds();

// landingOf(graph, options), over the graph WorldGraph answered for the visitor. Degree counts every edge, in or out,
// once at each node it touches (a mirror pair is two edges, a link to itself counts once), as OWGraph::DegreeOf.
// Heads: the Characters (type `character`, as stored) with at least one edge and not known-of, most linked first, ties
// by id in code point order, the first `heads`. A node within `hops` steps of a head joins the head it is fewest steps
// from, a step following an edge either way (a mirror pair is one step), a tie going to the higher-ranked head. Gaps:
// a known-of node is never one; no_place and nobody_here read only a Character's `location` edge to a Location node,
// so a place the visitor may not see reads as none. Counted in integers, ids compared by code point (a UTF-8 id's
// bytes), never by UTF-16 unit: the same graph and options give the same answer, byte for byte. Pure.
OW_API OWWorldLanding LandingOf(const OWWorldGraph& graph, const OWLandingOptions& options = {});

// The same with the options as JSON, as the packages take them: an object or null. An unknown key is refused by name
// before anything is read (the first in the object's order); a value given as null takes its default; any other value
// that is not a whole number of 1 or more (a fraction, a string, a boolean) is refused by name, in the order heads,
// hops, size. A whole float reads whole (2.0 as 2), as JavaScript reads it.
OW_API OWWorldLanding LandingOf(const OWWorldGraph& graph, const OWJsonValue& options);

}  // namespace OWLanding
}  // namespace OnlyWorlds
