#include "OWNamesLibrary.h"

#include "OWConvert.h"
#include "OWNames.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

using OWConvert::Str;
using OWConvert::Utf8;

bool UOWNames::TypeWord(const UOWWorld* World, const FString& Type, const FString& Supertype, bool bMany, FString& Word)
{
	Word.Reset();
	const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native) return false;
	// "" is no supertype: the core reads a blank one as none (straight to the type's word).
	const std::string T = Utf8(Type), S = Utf8(Supertype);
	const std::optional<std::string> Found = OnlyWorlds::OWNames::TypeWord(Native->info, T,
		S.empty() ? std::nullopt : std::optional<std::string_view>(S), bMany);
	if (!Found) return false;
	Word = Str(*Found);
	return true;
}
