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

#include "OWJson.h"

#include <string>
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
struct OWReportThreads {
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

namespace OWReport {

// The pairs, as the fixture names them: type and supertype, each trimmed and folded as the room profile's are.
extern const char* const Type;            // "narrative"
extern const char* const Supertype;       // "Report"
extern const char* const BoardSupertype;  // "Board"

// The parent kinds and the lint kinds.
extern const char* const None;     // "none"
extern const char* const Report;   // "report"
extern const char* const Board;    // "board"
extern const char* const Missing;  // "missing"
extern const char* const Other;    // "other"
extern const char* const Self;     // "self"
extern const char* const Chain;    // "chain"

// A report: the pair narrative + Report and the mark (a room element). Without the mark it is lore.
bool IsReport(const OWJsonValue& element);
// A board entry: the pair narrative + Board and the mark.
bool IsBoardEntry(const OWJsonValue& element);

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
OWReportThreads ThreadsOf(const std::vector<const OWJsonValue*>& narratives);

}  // namespace OWReport
}  // namespace OnlyWorlds
