#pragma once
// The record profile (schema-dist profiles/record.md, serial 102): one run of a game a world's Characters took part in
// (a battle, a crossing, a match), kept in the world they live in, on kinds the standard already has. The run is an
// Event carrying the mark x_record ({tool, played_at, version?, seed?, outcome?, seats?}), the game's own tally beside
// it under its own prefix (never read here); who took part is the Event's `characters` alone; a Narrative that lists
// the Event in `events` and names a Character as `protagonist` or `narrator` is that Character's story of it. A
// Character's experience is the list RecordsOf reads, never a field on the Character.
//
// keel's elements/record.py (record_mark, records_of) is the reading this follows, the npm SDK's recordMarkOf and
// recordsOf and the Python package's record_mark_of and records_of the same. Held to the profile's conformance fixture
// (profiles/record.fixture.json).

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// A record's mark as read: the two required members as written, each optional member absent when unset (null or
// missing), the seats in the order written.
struct OWRecordMark {
    std::string tool;       // the game's id, the tools list's form: [a-z0-9-], 1 to 32
    std::string played_at;  // when the run was played, real time: RFC 3339 in UTC, as written
    std::optional<std::string> version;  // the engine's version; with seed, the replay
    std::optional<std::string> seed;     // the run's seed, a string
    std::optional<std::string> outcome;  // one lowercase word the game chose, 1 to 24 letters a to z
    // Character id to the seat in the game's own words. Decoration, never who took part.
    std::vector<std::pair<std::string, std::string>> seats;

    // The mark in the profile's shape: {tool, played_at, version, seed, outcome, seats}, an unset member null and
    // seats an object (empty when none).
    OWJsonValue ToJson() const;
};

// One Character's story of a record: about them ("protagonist"), told by them ("narrator"), or "both".
struct OWRecordStory {
    std::string id;
    std::string name;  // as served, "" when it has none
    std::string as;
};

// One record of a Character, as RecordsOf reads it.
struct OWCharacterRecord {
    std::string event_id;
    std::string event_name;  // as served, "" when it has none
    std::string tool;
    std::string played_at;
    std::optional<std::string> version;
    std::optional<std::string> seed;
    std::optional<std::string> outcome;
    std::optional<std::string> seat;  // this Character's seat, when the mark names one for them
    std::vector<OWRecordStory> stories;  // in code point order of their ids

    // {event: {id, name}, tool, played_at, version, seed, outcome, seat, stories: [{id, name, as}]}, unset members null.
    OWJsonValue ToJson() const;
};

// What RecordsOf answers: the records, newest first, and a caller's mistake when there was one.
struct OWCharacterRecords {
    std::vector<OWCharacterRecord> records;
    // Empty unless the call was a mistake (elements that are not a list): it names the mistake and nothing is read.
    std::string error;

    // The records as a JSON list of OWCharacterRecord::ToJson.
    OWJsonValue ToJson() const;
};

namespace OWRecord {

// The Event's field that carries the mark (the packages' RECORD_FIELD), and the mark's six members in the profile's
// order (the first two required).
extern const char* const Mark;  // "x_record"
const std::vector<std::string>& Members();  // tool, played_at, version, seed, outcome, seats

// The mark on an element, read (the packages' recordMarkOf): the mark when the element is an Event (`type` "event")
// whose x_record has the profile's shape, else absent. A broken writer never shows as a record: the value is no mark
// when it is not an object, when `tool` or `played_at` is missing or out of form, or when `version`, `seed`, `outcome`
// or `seats` is anything but its kind, null or absent (a number seed, a capitalised or empty outcome, a seats list, a
// seat that is not a string). A mark on any element but an Event is no mark. Proposal is RecordsOf's to read, not this.
//
// `played_at` is in form when it is YYYY-MM-DDTHH:MM, optionally :SS and then optionally a fraction (.digits), then Z,
// in ASCII digits, and names a real calendar instant, checked here by its parts and never by a date parser: year 0001
// or later, month 1 to 12, the day within its month (leap years Gregorian), hour 0 to 23, minute and second 0 to 59.
std::optional<OWRecordMark> MarkOf(const OWJsonValue& element);

// `played_at` as an instant: whole milliseconds from 0001-01-01T00:00Z (a fraction truncated past three digits), or
// absent when it is not in MarkOf's form. The order RecordsOf reads.
std::optional<int64_t> InstantOf(std::string_view played_at);

// A Character's records in a world (the packages' recordsOf), `elements` the world's elements as this reader is served
// them, a JSON list. Pure: nothing is stored or written.
//
// Every Event whose mark MarkOf reads, not still proposed (the review profile's x_review_proposed, any value but null),
// whose `characters` list names `character`; each with the Narratives, not still proposed, whose `events` list names
// the Event and whose `protagonist` or `narrator` (or both) is `character`. Newest first by played_at as an instant to
// the millisecond (never compared as a string), a tie by the Event's id in code point order; stories in code point
// order of their ids. A seat is the mark's seat for `character`, never a fallback for `characters`: a seat for a
// Character the Event does not list makes no record. A multi-link that is not a list lists nothing; an element whose id
// is not a string is never read (it cannot be named). An id no record names, or one in no element, reads an empty list.
OWCharacterRecords RecordsOf(const std::string& character, const OWJsonValue& elements);

}  // namespace OWRecord
}  // namespace OnlyWorlds
