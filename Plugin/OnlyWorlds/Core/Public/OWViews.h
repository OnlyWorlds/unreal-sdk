#pragma once
// How an element is shown by its kind (schema-dist views.json, version 3 since serial 115): for a report, a charter, a
// message and a line in a room, which field gives the one sentence for a card, a list row or a hover, which fields lead
// the page and which fold, the layout, and the marks shown at the head of any element's page. In the file's words,
// "presentation data, not the standard": a kind with no row draws as its type draws, in the standard's section order;
// each tool keeps its own drawing and reads the row; nothing here says who may see or do what (that is the server's,
// by what it serves).
//
// The table is generated (OWViews.generated.cpp, written by codegen/generate_views.py from the vendored file, which it
// checks first and refuses whole when it fails); ViewOf is this SDK's reader, held to the file's own conformance fixture
// (views.fixture.json) by tests/test_views.cpp. The Unity SDK's OWViews.ViewOf and the Godot SDK's OWViews.view_of read
// the same rules. A profile's pair is matched by that profile's own reader: OWLens::IsRoomElement for the room,
// OWCharter::IsCharterElement for the charter. Types and supertypes are trimmed and folded as the room profile folds
// them (OWMessage::Fold).
//
// Element bodies are JSON objects (OWJsonValue), as the API serves them and a world folder keeps them. Every function
// is total and pure.

#include "OWApi.h"
#include "OWJson.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// A mark views.json shows at the head of every kind's page (`known_of`, `hidden`, the two review marks,
// `x_served_trimmed`), and the page or document that defines how it is read.
struct OWViewBanner {
    std::string mark;
    std::string defined_by;
};

// What a row matches: a type alone (supertype ""), a type with a supertype, or a profile's pair, which matches only
// with that profile's mark, by that profile's own reader (profile "room" or "charter", pair as the file writes it,
// "narrative/Report"; type and supertype are then the pair's two halves). "" is none: the generator refuses an empty
// word.
struct OWViewKind {
    std::string type;
    std::string supertype;
    std::string profile;
    std::string pair;
};

// A field a row shows folded, under its label.
struct OWViewFold {
    std::string field;
    std::string label;
};

// The layout a row is drawn in (one of OWViews::Layouts: "document", "conversation") and the link a conversation
// follows ("parent_narrative"; "" when the layout names none).
struct OWViewLayout {
    std::string name;
    std::string by;
};

// A row of views.json: how elements of one kind are shown. A list the file leaves out is empty.
struct OWViewRow {
    std::string id;  // distinct in the table ("report", "room-line")
    OWViewKind kind;
    std::string from;                   // the page that defines the kind
    std::vector<std::string> sentence;  // the fields that give the one sentence, the first that holds something winning
    std::vector<std::string> lead;      // the fields drawn open first, in order
    std::vector<OWViewFold> fold;       // the fields drawn folded, each under its label
    OWViewLayout layout;
    std::vector<std::string> banner;    // marks the page shows after the table's own banners
};

// A generic link of a type (a Pin's `element`) and the two keys a served body carries for it.
struct OWViewGenericLink {
    std::string field;     // "element"
    std::string type_key;  // "element_type"
    std::string id_key;    // "element_id"
};

// What ViewOf answers for one element. A copy is its own: changing one changes no other, and never the table.
struct OW_API OWView {
    // The row that shows it, the table's own (valid for the program's life); nullptr when no row matches and the
    // element draws as its type.
    const OWViewRow* row = nullptr;
    // The one sentence for a card, a list row or a hover; absent when there is no row or no field the row names holds
    // anything. When present it holds something.
    std::optional<std::string> sentence;
    // The marks to show at the head of its page, in order: the table's banners the element carries, then the row's own.
    std::vector<std::string> banners;
    // The fields its page draws, in the order drawn (the file's `document` layout); see ViewOf.
    std::vector<std::string> order;

    // The answer in the fixture's shape: {row: the row's id or null, sentence: the text or null, banners, order}.
    OWJsonValue ToJson() const;
};

namespace OWViews {

// views.json's format (`_meta.version`) as baked: 3.
extern OW_API const int Version;
// sha256 of the views.json the table was baked from, as the pinned MANIFEST.json lists it (lowercase hex).
extern OW_API const char* const Sha256;

// The marks every page shows at its head, in the file's order.
OW_API const std::vector<OWViewBanner>& Banners();
// The layouts the file defines, name and sentence, in the file's order.
OW_API const std::vector<std::pair<std::string, std::string>>& Layouts();
// The rows, in the file's order.
OW_API const std::vector<OWViewRow>& Rows();

// A type's section fields in the schema's own order (schema/<type>.yaml: its sections as listed, each section's fields
// as listed), as the walk lists them: the order the document layout draws the rest in, and where a tool that draws
// empty fields to be filled draws them. The type is trimmed and folded; a word that is not one of the 22 has none. A
// Pin's generic link is its one name, `element`. The base fields (name, description, supertype, subtype, image_url)
// are in no section.
OW_API const std::vector<std::string>& SectionFields(std::string_view type);
// A type's generic links and their two keys, from the walk; the type trimmed and folded. Empty for every type but a Pin.
OW_API const std::vector<OWViewGenericLink>& GenericLinks(std::string_view type);

// How `element` is shown, by the table baked at this pin: the row that matches it, its one sentence, the marks to show
// at the head of its page, and the fields its page draws in order. Pass the element as the reader is served it: the
// banners read what was served (a known-of stub, the hidden mark, a trimmed list). Anything that is not a JSON object
// has no row, no sentence, no banners and no fields drawn.
//
// The row (the file's `match`): a profile's pair only with that profile's mark, by its own reader; a type with a
// supertype; a type alone. The most specific row wins, in that order; where two rows match on one rung (a table at
// fault, which the generator refuses) the first in the table wins and the page is still drawn.
//
// The sentence: the first field the row names in `sentence` whose value is a string holding something after trimming
// White_Space; that value trimmed, cut at its first line break (U+000A or U+000D only: U+2028 is no cut), and trimmed
// again. A field whose value is not a string holds nothing.
//
// The banners, in Banners()' order: known_of and hidden only when true; the two review marks only when the value is an
// object with at least one key; x_served_trimmed only when it names a field (a list holding a string with something
// left after trimming). Then the row's own `banner` marks the element carries, each read as the table reads it when it
// is one of the table's, otherwise by presence as the room profile reads a mark (not null; a string only when
// something is left after trimming; a list or an object only when it holds something; any other value counts). An
// element no row matches has no row and draws as its type; the banners still show.
//
// The order (the `document` layout): the row's lead, then the type's SectionFields the row puts in neither lead nor
// fold, then each fold; each field once, a field named in both lead and fold drawn in the lead. A field that holds
// nothing (absent, null, a string with nothing left after trimming, an empty list, an object with no keys) is not
// drawn; a number (0 too) and a bool (false too) hold something, and so does a list of blank strings. A generic link
// (a Pin's `element`) is one field of the order, under its schema name and at its place: it holds something only when
// both its keys (GenericLinks) are strings with something left after trimming; a body's own `element` key is never
// read. The base fields are drawn only where the row names one in lead or fold; extension keys never are. With no
// row, the type's section fields. A `conversation` draws each line's own fields this way; following its chain
// (layout.by) is the caller's.
OW_API OWView ViewOf(const OWJsonValue& element);

}  // namespace OWViews
}  // namespace OnlyWorlds
