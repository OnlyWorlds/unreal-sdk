#include "OWUnicode.h"
#include "OWUnicodeData.h"

#include <algorithm>

namespace OnlyWorlds {
namespace OWUnicode {

namespace D = UnicodeData;

namespace {

constexpr char32_t SBase = 0xAC00, LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
constexpr char32_t LCount = 19, VCount = 21, TCount = 28;
constexpr char32_t NCount = VCount * TCount;  // 588
constexpr char32_t SCount = LCount * NCount;  // 11172

template <typename Row, typename Key>
const Row* Find(const Row* rows, size_t count, Key key, Key (*of)(const Row&)) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        Key k = of(rows[mid]);
        if (k == key) return &rows[mid];
        if (k < key) lo = mid + 1; else hi = mid;
    }
    return nullptr;
}

char32_t CpOfClass(const D::ClassRow& r) { return r.cp; }
char32_t CpOfDecomp(const D::DecompRow& r) { return r.cp; }
char32_t CpOfFold(const D::FoldRow& r) { return r.cp; }
uint64_t KeyOfPair(const D::PairRow& r) { return r.key; }

// A code point's full canonical decomposition (the reference checker's `decompose`): the table holds field 5 applied
// until nothing changes; Hangul by arithmetic.
void Decompose(char32_t cp, std::u32string& out) {
    if (cp >= SBase && cp < SBase + SCount) {
        char32_t s = cp - SBase, t = s % TCount;
        out.push_back(LBase + s / NCount);
        out.push_back(VBase + (s % NCount) / TCount);
        if (t) out.push_back(TBase + t);
        return;
    }
    if (const D::DecompRow* r = Find(D::Decomps, D::DecompCount, cp, CpOfDecomp)) {
        out.append(D::DecompData + r->offset, r->length);
        return;
    }
    out.push_back(cp);
}

// Canonical ordering: each run of non-starters, stably by class.
void Order(std::u32string& cps) {
    size_t i = 0, n = cps.size();
    while (i < n) {
        size_t j = i;
        while (j < n && CombiningClass(cps[j])) ++j;
        if (j - i > 1)
            std::stable_sort(cps.begin() + i, cps.begin() + j,
                             [](char32_t a, char32_t b) { return CombiningClass(a) < CombiningClass(b); });
        i = j + 1;
    }
}

// The primary composite of a starter and the next code point, or 0. Hangul L+V and LV+T by arithmetic.
char32_t ComposePair(char32_t a, char32_t b) {
    if (a >= LBase && a < LBase + LCount && b >= VBase && b < VBase + VCount)
        return SBase + ((a - LBase) * VCount + (b - VBase)) * TCount;
    if (a >= SBase && a < SBase + SCount && (a - SBase) % TCount == 0 && b > TBase && b < TBase + TCount)
        return a + (b - TBase);
    const D::PairRow* r = Find(D::Pairs, D::PairCount, (uint64_t(a) << 21) | b, KeyOfPair);
    return r ? r->composite : 0;
}

}  // namespace

const char* Version() { return D::Version; }

bool IsUtf8(const uint8_t* b, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = b[i];
        if (c < 0x80) { ++i; continue; }
        size_t need;
        uint8_t lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c == 0xE0) { need = 2; lo = 0xA0; }
        else if (c == 0xED) { need = 2; hi = 0x9F; }
        else if (c >= 0xE1 && c <= 0xEF) need = 2;
        else if (c == 0xF0) { need = 3; lo = 0x90; }
        else if (c == 0xF4) { need = 3; hi = 0x8F; }
        else if (c >= 0xF1 && c <= 0xF3) need = 3;
        else return false;
        if (i + need >= n) return false;  // the sequence runs past the end
        if (b[i + 1] < lo || b[i + 1] > hi) return false;
        for (size_t k = 2; k <= need; ++k)
            if (b[i + k] < 0x80 || b[i + k] > 0xBF) return false;
        i += need + 1;
    }
    return true;
}

bool IsAscii(std::string_view s) {
    for (unsigned char c : s)
        if (c > 0x7F) return false;
    return true;
}

std::u32string Decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    auto at = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
    while (i < n) {
        uint8_t c = at(i);
        if (c < 0x80) { out.push_back(c); ++i; continue; }
        size_t need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (need == 0 || i + need >= n) { out.push_back(0xFFFD); ++i; continue; }
        char32_t cp = c & (0x3F >> need);
        for (size_t k = 1; k <= need; ++k) cp = (cp << 6) | (at(i + k) & 0x3F);
        out.push_back(cp);
        i += need + 1;
    }
    return out;
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string Encode(std::u32string_view cps) {
    std::string out;
    out.reserve(cps.size());
    for (char32_t c : cps) AppendUtf8(out, c);
    return out;
}

