#include "OWAddress.h"

#include "OWUnicode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>

namespace OnlyWorlds {
namespace OWAddress {

const char* const Human = "human";
const char* const Text = "text";
const char* const HumanPrefix = "https://www.onlyworlds.com/w/";
const char* const TextPrefix = "ow://";
const char* const LensKey = "lens";

const char* const Refusals[10] = {"not_an_address", "uppercase", "segments", "bad_world", "unknown_type",
                                  "bad_id",         "bad_moment", "bad_lens", "no_world",  "bad_form"};

const char* const TypeWords[22] = {"character", "creature",  "species",    "family",   "collective", "institution",
                                   "location",  "object",    "construct",  "ability",  "trait",      "title",
                                   "language",  "law",       "event",      "narrative", "phenomenon", "relation",
                                   "map",       "pin",       "marker",     "zone"};

namespace {

// Unicode's White_Space, as the room profile trims (not Python's str.isspace, which OWUnicode::StripWhite follows: that
// set also holds U+001C to U+001F). Each code point's UTF-8, so a trim matches bytes and decodes nothing.
const char* const WhiteSpace[] = {
    "\x09", "\x0A", "\x0B", "\x0C", "\x0D", "\x20",
    "\xC2\x85",                      // U+0085
    "\xC2\xA0",                      // U+00A0
    "\xE1\x9A\x80",                  // U+1680
    "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
    "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A",  // U+2000 to U+200A
    "\xE2\x80\xA8", "\xE2\x80\xA9",  // U+2028, U+2029
    "\xE2\x80\xAF",                  // U+202F
    "\xE2\x81\x9F",                  // U+205F
    "\xE3\x80\x80",                  // U+3000
};

std::string_view TrimWhiteSpace(std::string_view s) {
    bool more = true;
    while (more && !s.empty()) {
        more = false;
        for (const char* w : WhiteSpace) {
            std::string_view ws(w);
            if (s.size() >= ws.size() && s.compare(0, ws.size(), ws) == 0) {
                s.remove_prefix(ws.size());
                more = true;
                break;
            }
        }
    }
    more = true;
    while (more && !s.empty()) {
        more = false;
        for (const char* w : WhiteSpace) {
            std::string_view ws(w);
            if (s.size() >= ws.size() && s.compare(s.size() - ws.size(), ws.size(), ws) == 0) {
                s.remove_suffix(ws.size());
                more = true;
                break;
            }
        }
    }
    return s;
}

char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = LowerAscii(c);
    return out;
}

// Equal when ASCII letters are folded, and nothing else is: no locale, no Unicode case mapping.
bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
    return true;
}

bool StartsWith(std::string_view s, std::string_view head) { return s.substr(0, head.size()) == head; }

bool StartsWithIgnoringAsciiCase(std::string_view s, std::string_view head) {
    return s.size() >= head.size() && EqualsIgnoringAsciiCase(s.substr(0, head.size()), head);
}

std::vector<std::string_view> Split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    while (true) {
        size_t at = s.find(sep);
        out.push_back(s.substr(0, at));
        if (at == std::string_view::npos) return out;
        s.remove_prefix(at + 1);
    }
}

bool IsTypeWord(std::string_view word) {
    for (const char* t : TypeWords)
        if (word == t) return true;
    return false;
}

// nullptr for one of the 22 words; uppercase for one in another case; else unknown_type.
const char* TypeRefusal(std::string_view word) {
    if (IsTypeWord(word)) return nullptr;
    for (const char* t : TypeWords)
        if (EqualsIgnoringAsciiCase(word, t)) return "uppercase";
    return "unknown_type";
}

// A world's slug: 3 to 40 lowercase letters, numbers and single hyphens, no hyphen at either end, never a type word
// (the site reserves them).
bool IsSlug(std::string_view s) {
    if (s.size() < 3 || s.size() > 40 || s.front() == '-' || s.back() == '-') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '-') {
            if (s[i - 1] == '-') return false;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return !IsTypeWord(s);
}

// A step as written: decimal, an optional minus, no plus, no leading zero, no -0, within the 32-bit range.
bool ParseStep(std::string_view s, int32_t& value) {
    bool negative = !s.empty() && s[0] == '-';
    std::string_view digits = negative ? s.substr(1) : s;
    if (digits.empty() || digits.size() > 10) return false;
    for (char c : digits)
        if (c < '0' || c > '9') return false;
    if (digits[0] == '0' && (digits.size() > 1 || negative)) return false;
    int64_t n = 0;
    for (char c : digits) n = n * 10 + (c - '0');
    if (negative) n = -n;
    if (n < INT32_MIN || n > INT32_MAX) return false;
    value = static_cast<int32_t>(n);
    return true;
}

