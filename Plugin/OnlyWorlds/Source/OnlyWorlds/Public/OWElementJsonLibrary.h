#pragma once
// A struct's JSON merged into its element as read, so a write keeps what a struct cannot hold: the extension keys
// (x_*, atlas_*, shadow_*), the keys a newer schema adds, the server's stamps. schema-dist's extension-passthrough
// ruling: extensions are merged back on write, never dropped. Hand-written, one function for every type, beside
// UOWElements (OWModels.h, generated), whose doors Get<Type>Json give a struct's JSON. The merge itself is
// OWModelWrite::Merge, on the core's OWJsonValue.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWElementJsonLibrary.generated.h"

/** An element's JSON text with a struct's laid over it, for a write that keeps what the struct cannot hold. */
UCLASS()
class ONLYWORLDS_API UOWElementJson : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The element as read (ElementJson: UOWWorld::GetElementJson, or a read node's Json) with every key of the struct's
	 * JSON (StructJson: a Get<Type>Json door) laid over it in place, and every other key kept in its place and order:
	 * the extensions, the keys a newer schema adds, type, world and the server's stamps (the write nodes strip the
	 * read-only ones before sending). A struct's unset ("", null, []) lands over a set value; over an unset or absent
	 * one the read's form is kept, so an unchanged struct merges back to the element as read. An integer the same as
	 * the read's keeps the read's spelling. A key the read lacks and the struct sets is added at the end. The id: the
	 * read's when the struct's is ""; two different ids are refused. Upsert the answer to write the element whole and
	 * keep its extensions. False with Reason ("element: " or "struct: " and why the text is not a JSON object, "id: "
	 * and the two ids) and Merged "".
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	static bool MergeElementJson(const FString& ElementJson, const FString& StructJson, FString& Merged, FString& Reason);
};
