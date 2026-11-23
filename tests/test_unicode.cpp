// OWUnicode: the set's NFC pairs and folds by name, strict UTF-8, Python's strip, the slug; and the NFC proven on
// Unicode's own NormalizationTest-15.1.0.txt (the workshop's cross-check, never a reader's input), with the slug's
// NFKD proven on the same file's NFKD columns.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWUnicode.h"
#include "OWWorld.h"

#include <cstdio>
#include <sstream>
#include <unordered_set>

using namespace OnlyWorlds;
using owtest::P;

namespace {
std::string U(std::u32string s) { return OWUnicode::Encode(s); }
}  // namespace

OW_TEST(unicode_nfc_pairs_of_the_set) {
    // fixture's pair -> whether the two are one name after NFC (each refusal's pair collides; each acceptance's not)
    struct C { std::u32string a, b; bool same; const char* what; } cases[] = {
        {U"豈", U"豈", true, "singleton"},
        {U"Å", U"Å", true, "singleton then composition (angstrom)"},
        {U"क़", U"क़", true, "an excluded composite"},
        {U"lệ", U"lệ", true, "canonical ordering"},
        {U"lá̀", U"là́", false, "marks of one class keep their order"},
        {U"한", U"한", true, "Hangul by arithmetic"},
        {U"ΐ", U"ΐ", true, "a non-starter decomposition"},
        {U"ﬁ", U"fi", false, "compatibility is not canonical"},
        {U"\U00011383", U"\U00011382\U000113C9", false, "unassigned in 15.1"},
    };
    for (const C& c : cases)
        t.Eq(OWUnicode::Nfc(U(c.a)) == OWUnicode::Nfc(U(c.b)), c.same, std::string("NFC: ") + c.what);
    t.Eq(OWUnicode::Nfc(U(U"क़")), U(U"क़"), "an excluded composite stays decomposed");
    t.Eq(OWUnicode::Nfc(U(U"̈́")), U(U"̈́"), "U+0344 decomposes, nothing to compose onto");
}

OW_TEST(unicode_fold_is_the_c_and_s_rows) {
    struct C { std::u32string a, b; bool same; const char* what; } cases[] = {
        {U"Abbey", U"abbey", true, "ASCII"},
        {U"Σ", U"ς", true, "final sigma folds to sigma (C row)"},
        {U"ﬆ", U"st", false, "the st ligature: only an F row (full folding)"},
        {U"ß", U"ss", false, "sharp s: F row only"},
        {U"ẞ", U"ß", true, "capital sharp s: S row to U+00DF"},
        {U"Ὼͅ", U"Ὼι", true, "iota subscript folds to iota (C row)"},
        {U"İ", U"i̇", false, "dotted capital I: T and F rows only"},
        {U"Café", U"café", true, "NFC, then the fold"},
        {U"K", U"k", true, "Kelvin: NFC to K, then k"},
    };
    for (const C& c : cases)
        t.Eq(OWUnicode::Fold(U(c.a)) == OWUnicode::Fold(U(c.b)), c.same, std::string("fold: ") + c.what);
}

OW_TEST(unicode_utf8_and_strip) {
    auto utf8 = [](const char* s, size_t n) { return OWUnicode::IsUtf8(reinterpret_cast<const uint8_t*>(s), n); };
    t.Ok(utf8("caf\xc3\xa9", 5), "well-formed");
    t.Ok(!utf8("caf\xe9", 4), "a Latin-1 byte");
    t.Ok(!utf8("\xc0\xaf", 2), "overlong");
    t.Ok(!utf8("\xed\xa0\x80", 3), "a surrogate");
    t.Ok(!utf8("\xf4\x90\x80\x80", 4), "past U+10FFFF");
    t.Ok(!utf8("\xe2\x82", 2), "cut short");
    t.Ok(utf8("\xf0\x9f\x98\x80", 4), "a four-byte sequence");
    t.Eq(std::string(OWUnicode::StripWhite(U(U"　 \t id \n\u0085"))), std::string("id"), "Python's white space");
    t.Eq(std::string(OWUnicode::StripWhite(U(U"​ id"))), U(U"​ id"), "U+200B is not white space to Python");
    t.Eq(std::string(OWUnicode::StripWhite(" \x1c\x1f ")), std::string(""), "U+001C..U+001F are (bidi B and S)");
}

