// OWText: names compared as the profiles compare them (room.md's "trimmed and without regard to case, defined once";
// ordered by code point). The TypeScript SDK's test/names-compare.test.mjs (4.180.0), case for case. Each case is a
// place where a language's own function answers differently: JavaScript's trim() strips a byte order mark, Python's
// strip() U+001C to U+001F, toLowerCase() leaves the long s, and UTF-16 units put a letter beyond U+FFFF before U+FF5A.
// npm's seventh test (a value that is no string is refused with a TypeError) has no counterpart: the parameter is a
// std::string_view.

#include "ow_test.h"

#include "OWJson.h"
#include "OWLens.h"
#include "OWMessage.h"
#include "OWText.h"
#include "OWUnicode.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace OnlyWorlds;

namespace {

std::string Cp(std::initializer_list<char32_t> points) {
    std::string out;
    for (char32_t c : points) OWUnicode::AppendUtf8(out, c);
    return out;
}

std::string Hex(char32_t c) {
    const char* digits = "0123456789abcdef";
    std::string s;
    do {
        s.insert(s.begin(), digits[c % 16]);
        c /= 16;
    } while (c);
    return "U+" + s;
}

// The 25 code points of Unicode's White_Space property (PropList.txt).
const std::vector<char32_t> WhiteSpace = {
    0x09,   0x0a,   0x0b,   0x0c,   0x0d,   0x20,   0x85,   0xa0,   0x1680, 0x2000, 0x2001, 0x2002, 0x2003,
    0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200a, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000,
};

std::string AllWhite() {
    std::string s;
    for (char32_t w : WhiteSpace) s += Cp({w});
    return s;
}

}  // namespace

OW_TEST(text_trim_and_fold_trims_each_white_space_code_point_at_either_end_and_nothing_inside) {
    for (char32_t w : WhiteSpace)
        t.Eq(OWText::TrimAndFold(Cp({w}) + "in design" + Cp({w})), std::string("in design"), Hex(w));
    t.Eq(OWText::TrimAndFold(AllWhite() + "Locked" + AllWhite()), std::string("locked"), "all 25 at both ends");
    t.Eq(OWText::TrimAndFold("in" + Cp({0x3000}) + "design"), "in" + Cp({0x3000}) + "design", "nothing inside");
    t.Eq(OWText::TrimAndFold(""), std::string(), "the empty text");
    t.Eq(OWText::TrimAndFold(AllWhite()), std::string(), "white space alone");
}

OW_TEST(text_trim_and_fold_keeps_what_is_no_white_space_where_a_language_trim_takes_it) {
    // JavaScript's trim() strips U+FEFF; Python's strip() (OWUnicode::StripWhite) strips U+001C to U+001F. None is
    // White_Space.
    for (char32_t keep : {char32_t(0xfeff), char32_t(0x200b), char32_t(0x180e), char32_t(0x1c), char32_t(0x1d),
                          char32_t(0x1e), char32_t(0x1f)}) {
        t.Eq(OWText::TrimAndFold(Cp({keep}) + "locked"), Cp({keep}) + "locked", "leading " + Hex(keep));
        t.Eq(OWText::TrimAndFold("locked" + Cp({keep})), "locked" + Cp({keep}), "trailing " + Hex(keep));
    }
    t.Ok(std::string(OWUnicode::StripWhite("\x1Clocked")) != OWText::TrimAndFold("\x1Clocked"),
         "Python's strip() is not the trim");
}

OW_TEST(text_trim_and_fold_folds_by_the_simple_rows_only_never_by_lower_casing_or_full_folding) {
    t.Eq(OWText::TrimAndFold("ME" + Cp({0x17f, 0x17f}) + "AGE"), std::string("message"), "the long s folds to s");
    t.Eq(OWText::TrimAndFold(Cp({0x212a}) + "ing"), std::string("king"), "the Kelvin sign folds to k");
    t.Eq(OWText::TrimAndFold("Me" + Cp({0xdf}) + "age"), "me" + Cp({0xdf}) + "age", "the sharp s: full folding only");
    t.Eq(OWText::TrimAndFold(Cp({0x131})), Cp({0x131}), "the dotless i has no row");
    t.Eq(OWText::TrimAndFold(Cp({0x130})), Cp({0x130}), "the dotted capital I has only F and T rows");
    t.Eq(OWText::TrimAndFold("te" + Cp({0xfb06})), "te" + Cp({0xfb06}), "the st ligature: full folding only");
    t.Eq(OWText::TrimAndFold(Cp({0x10400})), Cp({0x10428}), "an astral letter folds by its row");
    t.Eq(OWText::TrimAndFold("LOCKED"), std::string("locked"), "ASCII");
    // Beside npm's cases: no NFC first, so the fold is not OW-18's (OWUnicode::Fold composes A + U+030A).
    t.Eq(OWText::TrimAndFold("A" + Cp({0x30a})), "a" + Cp({0x30a}), "a decomposed letter is not composed");
    t.Ok(OWUnicode::Fold("A" + Cp({0x30a})) != OWText::TrimAndFold("A" + Cp({0x30a})), "OW-18's fold is another");
}

