#pragma once
// The Open with menu: the rows that open a world, or one element of it, in a tool, in order, each with the word it says
// when it cannot open now. Pure: a menu draws these rows and decides no word of its own. The rows come from the
// OnlyWorlds tools list (github.com/OnlyWorlds/tools, tools.json format 1, its README's "Open with"), baked into the
// core as OWTools::All() (OWTools.generated.cpp, from codegen/generate_tools.py).
//
// A port of the npm SDK's openersFor and the Python package's openers_for, held to their shared fixture
// (tests/data/openers.fixture.json), so every package gives the same rows.
//
// OpenersFor gives, in order: first the site's own row (id "onlyworlds", name "onlyworlds.com", the world's or the
// element's page on www.onlyworlds.com, for every audience, its one word "You are here" when surface.tool is
// "onlyworlds" (the site itself), else "Needs a connection" when offline), then one
// row per tools row, in the list's order, that is "listed" and has a door for this kind. A row with no open, or with
// no door for this kind, is not there. On an element, a tool with only a world door is a row to its world door that
// says "Opens the world". On an element given as the options' element (the element as served, the mark flat on it), a
// row whose open.element_skips names a mark the element carries (ElementMarks(), each read as OWGraph::WorldGraph
// reads it: MarksOf) is not there: its door cannot open it. A world door alone opens the world, so it stays. On an
// element's menu, a row whose open.element_types is a list that does not hold the address's type word is not there
// either, world door or not: the tool opens other kinds of element, so its world is not offered from this one (the
// type is read from the address, so no element body is needed; a world's menu reads no element types). For the
// public audience (the signed-out reader) a row whose needs is "device" is not there, since a visitor's device holds no
// world of this one's, and a row whose needs is "account" is there and says it needs one.
// A lens and a moment on the address ride on no href.
//
// Each row's state, the first that holds: "You are here" for the surface's own tool (its client is surface.tool); then,
// when surface.online is false, "Needs a connection" for a row whose href is a web address (it begins http: or https:,
// in any case; a row whose link opens an app on the device keeps its own word, and the href stays in the row either
// way); then, for the public audience, "Needs an account" for a row whose needs is "account", whatever signed_in says
// (the href stays in the row; a menu draws it as a row that opens nothing); then what needs asks of this surface: "account" outside a browser "Opens in your browser", in a browser signed
// out "Sign in to <name> first" (none when signed_in is unknown: a word shows only when the surface knows it is true),
// "device" on a phone "Needs the world on this phone"; then "Opens the world" (a world door on an element: a sign-in
// the row needs is the more urgent word); else none.
//
// Refusals, as OWAddressWritten spells them: reason is the address profile's word for whatever OWAddress::Write refuses
// of the whole address in its own form (the site's when it has none): a form, type, id, pairing, moment or lens
// (bad_moment, bad_lens), a slug or anything else for the world's id (bad_world); and an address without a world
// (no_world), the text form inside a world included;
// reason is empty and message names the rest (the npm SDK's TypeError): a kind that is neither, an element's openers
// asked of an address with no element, an unknown key on the address, the surface or the options, a missing or unknown
// audience, a surface value of the wrong type, an element given for a world's menu or naming another element than the
// address, a tools row that is not one (a needs, an element_skips or an element_types word outside the list's format,
// an empty or repeating element_types, or skips or element_types on a row with no element door, included).

#include "OWAddress.h"
#include "OWJson.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// A tool's open in the tools list: its link templates and what they need first. world holds {world} (the world's id);
// element holds {id} and, if it needs them, {world} and {type} (the type word); either absent without one. needs is
// "account" (signed in to the tool), "device" (the world already in the tool on this device), or absent for nothing. A
// public page offers those rows, and an "account" row only as one that opens nothing ("Needs an account").
// element_skips is what the element door does not open, in the lens profile's graph section's words (ElementMarks():
// room, hidden, proposed, map_space, reading_aid); empty when it opens every element, and on a list older than the key.
// element_types is the element types the element door opens, as type words (OWAddress::TypeWords: "character"), each
// once; absent (the list's null, or a list older than the key) when it opens every type. Only on a row with an element
// door, and never an empty list.
struct OWToolOpen {
    std::optional<std::string> world;
    std::optional<std::string> element;
    std::optional<std::string> needs;
    std::vector<std::string> element_skips = {};
    std::optional<std::vector<std::string>> element_types = std::nullopt;
};

// The keys of one tools list row that OpenersFor reads; the list's other keys are not kept.
struct OWTool {
    std::string id;    // the list's id for the tool, the row's id in the menu
    std::string name;
    std::string url;
    std::optional<std::string> client;  // the tool's client name (X-OW-Client), which a surface names itself by
    std::string rung;                   // "listed" rows are offered; "built" and "delisted" rows never are
    std::optional<OWToolOpen> open;     // absent: never offered
};