// Whether a JSON number's magnitude is 1 or more, read from its text alone (for a number a double cannot hold).
bool AtLeastOne(std::string_view text) {
    if (!text.empty() && text[0] == '-') text.remove_prefix(1);
    size_t e = text.find_first_of("eE");
    long long exponent = 0;
    if (e != std::string_view::npos) {
        std::string_view x = text.substr(e + 1);
        bool negative = !x.empty() && x[0] == '-';
        if (!x.empty() && (x[0] == '-' || x[0] == '+')) x.remove_prefix(1);
        for (char c : x) exponent = std::min(exponent * 10 + (c - '0'), 1000000000LL);
        if (negative) exponent = -exponent;
        text = text.substr(0, e);
    }
    size_t point = text.find('.');
    std::string_view whole = text.substr(0, point);
    std::string_view fraction = point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    // The decimal place of the first digit that is not 0 (0 for the ones), then moved by the exponent.
    long long place;
    if (size_t first = whole.find_first_not_of('0'); first != std::string_view::npos) {
        place = static_cast<long long>(whole.size() - first) - 1;
    } else if (size_t f = fraction.find_first_not_of('0'); f != std::string_view::npos) {
        place = -static_cast<long long>(f) - 1;
    } else {
        return false;  // zero
    }
    return place + exponent >= 0;
}

// A JSON moment as a step: an integer literal, or a number with a zero fraction (3.0, as JavaScript has one number
// type), within the 32-bit range. Anything else (a fraction, a string, a boolean) is not a step.
bool JsonStep(const OWJsonValue& v, int32_t& value) {
    if (!v.IsNumber()) return false;
    const std::string& text = v.Text();
    if (text.find_first_of(".eE") == std::string::npos) {
        // An integer literal: its digits, however many (one past 64 bits is past 32 too).
        bool negative = text[0] == '-';
        std::string_view digits = std::string_view(text).substr(negative ? 1 : 0);
        size_t first = digits.find_first_not_of('0');
        digits = first == std::string_view::npos ? std::string_view() : digits.substr(first);
        if (digits.size() > 10) return false;
        int64_t n = 0;
        for (char c : digits) n = n * 10 + (c - '0');
        if (negative) n = -n;
        if (n < INT32_MIN || n > INT32_MAX) return false;
        value = static_cast<int32_t>(n);
        return true;
    }
    double d = 0;
    std::from_chars_result fc = std::from_chars(text.data(), text.data() + text.size(), d);
    if (fc.ec == std::errc::result_out_of_range) {
        // Past a double: one too large is no step (JavaScript reads Infinity), one too small reads as 0, as it does.
        if (AtLeastOne(text)) return false;
        d = 0;
    }
    if (!std::isfinite(d) || std::floor(d) != d || d < INT32_MIN || d > INT32_MAX) return false;
    value = static_cast<int32_t>(d);
    return true;
}

// The refusal for these parts (nullptr with the address in `written`), in the writer's order: bad_form, segments,
// no_world, then the reader's. `moment_refusal` is a JSON moment that was not a step: it refuses at the moment's place.
const char* WriteParts(const OWAddressParts& p, std::string& written, const char* moment_refusal = nullptr) {
    bool human = p.form == Human;
    if (!human && p.form != Text) return "bad_form";
    if (p.type.has_value() != p.id.has_value()) return "segments";
    if (!p.world && (human || !p.type)) return "no_world";
    if (p.world && !IsUuid(*p.world)) return "bad_world";
    if (p.type) {
        if (const char* r = TypeRefusal(*p.type)) return r;
        if (!IsUuid(*p.id)) return "bad_id";
    }
    if (moment_refusal) return moment_refusal;
    if (p.lens && !IsUuid(*p.lens)) return "bad_lens";

    written = human ? HumanPrefix : TextPrefix;
    if (p.world) written += Lower(*p.world);
    if (p.type) {
        if (p.world) written += '/';
        written += *p.type + "/" + Lower(*p.id);
    }
    if (p.moment) written += "@" + std::to_string(*p.moment);
    if (p.lens) written += std::string("?") + LensKey + "=" + Lower(*p.lens);
    return nullptr;
}

OWAddressWritten Written(const char* refusal, std::string address) {
    OWAddressWritten w;
    w.ok = refusal == nullptr;
    if (refusal) {
        w.reason = refusal;
        w.message = std::string("not an address the profile writes: ") + refusal;
    } else {
        w.address = std::move(address);
    }
    return w;
}

OWAddressReading Refused(const char* reason) {
    OWAddressReading r;
    r.reason = reason;
    return r;
}

}  // namespace

bool IsUuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