OW_TEST(text_trim_and_fold_is_the_fold_is_room_element_matches_pairs_by) {
    auto room = [](const std::string& type, const std::string& supertype) {
        OWJsonValue e = OWJsonValue::Object();
        e.Set("id", OWJsonValue::String("0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e01"));
        e.Set("type", OWJsonValue::String(type));
        e.Set("supertype", OWJsonValue::String(supertype));
        e.Set("x_room_of", OWJsonValue::String("0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f"));
        return e;
    };
    t.Eq(OWText::TrimAndFold(Cp({0x85}) + "NARRATIVE" + Cp({0x3000})), std::string("narrative"), "the fold");
    t.Ok(OWLens::IsRoomElement(room(Cp({0x85}) + "NARRATIVE" + Cp({0x3000}), "ME" + Cp({0x17f, 0x17f}) + "AGE")),
         "a pair that folds to narrative + message is a room element");
    t.Ok(!OWLens::IsRoomElement(room(Cp({0xfeff}) + "narrative", "Message")), "a byte order mark stays, so no pair");
    t.Eq(OWText::TrimAndFold(" Me" + Cp({0x17f}) + "sage\t"), OWMessage::Fold(" Me" + Cp({0x17f}) + "sage\t"),
         "OWMessage::Fold is the same fold");
}

OW_TEST(text_by_code_point_orders_by_code_point_where_utf16_units_part_from_it) {
    const std::string astral = Cp({0x10428}), full_width = Cp({0xff5a});
    t.Ok(OWText::CompareByCodePoint(full_width, astral) < 0, "U+FF5A before U+10428");
    t.Ok(OWText::CompareByCodePoint(astral, full_width) > 0, "U+10428 after U+FF5A");
    std::vector<std::string> sorted = {astral, full_width, "z", "Z"};
    std::sort(sorted.begin(), sorted.end(), OWText::ByCodePoint());
    t.Ok(sorted == std::vector<std::string>{"Z", "z", full_width, astral}, "sorted: Z, z, U+FF5A, U+10428");
    t.Eq(OWText::CompareByCodePoint("", ""), 0, "two empty texts");
    t.Eq(OWText::CompareByCodePoint("abc", "abc"), 0, "equal");
    t.Ok(OWText::CompareByCodePoint("ab", "abc") < 0, "a prefix first");
    t.Ok(OWText::CompareByCodePoint("abc", "ab") > 0, "a prefix first, the other way");
    t.Ok(OWText::CompareByCodePoint("Zebra", "apple") < 0, "by code point, never a collation");
    // Beside npm's cases: a byte past 0x7F is no negative char, and a lone surrogate (WTF-8, from a JSON escape) keeps
    // its own value, between U+D7FF and U+E000.
    t.Ok(OWText::CompareByCodePoint("z", Cp({0xe9})) < 0, "U+00E9 after z");
    t.Ok(OWText::CompareByCodePoint(Cp({0xd7ff}), Cp({0xd800})) < 0 && OWText::CompareByCodePoint(Cp({0xd800}), Cp({0xe000})) < 0,
         "a lone surrogate between U+D7FF and U+E000");
    t.Ok(!OWText::ByCodePoint()("abc", "abc"), "the ordering is strict");
}

OW_TEST(text_names_ordered_as_the_profiles_order_them_folds_by_code_point_ties_by_id) {
    struct Row {
        std::string id, name;
    };
    std::vector<Row> rows = {
        {"b2", " apple"},
        {"a1", Cp({0x2003}) + "APPLE"},
        {"c3", Cp({0x10400})},
        {"d4", Cp({0xff3a})},
    };
    std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
        int c = OWText::CompareByCodePoint(OWText::TrimAndFold(x.name), OWText::TrimAndFold(y.name));
        return c != 0 ? c < 0 : OWText::CompareByCodePoint(x.id, y.id) < 0;
    });
    std::string order;
    for (const Row& r : rows) order += r.id + " ";
    t.Eq(order, std::string("a1 b2 d4 c3 "), "folds by code point, ties by id");
}
