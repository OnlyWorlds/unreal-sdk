#include "OWApiError.h"

#include "OWJson.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace OnlyWorlds {

namespace {

constexpr int32_t Largest = std::numeric_limits<int32_t>::max();

// A member's text as Newtonsoft's ToString gives it for the envelope's scalars: a string's, a number's source, a
// boolean's word; "" for null, an object, an array or an absent member.
std::string TextOf(const OWJsonValue* v) {
    if (!v) return "";
    if (v->IsString() || v->IsNumber()) return v->Text();
    if (v->IsBool()) return v->AsBool() ? "true" : "false";
    return "";
}

// The first 200 characters (code points, so a cut never splits a UTF-8 sequence), then "..." when there was more.
std::string Truncate(const std::string& s) {
    size_t i = 0, chars = 0;
    while (i < s.size() && chars < 200) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        ++chars;
    }
    if (i >= s.size()) return s;
    return s.substr(0, i) + "...";
}

bool Digits(std::string_view s, size_t from, size_t n, int& out) {
    if (from + n > s.size()) return false;
    out = 0;
    for (size_t i = from; i < from + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + (s[i] - '0');
    }
    return true;
}

// Days from 1970-01-01 to a civil date (Howard Hinnant's days_from_civil).
int64_t DaysFromCivil(int64_t y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// An IMF-fixdate (RFC 9110 5.6.7: "Sun, 06 Nov 1994 08:49:37 GMT") as Unix seconds, or absent.
std::optional<int64_t> HttpDate(std::string_view v) {
    static const char* const DayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char* const Months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (v.size() != 29 || v.substr(3, 2) != ", " || v[7] != ' ' || v[11] != ' ' || v[16] != ' ' || v[19] != ':' ||
        v[22] != ':' || v.substr(25) != " GMT")
        return std::nullopt;
    bool named = false;
    for (const char* d : DayNames) named = named || v.substr(0, 3) == d;
    int month = 0;
    for (int i = 0; i < 12; ++i)
        if (v.substr(8, 3) == Months[i]) month = i + 1;
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (!named || month == 0 || !Digits(v, 5, 2, day) || !Digits(v, 12, 4, year) || !Digits(v, 17, 2, hour) ||
        !Digits(v, 20, 2, minute) || !Digits(v, 23, 2, second))
        return std::nullopt;
    static const int Lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int last = Lengths[month - 1] + (month == 2 && leap ? 1 : 0);
    if (day < 1 || day > last || hour > 23 || minute > 59 || second > 60) return std::nullopt;
    return DaysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

// The strings of a JSON list in its order, anything that is not a string left out; a value that is no list reads empty.
std::vector<std::string> Strings(const OWJsonValue* list) {
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& v : list->Items())
            if (v.IsString()) out.push_back(v.Text());
    return out;
}

bool AsciiWhite(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

}  // namespace

OWApiError OWApiError::FromResponse(const OWResponse& response, int64_t now) {
    OWApiError e;
    e.kind = Kind::Api;
    e.status = response.status;
    e.raw_body = response.body;
    if (const std::string* after = response.Header("Retry-After")) e.retry_after = ParseRetryAfter(*after, now);

    // A non-2xx is not sure to carry the envelope: a proxy or a gateway can answer HTML. Read it defensively, so a 502
    // is a clean error with the page's start as its message.
    OWJsonParseResult parsed = OWJson::Parse(response.body);
    if (!parsed.ok || !parsed.value.IsObject()) {
        e.message = Truncate(response.body);
        return e;
    }
    const OWJsonValue& envelope = parsed.value;
    const OWJsonValue* inner = envelope.Find("error");
    const OWJsonValue& error = inner && inner->IsObject() ? *inner : envelope;
    // The upload host refuses with {"error": "<code>"}, not keel's envelope: the string is the code.
    if (const OWJsonValue* c = error.Find("code")) e.code = TextOf(c);
    else if (inner && inner->IsString()) e.code = inner->Text();
    e.type = TextOf(error.Find("type"));
    e.message = TextOf(error.Find("message"));
    e.param = TextOf(error.Find("param"));
    e.doc_url = TextOf(error.Find("doc_url"));
    return e;
}

OWApiError OWApiError::Of(Kind kind, std::string message, int status, std::string raw_body) {
    OWApiError e;
    e.kind = kind;
    e.message = std::move(message);
    e.status = status;
    e.raw_body = std::move(raw_body);
    return e;
}

std::optional<int32_t> OWApiError::ParseRetryAfter(std::string_view value, int64_t now) {
    while (!value.empty() && AsciiWhite(value.front())) value.remove_prefix(1);
    while (!value.empty() && AsciiWhite(value.back())) value.remove_suffix(1);
    if (value.empty()) return std::nullopt;
    bool digits = true, letter = false;
    for (char c : value) {
        if (c < '0' || c > '9') digits = false;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letter = true;
    }
    if (digits) {
        int64_t n = 0;
        for (char c : value) {
            n = n * 10 + (c - '0');
            if (n > Largest) return Largest;
        }
        return static_cast<int32_t>(n);
    }
    // Only a value with a letter in it is read as a date, so "1.5", "-1" or "+5" is none, never "retry now".
    if (!letter) return std::nullopt;
    std::optional<int64_t> when = HttpDate(value);
    if (!when) return std::nullopt;
    int64_t seconds = *when - now;
    return seconds <= 0 ? 0 : seconds >= Largest ? Largest : static_cast<int32_t>(seconds);
}

std::string OWApiError::ToString() const {
    std::string text;
    switch (kind) {
        case Kind::Api: text = "OnlyWorlds API " + std::to_string(status); break;
        case Kind::Transport: text = "OnlyWorlds transport"; break;
        case Kind::Refused: text = "OnlyWorlds refused"; break;
        case Kind::Answer: text = "OnlyWorlds answer"; break;
    }
    if (!code.empty()) text += " [" + code + "]";
    if (!param.empty()) text += " (field: " + param + ")";
    if (!message.empty()) text += ": " + message;
    return text;
}

bool OWApiError::IsValidationError() const { return status == 422 || status == 400; }
bool OWApiError::IsAuthError() const {
    return code == "invalid_credentials" || code == "key_revoked" || code == "world_gone";
}
bool OWApiError::IsNotFound() const { return status == 404 && code == "not_found"; }
bool OWApiError::IsOwnerOnly() const { return status == 403 && code == "owner_only"; }
bool OWApiError::IsViewAsOwnerOnly() const { return status == 403 && code == "view_as_owner_only"; }
bool OWApiError::IsResyncRequired() const { return status == 409 && code == "resync_required"; }

std::optional<std::string> OWApiError::ResyncReason() const {
    if (!IsResyncRequired() || raw_body.empty()) return std::nullopt;
    OWJsonParseResult parsed = OWJson::Parse(raw_body);
    const OWJsonValue* inner = parsed.ok ? parsed.value.Find("error") : nullptr;
    const OWJsonValue* reason = inner && inner->IsObject() ? inner->Find("reason") : nullptr;
    if (!reason || !reason->IsString()) return std::nullopt;
    if (reason->Text() == "role" || reason->Text() == "view") return reason->Text();
    return std::nullopt;
}

bool OWApiError::IsRetryable() const {
    // A same-key request still running, an upload or a removal not yet done: they clear by themselves.
    if (code == "idempotency_in_flight" || code == "upload_pending" || code == "removal_in_flight") return status == 409;
    // A daily allowance used up (until midnight UTC), a feature switched off.
    if (code == "quota_exceeded" || code == "ask_unavailable" || code == "media_unavailable") return false;
    return status == 429 || status >= 500;
}

bool OWApiError::IsProposed() const { return status == 202 && proposal.has_value(); }
bool OWApiError::IsPreconditionFailed() const { return status == 412 && code == "precondition_failed"; }
bool OWApiError::IsIdConflict() const { return status == 409 && code == "id_conflict"; }
bool OWApiError::IsIdempotencyConflict() const { return status == 409 && code == "idempotency_error"; }
bool OWApiError::IsKnownOfNotWritable() const { return status == 422 && code == "known_of_not_writable"; }

std::optional<OWJsonValue> OWApiError::Current() const {
    // The Unity SDK's Current: on any 412, and on a 409 stale_proposal.
    bool stale = status == 409 && code == "stale_proposal";
    if ((status != 412 && !stale) || raw_body.empty()) return std::nullopt;
    OWJsonParseResult parsed = OWJson::Parse(raw_body);
    const OWJsonValue* now = parsed.ok ? parsed.value.Find("current") : nullptr;
    if (!now || !now->IsObject()) return std::nullopt;
    return *now;
}

std::optional<OWFieldsRefusal> OWApiError::FieldsRefusal() const {
    if (status != 422 || param != "fields" || raw_body.empty()) return std::nullopt;
    OWJsonParseResult parsed = OWJson::Parse(raw_body);
    // Inside the envelope's `error` object only, as npm reads them: a body without the wrapper carries none.
    const OWJsonValue* inner = parsed.ok && parsed.value.IsObject() ? parsed.value.Find("error") : nullptr;
    if (!inner || !inner->IsObject()) return std::nullopt;
    const OWJsonValue* unknown = inner->Find("unknown_fields");
    if (!unknown || !unknown->IsArray()) return std::nullopt;
    OWFieldsRefusal r;
    r.unknown_fields = Strings(unknown);
    if (const OWJsonValue* held = inner->Find("types_with_field"); held && held->IsObject())
        for (size_t i = 0; i < held->Keys().size(); ++i)
            if (held->Items()[i].IsArray()) r.types_with_field.emplace_back(held->Keys()[i], Strings(&held->Items()[i]));
    r.allowed_fields = Strings(inner->Find("allowed_fields"));
    return r;
}

const std::vector<std::string>* OWFieldsRefusal::TypesWith(std::string_view name) const {
    for (const auto& [field, types] : types_with_field)
        if (field == name) return &types;
    return nullptr;
}

OWJsonValue OWFieldsRefusal::ToJson() const {
    auto list = [](const std::vector<std::string>& items) {
        OWJsonValue a = OWJsonValue::Array();
        for (const std::string& s : items) a.Push(OWJsonValue::String(s));
        return a;
    };
    OWJsonValue answer = OWJsonValue::Object();
    answer.Set("unknown_fields", list(unknown_fields));
    OWJsonValue by_name = OWJsonValue::Object();
    for (const auto& [field, types] : types_with_field) by_name.Set(field, list(types));
    answer.Set("types_with_field", std::move(by_name));
    answer.Set("allowed_fields", list(allowed_fields));
    return answer;
}

}  // namespace OnlyWorlds
