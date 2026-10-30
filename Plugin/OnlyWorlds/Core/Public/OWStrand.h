#pragma once
// The strand profile (schema-dist profiles/strand.md, serial 98): a world in miniature, one story read off four links
// the standard already has. The World's mark x_strand names the story (a Narrative), one of its events and one of that
// Event's locations; the hero is the story's protagonist and where the hero is now is the hero's location.
//
// StrandOf answers the six slots as one reader sees the world: keel's elements/strand.py (strand_of) is the reading this
// follows, the npm SDK's strandOf and the Python package's strand_of the same. The world and the reader are
// OWLens::Resolve's, read through the lens reader's own view, so a reader means one thing in both. Held to the
// profile's conformance fixture (profiles/strand.fixture.json).

#include "OWJson.h"

#include <optional>
#include <string>
#include <vector>

namespace OnlyWorlds {

// A filled strand slot: the element's id and its name as the reader is served it ("" when it has none).
struct OWStrandSlot {
    std::string id;
    std::string name;
};

// What StrandOf answers: five slots, each absent when empty, and whether then and now are one place. An empty slot is
// what there is to make next, never an error.
struct OWWorldStrand {
    std::optional<OWStrandSlot> narrative;  // the story: the Narrative the mark names
    std::optional<OWStrandSlot> event;      // the happening: the Event the mark names, when in the story's events
    std::optional<OWStrandSlot> location;   // the place, then: the Location the mark names, when in the event's locations
    std::optional<OWStrandSlot> character;  // the hero: the story's protagonist
    std::optional<OWStrandSlot> now;        // the place, now: the hero's location
    // True when now and location are both filled with one id, false when with two, absent otherwise. Ids, never names:
    // two places may share a name.
    std::optional<bool> same;
    // Empty unless the call was a mistake (a world with no elements list, a rung outside the four): it names the
    // mistake and nothing is read.
    std::string error;

    // The strand in the profile's shape: {narrative, event, location, character, now, same}, each slot {id, name} and
    // an empty one null.
    OWJsonValue ToJson() const;
};

namespace OWStrand {

// The World's field that carries the mark, and the mark's three members in the profile's order.
extern const char* const Mark;  // "x_strand"
const std::vector<std::string>& Members();  // narrative, event, location

// The world's strand as `reader` sees it (the profile's strandOf). Pure: nothing is stored or written.
//
// `world` is what OWLens::Resolve takes, with the World's x_strand beside it: `elements` (the bodies as the owner is
// served them), `hidden` (the ids the hidden mark keeps), `open`: true for a world open to anyone, and the mark.
// `reader` is a lens reader (`rung` owner, contributor, guest with its `whole` and `known_of`, or public; on an owner,
// `as`). A public reader of a world not open to anyone reads nothing.
//
// The story is the element the mark names when it is a Narrative in the reader's view; with none, every slot is empty,
// since a strand hangs off its story. The happening is the element the mark names when it is an Event in the reader's
// view and in the story's events as the reader is served them; the place, then, the element the mark names when it is
// a Location in the reader's view and in that event's locations, empty when the event is. The hero is the story's
// protagonist when it is a Character in the reader's view; the place, now, the hero's location when it is a Location
// in the reader's view, empty when the hero is. A slot holds only its own kind, a list is read for membership and never
// its order, and no slot is filled from another: not the hero from the event's characters, not the place then from the
// hero's location, nor the reverse. Each slot's links are read from the body the reader is served, so a story known
// only as a stub fills its own slot and nothing past it.
//
// The mark is an object whose three members are each a string; any other shape (a bare string, a member missing, null
// or not a string) is no mark, and the strand is empty. An id that names no element in the reader's view, an empty
// string included, is an empty slot: there is no id-format test here (a malformed id is the doctor's to name).
OWWorldStrand StrandOf(const OWJsonValue& world, const OWJsonValue& reader);

}  // namespace OWStrand
}  // namespace OnlyWorlds
