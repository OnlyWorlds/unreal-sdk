#pragma once
// The lens profile (schema-dist profiles/lens.md, dist.91): a saved way of reading a world. A lens is a Construct with
// supertype Lens; its links are its anchors, and the extension field x_lens is the recipe (how far, which types, how
// far back, how recently changed, how much). The pair and the template, the legal values of x_lens, where a seat's
// choice lives, an element's date by the profile's table, and the one resolution every reader agrees on
// (resolveLens), held to the profile's conformance fixture (profiles/lens.fixture.json). keel's reader
// (elements/lens.py, with the served bodies of elements/serialize.py) is the rule this follows; the graph section is
// OWGraph's (OWGraph.h), which reads through this resolution.
//
// Element bodies are JSON objects (OWJsonValue), as the API serves them and a world folder keeps them. A whole float in
// x_lens (7.0) reads whole, as in every reader; only a fraction is refused.
// Every function is total: a value of the wrong kind reads as the profile's "no".

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// The mail rule's part (rule 9): the messages addressed to the reader's Character and not its own, thirty days on
// created_at before the reading's clock, the newest kept up to the lens's budget.
struct OWLensMail {
    std::vector<std::string> unread;  // ids, in created_at order
    std::vector<std::string> read;
    // Each kept message: unread ones (every one under `all`) as the reader is served them, read ones as their title
    // ({id, name, narrator, created_at}); in created_at order.
    std::vector<OWJsonValue> rows;
    // The cut by reason ("window", "budget"); a reason with no count is absent.
    std::vector<std::pair<std::string, int64_t>> cut;
    // The unread and the read the reader may see, before the window and the cap.
    int64_t total_unread = 0;
    int64_t total_read = 0;
};

// What Resolve answers.
struct OWLensReading {
    // The kept elements' ids in the profile's order: anchors first in the lens's link order, then what the hop reached.
    std::vector<std::string> kept;
    // The kept elements as `show` makes them, in the same order; an anchor's row carries its `note`.
    std::vector<OWJsonValue> rows;
    // The cut counted by reason, in the profile's order; a reason with no count is absent, and a reader that is not the
    // owner never carries the owner's two.
    std::vector<std::pair<std::string, int64_t>> cut;
    // Notes on ids that are not anchors of the lens read, counted only where the reader sees the id.
    int64_t notes_ignored = 0;
    // The mail part, when the lens names a mail rule.
    std::optional<OWLensMail> mail;
    // The first x_lens key outside the profile's table (x_lens itself when it is not an object): nothing resolved.
    std::optional<std::string> unreadable;
    // Empty unless the call was a mistake (a world with no elements list, a rung outside the four, an as_of or clock
    // that does not read): it names the mistake and nothing is read.
    std::string error;

    // A reason's count (0 when absent).
    int64_t Cut(std::string_view reason) const;
    // The reading in the fixture's shape: {kept, rows, cut, notes_ignored, mail (null when none), unreadable}.
    OWJsonValue ToJson() const;
};

struct OWLensOptions {
    // The reading's date, a whole number in the world's unit. Absent: the world's now (`now`, else `time_current` as a
    // folder spells it, else `time_range_current` as the API serves it); with neither, no fence, and the window cuts
    // nothing.
    std::optional<int64_t> as_of;
    // The reading's wall clock, an RFC 3339 date-time, for the mail rule's thirty days and changed_within. Absent: the
    // machine's clock.
    std::optional<std::string> clock;
};

