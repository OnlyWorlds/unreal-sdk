#include "OWReport.h"

#include "OWLens.h"
#include "OWMessage.h"

#include <map>
#include <optional>
#include <string_view>

// The report profile's rule 9, as the npm SDK's reportThreadsOf (src/report.ts) and the Python package's
// report_threads_of (src/onlyworlds/report.py) read it.

namespace OnlyWorlds {

namespace {

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

// The element's pair, type and supertype folded with a slash between; "" when either is not text.
std::string PairOf(const OWJsonValue& el) {
    const OWJsonValue* t = Get(el, "type");
    const OWJsonValue* s = Get(el, "supertype");
    if (!t || !t->IsString() || !s || !s->IsString()) return std::string();
    return OWMessage::Fold(t->Text()) + "/" + OWMessage::Fold(s->Text());
}

bool IsRoomPair(const OWJsonValue& el, const char* supertype) {
    return OWLens::IsRoomElement(el) &&
           PairOf(el) == OWMessage::Fold(OWReport::Type) + "/" + OWMessage::Fold(supertype);
}

// "report" or "board" for a room element of that pair, else nullopt.
std::optional<std::string> RoomKindOf(const OWJsonValue* el) {
    if (!el) return std::nullopt;
    if (IsRoomPair(*el, OWReport::Supertype)) return std::string(OWReport::Report);
    if (IsRoomPair(*el, OWReport::BoardSupertype)) return std::string(OWReport::Board);
    return std::nullopt;
}

// The parent's id as written, never trimmed; none when it is no string or nothing is left after trimming White_Space.
std::optional<std::string> ParentOf(const OWJsonValue& el) {
    const OWJsonValue* p = Get(el, "parent_narrative");
    if (!p || !p->IsString() || OWMessage::Trim(p->Text()).empty()) return std::nullopt;
    return p->Text();
}

}  // namespace

namespace OWReport {

const char* const Type = "narrative";
const char* const Supertype = "Report";
const char* const BoardSupertype = "Board";

const char* const None = "none";
const char* const Report = "report";
const char* const Board = "board";
const char* const Missing = "missing";
const char* const Other = "other";
const char* const Self = "self";
const char* const Chain = "chain";

bool IsReport(const OWJsonValue& element) { return IsRoomPair(element, Supertype); }
bool IsBoardEntry(const OWJsonValue& element) { return IsRoomPair(element, BoardSupertype); }

OWReportThreads ThreadsOf(const std::vector<const OWJsonValue*>& narratives) {
    OWReportThreads out;
    // The elements by id, the first of two with one id; the reports among them in the order given.
    std::map<std::string, const OWJsonValue*> held;
    std::vector<const OWJsonValue*> reports;
    for (const OWJsonValue* el : narratives) {
        if (!el || !el->IsObject()) continue;
        const OWJsonValue* id = el->Find("id");
        if (!id || !id->IsString() || id->Text().empty() || held.count(id->Text())) continue;
        held.emplace(id->Text(), el);
        if (IsReport(*el)) reports.push_back(el);
    }
    auto element_of = [&](const std::string& id) -> const OWJsonValue* {
        auto it = held.find(id);
        return it == held.end() ? nullptr : it->second;
    };
    auto kind_of = [&](const OWJsonValue& report) -> std::string {
        std::optional<std::string> parent = ParentOf(report);
        if (!parent) return None;
        if (*parent == report.Find("id")->Text()) return Self;
        const OWJsonValue* target = element_of(*parent);
        if (!target) return Missing;
        return RoomKindOf(target).value_or(Other);
    };

    std::vector<std::string> order;  // the parents, in the order first named
    std::map<std::string, std::vector<const OWJsonValue*>> members;
    for (const OWJsonValue* report : reports) {
        const std::string id = report->Find("id")->Text();
        const std::string kind = kind_of(*report);
        out.parents.emplace_back(id, kind);
        if (kind == Self) out.lints.push_back({Self, id});
        if (kind == Report || kind == Board) {
            const std::string parent = *ParentOf(*report);
            if (!members.count(parent)) order.push_back(parent);
            members[parent].push_back(report);
            // A chain: the parent is a report whose own parent reads "report". One that names itself reads "self", as
            // naming no parent, so a report under it is no chain.
            if (kind == Report && kind_of(*element_of(parent)) == Report) out.lints.push_back({Chain, id});
        }
    }
    for (const std::string& parent : order) {
        OWReportGroup g;
        g.parent = parent;
        g.kind = RoomKindOf(element_of(parent)).value_or(std::string());
        for (const OWJsonValue* r : OWMessage::InCreatedOrder(members[parent])) g.reports.push_back(r->Find("id")->Text());
        out.groups.push_back(std::move(g));
    }
    return out;
}

}  // namespace OWReport

OWJsonValue OWReportThreads::ToJson() const {
    OWJsonValue answer = OWJsonValue::Object();
    OWJsonValue ps = OWJsonValue::Object();
    for (const auto& [id, kind] : parents) ps.Set(id, OWJsonValue::String(kind));
    answer.Set("parents", std::move(ps));
    OWJsonValue gs = OWJsonValue::Array();
    for (const OWReportGroup& g : groups) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("parent", OWJsonValue::String(g.parent));
        row.Set("kind", OWJsonValue::String(g.kind));
        OWJsonValue ids = OWJsonValue::Array();
        for (const std::string& r : g.reports) ids.Push(OWJsonValue::String(r));
        row.Set("reports", std::move(ids));
        gs.Push(std::move(row));
    }
    answer.Set("groups", std::move(gs));
    OWJsonValue ls = OWJsonValue::Array();
    for (const OWReportLint& l : lints) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("kind", OWJsonValue::String(l.kind));
        row.Set("report", OWJsonValue::String(l.report));
        ls.Push(std::move(row));
    }
    answer.Set("lints", std::move(ls));
    return answer;
}

