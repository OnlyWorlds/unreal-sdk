#include "OWRecord.h"

#include "OWMessage.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// keel's records_of (elements/record.py) over the elements as served: the mark checked member by member, played_at by
// the message page's grammar (OWMessage::TimeKey, by its parts, never a date parser), the order by the instant to the
// millisecond, ties and stories by id in code point order (a UTF-8 id's bytes compare in code point order).

namespace OnlyWorlds {

namespace OWRecord {
const char* const Mark = "x_record";

const std::vector<std::string>& Members() {
    static const std::vector<std::string> members = {"tool", "played_at", "version", "seed", "outcome", "seats"};
    return members;
}
}  // namespace OWRecord

namespace {

const char* const Proposed = "x_review_proposed";

OWJsonValue OrNull(const std::optional<std::string>& v) { return v ? OWJsonValue::String(*v) : OWJsonValue::Null(); }

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

bool IsType(const OWJsonValue& el, std::string_view type) {
    const OWJsonValue* t = Get(el, "type");
    return t && t->IsString() && t->Text() == type;
}

// Proposed as the review profile's reader that shows a world reads it (proposedMarkOf; record rule 1, serial 103): an
// x_review_proposed object with a key. Absent, null, an empty mark ({}, "", []) or any value that is not an object is
// settled.
bool IsProposed(const OWJsonValue& el) {
    const OWJsonValue* p = Get(el, Proposed);
    return p && p->IsObject() && p->Size() > 0;
}

// A string id, or nullptr: an element whose id is not a string cannot be named.
const std::string* IdOf(const OWJsonValue& el) {
    const OWJsonValue* id = Get(el, "id");
    return id && id->IsString() ? &id->Text() : nullptr;
}

std::string NameOf(const OWJsonValue& el) {
    const OWJsonValue* name = Get(el, "name");
    return name && name->IsString() ? name->Text() : std::string();
}

// Membership in a multi-link as served; a value that is not a list lists nothing.
bool Lists(const OWJsonValue& el, std::string_view field, const std::string& id) {
    const OWJsonValue* list = Get(el, field);
    if (!list || !list->IsArray()) return false;
    for (const OWJsonValue& entry : list->Items())
        if (entry.IsString() && entry.Text() == id) return true;
    return false;
}

bool Names(const OWJsonValue& el, std::string_view field, const std::string& id) {
    const OWJsonValue* v = Get(el, field);
    return v && v->IsString() && v->Text() == id;
}

bool AllOf(std::string_view s, size_t min, size_t max, bool (*ok)(char)) {
    if (s.size() < min || s.size() > max) return false;
    for (char c : s)
        if (!ok(c)) return false;
    return true;
}

bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Lower(char c) { return c >= 'a' && c <= 'z'; }
bool ToolChar(char c) { return Lower(c) || Digit(c) || c == '-'; }

// Seconds from 0001-01-01T00:00Z to 1970-01-01T00:00Z on the proleptic Gregorian calendar (719162 days).
const int64_t SecondsBefore1970 = 62135596800;

bool OptionalString(const OWJsonValue* v, std::optional<std::string>& out, bool (*ok)(std::string_view) = nullptr) {
    if (!v || v->IsNull()) return true;
    if (!v->IsString() || (ok && !ok(v->Text()))) return false;
    out = v->Text();
    return true;
}

bool OutcomeForm(std::string_view s) { return AllOf(s, 1, 24, Lower); }

}  // namespace

OWJsonValue OWRecordMark::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("tool", OWJsonValue::String(tool));
    out.Set("played_at", OWJsonValue::String(played_at));
    out.Set("version", OrNull(version));
    out.Set("seed", OrNull(seed));
    out.Set("outcome", OrNull(outcome));
    OWJsonValue s = OWJsonValue::Object();
    for (const auto& [id, seat] : seats) s.Set(id, OWJsonValue::String(seat));
    out.Set("seats", std::move(s));
    return out;
}

OWJsonValue OWCharacterRecord::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    OWJsonValue event = OWJsonValue::Object();
    event.Set("id", OWJsonValue::String(event_id));
    event.Set("name", OWJsonValue::String(event_name));
    out.Set("event", std::move(event));
    out.Set("tool", OWJsonValue::String(tool));
    out.Set("played_at", OWJsonValue::String(played_at));
    out.Set("version", OrNull(version));
    out.Set("seed", OrNull(seed));
    out.Set("outcome", OrNull(outcome));
    out.Set("seat", OrNull(seat));
    OWJsonValue list = OWJsonValue::Array();
    for (const OWRecordStory& s : stories) {
        OWJsonValue story = OWJsonValue::Object();
        story.Set("id", OWJsonValue::String(s.id));
        story.Set("name", OWJsonValue::String(s.name));
        story.Set("as", OWJsonValue::String(s.as));
        list.Push(std::move(story));
    }
    out.Set("stories", std::move(list));
    return out;
}

