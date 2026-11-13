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

}  // namespace OnlyWorlds