OW_TEST(unicode_slug_and_filename) {
    struct C { const char* name; const char* slug; } cases[] = {
        {"Brother Tom\xc3\xa1s", "brother-tomas"}, {"  --Hello__World!! ", "hello-world"}, {"\xef\xac\x81re", "fire"},
        {"\xef\xbc\xa1" "bc", "abc"}, {"\xe6\x97\xa5\xe6\x9c\xac", ""}, {"", ""}, {"R2-D2", "r2-d2"},
        {"aaaaaaaaaa aaaaaaaaaa aaaaaaaaaa aaaaaa b", "aaaaaaaaaa-aaaaaaaaaa-aaaaaaaaaa-aaaaaa"},    // cut on a hyphen
        {"aaaaaaaaaa aaaaaaaaaa aaaaaaaaaa aaaaaaaa b", "aaaaaaaaaa-aaaaaaaaaa-aaaaaaaaaa-aaaaaaa"}}; // cut mid-word
    for (const C& c : cases) t.Eq(OWWorld::Slugify(c.name), std::string(c.slug), std::string("slug of ") + c.name);
    std::string id = "0695db52-0000-7000-8000-00000000ab01";
    t.Eq(OWWorld::ElementFilename(id, "Ana"), std::string("ana--0000ab01.json"), "slug and tail");
    t.Eq(OWWorld::ElementFilename(id, "\xe6\x97\xa5"), id + ".json", "no slug: the full id");
    t.Eq(OWWorld::ElementFilename(id, "Ana", true), "ana--" + id + ".json", "the collision fallback");
    t.Eq(OWWorld::ElementFilename("abcdefgh--ijk", "x"), std::string("x--defghijk.json"), "a tail that would hold -- is compacted");
}

namespace {
std::u32string Col(const std::string& col) {
    std::u32string out;
    std::istringstream s(col);
    std::string hex;
    while (s >> hex) out.push_back(static_cast<char32_t>(std::stoul(hex, nullptr, 16)));
    return out;
}
}  // namespace

OW_TEST(unicode_nfc_on_normalization_test) {
    OWBytes bytes;
    if (P().normalization_test.empty()) {
        t.Skip("set OW_NORMALIZATION_TEST to Unicode's NormalizationTest.txt, version 15.1.0 "
               "(https://www.unicode.org/Public/15.1.0/ucd/NormalizationTest.txt)");
        return;
    }
    if (!OWFiles::Read(P().normalization_test, bytes)) {
        t.Skip("no NormalizationTest-15.1.0.txt at " + P().normalization_test + " (OW_NORMALIZATION_TEST)");
        return;
    }
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string raw;
    std::string part;
    std::unordered_set<char32_t> listed;
    size_t lines = 0, bad = 0, bad_nfkd = 0;
    while (std::getline(in, raw)) {
        std::string line = raw.substr(0, raw.find('#'));
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        if (line.rfind("@Part", 0) == 0) {
            part = line;
            continue;
        }
        if (line.empty()) continue;
        std::u32string c[5];
        size_t at = 0;
        for (int k = 0; k < 5; ++k) {
            size_t semi = line.find(';', at);
            c[k] = Col(line.substr(at, semi - at));
            at = semi + 1;
        }
        if (part == "@Part1") listed.insert(c[0][0]);
        ++lines;
        // NFC: c2 == NFC(c1) == NFC(c2) == NFC(c3); c4 == NFC(c4) == NFC(c5).
        if (!(c[1] == OWUnicode::Nfc(c[0]) && c[1] == OWUnicode::Nfc(c[1]) && c[1] == OWUnicode::Nfc(c[2]) &&
              c[3] == OWUnicode::Nfc(c[3]) && c[3] == OWUnicode::Nfc(c[4]))) {
            if (++bad <= 5) t.Fail("NFC mismatch: " + raw);
        }
        // NFKD: c5 == NFKD(c1) == NFKD(c2) == NFKD(c3) == NFKD(c4) == NFKD(c5).
        bool k_ok = true;
        for (int k = 0; k < 5; ++k) k_ok = k_ok && OWUnicode::CompatDecompose(c[k]) == c[4];
        if (!k_ok && ++bad_nfkd <= 5) t.Fail("NFKD mismatch: " + raw);
    }
    size_t own = 0;
    for (char32_t cp = 0; cp < 0x110000; ++cp) {
        if ((cp >= 0xD800 && cp <= 0xDFFF) || listed.count(cp)) continue;
        std::u32string one(1, cp);
        if (OWUnicode::Nfc(one) != one && ++own <= 5) t.Fail("U+" + std::to_string(cp) + " is not in Part 1 and is not its own NFC");
    }
    std::printf("       NormalizationTest-15.1.0: %zu lines, %zu NFC mismatches, %zu NFKD mismatches; "
                "%zu code points outside Part 1 not their own NFC\n", lines, bad, bad_nfkd, own);
    t.Eq(lines, size_t(19074), "every line of the file read");
    t.Eq(bad + bad_nfkd + own, size_t(0), "mismatches");
}
