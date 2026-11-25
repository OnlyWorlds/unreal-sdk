#include "OWModelRead.h"

#include "OWConvert.h"
#include "OWWorldObject.h"

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace OWModelRead
{
	FString Str(const OnlyWorlds::OWJsonValue& Body, const char* Key)
	{
		const OnlyWorlds::OWJsonValue* V = Body.Find(Key);
		return V && V->IsString() ? OWConvert::Str(V->Text()) : FString();
	}

	TOptional<int64> Int(const OnlyWorlds::OWJsonValue& Body, const char* Key)
	{
		const OnlyWorlds::OWJsonValue* V = Body.Find(Key);
		if (!V || !V->IsNumber()) return TOptional<int64>();
		// An integer literal by its text, so a value past 2^53 keeps every digit.
		const std::string& T = V->Text();
		int64_t N = 0;
		const std::from_chars_result R = std::from_chars(T.data(), T.data() + T.size(), N);
		if (R.ec == std::errc() && R.ptr == T.data() + T.size()) return static_cast<int64>(N);
		// Any other spelling of an integral value (4255.0, 1e3).
		const double D = V->AsDouble();
		if (std::isfinite(D) && D == std::floor(D) && std::fabs(D) < 9.2e18) return static_cast<int64>(D);
		return TOptional<int64>();
	}

	bool HasInt(const OnlyWorlds::OWJsonValue& Body, const char* Key) { return Int(Body, Key).IsSet(); }

	int64 IntValue(const OnlyWorlds::OWJsonValue& Body, const char* Key) { return Int(Body, Key).Get(0); }

	bool Bool(const OnlyWorlds::OWJsonValue& Body, const char* Key)
	{
		const OnlyWorlds::OWJsonValue* V = Body.Find(Key);
		return V && V->IsBool() && V->AsBool();
	}

	TArray<FString> Ids(const OnlyWorlds::OWJsonValue& Body, const char* Key)
	{
		TArray<FString> Out;
		const OnlyWorlds::OWJsonValue* V = Body.Find(Key);
		if (!V || !V->IsArray()) return Out;
		for (const OnlyWorlds::OWJsonValue& Item : V->Items())
		{
			if (Item.IsString()) Out.Add(OWConvert::Str(Item.Text()));
		}
		return Out;
	}

	void Each(const UOWWorld* World, const char* Type, TFunctionRef<void(const OnlyWorlds::OWJsonValue&)> Fn)
	{
		const OnlyWorlds::OWWorld* W = World ? World->Native() : nullptr;
		if (!W) return;
		for (const OnlyWorlds::OWElement& E : W->elements)
		{
			if (E.Type() == Type) Fn(E.body);
		}
	}

	const OnlyWorlds::OWJsonValue* Find(const UOWWorld* World, const FString& Id, const char* Type)
	{
		OnlyWorlds::OWWorld* W = World ? World->Native() : nullptr;
		const OnlyWorlds::OWElement* E = W ? W->Get(OWConvert::Utf8(Id)) : nullptr;
		return E && E->Type() == Type ? &E->body : nullptr;
	}

	bool Body(const FString& Json, OnlyWorlds::OWJsonValue& Out, FString& Why)
	{
		Why.Reset();
		const std::string Text = OWConvert::Utf8(Json);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		if (!Parsed.ok)
		{
			Why = TEXT("not JSON: ") + OWConvert::Str(Parsed.error);
			return false;
		}
		if (!Parsed.value.IsObject())
		{
			Why = TEXT("not a JSON object");
			return false;
		}
		Out = std::move(Parsed.value);
		return true;
	}
}
