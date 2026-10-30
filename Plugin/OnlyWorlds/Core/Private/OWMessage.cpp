#include "OWMessage.h"

#include "OWUnicode.h"
#include "OWUnicodeData.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <unordered_map>

// A port of the TypeScript SDK's src/message.ts (@onlyworlds/sdk 4.102.4), with the room profile's trim and fold
// (room.ts) and the review profile's proposed mark (review.ts proposedMarkOf). Where a line mirrors one of its rules,
// the comment says which.

namespace OnlyWorlds {
namespace OWMessage {

const char* const Type = "narrative";
const char* const Supertype = "Message";
const char* const NoteSupertype = "Note";
const char* const ReadMarkType = "relation";
const char* const ReadMarkSupertype = "Read";
const char* const ReadMarkName = "Read: <the reader's name>";
const char* const ReaderField = "actor";
const char* const MessagesField = "narratives";
const char* const ServedTrimmed = "x_served_trimmed";

const char* const Verified = "verified";
const char* const Mismatch = "mismatch";
const char* const Unverifiable = "unverifiable";
const char* const Unseen = "unseen";

namespace {

namespace D = UnicodeData;

// Unicode's White_Space, the 25 code points the room profile trims by (not JavaScript's trim, which also strips
// U+FEFF; not Python's str.strip, which also strips U+001C to U+001F). Each code point's UTF-8.
const char* const WhiteSpace[] = {
    "\x09", "\x0A", "\x0B", "\x0C", "\x0D", "\x20", "\xC2\x85", "\xC2\xA0", "\xE1\x9A\x80",
    "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
    "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A",
    "\xE2\x80\xA8", "\xE2\x80\xA9", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80",
};

// A JSON number as JavaScript holds it (only compared here, never printed).
double JsNumber(const OWJsonValue& v) {
    const std::string& t = v.Text();
    double d = 0;
    std::from_chars_result r = std::from_chars(t.data(), t.data() + t.size(), d);
    if (r.ec == std::errc::result_out_of_range) {
        // Past a double's range: Infinity when the exponent is positive, 0 when it is not (a fine enough line for a
        // comparison; no message field holds such a number).
        size_t e = t.find_first_of("eE");
        bool big = e != std::string::npos && t[e + 1] != '-';
        bool negative = !t.empty() && t[0] == '-';
        return big ? (negative ? -INFINITY : INFINITY) : 0.0;
    }
    return d;
}

// JavaScript's ===, for two property reads (nullptr is undefined): an object or a list is equal only to itself.
bool Same(const OWJsonValue* a, const OWJsonValue* b) {
    if (!a || !b) return !a && !b;
    if (a->kind() != b->kind()) return false;
    switch (a->kind()) {
        case OWJsonValue::Kind::Null: return true;
        case OWJsonValue::Kind::Bool: return a->AsBool() == b->AsBool();
        case OWJsonValue::Kind::Number: return JsNumber(*a) == JsNumber(*b);
        case OWJsonValue::Kind::String: return a->Text() == b->Text();
        default: return a == b;
    }
}

bool IsString(const OWJsonValue* v, std::string_view s) { return v && v->IsString() && v->Text() == s; }

// isBody: an object that is not a list.
bool IsBody(const OWJsonValue& v) { return v.IsObject(); }

// carries: the key is there with a value (null counts: it is the owner's hand).
bool Carries(const OWJsonValue& body, std::string_view key) { return body.IsObject() && body.Has(key); }

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

// A list holding the string (Array.prototype.includes with a string).
bool Includes(const OWJsonValue* list, std::string_view s) {
    if (!list || !list->IsArray()) return false;
    for (const OWJsonValue& v : list->Items())
        if (v.IsString() && v.Text() == s) return true;
    return false;
}

bool MatchesPair(const OWJsonValue& el, std::string_view type, std::string_view supertype) {
    if (!IsBody(el)) return false;
    const OWJsonValue* t = el.Find("type");
    const OWJsonValue* s = el.Find("supertype");
    if (!t || !t->IsString() || !s || !s->IsString()) return false;
    return Fold(t->Text()) == Fold(type) && Fold(s->Text()) == Fold(supertype);
}

bool IsReadMark(const OWJsonValue& el) { return MatchesPair(el, ReadMarkType, ReadMarkSupertype); }

// characterOf: the row whose id is the membership (null is the owner's row); its Character when a non-empty string.
std::optional<std::string> CharacterOf(const OWJsonValue* membership, const OWJsonValue* members) {
    if (!members || !members->IsArray()) return std::nullopt;
    for (const OWJsonValue& row : members->Items()) {
        if (!IsBody(row) || !Same(row.Find("id"), membership)) continue;
        const OWJsonValue* c = row.Find("character");
        if (c && c->IsString() && !c->Text().empty()) return c->Text();
        return std::nullopt;  // the first row with the id decides (find)
    }
    return std::nullopt;
}

// handOf: the author's Character, from created_by; absent when the body carries no created_by.
std::optional<std::string> HandOf(const OWJsonValue& body, const OWJsonValue* members) {
    if (!Carries(body, "created_by")) return std::nullopt;
    return CharacterOf(body.Find("created_by"), members);
}

// isOwnHand: no created_by (a folder): the narrator is trusted; with it (the wire): a verified sender only.
bool IsOwnHand(const OWJsonValue& body, std::string_view character_id, const OWJsonValue* members) {
    if (!Carries(body, "created_by")) return IsString(body.Find("narrator"), character_id);
    OWSender s = SenderStatus(body, members);
    return s.status == Verified && s.claimed && *s.claimed == character_id;
}

// proposedMarkOf(note) !== null: x_review_proposed an object with something in it (isPresent, not a list).
bool IsProposed(const OWJsonValue& el) {
    const OWJsonValue* v = Get(el, "x_review_proposed");
    return v && v->IsObject() && v->Size() > 0;
}

// isHiddenMark: any value but absent, null and false.
bool IsHidden(const OWJsonValue& el) {
    const OWJsonValue* v = Get(el, "hidden");
    return v && !v->IsNull() && !(v->IsBool() && !v->AsBool());
}

bool Digits(std::string_view s, size_t at, size_t n, int64_t& out) {
    if (at + n > s.size()) return false;
    out = 0;
    for (size_t i = at; i < at + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + (s[i] - '0');
    }
    return true;
}

int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int64_t DaysFromCivil(int64_t y, int64_t m, int64_t d) {
    int64_t yy = m <= 2 ? y - 1 : y;
    int64_t era = FloorDiv(yy, 400);
    int64_t yoe = yy - era * 400;
    int64_t doy = FloorDiv(153 * (m + (m > 2 ? -3 : 9)) + 2, 5) + d - 1;
    int64_t doe = yoe * 365 + FloorDiv(yoe, 4) - FloorDiv(yoe, 100) + doy;
    return era * 146097 + doe - 719468;
}

}  // namespace

std::vector<const OWJsonValue*> List(const OWJsonValue& array) {
    std::vector<const OWJsonValue*> out;
    if (array.IsArray())
        for (const OWJsonValue& v : array.Items()) out.push_back(&v);
    return out;
}

std::string_view Trim(std::string_view s) {
    auto strip = [&](bool front) {
        bool more = true;
        while (more && !s.empty()) {
            more = false;
            for (const char* w : WhiteSpace) {
                std::string_view ws(w);
                if (s.size() < ws.size()) continue;
                bool at = front ? s.compare(0, ws.size(), ws) == 0 : s.compare(s.size() - ws.size(), ws.size(), ws) == 0;
                if (at) {
                    front ? s.remove_prefix(ws.size()) : s.remove_suffix(ws.size());
                    more = true;
                    break;
                }
            }
        }
    };
    strip(true);
    strip(false);
    return s;
}

std::string Fold(std::string_view utf8) {
    std::u32string cps = OWUnicode::Decode(Trim(utf8));
    for (char32_t& c : cps) {
        const D::FoldRow* end = D::Folds + D::FoldCount;
        const D::FoldRow* r = std::lower_bound(D::Folds, end, c, [](const D::FoldRow& row, char32_t k) { return row.cp < k; });
        if (r != end && r->cp == c) c = r->to;
    }
    return OWUnicode::Encode(cps);
}

bool IsMessage(const OWJsonValue& element) { return MatchesPair(element, Type, Supertype); }
bool IsNote(const OWJsonValue& element) { return MatchesPair(element, Type, NoteSupertype); }

std::vector<std::string> RecipientsOf(const OWJsonValue& message) {
    std::vector<std::string> out;
    if (!IsBody(message) || IsNote(message)) return out;
    const OWJsonValue* list = message.Find("characters");
    if (list && list->IsArray())
        for (const OWJsonValue& c : list->Items())
            if (c.IsString() && !c.Text().empty()) out.push_back(c.Text());
    return out;
}

bool IsAddressedTo(const OWJsonValue& message, std::string_view character_id) {
    if (!IsBody(message) || IsNote(message) || character_id.empty()) return false;
    if (IsToEveryone(message)) return true;
    return Includes(message.Find("characters"), character_id);
}

bool IsToEveryone(const OWJsonValue& message, bool keyless) {
    if (!IsBody(message) || IsNote(message) || keyless || HasUnseenRecipients(message) == std::optional<bool>(true))
        return false;
    const OWJsonValue* list = message.Find("characters");
    return !list || list->IsNull() || (list->IsArray() && list->Size() == 0);
}

std::optional<bool> HasUnseenRecipients(const OWJsonValue& message, bool keyless) {
    if (!IsBody(message) || IsNote(message)) return false;
    if (keyless) return std::nullopt;
    return Includes(message.Find(ServedTrimmed), "characters");
}

bool IsSentBy(const OWJsonValue& message, std::string_view character_id) {
    return IsBody(message) && !character_id.empty() && IsString(message.Find("narrator"), character_id);
}

OWSender SenderStatus(const OWJsonValue& message, const OWJsonValue* members) {
    OWSender out;
    const OWJsonValue* narrator = Get(message, "narrator");
    if (narrator && narrator->IsString() && !narrator->Text().empty()) out.claimed = narrator->Text();
    if (!out.claimed && Includes(Get(message, ServedTrimmed), "narrator")) {
        out.status = Unseen;
        return out;
    }
    if (!out.claimed || !IsBody(message)) {
        out.status = Unverifiable;
        return out;
    }
    std::optional<std::string> hand = HandOf(message, members);
    if (!hand) {
        out.status = Unverifiable;
    } else if (*hand == *out.claimed) {
        out.status = Verified;
    } else {
        out.status = Mismatch;
        out.written_by = hand;
    }
    return out;
}

OWEdited EditedBy(const OWJsonValue& message, const OWJsonValue* members) {
    OWEdited out;
    if (!IsBody(message) || !Carries(message, "changed_by")) return out;
    if (Carries(message, "created_by") && Same(message.Find("changed_by"), message.Find("created_by"))) return out;
    out.edited = true;
    out.by = CharacterOf(message.Find("changed_by"), members);
    return out;
}

const OWJsonValue* ReadMarkOf(const std::vector<const OWJsonValue*>& relations, std::string_view character_id,
                              const OWJsonValue* members, bool keyless) {
    if (keyless || character_id.empty()) return nullptr;
    const OWJsonValue* mark = nullptr;
    std::optional<OWTimeKey> mark_key;
    for (const OWJsonValue* rel : relations) {
        if (!rel || !IsReadMark(*rel) || !IsString(rel->Find(ReaderField), character_id)) continue;
        bool has_author = Carries(*rel, "created_by");
        // Its last writer is its author: a changed_by beside no created_by, or another than it, is ignored.
        if (Carries(*rel, "changed_by") && (!has_author || !Same(rel->Find("changed_by"), rel->Find("created_by"))))
            continue;
        // Its author is the reader: the sender rule on actor, where created_by exists.
        if (has_author && HandOf(*rel, members) != std::optional<std::string>(std::string(character_id))) continue;
        std::optional<OWTimeKey> key = TimeKey(rel->Find("updated_at"));
        if (!mark || (key && (!mark_key || *mark_key < *key))) {
            mark = rel;
            mark_key = key;
        }
    }
    return mark;
}

std::vector<const OWJsonValue*> UnreadFor(const std::vector<const OWJsonValue*>& messages, std::string_view character_id,
                                          const OWJsonValue* read_mark, const OWJsonValue* members) {
    std::set<std::string> read;
    if (read_mark && IsBody(*read_mark))
        if (const OWJsonValue* on = read_mark->Find(MessagesField); on && on->IsArray())
            for (const OWJsonValue& id : on->Items())
                if (id.IsString()) read.insert(id.Text());
    std::vector<const OWJsonValue*> out;
    for (const OWJsonValue* m : messages) {
        if (!m || !IsBody(*m) || !IsAddressedTo(*m, character_id)) continue;
        if (IsOwnHand(*m, character_id, members)) continue;
        const OWJsonValue* id = m->Find("id");
        if (id && id->IsString() && read.count(id->Text())) continue;
        out.push_back(m);
    }
    return InCreatedOrder(std::move(out));
}

std::vector<const OWJsonValue*> ThreadOf(const std::vector<const OWJsonValue*>& messages, std::string_view root_id) {
    if (root_id.empty()) return {};
    std::unordered_map<std::string, const OWJsonValue*> by_id;
    for (const OWJsonValue* m : messages) {
        const OWJsonValue* id = m && IsBody(*m) ? m->Find("id") : nullptr;
        if (id && id->IsString() && !by_id.count(id->Text())) by_id.emplace(id->Text(), m);
    }
    auto reaches = [&](const OWJsonValue* at) {
        std::set<std::string> seen;
        while (at) {
            const OWJsonValue* id = at->Find("id");
            if (IsString(id, root_id)) return true;
            if (!id || !id->IsString() || seen.count(id->Text())) return false;
            seen.insert(id->Text());
            const OWJsonValue* parent = at->Find("parent_narrative");
            if (IsString(parent, root_id)) return true;
            if (!parent || !parent->IsString()) return false;
            auto next = by_id.find(parent->Text());
            at = next == by_id.end() ? nullptr : next->second;
        }
        return false;
    };
    std::vector<const OWJsonValue*> out;
    for (const OWJsonValue* m : messages)
        if (m && IsBody(*m) && !IsNote(*m) && reaches(m)) out.push_back(m);
    return InCreatedOrder(std::move(out));
}

std::optional<std::string> NoteOf(const std::vector<const OWJsonValue*>& narratives, std::string_view character_id,
                                  const OWJsonValue* members) {
    if (character_id.empty()) return std::nullopt;
    std::optional<std::string> line;
    std::optional<OWTimeKey> line_key;
    for (const OWJsonValue* el : narratives) {
        if (!el || !IsNote(*el)) continue;
        const OWJsonValue* id = el->Find("id");
        if (!id || !id->IsString() || id->Text().empty() || !IsString(el->Find("narrator"), character_id)) continue;
        if (IsHidden(*el) || IsProposed(*el) || !IsOwnHand(*el, character_id, members)) continue;
        std::optional<OWTimeKey> key = TimeKey(el->Find("created_at"));
        // A readable time beats an unreadable one; the newer, or the later of two equal, wins.
        if (!line || (key ? (!line_key || !(*key < *line_key)) : !line_key)) {
            line = id->Text();
            line_key = key;
        }
    }
    return line;
}

std::optional<OWTimeKey> TimeKey(const OWJsonValue* value) {
    // /^(\d{4})-(\d{2})-(\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(?:[Zz]|([+-])(\d{2}):(\d{2}))?$/, ASCII digits.
    if (!value || !value->IsString()) return std::nullopt;
    std::string_view s = value->Text();
    int64_t y, mo, d, h, mi, sec;
    if (!Digits(s, 0, 4, y) || s.size() < 19 || s[4] != '-' || !Digits(s, 5, 2, mo) || s[7] != '-' ||
        !Digits(s, 8, 2, d) || (s[10] != 'T' && s[10] != 't') || !Digits(s, 11, 2, h) || s[13] != ':' ||
        !Digits(s, 14, 2, mi) || s[16] != ':' || !Digits(s, 17, 2, sec))
        return std::nullopt;
    size_t at = 19;
    std::string fraction;
    if (at < s.size() && s[at] == '.') {
        size_t start = ++at;
        while (at < s.size() && s[at] >= '0' && s[at] <= '9') ++at;
        if (at == start) return std::nullopt;
        fraction = std::string(s.substr(start, at - start));
    }
    int64_t offset = 0;
    if (at < s.size() && (s[at] == 'Z' || s[at] == 'z')) {
        ++at;
    } else if (at < s.size() && (s[at] == '+' || s[at] == '-')) {
        int64_t oh, om;
        if (!Digits(s, at + 1, 2, oh) || at + 3 >= s.size() || s[at + 3] != ':' || !Digits(s, at + 4, 2, om))
            return std::nullopt;
        if (oh > 23 || om > 59) return std::nullopt;
        offset = (oh * 3600 + om * 60) * (s[at] == '-' ? -1 : 1);
        at += 6;
    }
    if (at != s.size()) return std::nullopt;
    static const int days_in[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    if (mo < 1 || mo > 12 || d < 1 || d > (mo == 2 && leap ? 29 : days_in[mo - 1]) || h > 23 || mi > 59 || sec > 59)
        return std::nullopt;
    OWTimeKey k;
    k.seconds = DaysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - offset;
    std::string micro = (fraction + "000000").substr(0, 6);
    for (char c : micro) k.micros = k.micros * 10 + (c - '0');
    return k;
}

std::vector<const OWJsonValue*> InCreatedOrder(std::vector<const OWJsonValue*> items) {
    std::vector<std::pair<std::optional<OWTimeKey>, const OWJsonValue*>> keyed;
    for (const OWJsonValue* item : items) keyed.emplace_back(TimeKey(item ? Get(*item, "created_at") : nullptr), item);
    // Stable: equal times, and unreadable ones, keep the list's order.
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
        if (a.first && b.first) return *a.first < *b.first;
        return a.first.has_value() && !b.first.has_value();
    });
    std::vector<const OWJsonValue*> out;
    for (auto& [key, item] : keyed) out.push_back(item);
    return out;
}

}  // namespace OWMessage
}  // namespace OnlyWorlds