OWAddressReading Parse(std::string_view text) {
    // 1. Trim White_Space at both ends.
    std::string_view s = TrimWhiteSpace(text);

    // 2. The prefix, as written; in another case it is uppercase.
    std::string form;
    if (StartsWith(s, HumanPrefix)) form = Human;
    else if (StartsWith(s, TextPrefix)) form = Text;
    else if (StartsWithIgnoringAsciiCase(s, HumanPrefix) || StartsWithIgnoringAsciiCase(s, TextPrefix))
        return Refused("uppercase");
    else
        return Refused("not_an_address");
    bool human = form == Human;
    std::string_view rest = s.substr(std::string_view(human ? HumanPrefix : TextPrefix).size());

    // 3. Drop a fragment, split off the query at the first ?, split the path on /, and the last segment at its first @
    // into the id and the moment.
    rest = rest.substr(0, rest.find('#'));
    std::optional<std::string_view> query;
    if (size_t q = rest.find('?'); q != std::string_view::npos) {
        query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    std::vector<std::string_view> segments = Split(rest, '/');
    std::optional<std::string_view> moment_text;
    std::string_view& last = segments.back();
    if (size_t at = last.find('@'); at != std::string_view::npos) {
        moment_text = last.substr(at + 1);
        last = last.substr(0, at);
    }

    // 4. Segments: the human form one or three, a text form one, two or three, none empty; a lone type word is a type
    // with no id.
    size_t count = segments.size();
    bool count_ok = human ? (count == 1 || count == 3) : (count >= 1 && count <= 3);
    bool any_empty = false;
    for (std::string_view seg : segments) any_empty = any_empty || seg.empty();
    if (!count_ok || any_empty || (count == 1 && IsTypeWord(segments[0]))) return Refused("segments");

    OWAddressReading r;
    r.form = form;
    std::optional<std::string_view> world, type, id;
    if (count == 1) {
        world = segments[0];
    } else if (count == 2) {
        type = segments[0];
        id = segments[1];
    } else {
        world = segments[0];
        type = segments[1];
        id = segments[2];
    }

    // 5. The world: an id, or in the human form only a slug.
    if (world) {
        if (IsUuid(*world)) r.world = Lower(*world);
        else if (human && IsSlug(*world)) {
            r.world = std::string(*world);
            r.world_is_slug = true;
        } else {
            return Refused("bad_world");
        }
    }

    // 6. The type word; 7. the element id.
    if (type) {
        if (const char* refusal = TypeRefusal(*type)) return Refused(refusal);
        if (!IsUuid(*id)) return Refused("bad_id");
        r.type = std::string(*type);
        r.id = Lower(*id);
    }

    // 8. The moment.
    if (moment_text) {
        int32_t step = 0;
        if (!ParseStep(*moment_text, step)) return Refused("bad_moment");
        r.moment = step;
    }

    // 9. The lens; any other query key is ignored (and the address is then not canonical).
    if (query) {
        int lenses = 0;
        std::string_view lens;
        for (std::string_view pair : Split(*query, '&')) {
            size_t eq = pair.find('=');
            std::string_view key = pair.substr(0, eq);
            std::string_view value = eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1);
            if (key == LensKey) {
                ++lenses;
                lens = value;
            } else if (EqualsIgnoringAsciiCase(key, LensKey)) {
                return Refused("bad_lens");
            }
        }
        if (lenses > 1 || (lenses == 1 && !IsUuid(lens))) return Refused("bad_lens");
        if (lenses == 1) r.lens = Lower(lens);
    }

    r.ok = true;
    // Canonical: the string as received (untrimmed) is what a writer writes for this reading; a slug never is.
    std::string written;
    r.canonical = !r.world_is_slug && WriteParts({r.form, r.world, r.type, r.id, r.lens, r.moment}, written) == nullptr &&
                  written == text;
    return r;
}

namespace {

// An id is 36 characters: a lens found in prose ends where its id ends.
constexpr size_t IdLength = 36;

size_t TypeWordMax() {
    size_t most = 0;
    for (const char* t : TypeWords) most = std::max(most, std::string_view(t).size());
    return most;
}

// The characters a path segment can hold (an id, a slug, a type word in any case) and the slash between them.
bool IsPathChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '/' || c == '-';
}

// The run after /w/: the slug's alphabet (lowercase letters, digits, hyphens) and the id's (hex in either case).
bool IsWorldRunChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'F') || c == '-';
}

// The code point whose UTF-8 starts at byte i, decoded whole (U+FFFD for a byte that begins no sequence).
char32_t CodePointAt(std::string_view s, size_t i) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    const size_t length = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    const std::u32string cps = OWUnicode::Decode(s.substr(i, length));
    return cps.size() == 1 ? cps[0] : char32_t(0xFFFD);
}

// Whether the character at byte i glues a run into a word: a letter of any script, a combining mark (Unicode categories
// L and M) or an underscore.
bool IsInAWord(std::string_view s, size_t i) {
    if (i >= s.size()) return false;
    if (s[i] == '_') return true;
    return OWUnicode::IsLetterOrMark(CodePointAt(s, i));
}

