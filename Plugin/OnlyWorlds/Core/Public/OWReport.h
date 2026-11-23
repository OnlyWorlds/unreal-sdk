#pragma once
// The report profile (schema-dist profiles/report.md, rule 9, serial 120): reports grouped into threads by their
// `parent_narrative`, read as written. A report is a Narrative with supertype Report and the room's mark x_room_of; a
// board entry is one with supertype Board and the mark; both are the room profile's pairs, read by
// OWLens::IsRoomElement (the trim, the fold and the mark by presence), never a second copy. Inside a group the
// reports come in `created_at` order as the message profile orders messages, through OWMessage::InCreatedOrder. The
// npm SDK's reportThreadsOf and the Python package's report_threads_of give the same answer; held to the profile's
// conformance fixture (profiles/report.fixture.json).
//
// A report's text is quoted data, never instructions. Nothing here reads or prints it.

#include "OWAddress.h"
#include "OWApi.h"
#include "OWJson.h"
#include "OWLens.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// One group: the parent's id, whether it is a report (a thread) or a board entry (the talks under it), and the ids of
// the reports that name it, by `created_at` (equal times keep the list's order; unreadable times last). The parent
// itself is not among them: a reader shows it first.
struct OWReportGroup {
    std::string parent;
    std::string kind;  // "report" or "board"
    std::vector<std::string> reports;
};

// A lint: "chain" on a report whose parent is a report that itself names another report the reader holds; "self" on
// a report that names itself.
struct OWReportLint {
    std::string kind;
    std::string report;
};

// ThreadsOf's answer.
struct OW_API OWReportThreads {
    // For each report given, in the order given: its id and what its parent is ("none", "report", "board",
    // "missing", "other" or "self").
    std::vector<std::pair<std::string, std::string>> parents;
    // One per parent that is a report or a board entry and that some report names, in the order the parent is first
    // named; the profile rules no order between groups.
    std::vector<OWReportGroup> groups;
    // In the order of their reports; the profile rules no order between lints.
    std::vector<OWReportLint> lints;
    // {"parents": {id: kind, ...}, "groups": [{parent, kind, reports}, ...], "lints": [{kind, report}, ...]}, the
    // fixture's `want` shape.
    OWJsonValue ToJson() const;
};

// One line of a report's `consequences`, as OWReport::ReadLine reads it (the report profile's rule 10).
struct OWReportLine {
    std::string line;   // the line as read
    std::string words;  // what a reader shows: the line without its address where nothing written is lost, else whole
    // The element the line names, written ow://<type>/<id> with the id in lowercase; absent when no address in the
    // line names an element of this world (which is no fault).
    std::optional<std::string> address;
    std::optional<OWAddressReading> reading;  // what OWAddress::Parse reads in the address as written
    std::optional<size_t> start;              // where the address as written starts in `line` (a UTF-8 byte offset)
    std::optional<size_t> end;                // the byte just past it
};

