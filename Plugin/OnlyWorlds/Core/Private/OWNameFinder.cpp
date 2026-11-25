#include "OWNameFinder.h"

#include "OWAddress.h"
#include "OWMessage.h"
#include "OWUnicode.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace OnlyWorlds {

namespace {

// A name shorter than this many code points, after its white space is trimmed, is never looked for.
const size_t NameMin = 3;
// The mark the matcher reads between two characters wherever a name may end: before a character that is not a word
// character, and at the end of the text. No code point is this large.
const uint32_t EndMark = 0xFFFFFFFFu;
const int32_t None = -1;

// One name, as compared, and every element that holds it.
struct Hit {
    size_t length = 0;  // in code points
    std::vector<size_t> elements;
    std::string first_id;
    // The ids of `elements`, kept once a second element holds the name.
    std::unordered_set<std::string> ids;
    bool many = false;
    std::string name;
};

// A state of the matcher. The names are held backwards, each from its last character to its first, with the end mark
// before every character that is not a word character and once at the name's end: so a text read from its end towards
// its start, with the same mark laid wherever a name may end, passes each character once (Aho and Corasick's failure
// links), and the state after a character says which whole names start there.
struct State {
    uint32_t token = 0;      // the token that leads here from the parent
    int32_t fail = None;
    int32_t hit = None;      // the name that is this state, read backwards
    int32_t longest = None;  // the longest name that ends, read backwards, at this state: `hit`, or the fail state's
    int32_t first_child = None;
    int32_t next_sibling = None;
};

bool IsWordPoint(char32_t cp) {
    if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_';
    return OWUnicode::IsLetterOrMark(cp) || OWUnicode::IsDecimalDigit(cp);
}

// The text as code points, each with the byte it starts at, and the text's size after the last. A lead byte with all
// of its continuation bytes (10xxxxxx) is one code point, as OWUnicode::Decode reads it (so WTF-8's lone surrogates
// too); any other byte is one U+FFFD, one byte wide, so every byte an ASCII character holds starts a code point.
void Decode(std::string_view s, std::vector<char32_t>& cps, std::vector<size_t>& at) {
    cps.clear();
    at.clear();
    cps.reserve(s.size());
    at.reserve(s.size() + 1);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        at.push_back(i);
        if (c < 0x80) {
            cps.push_back(c);
            ++i;
            continue;
        }
        const size_t need = c >= 0xF8 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        bool whole = need > 0 && i + need < n;
        for (size_t k = 1; whole && k <= need; ++k) whole = (static_cast<uint8_t>(s[i + k]) & 0xC0) == 0x80;
        if (!whole) {
            cps.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t cp = c & (0x3F >> need);
        for (size_t k = 1; k <= need; ++k) cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        cps.push_back(cp);
        i += need + 1;
    }
    at.push_back(n);
}

// Every Markdown link `[label](target)` in a text as [start, end) in bytes: a `[`, a label up to the first `]`, a `(`
// right after it, a target up to the first `)`. One pass: each bracket and parenthesis is searched for once.
void MarkdownLinks(std::string_view text, std::vector<std::pair<size_t, size_t>>& spans) {
    const size_t npos = std::string_view::npos;
    bool haveClose = false, haveParen = false;
    size_t close = 0, paren = 0;
    size_t open = text.find('[');
    while (open != npos) {
        if (!haveClose || close <= open) {
            close = text.find(']', open + 1);
            haveClose = true;
            if (close == npos) break;
        }
        if (close + 1 >= text.size() || text[close + 1] != '(') {
            open = text.find('[', open + 1);
            continue;
        }
        if (!haveParen || paren < close + 2) {
            paren = text.find(')', close + 2);
            haveParen = true;
            if (paren == npos) break;
        }
        spans.emplace_back(open, paren + 1);
        open = text.find('[', paren + 1);
    }
}

// The new scanners (the TypeScript SDK 4.208.0) read code points, since a bare web address ends at any of Unicode's
// White_Space; their spans are code point places. Every character they test is in the BMP, so a place here and a
// UTF-16 unit there answer alike.
using Points = std::vector<char32_t>;
using Spans = std::vector<std::pair<size_t, size_t>>;

int64_t PointAt(const Points& t, int64_t i) { return i >= 0 && i < static_cast<int64_t>(t.size()) ? static_cast<int64_t>(t[static_cast<size_t>(i)]) : -1; }

int64_t FindFrom(const Points& t, char32_t c, int64_t from) {
    for (int64_t i = from < 0 ? 0 : from; i < static_cast<int64_t>(t.size()); ++i)
        if (t[static_cast<size_t>(i)] == c) return i;
    return -1;
}

int64_t FindFrom(const Points& t, const char* s, int64_t from) {
    const int64_t n = static_cast<int64_t>(t.size());
    const int64_t len = static_cast<int64_t>(std::char_traits<char>::length(s));
    for (int64_t i = from < 0 ? 0 : from; i + len <= n; ++i) {
        int64_t k = 0;
        while (k < len && t[static_cast<size_t>(i + k)] == static_cast<char32_t>(s[k])) ++k;
        if (k == len) return i;
    }
    return -1;
}

// Unicode's White_Space code points: the end of a bare web address and of an autolink.
bool IsWhiteSpace(int64_t c) {
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
        || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

bool IsAsciiLetter(int64_t c) { return (c >= 0x41 && c <= 0x5a) || (c >= 0x61 && c <= 0x7a); }

bool IsSchemeChar(int64_t c) { return IsAsciiLetter(c) || (c >= 0x30 && c <= 0x39) || c == '+' || c == '.' || c == '-'; }

// True when `word` is spelled at `at`, its letters in ASCII case alone.
bool SpelledAt(const Points& t, int64_t at, const char* word) {
    for (int64_t k = 0; word[k] != 0; ++k)
        if ((PointAt(t, at + k) | 0x20) != word[k]) return false;
    return true;
}

// Every bare web address and every autolink, in any text. A bare address starts at `http://` or `https://` (the
// scheme in any ASCII case) and runs to just before the first white space or `<`. An autolink is `<`, a scheme (an
// ASCII letter, then 1 to 31 of letters, digits, `+`, `.` or `-`), `:`, then anything but white space, a control
// character, `<` or `>`, up to the closing `>`.
void WebAddresses(const Points& t, Spans& spans) {
    const int64_t n = static_cast<int64_t>(t.size());
    int64_t floorAt = 0;
    int64_t p = FindFrom(t, "://", 0);
    while (p != -1) {
        int64_t at = -1;
        if (p - 5 >= floorAt && SpelledAt(t, p - 5, "https")) at = p - 5;
        else if (p - 4 >= floorAt && SpelledAt(t, p - 4, "http")) at = p - 4;
        if (at == -1) { p = FindFrom(t, "://", p + 1); continue; }
        int64_t end = p + 3;
        while (end < n && !IsWhiteSpace(t[static_cast<size_t>(end)]) && t[static_cast<size_t>(end)] != '<') ++end;
        spans.emplace_back(static_cast<size_t>(at), static_cast<size_t>(end));
        floorAt = end;
        p = FindFrom(t, "://", end);
    }
    int64_t open = FindFrom(t, U'<', 0);
    while (open != -1) {
        int64_t i = open + 1;
        if (IsAsciiLetter(PointAt(t, i))) {
            int64_t k = 1;
            ++i;
            while (i < n && k < 32 && IsSchemeChar(t[static_cast<size_t>(i)])) { ++i; ++k; }
            if (k >= 2 && PointAt(t, i) == ':') {
                ++i;
                int64_t c = PointAt(t, i);
                while (i < n && c != '>' && c != '<' && c >= 0x20 && c != 0x7f && !IsWhiteSpace(c)) c = PointAt(t, ++i);
                if (i < n && c == '>') {
                    spans.emplace_back(static_cast<size_t>(open), static_cast<size_t>(i + 1));
                    open = FindFrom(t, U'<', i + 1);
                    continue;
                }
            }
        }
        open = FindFrom(t, U'<', open + 1);
    }
}

struct Fence { int64_t ch = 0, length = 0, after = 0; };

// A line's fence, after at most three spaces: a run of three or more backticks or tildes.
bool FenceOf(const Points& t, int64_t start, int64_t end, Fence& f) {
    int64_t i = start;
    while (i < end && i - start < 3 && t[static_cast<size_t>(i)] == ' ') ++i;
    const int64_t ch = PointAt(t, i);
    if (ch != '`' && ch != '~') return false;
    int64_t j = i;
    while (j < end && static_cast<int64_t>(t[static_cast<size_t>(j)]) == ch) ++j;
    if (j - i < 3) return false;
    f = {ch, j - i, j};
    return true;
}

// The Markdown stretches no name is read in (Find's `markdown`). Fenced code blocks first: a line whose fence opens
// one (a backtick fence holds no backtick in the rest of its line) runs to a later line holding only a fence of the
// same character, at least as long, and spaces or tabs after it, or to the end of the text. Outside them: an inline
// code span (a run of backticks to the next run of exactly as many; a run with none is prose), an HTML comment (`<!--`
// to the first `-->`), an HTML tag (`<`, an optional `/`, an ASCII letter, to the first `>`), and a reference
// definition's line (after at most three spaces, `[`, a label, and the line's first `]` followed by `:`).
void MarkdownStretches(const Points& t, Spans& spans) {
    const int64_t n = static_cast<int64_t>(t.size());
    std::vector<std::pair<int64_t, int64_t>> lines;
    for (int64_t start = 0;;) {
        const int64_t nl = FindFrom(t, U'\n', start);
        if (nl == -1) { lines.emplace_back(start, n); break; }
        lines.emplace_back(start, nl);
        start = nl + 1;
    }
    std::vector<std::pair<int64_t, int64_t>> outside;
    int64_t segStart = 0;
    int64_t bracket = -2;  // the next ] at or after the place last asked about, searched for once per place passed
    const int64_t lineCount = static_cast<int64_t>(lines.size());
    for (int64_t k = 0; k < lineCount; ++k) {
        const int64_t ls = lines[static_cast<size_t>(k)].first, le = lines[static_cast<size_t>(k)].second;
        Fence open;
        const bool fence = FenceOf(t, ls, le, open);
        const int64_t tick = fence && open.ch == '`' ? FindFrom(t, U'`', open.after) : -1;
        if (!fence || (tick != -1 && tick < le)) {
            int64_t i = ls;
            while (i < le && i - ls < 3 && t[static_cast<size_t>(i)] == ' ') ++i;
            if (PointAt(t, i) == '[') {
                if (bracket != -1 && bracket < i + 1) bracket = FindFrom(t, U']', i + 1);
                const int64_t close = bracket;
                if (close != -1 && close < le && close > i + 1 && PointAt(t, close + 1) == ':')
                    spans.emplace_back(static_cast<size_t>(ls), static_cast<size_t>(le));
            }
            continue;
        }
        int64_t m = k + 1;
        for (; m < lineCount; ++m) {
            const int64_t cs = lines[static_cast<size_t>(m)].first, ce = lines[static_cast<size_t>(m)].second;
            Fence shut;
            if (!FenceOf(t, cs, ce, shut) || shut.ch != open.ch || shut.length < open.length) continue;
            int64_t j = shut.after;
            while (j < ce && (t[static_cast<size_t>(j)] == ' ' || t[static_cast<size_t>(j)] == '\t' || t[static_cast<size_t>(j)] == '\r')) ++j;
            if (j == ce) break;
        }
        const int64_t blockEnd = m < lineCount ? lines[static_cast<size_t>(m)].second : n;
        if (ls > segStart) outside.emplace_back(segStart, ls);
        spans.emplace_back(static_cast<size_t>(ls), static_cast<size_t>(blockEnd));
        segStart = blockEnd;
        k = m;
    }
    if (segStart < n) outside.emplace_back(segStart, n);
    for (const auto& [s, e] : outside) {
        // Inline code spans: every maximal backtick run, then each opening run to the next run of its length.
        std::vector<std::pair<int64_t, int64_t>> runs;  // (at, length)
        int64_t r0 = FindFrom(t, U'`', s);
        while (r0 != -1 && r0 < e) {
            int64_t j = r0;
            while (j < e && t[static_cast<size_t>(j)] == '`') ++j;
            runs.emplace_back(r0, j - r0);
            r0 = FindFrom(t, U'`', j);
        }
        std::unordered_map<int64_t, std::vector<size_t>> byLength;
        for (size_t idx = 0; idx < runs.size(); ++idx) byLength[runs[idx].second].push_back(idx);
        std::unordered_map<int64_t, size_t> cursor;
        size_t r = 0;
        while (r < runs.size()) {
            const int64_t at = runs[r].first, len = runs[r].second;
            const std::vector<size_t>& list = byLength[len];
            size_t& c = cursor[len];
            while (c < list.size() && list[c] <= r) ++c;
            if (c < list.size()) {
                spans.emplace_back(static_cast<size_t>(at), static_cast<size_t>(runs[list[c]].first + len));
                r = list[c] + 1;
            } else {
                ++r;
            }
        }
        // HTML comments and tags.
        int64_t gt = -2, commentEnd = -2;
        int64_t lt = FindFrom(t, U'<', s);
        while (lt != -1 && lt < e) {
            if (PointAt(t, lt + 1) == '!' && PointAt(t, lt + 2) == '-' && PointAt(t, lt + 3) == '-') {
                if (commentEnd != -1 && commentEnd < lt + 4) commentEnd = FindFrom(t, "-->", lt + 4);
                if (commentEnd != -1 && commentEnd + 3 <= e) {
                    spans.emplace_back(static_cast<size_t>(lt), static_cast<size_t>(commentEnd + 3));
                    lt = FindFrom(t, U'<', commentEnd + 3);
                    continue;
                }
            } else {
                const int64_t name = PointAt(t, lt + 1) == '/' ? lt + 2 : lt + 1;
                if (IsAsciiLetter(PointAt(t, name))) {
                    if (gt != -1 && gt < name) gt = FindFrom(t, U'>', name);
                    if (gt != -1 && gt < e) {
                        spans.emplace_back(static_cast<size_t>(lt), static_cast<size_t>(gt + 1));
                        lt = FindFrom(t, U'<', gt + 1);
                        continue;
                    }
                }
            }
            lt = FindFrom(t, U'<', lt + 1);
        }
    }
}

// The stretches no name is read in, as code point places, in order, none overlapping: every address and every
// Markdown link (found in bytes, each byte they start or end at starting a code point), every bare web address and
// autolink, and with `markdown` the Markdown stretches too.
Spans PassedOver(std::string_view text, const Points& cps, const std::vector<size_t>& at, bool markdown) {
    Spans spans;
    MarkdownLinks(text, spans);
    for (const OWAddressFound& a : OWAddress::FindAll(text)) spans.emplace_back(a.start, a.end);
    auto place = [&](size_t byte) { return static_cast<size_t>(std::lower_bound(at.begin(), at.end(), byte) - at.begin()); };
    for (auto& s : spans) s = {place(s.first), place(s.second)};
    WebAddresses(cps, spans);
    if (markdown) MarkdownStretches(cps, spans);
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<size_t, size_t>> out;
    for (const auto& s : spans) {
        if (!out.empty() && s.first < out.back().second) {
            if (s.second > out.back().second) out.back().second = s.second;
        } else {
            out.push_back(s);
        }
    }
    return out;
}

uint64_t Key(int32_t state, uint32_t token) { return (static_cast<uint64_t>(static_cast<uint32_t>(state)) << 32) | token; }

}  // namespace

struct OWNameFinder::Impl {
    bool folded = false;
    std::vector<State> states;  // states[0] is the root
    std::unordered_map<uint64_t, int32_t> next;
    std::vector<Hit> hits;
    std::vector<OWNameShared> shared;
    std::string error;

