#pragma once
// What the generated OWModels.cpp writes fields with (OWToJson): one helper per field kind, on the core's OWJsonValue,
// each unset written as keel serves it (elements/serialize.py's serialize): a string "", an integer and a single link
// null, a link list []. The reading side is OWModelRead.h.

#include "CoreMinimal.h"

#include "OWJson.h"

namespace OWModelWrite
{
	/** A string field: its text, "" the unset (string-empty-is-unset: no third state). */
	OnlyWorlds::OWJsonValue Str(const FString& Value);
	/** A single link, or a generic pair's id: the id, null for "" (keel refuses "" as a UUID and stores null). */
	OnlyWorlds::OWJsonValue Link(const FString& Value);
	/** A generated struct's integer from its two members: its digits when bHas, else null (nullable-by-default). */
	OnlyWorlds::OWJsonValue Int(bool bHas, int64 Value);
	OnlyWorlds::OWJsonValue Bool(bool bValue);
	/** A multi link: the ids in order, [] for none. */
	OnlyWorlds::OWJsonValue Ids(const TArray<FString>& Values);

	/** A body as the JSON text a write node takes, the form UOWWorld::GetElementJson gives (number texts kept). */
	FString Text(const OnlyWorlds::OWJsonValue& Body);

	/**
	 * A struct's body laid over its element as read (UOWElementJson::MergeElementJson): Out is AsRead with each key of
	 * FromStruct set in place (appended when AsRead lacks it), but where FromStruct's value is unset (null, "", []) and
	 * AsRead's is unset or absent, and where both are numbers of the same value (CanonicalNumber): there the read's is
	 * kept. The id is FromStruct's only when it is not "" or null. False with Why, Out untouched: either side not an
	 * object ("element: ", "struct: "), an id that is neither a string nor null, two different ids ("id: ").
	 */
	bool Merge(const OnlyWorlds::OWJsonValue& AsRead, const OnlyWorlds::OWJsonValue& FromStruct, OnlyWorlds::OWJsonValue& Out,
		FString& Why);
}