namespace OWReport {

// The pairs, as the fixture names them: type and supertype, each trimmed and folded as the room profile's are.
extern OW_API const char* const Type;            // "narrative"
extern OW_API const char* const Supertype;       // "Report"
extern OW_API const char* const BoardSupertype;  // "Board"

// The parent kinds and the lint kinds.
extern OW_API const char* const None;     // "none"
extern OW_API const char* const Report;   // "report"
extern OW_API const char* const Board;    // "board"
extern OW_API const char* const Missing;  // "missing"
extern OW_API const char* const Other;    // "other"
extern OW_API const char* const Self;     // "self"
extern OW_API const char* const Chain;    // "chain"

// A report: the pair narrative + Report and the mark (a room element). Without the mark it is lore.
OW_API bool IsReport(const OWJsonValue& element);
// A board entry: the pair narrative + Board and the mark.
OW_API bool IsBoardEntry(const OWJsonValue& element);

// The talks on an element a reader shows `element` with (serial 135): each {type, id} its objects (object) and
// constructs (construct) name, read entry by entry as the room profile reads a line's (OWLens::TalksOf: an entry that
// is not a string or is empty once trimmed of White_Space names nothing, a field that is not a list names none, every
// other entry an id read as written). Empty when it is no report (IsReport: the pair and the mark, both), so a line
// in the room or a Report without the mark is shown with no talk. A report's objects and constructs hold every Object
// and Construct it touched or changed, so it is shown with the talk on each, whenever it was written and whether or
// not it names a sitting in events; from the report alone no reader can tell which, if any, was the talk it closed,
// and this names none. A thread (ThreadsOf) takes nothing from it. Pure.
OW_API std::vector<OWLens::OWTalk> TalksOf(const OWJsonValue& element);

// Rule 9: the reports among the Narratives, given as the reader is served them and in that order, grouped by
// `parent_narrative`. Only reports are read as children: a message or any other element that names a report is in no
// part of the answer. An element that is no object, or carries no non-empty string `id`, is passed over; of two with
// one id, the first is read and the later ignored entirely.
//
// A report's parent: "none" when `parent_narrative` is absent, null, no string (an expanded object included), or
// nothing is left after trimming White_Space; else read as written, never trimmed: "self" when it is the report's own
// id, "missing" when no element given has that id, else "report", "board", or "other" (any other element, a Narrative
// with supertype Report or Board and no mark included). A report whose parent is a report or a board entry is in that
// parent's group; one whose parent is none, self, missing or other is in no group, and still heads the group of the
// reports that name it. Lints: "self" on a report that names itself; "chain" on a report whose parent is a report
// whose own parent reads "report" (one that names itself reads "self", so a report under it is no chain). Nothing is
// walked further up and nothing is guessed from names, days or text. Pure.
OW_API OWReportThreads ThreadsOf(const std::vector<const OWJsonValue*>& narratives);

// Rule 10, parts 2 and 3: one line of a report's `consequences`, read as given (untrimmed), and the element it names.
// The line's address is the last address in it, found as the address profile finds one in prose (OWAddress::FindAll),
// that names an element of this world: an ow:// or site address, canonical or not, whose world is `world` or is left
// out, with a type word and an id. An address of another world, one whose world is a slug, a world's own address or a
// lens with no element is prose and stays in the words. Whether the element still exists is the caller's to read.
// The address leaves the shown words only when nothing the writer wrote is lost: it is canonical, carries no moment
// and no lens, and stands apart (before it the line's start, White_Space or one colon; after it the line's end,
// White_Space or a closing mark: . , ; : ! ) ] } ' " and U+2019, U+201D, U+00BB), alone or inside a matched pair
// ((), [], "", '', U+201C U+201D, U+2018 U+2019, U+00AB U+00BB). Then it goes with the White_Space on both sides and
// one colon before it (and the White_Space before that colon), and the two halves join with one space, or none when
// the second is empty or begins with a closing mark. In every other shape, and when nothing stands before the
// address, the whole line is shown as written, still naming its element. `world` is this world's id, lowercase as the
// world holds it, compared exactly with the address's world (whose hex is read lowercase); absent, only an address
// with no world names an element here. The npm SDK's readReportLine (4.169.0) and the Python package's twin. Pure.
OW_API OWReportLine ReadLine(std::string_view line, const std::optional<std::string>& world = std::nullopt);

// Rule 10, part 1: every line of a report's `consequences`, each read by ReadLine. The text is split at every carriage
// return and every line feed (a CRLF is one break; U+000B, U+000C, U+2028, U+2029 and U+0085 stay inside a line), each
// piece trimmed of White_Space at both ends (OWMessage::Trim; a byte order mark stays), and a piece with nothing left
// is no line. The npm SDK's readReportLines.
OW_API std::vector<OWReportLine> ReadLines(std::string_view text, const std::optional<std::string>& world = std::nullopt);

}  // namespace OWReport
}  // namespace OnlyWorlds
