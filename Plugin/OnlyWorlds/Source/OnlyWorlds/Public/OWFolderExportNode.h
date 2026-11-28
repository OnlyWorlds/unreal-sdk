#pragma once
// An onlyworlds.com world written as a world folder, on Blueprints: the core's OWFolderExport::Export as one async node,
// ExportWorldToFolder. It reads the world the config's key reaches (GET requests only), and writes it as the folder the
// onlyworlds Python package's export_world writes, with where it came from in its world.json (source, exported_as,
// producer). Open the folder later with Open World Folder. The folder must not exist or be empty; a refused export
// leaves nothing behind.

#include "CoreMinimal.h"

#include "OWClientNodes.h"

#include "OWFolderExportNode.generated.h"

/** What ExportWorldToFolder wrote: empty on OnFailure. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWFolderExportReport
{
	GENERATED_BODY()

	/** The folder written, absolute. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString Folder;

	/** The world's id and name, from its world.json. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString WorldId;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	FString WorldName;

	/** Elements written per type ("character"), types with none left out. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	TMap<FString, int32> Counts;

	/** Every element written. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Client")
	int32 Total = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWFolderExportAnswer, const FOWFolderExportReport&, Report, const FOWApiError&, Error);

/** An account world written as a world folder. */
UCLASS()
class ONLYWORLDS_API UOWClientFolderExport : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** What was written. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWFolderExportAnswer OnSuccess;

	/** Why nothing was: the server's refusal (Kind Api: a wrong key or PIN is Status 401), no answer (Transport), or the export's own refusal (Refused: a folder that is not empty, an element it cannot write), the reason in Error.Line. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Client")
	FOWFolderExportAnswer OnFailure;

	/** Read the world the key reaches, GETs only, and write it as a world folder at Folder (absolute, or relative to the project's folder), which must not exist or be empty. Every element of the 22 types, known-of stubs without their empty fields; world.json with source, exported_as and producer. Map images are not downloaded. The core's OWFolderExport::Export. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Client", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFolderExport* ExportWorldToFolder(const FOWClientConfig& Config, const FString& Folder);

	virtual void Activate() override;

private:
	FString Target;
};
