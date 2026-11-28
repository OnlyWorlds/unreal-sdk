#pragma once
// A listener for the folder export node's tests (OWFolderExportNodeTests.cpp): its OnSuccess and OnFailure bound with
// AddDynamic, as a graph's async node binds them, each answer counted and kept. Tests only.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "OWFolderExportNode.h"

#include "OWFolderExportNodeProbe.generated.h"

UCLASS(Transient)
class UOWFolderExportNodeProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Successes = 0;
	int32 Failures = 0;
	FOWFolderExportReport Report;
	FOWApiError Error;

	void Watch(UOWClientFolderExport* Node)
	{
		Node->OnSuccess.AddDynamic(this, &UOWFolderExportNodeProbe::Succeeded);
		Node->OnFailure.AddDynamic(this, &UOWFolderExportNodeProbe::Failed);
	}

	UFUNCTION()
	void Succeeded(const FOWFolderExportReport& InReport, const FOWApiError& InError)
	{
		++Successes;
		Report = InReport;
		Error = InError;
	}

	UFUNCTION()
	void Failed(const FOWFolderExportReport& InReport, const FOWApiError& InError)
	{
		++Failures;
		Report = InReport;
		Error = InError;
	}
};