bool Parses(std::string_view text, size_t index, size_t end) { return Parse(text.substr(index, end - index)).ok; }

}  // namespace

std::optional<OWAddressFound> ReadAt(std::string_view text, size_t index) {
    const size_t n = text.size();
    if (index > n) return std::nullopt;
    const std::string_view at = text.substr(index);
    const bool human = StartsWith(at, HumanPrefix);
    size_t start;
    if (human) start = index + std::string_view(HumanPrefix).size();
    else if (StartsWith(at, TextPrefix)) start = index + std::string_view(TextPrefix).size();
    else return std::nullopt;

    static const size_t type_word_max = TypeWordMax();
    size_t end = std::string_view::npos, low = start, run = start;
    if (human) {
        // The site's form: the run after /w/ is read whole, once, with no order between an id and a slug.
        size_t w = start;
        while (w < n && IsWorldRunChar(text[w])) ++w;
        if (IsInAWord(text, w)) return std::nullopt;
        const std::string_view world = text.substr(start, w - start);
        if (!IsUuid(world) && !IsSlug(world)) return std::nullopt;
        end = low = run = w;
        const size_t element_path_max = 1 + type_word_max + 1 + IdLength;
        while (run < n && run - w <= element_path_max && IsPathChar(text[run])) ++run;
    } else {
        const size_t path_max = IdLength + 1 + type_word_max + 1 + IdLength;
        while (run < n && run - start <= path_max && IsPathChar(text[run])) ++run;
    }

    for (size_t p = run; p > low; --p) {
        if (Parses(text, index, p)) {
            end = p;
            break;
        }
    }
    if (end == std::string_view::npos) return std::nullopt;

    // A moment: its whole run of an optional minus and digits, taken only when it reads as a step.
    if (end < n && text[end] == '@') {
        size_t step_end = end + 1;
        if (step_end < n && text[step_end] == '-') ++step_end;
        const size_t digits = step_end;
        while (step_end < n && text[step_end] >= '0' && text[step_end] <= '9') ++step_end;
        if (step_end > digits && Parses(text, index, step_end)) end = step_end;
    }

    // A lens: an id, which ends where its 36 characters end.
    const std::string lens_query = std::string("?") + LensKey + "=";
    if (StartsWith(text.substr(end), lens_query)) {
        const size_t lens_end = end + lens_query.size() + IdLength;
        if (lens_end <= n && IsUuid(text.substr(end + lens_query.size(), IdLength)) && Parses(text, index, lens_end))
            end = lens_end;
    }

    return OWAddressFound{Parse(text.substr(index, end - index)), end};
}

OWAddressWritten Write(const OWAddressParts& parts) {
    std::string written;
    const char* refusal = WriteParts(parts, written);
    return Written(refusal, std::move(written));
}

OWAddressWritten Write(const OWAddressReading& reading) {
    return Write(OWAddressParts{reading.ok ? reading.form : std::string(), reading.world, reading.type, reading.id,
                                reading.lens, reading.moment});
}

OWAddressWritten WriteJson(const OWJsonValue& address) {
    OWAddressWritten w;
    if (!address.IsObject()) {
        w.message = "an address as JSON is an object";
        return w;
    }
    static const char* const keys[] = {"form", "world", "type", "id", "lens", "moment", "world_is_slug", "canonical"};
    for (const std::string& key : address.Keys()) {
        bool known = false;
        for (const char* k : keys) known = known || key == k;
        if (!known) {
            w.message = "not a key of an address: '" + key +
                        "'. The keys are form, world, type, id, lens, moment, world_is_slug, canonical.";
            return w;
        }
    }

    // A part: absent for a missing key or JSON null; a string's text; anything else present, as its JSON text (which is
    // never a form, a world, a type word or an id, so it refuses as that part would).
    auto part = [&](const char* key) -> std::optional<std::string> {
        const OWJsonValue* v = address.Find(key);
        if (!v || v->IsNull()) return std::nullopt;
        if (v->IsString()) return v->Text();
        return OWJson::Stringify(*v);
    };
    OWAddressParts p;
    p.form = part("form").value_or(std::string());
    p.world = part("world");
    p.type = part("type");
    p.id = part("id");
    p.lens = part("lens");

    const char* moment_refusal = nullptr;
    if (const OWJsonValue* m = address.Find("moment"); m && !m->IsNull()) {
        int32_t step = 0;
        if (JsonStep(*m, step)) p.moment = step;
        else moment_refusal = "bad_moment";
    }

    std::string written;
    const char* refusal = WriteParts(p, written, moment_refusal);
    return Written(refusal, std::move(written));
}

}  // namespace OWAddress
}  // namespace OnlyWorlds
