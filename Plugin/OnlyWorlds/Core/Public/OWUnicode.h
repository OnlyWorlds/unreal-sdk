#pragma once
// What the archive's name rules and the folder writer need from Unicode, at the version the fixture set pins (15.1.0),
// from Unicode's own files (codegen/generate.py), never from a runtime: NFC, OW-18's fold (NFC, then the C and S rows
// of CaseFolding), strict UTF-8, white space as Python's str.strip() has it, and the slug's NFKD.
//
// Strings are UTF-8 in std::string. A string that came out of a JSON escape may hold a lone surrogate, encoded the
// way UTF-8 would encode it (WTF-8): Decode reads it back as that code point, and nothing here makes one.

#include "OWApi.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace OnlyWorlds {
namespace OWUnicode {

// The Unicode version of the tables ("15.1.0").
OW_API const char* Version();

// Whether the bytes are well-formed UTF-8: no overlong form, no surrogate, nothing past U+10FFFF.
OW_API bool IsUtf8(const uint8_t* bytes, size_t size);
inline bool IsUtf8(std::string_view s) { return IsUtf8(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

OW_API bool IsAscii(std::string_view s);

// UTF-8 (or WTF-8) to code points. A byte that begins no sequence reads as U+FFFD; callers pass checked text.
OW_API std::u32string Decode(std::string_view utf8);
OW_API std::string Encode(std::u32string_view cps);
OW_API void AppendUtf8(std::string& out, char32_t cp);

// The canonical combining class of a code point (0 for a starter).
OW_API uint8_t CombiningClass(char32_t cp);

// Unicode Normalization Form C (UAX #15), as the reference checker's nfc computes it.
OW_API std::u32string Nfc(std::u32string_view cps);
OW_API std::string Nfc(std::string_view utf8);

// OW-18's one fold for every reader: NFC, then simple case folding (the C and S rows).
OW_API std::string Fold(std::string_view utf8);

// The string without the white space at its ends, as Python's str.strip() has it (how a world id is trimmed).
OW_API std::string_view StripWhite(std::string_view utf8);
OW_API bool IsWhite(char32_t cp);

// The compatibility decomposition (NFKD) of a string, for the element filename's slug, and whether a code point is a
// nonspacing mark (general category Mn).
OW_API std::u32string CompatDecompose(std::u32string_view cps);
OW_API bool IsNonspacingMark(char32_t cp);

// Whether a code point is a letter of any script or a combining mark (general category L or M): what cuts an address
// found in prose inside a word.
OW_API bool IsLetterOrMark(char32_t cp);

// Whether a code point is a decimal digit of any script (general category Nd): with the letters, the marks and the
// underscore, what OWNameFinder counts as inside a word.
OW_API bool IsDecimalDigit(char32_t cp);

// One code point's simple case folding (CaseFolding's C and S rows), the code point itself when it has none. A fold
// never leaves its plane, so it keeps a text's code points; it may change their UTF-8 length (U+212A, three bytes,
// folds to k).
OW_API char32_t SimpleFold(char32_t cp);

}  // namespace OWUnicode
}  // namespace OnlyWorlds
