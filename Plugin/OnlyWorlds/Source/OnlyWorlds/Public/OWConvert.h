#pragma once
// The seam between Unreal's strings and the core's: the core speaks UTF-8 std::string, Unreal TCHAR FString.

#include "CoreMinimal.h"
#include "Misc/Paths.h"

#include <string>

namespace OWConvert
{
	inline std::string Utf8(const FString& S)
	{
		FTCHARToUTF8 C(*S, S.Len());
		return std::string(C.Get(), C.Length());
	}

	inline FString Str(const std::string& S)
	{
		FUTF8ToTCHAR C(S.data(), static_cast<int32>(S.size()));
		return FString(C.Length(), C.Get());
	}

	// A path for the core: a relative one is taken from the project's folder (in a packaged game, the staged
	// project folder), an absolute one as given.
	inline std::string Path(const FString& P)
	{
		FString Full = FPaths::IsRelative(P) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), P) : P;
		return Utf8(Full);
	}
}
