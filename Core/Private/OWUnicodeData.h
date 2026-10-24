#pragma once
// The generated Unicode tables (OWUnicodeData.generated.cpp, written by codegen/generate.py). Private to the core.

#include <cstddef>
#include <cstdint>

namespace OnlyWorlds { namespace UnicodeData {

struct ClassRow { char32_t cp; uint8_t ccc; };
struct DecompRow { char32_t cp; uint32_t offset; uint32_t length; };
struct PairRow { uint64_t key; char32_t composite; };  // key: first << 21 | second
struct FoldRow { char32_t cp; char32_t to; };
struct RangeRow { char32_t first; char32_t last; };

extern const char* const Version;
extern const ClassRow Classes[];
extern const size_t ClassCount;
extern const DecompRow Decomps[];
extern const size_t DecompCount;
extern const char32_t DecompData[];
extern const PairRow Pairs[];
extern const size_t PairCount;
extern const FoldRow Folds[];
extern const size_t FoldCount;
extern const char32_t WhiteSpace[];
extern const size_t WhiteSpaceCount;
extern const DecompRow CompatDecomps[];
extern const size_t CompatDecompCount;
extern const char32_t CompatDecompData[];
extern const RangeRow NonspacingMarks[];
extern const size_t NonspacingMarkCount;

}}  // namespace OnlyWorlds::UnicodeData
