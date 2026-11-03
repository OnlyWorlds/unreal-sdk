#include "OWModelWrite.h"

#include "OWConvert.h"

#include <cstdint>

namespace OWModelWrite
{
	OnlyWorlds::OWJsonValue Str(const FString& Value) { return OnlyWorlds::OWJsonValue::String(OWConvert::Utf8(Value)); }

	OnlyWorlds::OWJsonValue Link(const FString& Value)
	{
		return Value.IsEmpty() ? OnlyWorlds::OWJsonValue::Null() : OnlyWorlds::OWJsonValue::String(OWConvert::Utf8(Value));
	}

	OnlyWorlds::OWJsonValue Int(bool bHas, int64 Value)
	{
		// int64_t, not int64: on a platform where the two are different types, the core's Number(int64_t) and
		// Number(double) would both take an int64.
		return bHas ? OnlyWorlds::OWJsonValue::Number(static_cast<int64_t>(Value)) : OnlyWorlds::OWJsonValue::Null();
	}

	OnlyWorlds::OWJsonValue Bool(bool bValue) { return OnlyWorlds::OWJsonValue::Bool(bValue); }

	OnlyWorlds::OWJsonValue Ids(const TArray<FString>& Values)
	{
		OnlyWorlds::OWJsonValue Out = OnlyWorlds::OWJsonValue::Array();
		for (const FString& V : Values) Out.Push(OnlyWorlds::OWJsonValue::String(OWConvert::Utf8(V)));
		return Out;
	}

	FString Text(const OnlyWorlds::OWJsonValue& Body)
	{
		return OWConvert::Str(OnlyWorlds::OWJson::Stringify(Body, /*keep_number_text=*/true));
	}
}