// What the surface drawing the menu knows of itself and its reader. Nothing is guessed.
struct OWOpenerSurface {
    std::optional<std::string> tool;  // the surface's own tool's client name, when it is a listed tool
    std::string audience;             // "member" (a seat in the world, a guest included) or "public" (signed out); required
    std::optional<bool> signed_in;    // absent when the surface cannot know, and then no row says so
    bool browser = true;              // false in a game or a desktop app, which cannot see the browser's sign-in
    bool phone = false;
    bool online = true;               // false when the device has no connection: every web row says "Needs a connection"
};

// What OpenersFor takes beside the surface: the element the menu is for, as the surface was served it (the API's body,
// the mark flat on it); a row whose door skips a mark it carries is not there. Null: every row, as before. Only for an
// element's menu, and only the element the address names. Read during the call, never kept.
struct OWOpenersOptions {
    const OWJsonValue* element = nullptr;
};

// One row of the menu.
struct OWOpener {
    std::string id;    // "onlyworlds" for the site's own row, else the tool's id in the list
    std::string name;  // "onlyworlds.com", or the tool's name
    std::string href;
    std::optional<std::string> state;  // absent when the row opens now and says nothing
};

// What OpenersFor gives: the rows when ok; else reason (an address refusal) or, with reason empty, message.
struct OWOpenersResult {
    bool ok = false;
    std::string reason;
    std::string message;
    std::vector<OWOpener> rows;
};

namespace OWOpeners {

extern const char* const World;    // "world"
extern const char* const Element;  // "element"
extern const char* const Member;   // "member"
extern const char* const Public;   // "public"

// The words a row says, sentence case, exactly.
extern const char* const YouAreHere;             // "You are here"
extern const char* const OpensInYourBrowser;     // "Opens in your browser"
extern const char* const NeedsTheWorldOnThisPhone;  // "Needs the world on this phone"
extern const char* const OpensTheWorld;          // "Opens the world"
extern const char* const NeedsAConnection;       // "Needs a connection"
extern const char* const NeedsAnAccount;         // "Needs an account"
std::string SignInFirst(std::string_view name);  // "Sign in to <name> first"

// The marks an element door may skip, the lens profile's graph section's words for what a lore view leaves an element
// out by, in its order: room, hidden, proposed, map_space (a Map, Pin or Marker; a Zone is content), reading_aid.
const std::vector<std::string>& ElementMarks();

// The marks an element carries, in ElementMarks()' order, each read by the reader OWGraph::WorldGraph uses:
// OWLens::IsRoomElement, OWLens::IsHidden, OWLens::IsProposed, the type folded (OWMessage::Fold) among
// OWGraph::MapSpaceTypes(), OWLens::IsReadingAid. Empty for one a lore view keeps.
std::vector<std::string> MarksOf(const OWJsonValue& element);

// The rows for a world ("world") or one element of it ("element") at these parts (their form is not read: the site's
// row is always the human form). tools is OWTools::All(), or the list as served through OpenersForJson.
OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressParts& address,
                           const OWOpenerSurface& surface, const OWOpenersOptions& options = {});

// The rows for a reading from OWAddress::Parse (a reading by slug refuses as bad_world, one with no world as no_world).
OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressReading& address,
                           const OWOpenerSurface& surface, const OWOpenersOptions& options = {});

// The rows with every argument as JSON, the shape the npm and Python packages take and the fixture writes: tools an
// array of rows (straight from tools.json: keys it does not read are ignored), the address an object of the address's
// keys (OWAddress::WriteJson's), the surface an object with snake_case keys (tool, audience, signed_in, browser,
// phone, online), the options an object ({element}: the element as served, or null) or nullptr for none. Checks in the
// npm SDK's order.
OWOpenersResult OpenersForJson(const OWJsonValue& tools, std::string_view kind, const OWJsonValue& address,
                               const OWJsonValue& surface, const OWJsonValue* options = nullptr);

}  // namespace OWOpeners

namespace OWTools {

extern const char* const Repository;  // "https://github.com/OnlyWorlds/tools"
extern const char* const Commit;      // the list's commit these rows came from
extern const char* const Sha256;      // tools.json's sha256 at Commit

// The OnlyWorlds tools list's rows (format 1), in the list's order, as of this release: the keys OpenersFor reads. A
// tool listed after a release is offered from the next one.
const std::vector<OWTool>& All();

}  // namespace OWTools
}  // namespace OnlyWorlds
