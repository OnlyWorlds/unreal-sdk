#pragma once
// The lens profile (schema-dist profiles/lens.md, dist.91): a saved way of reading a world. A lens is a Construct with
// supertype Lens; its links are its anchors, and the extension field x_lens is the recipe (how far, which types, how
// far back, how recently changed, how much). The pair and the template, the legal values of x_lens, where a seat's
// choice lives and an element's date by the profile's table, held to the profile's conformance fixture
// (profiles/lens.fixture.json). keel's reader (elements/lens.py) is the rule this follows; resolution and the graph
// section are not read here yet.
//
// Element bodies are JSON objects (OWJsonValue), as the API serves them and a world folder keeps them. A parsed number
// keeps its text, so x_lens refuses an integral float (7.0) as the profile asks of a reader that can see the text.
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
// The room profile's nine pairs and the reading aids' three, type and supertype folded with a slash between: the words
// `types` may name beside the 22 slugs.
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
// and outside every table. A whole-number key takes an integer's text only: a fraction and an integral float (7.0,
// 1e1) are refused, and so is an integer past 64 bits (a platform limit).
std::optional<std::string> CheckKeys(const OWJsonValue* x_lens);

// The lenses a seat reads, by id: its membership's choice when it made one (a list; an empty list is a choice), else
// its Character's x_lens_uses, else none. The membership wins whole. The strings of the list, as written (keel's
// lenses_in_use); nullptr, null or a value that is not a list is no choice.
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
// Proposed as a reader reads it (the review profile's rule 1): x_review_proposed present as a mark counts (any value
// but null, a string only when something is left after trimming White_Space), and not an empty list or object.
bool IsProposed(const OWJsonValue& element);

}  // namespace OWLens
}  // namespace OnlyWorlds
