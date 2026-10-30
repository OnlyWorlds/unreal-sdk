#pragma once
// The lens profile's graph section (schema-dist profiles/lens.md, "The graph", dist.91): a reading of a world as nodes
// and edges. The elements a reader sees are the nodes, the links between them the edges, and what is left out is
// counted by reason. It adds nothing to the world and stores nothing. Who sees what is OWLens's own reading of the
// reader, never a second copy; with a lens, the nodes are exactly what OWLens::Resolve keeps and its cut rides along.
// keel's graph reader for the profile (the fixture replay, tests/test_lens_profile.py's world_graph) is the rule this
// follows, with the profile itself where they differ; held to the fixture's graph kind (profiles/lens.fixture.json).
// A node's family and icon are OWPresentation's (the vendored presentation.json); colour stays the tool's.

#include "OWJson.h"
#include "OWLens.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// One element as a node.
struct OWGraphNode {
    std::string id;
    std::string type;  // the type slug
    OWJsonValue name;  // as the element carries it; null when it has none
    std::string family;  // the type's colour word (blue, green, pink or amber): a key for drawing, never a label
    std::string icon;    // the type's icon, a Material Symbols name
    // Why a node that is out by default is in: room, hidden, proposed, each that applies, in that order (map space
    // carries none: it is out for size, not standing).
    std::vector<std::string> marks;
    bool known_of = false;  // a guest's known-of stub: a node with no edges out
};

// One link: one element, one field, one target. `index` is the target's place in the field's list as the reader is
// served it (0 for a single link): the owner's as stored, any other reader's as keel serves it (a proposed id kept
// below the public rung).
struct OWGraphEdge {
    std::string source;
    std::string field;
    std::string target;
    int64_t index = 0;
};

// What WorldGraph answers.
struct OWWorldGraph {
    // By id, compared by code point (a UTF-8 id's bytes).
    std::vector<OWGraphNode> nodes;
    // By source id, then field name, both by code point, then index.
    std::vector<OWGraphEdge> edges;
    // The links left out because their target is not a node, by the target's reason, in OWGraph::EdgeReasons()'s
    // order; a reason with no count is absent, and a reader that is not the owner never carries the owner's reasons.
    std::vector<std::pair<std::string, int64_t>> edges_cut;
    // With a lens, the lens's own cut, unchanged; without one, the elements out by default the reader sees, by reason.
    std::vector<std::pair<std::string, int64_t>> cut;
    // With a lens the lens reader cannot read, the key it names (x_lens itself for a value that is not an object), and
    // the graph is empty.
    std::optional<std::string> unreadable;
    // A flag asked from the wrong rung, named: nothing was read and the graph is empty.
    std::string refused;
    // Empty unless the call was a mistake (a world with no elements list, a rung outside the four, an as_of or clock
    // that does not read, a lens neither a body nor an id): it names the mistake and nothing is read.
    std::string error;

    // The graph in the fixture's shape: {nodes, edges, edges_cut, cut, unreadable}, or {refused} when refused.
    OWJsonValue ToJson() const;
};

struct OWGraphOptions {
    // A lens body, or the id (a string) of a lens the reader is served whole; null for an unlensed graph.
    OWJsonValue lens;
    // The lens's reading: its date and its wall clock (OWLensOptions).
    std::optional<int64_t> as_of;
    std::optional<std::string> clock;
    // The flags, each bringing back one kind of element that is out of an unlensed graph by default, marked. hidden and
    // proposed are the owner's alone; a public reader takes none. With a lens, no flag adds a node.
    bool rooms = false;
    bool hidden = false;
    bool proposed = false;
    bool map_space = false;
};

namespace OWGraph {

// The profile's tables, as its fixture writes them.
// The extension fields a published profile declares a link, each a list of ids: the lens profile's and the spine's.
const std::vector<std::string>& DeclaredLinks();
// The flags (rooms, hidden, proposed, map_space), and the owner's two. A refusal names the first unmet one in the order
// hidden, proposed, rooms, map_space.
const std::vector<std::string>& Flags();
const std::vector<std::string>& OwnerOnlyFlags();
// Every reason a link is cut, one closed list (the graph's out-by-default reasons, not_visible, outside_lens, then the
// lens's own), and the owner's three.
const std::vector<std::string>& EdgeReasons();
const std::vector<std::string>& OwnerOnlyEdgeReasons();

// worldGraph(world, reader, options): `world` and `reader` are what OWLens::Resolve takes. Without a lens, the nodes
// are every element the reader sees less what is out by default: room elements, hidden ones and (for the owner, who
// sees them) proposed ones, and map space (Map, Pin, Marker, Zone), each back with its flag; and the reading aids,
// counted reading_aid, which no flag brings back. With a lens, the nodes are its kept. The edges are the schema's
// single and list links and the declared extension links, between nodes only; a link to an id that is not in the world
// (or not of the field's type) is dropped and not counted. Pure: it never writes.
OWWorldGraph WorldGraph(const OWJsonValue& world, const OWJsonValue& reader, const OWGraphOptions& options = {});

// Each node's degree: its edges, in or out, each edge counted once for every node it touches (a mirror pair is two
// edges, so each end counts 2; a link to itself counts once). Every node, 0 included, in the graph's node order.
std::vector<std::pair<std::string, int64_t>> DegreeOf(const OWWorldGraph& graph);
// Each node's out-degree: the distinct nodes its own links point at (a target in two fields counts once, a link to
// itself not at all). Every node, 0 included, in the graph's node order.
std::vector<std::pair<std::string, int64_t>> OutDegreeOf(const OWWorldGraph& graph);

}  // namespace OWGraph
}  // namespace OnlyWorlds