OWJsonValue OWCharacterRecords::ToJson() const {
    OWJsonValue out = OWJsonValue::Array();
    for (const OWCharacterRecord& r : records) out.Push(r.ToJson());
    return out;
}

namespace OWRecord {

std::optional<int64_t> InstantOf(std::string_view s) {
    // One grammar for a time in the standard: the message page's, read by OWMessage::TimeKey (its parts in range, an
    // offset applied, no offset read as UTC, T and Z in either case, the seconds required).
    OWJsonValue text = OWJsonValue::String(std::string(s));
    std::optional<OWMessage::OWTimeKey> key = OWMessage::TimeKey(&text);
    if (!key) return std::nullopt;
    // To the millisecond: the fraction truncated past three digits (.1239 is 123 ms).
    return (key->seconds + SecondsBefore1970) * 1000 + key->micros / 1000;
}

std::optional<OWRecordMark> MarkOf(const OWJsonValue& element) {
    if (!IsType(element, "event")) return std::nullopt;
    const OWJsonValue* m = Get(element, Mark);
    if (!m || !m->IsObject()) return std::nullopt;
    OWRecordMark out;
    const OWJsonValue* tool = m->Find("tool");
    if (!tool || !tool->IsString() || !AllOf(tool->Text(), 1, 32, ToolChar)) return std::nullopt;
    out.tool = tool->Text();
    const OWJsonValue* played = m->Find("played_at");
    if (!played || !played->IsString() || !InstantOf(played->Text())) return std::nullopt;
    out.played_at = played->Text();
    if (!OptionalString(m->Find("version"), out.version)) return std::nullopt;
    if (!OptionalString(m->Find("seed"), out.seed)) return std::nullopt;
    if (!OptionalString(m->Find("outcome"), out.outcome, OutcomeForm)) return std::nullopt;
    if (const OWJsonValue* seats = m->Find("seats"); seats && !seats->IsNull()) {
        if (!seats->IsObject()) return std::nullopt;
        for (size_t i = 0; i < seats->Size(); ++i) {
            if (!seats->Items()[i].IsString()) return std::nullopt;
            out.seats.emplace_back(seats->Keys()[i], seats->Items()[i].Text());
        }
    }
    return out;
}

OWCharacterRecords RecordsOf(const std::string& character, const OWJsonValue& elements) {
    OWCharacterRecords out;
    if (!elements.IsArray()) {
        out.error = "records_of: elements must be a list";
        return out;
    }
    // This Character's stories, by each Event they name (once per Narrative, however often it names it; a value in
    // events that is not a string names nothing): one pass over the world, then a lookup per record (npm 4.124.1).
    std::unordered_map<std::string, std::vector<OWRecordStory>> stories_of;
    for (const OWJsonValue& n : elements.Items()) {
        const std::string* story = IdOf(n);
        if (!story || !IsType(n, "narrative") || IsProposed(n)) continue;
        bool p = Names(n, "protagonist", character), t = Names(n, "narrator", character);
        if (!p && !t) continue;
        const OWJsonValue* events = Get(n, "events");
        if (!events || !events->IsArray()) continue;
        OWRecordStory s{*story, NameOf(n), p && t ? "both" : p ? "protagonist" : "narrator"};
        std::unordered_set<std::string> seen;
        for (const OWJsonValue& e : events->Items())
            if (e.IsString() && seen.insert(e.Text()).second) stories_of[e.Text()].push_back(s);
    }
    std::vector<int64_t> instants;
    for (const OWJsonValue& ev : elements.Items()) {
        const std::string* id = IdOf(ev);
        std::optional<OWRecordMark> mark = id ? MarkOf(ev) : std::nullopt;
        if (!mark || IsProposed(ev) || !Lists(ev, "characters", character)) continue;
        OWCharacterRecord r;
        r.event_id = *id;
        r.event_name = NameOf(ev);
        r.tool = mark->tool;
        r.played_at = mark->played_at;
        r.version = mark->version;
        r.seed = mark->seed;
        r.outcome = mark->outcome;
        for (const auto& [who, seat] : mark->seats)
            if (who == character) {
                r.seat = seat;
                break;
            }
        if (auto at = stories_of.find(*id); at != stories_of.end()) r.stories = at->second;
        std::stable_sort(r.stories.begin(), r.stories.end(),
                         [](const OWRecordStory& a, const OWRecordStory& b) { return a.id < b.id; });
        out.records.push_back(std::move(r));
        instants.push_back(*InstantOf(mark->played_at));
    }
    // Newest first by the instant, a tie by the Event's id (a UTF-8 id's bytes compare in code point order).
    std::vector<size_t> order(out.records.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (instants[a] != instants[b]) return instants[a] > instants[b];
        return out.records[a].event_id < out.records[b].event_id;
    });
    std::vector<OWCharacterRecord> sorted;
    sorted.reserve(order.size());
    for (size_t i : order) sorted.push_back(std::move(out.records[i]));
    out.records = std::move(sorted);
    return out;
}

}  // namespace OWRecord
}  // namespace OnlyWorlds
