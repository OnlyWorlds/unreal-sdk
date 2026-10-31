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
// says "Opens the world". For the public audience only rows whose needs is absent are there: a row that needs anything
// does not exist for the public. A lens and a moment on the address ride on no href.
//
// Each row's state, the first that holds: "You are here" for the surface's own tool (its client is surface.tool); then,
// when surface.online is false, "Needs a connection" for a row whose href is a web address (it begins http: or https:,
// in any case; a row whose link opens an app on the device keeps its own word, and the href stays in the row either
// way); then what needs asks of this surface: "account" outside a browser "Opens in your browser", in a browser signed
// out "Sign in to <name> first" (none when signed_in is unknown: a word shows only when the surface knows it is true),
// "device" on a phone "Needs the world on this phone"; then "Opens the world" (a world door on an element: a sign-in
// the row needs is the more urgent word); else none.
//
// Refusals, as OWAddressWritten spells them: reason is the address profile's word for whatever OWAddress::Write refuses
// of the whole address in its own form (the site's when it has none): a form, type, id, pairing, moment or lens
// (bad_moment, bad_lens), a slug or anything else for the world's id (bad_world); and an address without a world
// (no_world), the text form inside a world included;
// reason is empty and message names the rest (the npm SDK's TypeError): a kind that is neither, an element's openers
// asked of an address with no element, an unknown key on the address or the surface, a missing or unknown audience, a
// surface value of the wrong type, a tools row that is not one (a needs outside the list's format included).

#include "OWAddress.h"
#include "OWJson.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// A tool's open in the tools list: its link templates and what they need first. world holds {world} (the world's id);
// element holds {id} and, if it needs them, {world} and {type} (the type word); either absent without one. needs is
// "account" (signed in to the tool), "device" (the world already in the tool on this device), or absent for nothing,
// the one kind of row a public page offers.
struct OWToolOpen {
    std::optional<std::string> world;
    std::optional<std::string> element;
    std::optional<std::string> needs;
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
    std::string audience;             // "member" (a seat in the world, a guest included) or "public"; required
    std::optional<bool> signed_in;    // absent when the surface cannot know, and then no row says so
    bool browser = true;              // false in a game or a desktop app, which cannot see the browser's sign-in
    bool phone = false;
    bool online = true;               // false when the device has no connection: every web row says "Needs a connection"
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
std::string SignInFirst(std::string_view name);  // "Sign in to <name> first"

// The rows for a world ("world") or one element of it ("element") at these parts (their form is not read: the site's
// row is always the human form). tools is OWTools::All(), or the list as served through OpenersForJson.
OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressParts& address,
                           const OWOpenerSurface& surface);

// The rows for a reading from OWAddress::Parse (a reading by slug refuses as bad_world, one with no world as no_world).
OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressReading& address,
                           const OWOpenerSurface& surface);

// The rows with every argument as JSON, the shape the npm and Python packages take and the fixture writes: tools an
// array of rows (straight from tools.json: keys it does not read are ignored), the address an object of the address's
// keys (OWAddress::WriteJson's), the surface an object with snake_case keys (tool, audience, signed_in, browser,
// phone, online). Checks in the npm SDK's order.
OWOpenersResult OpenersForJson(const OWJsonValue& tools, std::string_view kind, const OWJsonValue& address,
                               const OWJsonValue& surface);

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