uint8_t CombiningClass(char32_t cp) {
    if (cp < 0x300) return 0;
    const D::ClassRow* r = Find(D::Classes, D::ClassCount, cp, CpOfClass);
    return r ? r->ccc : 0;
}

std::u32string Nfc(std::u32string_view text) {
    std::u32string cps;
    cps.reserve(text.size());
    for (char32_t c : text) Decompose(c, cps);
    Order(cps);
    // Compose each code point with the last starter unless blocked: `last` is the class of the last code point kept
    // after the starter (-1: none kept).
    std::u32string out;
    out.reserve(cps.size());
    ptrdiff_t starter = -1;
    int last = -1;
    for (char32_t c : cps) {
        int cc = CombiningClass(c);
        if (starter >= 0 && (last < 0 || (0 < last && last < cc))) {
            if (char32_t composite = ComposePair(out[starter], c)) {
                out[starter] = composite;
                continue;
            }
        }
        if (cc == 0) {
            starter = static_cast<ptrdiff_t>(out.size());
            last = -1;
        } else {
            last = cc;
        }
        out.push_back(c);
    }
    return out;
}

std::string Nfc(std::string_view utf8) {
    if (IsAscii(utf8)) return std::string(utf8);
    return Encode(Nfc(Decode(utf8)));
}

std::string Fold(std::string_view utf8) {
    if (IsAscii(utf8)) {  // the only C and S rows that land in ASCII start in ASCII: A to Z (and NFC changes no ASCII)
        std::string out(utf8);
        for (char& c : out)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return out;
    }
    std::u32string cps = Nfc(Decode(utf8));
    for (char32_t& c : cps)
        if (const D::FoldRow* r = Find(D::Folds, D::FoldCount, c, CpOfFold)) c = r->to;
    return Encode(cps);
}

bool IsWhite(char32_t cp) {
    return std::binary_search(D::WhiteSpace, D::WhiteSpace + D::WhiteSpaceCount, cp);
}

std::string_view StripWhite(std::string_view s) {
    // Decode from each end one code point at a time: white space is a handful of code points, and an id rarely has any.
    auto lead = [&](size_t at, char32_t& cp) -> size_t {
        std::u32string one = Decode(s.substr(at, std::min<size_t>(4, s.size() - at)));
        cp = one.empty() ? 0 : one[0];
        return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    };
    size_t a = 0, b = s.size();
    while (a < b) {
        char32_t cp;
        size_t len = lead(a, cp);
        if (!IsWhite(cp)) break;
        a += len;
    }
    while (b > a) {
        size_t start = b - 1;
        while (start > a && (static_cast<uint8_t>(s[start]) & 0xC0) == 0x80) --start;
        char32_t cp;
        lead(start, cp);
        if (!IsWhite(cp)) break;
        b = start;
    }
    return s.substr(a, b - a);
}

std::u32string CompatDecompose(std::u32string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c >= SBase && c < SBase + SCount) {
            Decompose(c, out);
        } else if (const D::DecompRow* r = Find(D::CompatDecomps, D::CompatDecompCount, c, CpOfDecomp)) {
            out.append(D::CompatDecompData + r->offset, r->length);
        } else {
            out.push_back(c);
        }
    }
    Order(out);
    return out;
}

bool IsNonspacingMark(char32_t cp) {
    size_t lo = 0, hi = D::NonspacingMarkCount;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < D::NonspacingMarks[mid].first) hi = mid;
        else if (cp > D::NonspacingMarks[mid].last) lo = mid + 1;
        else return true;
    }
    return false;
}

bool IsLetterOrMark(char32_t cp) {
    size_t lo = 0, hi = D::LetterAndMarkCount;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < D::LettersAndMarks[mid].first) hi = mid;
        else if (cp > D::LettersAndMarks[mid].last) lo = mid + 1;
        else return true;
    }
    return false;
}

bool IsDecimalDigit(char32_t cp) {
    size_t lo = 0, hi = D::DecimalDigitCount;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < D::DecimalDigits[mid].first) hi = mid;
        else if (cp > D::DecimalDigits[mid].last) lo = mid + 1;
        else return true;
    }
    return false;
}

char32_t SimpleFold(char32_t cp) {
    const D::FoldRow* end = D::Folds + D::FoldCount;
    const D::FoldRow* r = std::lower_bound(D::Folds, end, cp, [](const D::FoldRow& row, char32_t k) { return row.cp < k; });
    return r != end && r->cp == cp ? r->to : cp;
}

}  // namespace OWUnicode
}  // namespace OnlyWorlds
