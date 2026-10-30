#pragma once
// The names profile (schema-dist profiles/names.md), for Blueprints and C++: a world's own words for the 22 types,
// x_names on the World (Character shown as Employee, Institution as Team). The core (OWNames.h) does the work; this is
// its door. A display lookup only: the folder, the ids and every element keep the standard's names.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWNamesLibrary.generated.h"

class UOWWorld;

/** A world's own words for the types. A value that is not a names map is no word anywhere, never an error. */
UCLASS()
class ONLYWORLDS_API UOWNames : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The world's word for a type (a type slug, lowercase: "character"), printed as written: the Supertype's entry
	 * first (matched trimmed and without regard to case; "" for none), then the type's; bMany for a plural (a heading,
	 * a count), which prints the singular when the map has none. False, Word "", for the standard's name: print your
	 * usual label.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Names")
	static bool TypeWord(const UOWWorld* World, const FString& Type, const FString& Supertype, bool bMany, FString& Word);
};
