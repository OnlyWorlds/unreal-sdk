#include "OWElementJsonLibrary.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWModelWrite.h"

#include <string>
#include <string_view>

bool UOWElementJson::MergeElementJson(const FString& ElementJson, const FString& StructJson, FString& Merged, FString& Reason)
{
	Merged.Reset();
	Reason.Reset();
	const std::string ElementText = OWConvert::Utf8(ElementJson);
	const OnlyWorlds::OWJsonParseResult ElementParse = OnlyWorlds::OWJson::Parse(std::string_view(ElementText));
	if (!ElementParse.ok)
	{
		Reason = TEXT("element: ") + OWConvert::Str(ElementParse.error);
		return false;
	}
	const std::string StructText = OWConvert::Utf8(StructJson);
	const OnlyWorlds::OWJsonParseResult StructParse = OnlyWorlds::OWJson::Parse(std::string_view(StructText));
	if (!StructParse.ok)
	{
		Reason = TEXT("struct: ") + OWConvert::Str(StructParse.error);
		return false;
	}
	OnlyWorlds::OWJsonValue MergedBody;
	if (!OWModelWrite::Merge(ElementParse.value, StructParse.value, MergedBody, Reason)) return false;
	Merged = OWModelWrite::Text(MergedBody);
	return true;
}