    int32_t Child(int32_t from, uint32_t token) const {
        auto it = next.find(Key(from, token));
        return it == next.end() ? None : it->second;
    }

    int32_t Step(int32_t from, uint32_t token) const {
        for (int32_t s = from; s != None; s = states[static_cast<size_t>(s)].fail) {
            const int32_t to = Child(s, token);
            if (to != None) return to;
        }
        return 0;
    }
};

OWNameFinder::OWNameFinder(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

bool OWNameFinder::IsWord(char32_t cp) { return IsWordPoint(cp); }

OWNameFinder OWNameFinder::Of(const OWJsonValue& elements, OWNameCase name_case) {
    return Of(elements, name_case, [](const OWJsonValue& element) {
        const OWJsonValue* name = element.Find("name");
        return std::vector<OWJsonValue>{name ? *name : OWJsonValue()};
    });
}

OWNameFinder OWNameFinder::Of(const OWJsonValue& elements, OWNameCase name_case, const NamesOf& names) {
    auto refused = [](std::string why) {
        auto impl = std::make_shared<Impl>();
        impl->states.emplace_back();
        impl->error = "OWNameFinder: " + std::move(why);
        return OWNameFinder(std::move(impl));
    };
    if (name_case != OWNameCase::Exact && name_case != OWNameCase::Folded)
        return refused("the case is OWNameCase::Exact or OWNameCase::Folded, and must be given");
    if (!names) return refused("names is a function from an element to its names");
    if (!elements.IsArray()) return refused("elements is a list of objects with an id and a name");

    auto impl = std::make_shared<Impl>();
    Impl& m = *impl;
    m.folded = name_case == OWNameCase::Folded;
    m.states.emplace_back();
    std::vector<char32_t> points;
    std::vector<size_t> at;
    std::vector<uint32_t> tokens;
    for (size_t index = 0; index < elements.Size(); ++index) {
        const OWJsonValue& element = elements.Items()[index];
        const OWJsonValue* idValue = element.IsObject() ? element.Find("id") : nullptr;
        if (!idValue || !idValue->IsString() || idValue->Text().empty())
            return refused("every element is an object with a string id (element " + std::to_string(index) + ")");
        const std::string& id = idValue->Text();
        for (const OWJsonValue& raw : names(element)) {
            if (!raw.IsString()) continue;
            Decode(OWMessage::Trim(raw.Text()), points, at);
            if (points.size() < NameMin) continue;
            if (m.folded)
                for (char32_t& c : points) c = OWUnicode::SimpleFold(c);
            // The name backwards: the end mark, then each character from the last to the first, and the mark again
            // after (so, in the text, before) every character but the first that is not a word character.
            tokens.clear();
            tokens.push_back(EndMark);
            for (size_t k = points.size(); k-- > 0;) {
                tokens.push_back(static_cast<uint32_t>(points[k]));
                if (k > 0 && !IsWordPoint(points[k])) tokens.push_back(EndMark);
            }
            int32_t state = 0;
            for (uint32_t token : tokens) {
                int32_t child = m.Child(state, token);
                if (child == None) {
                    child = static_cast<int32_t>(m.states.size());
                    State s;
                    s.token = token;
                    s.next_sibling = m.states[static_cast<size_t>(state)].first_child;
                    m.states.push_back(s);
                    m.states[static_cast<size_t>(state)].first_child = child;
                    m.next.emplace(Key(state, token), child);
                }
                state = child;
            }
            State& end = m.states[static_cast<size_t>(state)];
            if (end.hit == None) {
                Hit hit;
                hit.length = points.size();
                hit.elements.push_back(index);
                hit.first_id = id;
                hit.name = OWUnicode::Encode(std::u32string_view(points.data(), points.size()));
                end.hit = static_cast<int32_t>(m.hits.size());
                m.hits.push_back(std::move(hit));
            } else {
                Hit& hit = m.hits[static_cast<size_t>(end.hit)];
                if (!hit.many) {
                    if (hit.first_id != id) {
                        hit.ids = {hit.first_id, id};
                        hit.many = true;
                        hit.elements.push_back(index);
                    }
                } else if (hit.ids.insert(id).second) {
                    hit.elements.push_back(index);
                }
            }
        }
    }

    // The failure links, level by level: where the matcher goes on when the text leaves the name it was reading.
    std::vector<int32_t> level, deeper;
    for (int32_t c = m.states[0].first_child; c != None; c = m.states[static_cast<size_t>(c)].next_sibling) {
        m.states[static_cast<size_t>(c)].fail = 0;
        m.states[static_cast<size_t>(c)].longest = m.states[static_cast<size_t>(c)].hit;
        level.push_back(c);
    }
    while (!level.empty()) {
        deeper.clear();
        for (int32_t s : level) {
            for (int32_t c = m.states[static_cast<size_t>(s)].first_child; c != None;
                 c = m.states[static_cast<size_t>(c)].next_sibling) {
                const uint32_t token = m.states[static_cast<size_t>(c)].token;
                int32_t back = m.states[static_cast<size_t>(s)].fail;
                int32_t to = None;
                while (back != None && (to = m.Child(back, token)) == None) back = m.states[static_cast<size_t>(back)].fail;
                State& child = m.states[static_cast<size_t>(c)];
                child.fail = back == None ? 0 : to;
                child.longest = child.hit != None ? child.hit : m.states[static_cast<size_t>(child.fail)].longest;
                deeper.push_back(c);
            }
        }
        std::swap(level, deeper);
    }

    for (const Hit& hit : m.hits)
        if (hit.elements.size() > 1) m.shared.push_back(OWNameShared{hit.name, hit.elements});
    return OWNameFinder(std::move(impl));
}

std::vector<OWNameFound> OWNameFinder::Find(std::string_view text) const { return Find(text, false); }

std::vector<OWNameFound> OWNameFinder::Find(std::string_view text, bool markdown) const {
    std::vector<OWNameFound> found;
    const Impl& m = *impl_;
    if (!m.error.empty() || m.hits.empty()) return found;
    std::vector<char32_t> cps;
    std::vector<size_t> at;
    Decode(text, cps, at);
    std::vector<std::pair<size_t, size_t>> spans = PassedOver(text, cps, at, markdown);
    const size_t length = cps.size();
    // Where a whole name starts and the longest one that does, as the backward pass meets them: last place first.
    std::vector<size_t> starts;
    std::vector<int32_t> longest;
    size_t from = 0;
    for (size_t span = 0; span <= spans.size(); ++span) {
        const size_t to = span < spans.size() ? spans[span].first : length;
        if (to > from) {
            starts.clear();
            longest.clear();
            int32_t state = 0;
            size_t k = to;
            // Whether a name may end at k: the text ends there, or the character at k is not a word character.
            bool mayEnd = k >= length || !IsWordPoint(cps[k]);
            while (k > from) {
                if (mayEnd) state = m.Step(state, EndMark);
                const char32_t c = cps[--k];
                mayEnd = !IsWordPoint(c);
                if (state != 0) state = m.Step(state, static_cast<uint32_t>(m.folded ? OWUnicode::SimpleFold(c) : c));
                const int32_t hit = m.states[static_cast<size_t>(state)].longest;
                if (hit != None && !(k > 0 && IsWordPoint(cps[k - 1]))) {
                    starts.push_back(k);
                    longest.push_back(hit);
                }
            }
            // Forwards over what was met: the first place a name starts takes its longest name, and the next name
            // starts at or after that one's end.
            size_t free = from;
            for (size_t r = starts.size(); r-- > 0;) {
                const size_t start = starts[r];
                if (start < free) continue;
                const Hit& hit = m.hits[static_cast<size_t>(longest[r])];
                const size_t end = start + hit.length;
                if (hit.elements.size() == 1) found.push_back(OWNameFound{at[start], at[end], hit.elements[0]});
                free = end;
            }
        }
        if (span < spans.size()) from = spans[span].second;
    }
    return found;
}

const std::vector<OWNameShared>& OWNameFinder::Shared() const { return impl_->shared; }

const std::string& OWNameFinder::Error() const { return impl_->error; }

}  // namespace OnlyWorlds
