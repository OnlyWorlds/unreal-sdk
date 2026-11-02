#include "OWRecord.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// keel's records_of (elements/record.py) over the elements as served: the mark checked member by member, played_at by
// its parts on the proleptic Gregorian calendar (never a date parser), the order by the instant to the millisecond,
// ties and stories by id in code point order (a UTF-8 id's bytes compare in code point order).

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

bool Leap(int64_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

int64_t MonthDays(int64_t y, int64_t m) {
    static const int64_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && Leap(y) ? 29 : days[m - 1];
}

// Days from 0001-01-01 on the proleptic Gregorian calendar.
int64_t DaysFrom1(int64_t y, int64_t m, int64_t d) {
    int64_t before = y - 1;
    int64_t days = before * 365 + before / 4 - before / 100 + before / 400;
    for (int64_t k = 1; k < m; ++k) days += MonthDays(y, k);
    return days + d - 1;
}

// `n` ASCII digits at `at` as a number, or -1.
int64_t Digits(std::string_view s, size_t at, size_t n) {
    if (at + n > s.size()) return -1;
    int64_t v = 0;
    for (size_t i = at; i < at + n; ++i) {
        if (!Digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

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
    // YYYY-MM-DDTHH:MM, then optionally :SS and optionally .fraction, then Z and nothing after.
    if (s.size() < 17 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':') return std::nullopt;
    int64_t y = Digits(s, 0, 4), mo = Digits(s, 5, 2), d = Digits(s, 8, 2), h = Digits(s, 11, 2), mi = Digits(s, 14, 2);
    if (y < 0 || mo < 0 || d < 0 || h < 0 || mi < 0) return std::nullopt;
    size_t at = 16;
    int64_t sec = 0, ms = 0;
    if (at < s.size() && s[at] == ':') {
        sec = Digits(s, at + 1, 2);
        if (sec < 0) return std::nullopt;
        at += 3;
        if (at < s.size() && s[at] == '.') {
            size_t start = ++at;
            while (at < s.size() && Digit(s[at])) ++at;
            if (at == start) return std::nullopt;  // a fraction is at least one digit
            // Truncated past three digits, padded short of them: .1 is 100 ms, .1239 is 123.
            for (size_t k = 0; k < 3; ++k) ms = ms * 10 + (start + k < at ? s[start + k] - '0' : 0);
        }
    }
    if (at + 1 != s.size() || s[at] != 'Z') return std::nullopt;
    // A real calendar instant, checked by its parts.
    if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > MonthDays(y, mo) || h > 23 || mi > 59 || sec > 59)
        return std::nullopt;
    int64_t seconds = ((DaysFrom1(y, mo, d) * 24 + h) * 60 + mi) * 60 + sec;
    return seconds * 1000 + ms;
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
