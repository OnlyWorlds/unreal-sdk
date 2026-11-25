#pragma once
// One world in Unreal: a UObject around the core's OnlyWorlds::OWWorld, which does the work. Open one with
// UOWLibrary (a world folder or an .owworld file); read its elements as structs with UOWElements (OWModels.h).

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWWorld.h"

#include <memory>

#include "OWWorldObject.generated.h"

UCLASS(BlueprintType)
class ONLYWORLDS_API UOWWorld : public UObject
{
	GENERATED_BODY()

public:
	/** Takes a world the core read. */
	void SetNative(std::unique_ptr<OnlyWorlds::OWWorld> InWorld);
	/** The core's world, for C++ callers (nullptr before SetNative). */
	OnlyWorlds::OWWorld* Native() const { return World.get(); }

	/** world.json's id. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetId() const;

	/** world.json's name. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetWorldName() const;

	/** The folder the world was read from ("" for a world from an .owworld file). */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetRoot() const;

	/** How many elements the world holds. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	int32 GetElementCount() const;

	/** The ids of the elements of one type ("character"), in the order read; every element's when Type is "". */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	TArray<FString> GetElementIds(const FString& Type) const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	bool HasElement(const FString& Id) const;

	/** An element's type ("character"): its declared type, else its folder's. "" when there is no such element. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetElementType(const FString& Id) const;

	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetElementName(const FString& Id) const;

	/** An element's file as JSON, in the folder format's layout. "" when there is no such element. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	FString GetElementJson(const FString& Id) const;

	/** Set a string field of an element; the element is then written in the folder format's form. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	bool SetStringField(const FString& Id, const FString& Key, const FString& Value);

	/** Set an integer field of an element. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	bool SetIntField(const FString& Id, const FString& Key, int64 Value);

	/** What could not be read, one line each. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds")
	TArray<FString> GetErrors() const;

	/**
	 * Write the world to a folder: every file an edit did not touch keeps the bytes it was read with. With
	 * bOnlyEdited, only the files an edit changed (to write back into the folder it was read from).
	 */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	bool WriteFolder(const FString& Path, bool bOnlyEdited, FString& Error);

	/** Pack the world as a portable .owworld file (its api block stripped, private records left out, and who wrote
	 * each element left out: the five author fields, unless bIncludeAuthorFields, for a copy of your own folder). */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	bool PackToFile(const FString& ArchivePath, FString& Error, bool bIncludeAuthorFields = false);

private:
	std::unique_ptr<OnlyWorlds::OWWorld> World;
};