// ---- A line of `consequences` (rule 10), as the npm SDK's readReportLine and readReportLines (src/report.ts) ----

namespace {

// The closing marks, character by character, as UTF-8: a question mark is none (after an address it begins a query).
const char* const Closing[] = {".", ",", ";", ":", "!", ")", "]", "}", "'", "\"",
                               "\xE2\x80\x99",  // U+2019
                               "\xE2\x80\x9D",  // U+201D
                               "\xC2\xBB"};     // U+00BB

// The pairs an address may stand in, opener then closer, as UTF-8 (a brace makes no pair).
const char* const Pairs[][2] = {{"(", ")"},
                                {"[", "]"},
                                {"\"", "\""},
                                {"'", "'"},
                                {"\xE2\x80\x9C", "\xE2\x80\x9D"},  // U+201C U+201D
                                {"\xE2\x80\x98", "\xE2\x80\x99"},  // U+2018 U+2019
                                {"\xC2\xAB", "\xC2\xBB"}};         // U+00AB U+00BB

// Whether the text at byte `at` begins with one of the closing marks. `at` is a code point's first byte.
bool ClosingAt(std::string_view s, size_t at) {
    for (const char* c : Closing) {
        std::string_view m(c);
        if (at + m.size() <= s.size() && s.compare(at, m.size(), m) == 0) return true;
    }
    return false;
}

// The profile's trim (OWMessage::Trim, White_Space; a byte order mark stays) at one end only: a sentinel that is no
// White_Space holds the other end.
std::string TrimEnd(std::string_view s) {
    const std::string held = "x" + std::string(s);
    return std::string(OWMessage::Trim(held).substr(1));
}
std::string TrimStart(std::string_view s) {
    const std::string held = std::string(s) + "x";
    std::string_view t = OWMessage::Trim(held);
    return std::string(t.substr(0, t.size() - 1));
}

// Whether a White_Space code point ends just before byte `at`, or begins at it.
bool WhiteBefore(std::string_view s, size_t at) { return at > 0 && TrimEnd(s.substr(0, at)).size() < at; }
bool WhiteAt(std::string_view s, size_t at) { return at < s.size() && TrimStart(s.substr(at)).size() < s.size() - at; }

}  // namespace

namespace OWReport {

OWReportLine ReadLine(std::string_view line, const std::optional<std::string>& world) {
    OWReportLine out;
    out.line = std::string(line);
    out.words = out.line;

    // Part 2: the last address that names an element of this world, from the text alone.
    const OWAddressFound* here = nullptr;
    const std::vector<OWAddressFound> found = OWAddress::FindAll(line);
    for (const OWAddressFound& f : found) {
        const OWAddressReading& r = f.reading;
        if (!r.type || !r.id) continue;
        if (!r.world || (!r.world_is_slug && world && *r.world == *world)) here = &f;
    }
    if (!here) return out;
    const OWAddressReading& reading = here->reading;
    const size_t start = here->start, end = here->end;
    out.address = "ow://" + *reading.type + "/" + *reading.id;
    out.reading = reading;
    out.start = start;
    out.end = end;

    // Part 3: it leaves the words only when nothing the writer wrote is lost.
    if (!reading.canonical || reading.moment || reading.lens) return out;
    size_t from = start, to = end;
    for (const auto& pair : Pairs) {
        std::string_view open(pair[0]), close(pair[1]);
        if (start < open.size() || line.compare(start - open.size(), open.size(), open) != 0) continue;
        // A matched pair goes with it (3a); an opener not closed right after keeps the whole line.
        if (end + close.size() > line.size() || line.compare(end, close.size(), close) != 0) return out;
        from = start - open.size();
        to = end + close.size();
        break;
    }
    const bool apart_before = from == 0 || WhiteBefore(line, from) || line[from - 1] == ':';
    const bool apart_after = to == line.size() || WhiteAt(line, to) || ClosingAt(line, to);
    if (!apart_before || !apart_after) return out;

    // The White_Space on both sides goes, and one colon before it with the White_Space before that colon (3c).
    std::string left = TrimEnd(line.substr(0, from));
    if (!left.empty() && left.back() == ':') left = TrimEnd(std::string_view(left).substr(0, left.size() - 1));
    const std::string right = TrimStart(line.substr(to));
    // Nothing stands before the address (3b): the whole line stays, so a line is never shown empty.
    if (left.empty()) return out;
    out.words = right.empty() ? left : ClosingAt(right, 0) ? left + right : left + " " + right;
    return out;
}

std::vector<OWReportLine> ReadLines(std::string_view text, const std::optional<std::string>& world) {
    std::vector<OWReportLine> out;
    size_t begin = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != '\r' && text[i] != '\n') continue;
        const std::string_view piece = OWMessage::Trim(text.substr(begin, i - begin));
        if (!piece.empty()) out.push_back(ReadLine(piece, world));
        begin = i + 1;
    }
    return out;
}

}  // namespace OWReport

}  // namespace OnlyWorlds
