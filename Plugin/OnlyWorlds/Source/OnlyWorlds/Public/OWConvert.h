#pragma once
// The seam between Unreal's strings and the core's: the core speaks UTF-8 std::string, Unreal TCHAR FString.
//
// Both ways keep every Unicode scalar value as it is, the noncharacters U+FFFE and U+FFFF included (the engine's own
// conversion writes '?' for those two, and both are valid in JSON and in an id). Two more rules:
// - A lone surrogate is kept too. The core holds one (from a JSON escape such as "\ud800") as the three bytes UTF-8
//   would give that code point (WTF-8, OWUnicode.h); Str makes it the one UTF-16 unit, and Utf8 makes a lone unit those
//   three bytes again, so the core writes it back as its escape. A high unit followed by a low one is one code point.
// - Bytes that are not UTF-8 (nor a WTF-8 surrogate) read as U+FFFD, one for each maximal subpart, as the Unicode
//   standard (chapter 3) and the WHATWG decoder have it. The core's JSON reader refuses such text before it holds it.

#include "CoreMinimal.h"
#include "Misc/Paths.h"

#include <string>

namespace OWConvert
{
	// Defined in OWConvert.cpp, so a game that includes this header takes in none of their local names.
	ONLYWORLDS_API std::string Utf8(const FString& S);
	ONLYWORLDS_API FString Str(const std::string& S);

	// A path for the core: a relative one is taken from the project's folder (in a packaged game, the staged
	// project folder), an absolute one as given.
	inline std::string Path(const FString& P)
	{
		FString Full = FPaths::IsRelative(P) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), P) : P;
		return Utf8(Full);
	}
}
