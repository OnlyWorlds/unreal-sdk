#pragma once
// Private to the core: one reader's view of a world as OWLens builds it, and a lens's reading over it, for the graph
// (OWGraph.cpp) and the charter's reader (OWCharter.cpp). They read who sees what, what a body serves, and what a lens
// kept and cut, through the lens reader's own View and resolution, never a second copy of their rules.

#include "OWJson.h"
#include "OWLens.h"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OnlyWorlds {
namespace OWLensView {

struct Sight {
    // Each element read: one of the 22 types and an id of its own (the first of an id wins), in the world's order.
    std::vector<const OWJsonValue*> els;
    std::vector<std::string> ids;
    std::vector<std::string> types;  // the type, trimmed and folded: a slug
    std::unordered_map<std::string, size_t> by_id;
    // Per element: a room element, under the hidden mark, proposed, a reading aid; in the reader's view (whole or a
    // known-of stub; never proposed); a known-of stub.
    std::vector<char> room, hidden, proposed, aid, view, stub;
    // Per element: a link to it is served to this reader (keel's scoped: the view, and a proposed one below the public
    // rung), which places an edge's index in the list as served.
    std::vector<char> scoped;
    // The owner, reading as itself or as another (the owner's counts), and the rung the reading is judged by.
    bool owner_counts = false;
    std::string seen_rung;
    // The body this reader is served of element i, as the lens reader serves it (keel's serialize: a link it cannot
    // see left out, a known-of element as its stub); each made once, valid while the world lives.
    std::function<const OWJsonValue&(size_t)> served;

    // With a lens: its kept ids in order, each cut element's first reason by (type, id), the counts as Resolve gives
    // them, and the key that made it unreadable.
    std::vector<std::string> kept;
    std::map<std::pair<std::string, std::string>, std::string> cut_by;
    std::vector<std::pair<std::string, int64_t>> cut;
    std::optional<std::string> unreadable;

    // The element a typed link names: one of that id, of that type.
    std::optional<size_t> Find(const std::string& type, const std::string& id) const {
        auto it = by_id.find(id);
        if (it == by_id.end() || types[it->second] != type) return std::nullopt;
        return it->second;
    }
};

// A reader as Resolve checks it: "" when it reads, else the mistake, prefixed by `where`.
std::string CheckReader(const OWJsonValue& reader, const std::string& where);

// The reader's view of the world, and with `lens` (a body or an id; nullptr for none) its reading by `options`.
// "" when it reads, else the caller's mistake, prefixed by `where`, and nothing is read.
std::string See(const OWJsonValue& world, const OWJsonValue& reader, const OWJsonValue* lens,
                const OWLensOptions& options, const std::string& where, Sight& out);

}  // namespace OWLensView
}  // namespace OnlyWorlds
