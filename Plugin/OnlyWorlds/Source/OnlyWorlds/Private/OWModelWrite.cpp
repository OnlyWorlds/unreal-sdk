#include "OWModelWrite.h"

#include "OWConvert.h"

#include <cstdint>
#include <string>
#include <utility>

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

	namespace
	{
		// The unset forms a struct writes, and a read may hold: null, "", [] (keel's serve, serialize.py).
		bool IsUnsetValue(const OnlyWorlds::OWJsonValue& V)
		{
			return V.IsNull() || (V.IsString() && V.Text().empty()) || (V.IsArray() && V.Size() == 0);
		}

		// A body's id into IdText ("" when absent, null or ""); false when it is neither a string nor null.
		bool IdOf(const OnlyWorlds::OWJsonValue& Body, std::string& IdText)
		{
			IdText.clear();
			const OnlyWorlds::OWJsonValue* V = Body.Find("id");
			if (!V || V->IsNull()) return true;
			if (!V->IsString()) return false;
			IdText = V->Text();
			return true;
		}
	}

	bool Merge(const OnlyWorlds::OWJsonValue& AsRead, const OnlyWorlds::OWJsonValue& FromStruct, OnlyWorlds::OWJsonValue& Out,
		FString& Why)
	{
		if (!AsRead.IsObject())
		{
			Why = TEXT("element: not a JSON object");
			return false;
		}
		if (!FromStruct.IsObject())
		{
			Why = TEXT("struct: not a JSON object");
			return false;
		}
		std::string ReadId, StructId;
		if (!IdOf(AsRead, ReadId))
		{
			Why = TEXT("element: its id is neither a string nor null");
			return false;
		}
		if (!IdOf(FromStruct, StructId))
		{
			Why = TEXT("struct: its id is neither a string nor null");
			return false;
		}
		if (!ReadId.empty() && !StructId.empty() && ReadId != StructId)
		{
			Why = FString::Printf(TEXT("id: the struct's id %s is not the element's %s"), *OWConvert::Str(StructId),
				*OWConvert::Str(ReadId));
			return false;
		}
		OnlyWorlds::OWJsonValue Laid = AsRead;
		for (size_t I = 0; I < FromStruct.Size(); ++I)
		{
			const std::string& FieldKey = FromStruct.Keys()[I];
			const OnlyWorlds::OWJsonValue& Given = FromStruct.Items()[I];
			if (FieldKey == "id")
			{
				if (!StructId.empty()) Laid.Set(FieldKey, Given);
				continue;
			}
			const OnlyWorlds::OWJsonValue* Held = Laid.Find(FieldKey);
			// An unset over an unset or an absent key is no change: the read's form stays (absent stays absent).
			if (IsUnsetValue(Given) && (!Held || IsUnsetValue(*Held))) continue;
			// The same number spelled otherwise (4255.0 read, 4255 written) is no change either.
			if (Held && Held->IsNumber() && Given.IsNumber()
				&& OnlyWorlds::OWJson::CanonicalNumber(Held->Text()) == OnlyWorlds::OWJson::CanonicalNumber(Given.Text()))
			{
				continue;
			}
			Laid.Set(FieldKey, Given);
		}
		Out = std::move(Laid);
		return true;
	}
}
