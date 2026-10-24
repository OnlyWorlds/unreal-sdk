#pragma once
// What the archive's name rules and the folder writer need from Unicode, at the version the fixture set pins (15.1.0),
// from Unicode's own files (codegen/generate.py), never from a runtime: NFC, OW-18's fold (NFC, then the C and S rows
// of CaseFolding), strict UTF-8, white space as Python's str.strip() has it, and the slug's NFKD.
//
// Strings are UTF-8 in std::string. A string that came out of a JSON escape may hold a lone surrogate, encoded the
// way UTF-8 would encode it (WTF-8): Decode reads it back as that code point, and nothing here makes one.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace OnlyWorlds {
namespace OWUnicode {

// The Unicode version of the tables ("15.1.0").
const char* Version();

// Whether the bytes are well-formed UTF-8: no overlong form, no surrogate, nothing past U+10FFFF.
bool IsUtf8(const uint8_t* bytes, size_t size);
inline bool IsUtf8(std::string_view s) { return IsUtf8(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

bool IsAscii(std::string_view s);

// UTF-8 (or WTF-8) to code points. A byte that begins no sequence reads as U+FFFD; callers pass checked text.
std::u32string Decode(std::string_view utf8);
std::string Encode(std::u32string_view cps);
void AppendUtf8(std::string& out, char32_t cp);

// The canonical combining class of a code point (0 for a starter).
uint8_t CombiningClass(char32_t cp);

// Unicode Normalization Form C (UAX #15), as the reference checker's nfc computes it.
std::u32string Nfc(std::u32string_view cps);
std::string Nfc(std::string_view utf8);

// OW-18's one fold for every reader: NFC, then simple case folding (the C and S rows).
std::string Fold(std::string_view utf8);

// The string without the white space at its ends, as Python's str.strip() has it (how a world id is trimmed).
std::string_view StripWhite(std::string_view utf8);
bool IsWhite(char32_t cp);

// The compatibility decomposition (NFKD) of a string, for the element filename's slug, and whether a code point is a
// nonspacing mark (general category Mn).
std::u32string CompatDecompose(std::u32string_view cps);
bool IsNonspacingMark(char32_t cp);

}  // namespace OWUnicode
}  // namespace OnlyWorlds
