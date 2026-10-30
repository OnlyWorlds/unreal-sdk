#pragma once
// The charter profile (schema-dist profiles/charter.md, serial 83 and 85): a world's reading aids, what every front door
// shows first. The Charter (a Law with supertype Charter: the rules here), the Guide (a Construct with supertype Guide:
// start here) and the Domains (Constructs with supertype Domain: the places in the world, each one sentence and the hub
// elements it links). An element is one when its pair, trimmed and folded as the room profile folds, AND the mark
// x_charter_of both hold, the mark read by presence (IsCharterElement); the pair without the mark is lore.
//
// CharterOf is the packet {charter, guide, domains} as one reader sees the world, DomainsOf the list alone: keel's
// elements/charter.py (reading_aids, served beside front_door on GET /me and the join reply; charter_skill in
// accounts/front_door.py) is the reading this follows. The world and the reader are OWLens::Resolve's, read through the
// lens reader's own view, so a reader means one thing in both. Held to the profile's conformance fixture
// (profiles/charter.fixture.json).

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// The Charter as one reader sees it: the rules a person or an AI follows here.
struct OWCharterLaw {
    std::string id;
    std::string name;  // as stored; "" when it has none
    // x_checks_rules (the checks profile's machine-checked rules) as stored; an empty list when absent or null.
    OWJsonValue rules = OWJsonValue::Array();
    // The world skill's Objects: the ids its description names, by any ow:// form, a site address or a bare id,
    // lowercase, each once in the order written, kept when it is an Object of this world the reader sees, the first
    // SkillMax of those kept (keel's charter_skill).
    std::vector<std::string> skill;
};

// The Guide as one reader sees it: where a stranger starts.
struct OWCharterGuide {
    std::string id;
    std::string name;
};

// A Domain's hub: a link field's target type and the id it names.
struct OWCharterHub {
    std::string type;
    std::string id;
};

// One Domain as one reader sees it: a place in the world, its sentence and its hubs.
struct OWCharterDomain {
    std::string id;
    std::string name;      // as served, untrimmed; "" when it has none
    std::string sentence;  // FirstLine of its description as served
    // The distinct {type, id} its served link fields name, in HubFields' order and each list's own, never itself; a
    // link the reader cannot see is not served, and a known-of stub serves none.
    std::vector<OWCharterHub> hubs;
};

// What CharterOf answers.
struct OWReadingAids {
    std::optional<OWCharterLaw> charter;  // absent when the reader sees no marked Charter
    std::optional<OWCharterGuide> guide;  // absent when the reader sees no marked Guide
    std::vector<OWCharterDomain> domains;  // the profile's order; empty when there is none
    // Empty unless the call was a mistake (a world with no elements list, a rung outside the four): it names the
    // mistake and nothing is read.
    std::string error;

    // The packet in the profile's shape: {charter, guide, domains}, an absent aid null.
    OWJsonValue ToJson() const;
};

namespace OWCharter {

// The mark every reading aid carries (the id of the world it was written for, read by presence), the Charter's field
// for the rules a machine checks, and at most this many skill Objects (keel's SKILL_MAX, D152).
extern const char* const Mark;        // "x_charter_of"
extern const char* const RulesField;  // "x_checks_rules"
constexpr size_t SkillMax = 5;

// The three pairs as the fixture writes them: type, supertype, and the aid's word (charter, guide, domain).
struct OWCharterPair {
    const char* type;
    const char* supertype;
    const char* aid;
};
const std::vector<OWCharterPair>& Pairs();
// A Domain's hub fields: the Construct's link fields in the schema's order, each with its target type.
const std::vector<std::pair<std::string, std::string>>& HubFields();

// True when `element` is a reading aid: its type and supertype, trimmed and folded, one of the pairs, AND the mark.
bool IsCharterElement(const OWJsonValue& element);
// The pair's word ("charter", "guide", "domain") from the type and supertype alone, trimmed and folded, the whole word,
// the mark ignored (the profile's readingAidOf): an unmarked Law with supertype Charter answers "charter". Absent for
// any other element; whether an element is a reading aid stays IsCharterElement's question.
std::optional<std::string> ReadingAidOf(const OWJsonValue& element);

// A Domain's sentence: the text trimmed of White_Space, up to its first line feed or carriage return, trimmed again.
std::string FirstLine(std::string_view text);

// The world's reading aids as `reader` sees them (the profile's charterOf). Pure: nothing is stored or written.
//
// `world` is what OWLens::Resolve takes: `elements` (the bodies as the owner is served them), `hidden` (the ids the
// hidden mark keeps), and `open`: true for a world open to anyone. `reader` is a lens reader (`rung` owner,
// contributor, guest with its `whole` and `known_of`, or public; on an owner, `as`). A public reader of a world not
// open to anyone reads nothing.
//
// The Charter is the newest marked Charter the reader sees (by created_at as an instant, none or an unreadable one the
// oldest, then the higher id by code point), read whole as stored: the Charter is every member rung's to read whole
// (D152), as keel reads its row. The Guide the same among marked Guides. The Domains are every marked Domain the reader
// sees: first in the order of the Guide's `constructs` as the reader is served the Guide (an id named twice keeps its
// first place; an entry that is no marked Domain, or one the reader cannot see, is skipped), then the rest by folded
// name and then id. Whether an element is an aid is read from the element as the owner is served it, since a stub
// serves no mark; what a Domain shows is what the reader is served.
OWReadingAids CharterOf(const OWJsonValue& world, const OWJsonValue& reader);
// The Domains alone, as CharterOf lists them for `reader` (the profile's domainsOf); empty on a caller's mistake.
std::vector<OWCharterDomain> DomainsOf(const OWJsonValue& world, const OWJsonValue& reader);

}  // namespace OWCharter
}  // namespace OnlyWorlds
