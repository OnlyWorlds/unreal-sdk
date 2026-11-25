#pragma once
// What the generated OWModels.cpp reads fields with: one helper per field kind, on the core's OWJsonValue.

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include "OWJson.h"

class UOWWorld;

namespace OWModelRead
{
	/** A string field; "" when the key is missing, null or not a string (scalar_str's unset, single's null). */
	FString Str(const OnlyWorlds::OWJsonValue& Body, const char* Key);
	/** An integer field; unset for null or a missing key. An integral number spelled N.0 or 1e3 reads as its value. */
	TOptional<int64> Int(const OnlyWorlds::OWJsonValue& Body, const char* Key);
	/** A generated struct's integer as its two members: whether it is set, and its value (0 when unset). */
	bool HasInt(const OnlyWorlds::OWJsonValue& Body, const char* Key);
	int64 IntValue(const OnlyWorlds::OWJsonValue& Body, const char* Key);
	bool Bool(const OnlyWorlds::OWJsonValue& Body, const char* Key);
	/** A multi link: the string ids of an array, in order. */
	TArray<FString> Ids(const OnlyWorlds::OWJsonValue& Body, const char* Key);

	/** Every element of the world whose type is Type, in the order read. */
	void Each(const UOWWorld* World, const char* Type, TFunctionRef<void(const OnlyWorlds::OWJsonValue&)> Fn);
	/** The body of the element with this id, when it is of type Type. */
	const OnlyWorlds::OWJsonValue* Find(const UOWWorld* World, const FString& Id, const char* Type);
	/**
	 * An element's JSON text, as a read node answers it, parsed to a body. False with Why when the text is not JSON
	 * ("not JSON: " and the parser's reason) or not an object ("not a JSON object").
	 */
	bool Body(const FString& Json, OnlyWorlds::OWJsonValue& Out, FString& Why);
}
