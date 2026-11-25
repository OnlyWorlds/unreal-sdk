#pragma once
// Opening a world, for Blueprints and C++: a world folder, or an .owworld file held to every reader rule of the folder
// format (section 4.7). The core does the work; this is its door.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWLibrary.generated.h"

class UOWWorld;

/** What a reader made of an .owworld file. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWArchiveVerdict
{
	GENERATED_BODY()

	/** True when the file is a world every conforming reader opens. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds")
	bool bAccepted = false;

	/** A refusal's one cause, from the folder format's list ("name_case", "unsafe_path", ...); "" when accepted. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds")
	FString Reason;

	/** What was found, in words. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds")
	FString Message;

	/** The world's id, when accepted. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds")
	FString WorldId;
};

UCLASS()
class ONLYWORLDS_API UOWLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Open a world folder (the folder holding world.json). A relative path is taken from the project's folder.
	 * nullptr, with Error set, when there is no world there, and when the folder holds world.json, elements/ or spatial/
	 * in another case and not as written (Elements/, World.JSON): those names are read as written, so the world would
	 * read as empty, and Error names the cause, name_case, and both spellings.
	 */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	static UOWWorld* OpenWorldFolder(const FString& Path, FString& Error);

	/** Open an .owworld file. nullptr when a reader refuses it; the verdict says why. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	static UOWWorld* OpenWorldFile(const FString& Path, FOWArchiveVerdict& Verdict);

	/** Hold an .owworld file to the reader rules without keeping the world. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	static FOWArchiveVerdict CheckWorldFile(const FString& Path);

	/** Pack a world folder as a portable .owworld file (its api block stripped, private records left out, and who wrote
	 * each element left out: the five author fields, unless bIncludeAuthorFields, for a copy of your own folder). */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds")
	static bool PackWorldFolder(const FString& Folder, const FString& ArchivePath, FString& Error,
		bool bIncludeAuthorFields = false);
};