namespace OWLens {

// The pair, the template's subtype, the recipe's field and a folder Character's list of the lenses it reads.
extern const char* const Type;             // "construct"
extern const char* const Supertype;        // "Lens"
extern const char* const TemplateSubtype;  // "Template"
extern const char* const Field;            // "x_lens"
extern const char* const UsesField;        // "x_lens_uses"
// The marks: a room element's, a reading aid's (the id of the world the aid belongs to) and the review profile's.
extern const char* const RoomMark;         // "x_room_of"
extern const char* const ReadingAidMark;   // "x_charter_of"
extern const char* const ProposedMark;     // "x_review_proposed"

// The profile's tables, as its fixture writes them.
// The anchor fields: the Construct's Involves links in the schema's order, less `titles`.
const std::vector<std::string>& AnchorFields();
// The schema's type order (schema.json's): rule 5's order across types.
const std::vector<std::string>& TypeOrder();
// The date table, type to its date field, in the fixture's order; the ten types not in it are timeless.
const std::vector<std::pair<std::string, std::string>>& DateTable();
// The room profile's ten pairs (the tenth, narrative/report, at serial 112) and the reading aids' three, type and
// supertype folded with a slash between, in the fixture's order: the words `types` may name beside the 22 slugs.
const std::vector<std::string>& RoomPairs();
const std::vector<std::string>& ReadingAidPairs();
// The cut reasons, one closed list in the profile's order, and the owner's two.
const std::vector<std::string>& CutReasons();
const std::vector<std::string>& OwnerOnlyReasons();

constexpr int64_t DefaultDepth = 1;
constexpr int64_t DefaultBudget = 50;
extern const char* const DefaultUndated;  // "include"
extern const char* const DefaultShow;     // "full"
extern const char* const DefaultMail;     // "none"
constexpr int64_t MailDays = 30;
constexpr int64_t MaxBudget = 1000;

// A field of one of the 22 types, as schema.json lists it (the base fields not among them): a text, a whole number, a
// single link, a list of links, or one half of a generic link's pair (its type key, its id key).
enum class OWFieldKind : uint8_t { Text, Int, Single, Multi, GenericType, GenericId };
struct OWLensField {
    const char* name;
    OWFieldKind kind;
    // A link's target type; for a generic id key, its type key's name; "" otherwise.
    const char* target;
};
// The type's own fields in schema.json's order (empty for a word that is not one of the 22).
std::vector<OWLensField> FieldsOf(std::string_view type);
// The fields a known-of stub keeps beside the base ones (presentation.json's types.<type>.public).
const std::vector<std::string>& PublicFieldsOf(std::string_view type);

// A lens: a Construct whose supertype, trimmed and folded as the room profile's pairs are, is Lens (the whole word:
// "Lenses" is not one).
bool IsLens(const OWJsonValue& element);
// A template: a lens whose subtype, trimmed and folded, is Template. It resolves to nothing, anchors or not.
bool IsTemplate(const OWJsonValue& element);

// Null when x_lens is legal, else the first key refused in the profile's order (depth, types, window, from, to,
// undated, changed_within, show, budget, mail, notes), or "x_lens" for a value that is not an object. nullptr and null
// are legal; a key whose value is null is absent; a key the page does not name is ignored; an empty string is present
// and outside every table. A whole-number key takes a whole number, a whole float included (7.0 and 1e1 read as 7 and
// 10); a fraction is refused, and so is a number past 64 bits (a platform limit).
std::optional<std::string> CheckKeys(const OWJsonValue* x_lens);

// The lenses a seat reads, by id: its membership's choice when it made one (a list; an empty list is a choice), else
// its Character's x_lens_uses, else none. The membership wins whole. The strings of the list, each once in the order
// given, "" dropped; nullptr, null or a value that is not a list is no choice. A null membership is no membership (a
// folder world); a keel membership that never chose is not this reading's case: keel starts that seat with its Titles'
// lenses and never reads x_lens_uses (the lens profile, serial 112).
std::vector<std::string> LensesInUse(const OWJsonValue* membership, const OWJsonValue* x_lens_uses);

// An element's date as the profile reads it: the whole number in its type's date field (the type trimmed and folded),
// in the world's own unit. An integral float reads whole (7.0 as 7: the standard's dates are whole numbers, and a
// stored date is one). Absent for a timeless type, a body with no known type, and a date field that is absent, null or
// not a whole number; a known-of stub serves none.
std::optional<int64_t> DateOf(const OWJsonValue& element);

// A room element: its type and supertype, trimmed and folded, one of the room profile's pairs, and the mark x_room_of.
bool IsRoomElement(const OWJsonValue& element);
// A reading aid (rule 4): the charter (law, Charter), its guide (construct, Guide) or a domain (construct, Domain), by
// the pair AND the mark x_charter_of; the pair without the mark is lore.
bool IsReadingAid(const OWJsonValue& element);
// The hidden mark on the element's own body (folder spec 4.8): hidden present and not null or false, so a doubt counts
// as hidden. The lens view reads it beside the world's hidden list; the npm SDK's isHiddenMark.
bool IsHidden(const OWJsonValue& element);
// Proposed as a reader reads it (the review profile's rule 1): x_review_proposed present as a mark counts (any value
// but null, a string only when something is left after trimming White_Space), and not an empty list or object.
bool IsProposed(const OWJsonValue& element);

// resolveLens(world, lens, reader, {asOf, clock}): the lens read for one reader over a world snapshot. Pure: it never
// writes, and nothing in the world moves.
//
// `world` is a JSON object: `elements` (the bodies as the owner is served them), and optionally `hidden` (ids the
// hidden mark keeps, beside each element's own `hidden`), `members` (the members list, which proves a message's
// sender) and the world's now. `lens` is a lens body, or the id (a string) of a lens the reader is served whole.
// `reader` is a JSON object: `rung` (owner, contributor, guest or public), `character` (for the mail rule), a guest's
// `whole` and `known_of`, and on an owner `as` (another reader as an object: its view, the owner's two counts).
OWLensReading Resolve(const OWJsonValue& world, const OWJsonValue& lens, const OWJsonValue& reader,
                      const OWLensOptions& options = {});

}  // namespace OWLens
}  // namespace OnlyWorlds
