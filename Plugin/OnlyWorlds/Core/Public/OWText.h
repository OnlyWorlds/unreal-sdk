#pragma once
// The profiles' one comparison and one order, public: names compared and sorted as every profile reader here already
// compares and sorts them (the room, names, lens, charter, report and map-space readers among them). Parity with the
// TypeScript SDK 4.180.0 (`trimAndFold`, `byCodePoint`) and the Unity SDK's OWText.
//
// Names as the profiles order them: by TrimAndFold(name) in code point order, ties by id in the same order:
//
//     std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
//         int c = OWText::CompareByCodePoint(OWText::TrimAndFold(a.name), OWText::TrimAndFold(b.name));
//         return c != 0 ? c < 0 : OWText::CompareByCodePoint(a.id, b.id) < 0;
//     });
//
// Strings are UTF-8 in std::string, as everywhere in the core. The fold is for comparing, never for showing or storing.

#include "OWApi.h"

#include <string>
#include <string_view>

namespace OnlyWorlds {
namespace OWText {

// A text trimmed and without regard to case, as the profiles define it once (schema-dist profiles/room.md): Unicode's
// White_Space (its 25 code points) removed at both ends and nothing inside touched, then simple case folding by Unicode
// 15.1's table (CaseFolding's C and S rows), with no normalization first. U+0085 and U+3000 are trimmed; a byte order
// mark, a zero width space and U+001C to U+001F stay (Python's strip() takes those four, which are no White_Space). The
// long s folds to s and the Kelvin sign to k; the sharp s, the dotted capital I and the st ligature, which have only a
// full or a Turkic folding, stay. OWMessage::Trim and OWMessage::Fold are the same two steps under the message
// profile's names; OWUnicode::Fold is OW-18's fold (NFC first, for a world file's names), not this one.
OW_API std::string TrimAndFold(std::string_view utf8);

// Two strings compared by code point, the order every profile names (never by UTF-16 unit, which puts a letter beyond
// U+FFFF before U+FF5A, and never by a locale's collation): negative when `a` comes first, positive when `b` does, 0
// when they are equal; a string that is a prefix of the other comes first. A UTF-8 string's bytes compared unsigned
// are its code points' order, and so are a lone surrogate's in the WTF-8 a JSON escape may leave.
OW_API int CompareByCodePoint(std::string_view a, std::string_view b);

// The same order as a strict weak ordering, for std::sort, std::map and std::set: true when `a` comes first.
struct ByCodePoint {
    bool operator()(std::string_view a, std::string_view b) const { return CompareByCodePoint(a, b) < 0; }
};

}  // namespace OWText
}  // namespace OnlyWorlds
