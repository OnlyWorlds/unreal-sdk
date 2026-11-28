#include "OWFolderExportNode.h"

#include "UObject/WeakObjectPtrTemplates.h"

#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWFolderExport.h"

#include <utility>

UOWClientFolderExport* UOWClientFolderExport::ExportWorldToFolder(const FOWClientConfig& Config, const FString& Folder)
{
	UOWClientFolderExport* Node = NewObject<UOWClientFolderExport>();
	Node->Settings = Config;
	Node->Target = Folder;
	return Node;
}

void UOWClientFolderExport::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientFolderExport> Self(this);
	OnlyWorlds::OWFolderExport::Export(*C, OWConvert::Path(Target), {}, [Self](OnlyWorlds::OWFolderExportReport R)
	{
		UOWClientFolderExport* Node = Self.Get();
		if (!Node || !Node->Answering()) return;
		if (R.ok)
		{
			FOWFolderExportReport Out;
			Out.Folder = OWConvert::Str(R.folder);
			Out.WorldId = OWConvert::Str(R.world_id);
			Out.WorldName = OWConvert::Str(R.world_name);
			for (const auto& KV : R.counts) Out.Counts.Add(OWConvert::Str(KV.first), static_cast<int32>(KV.second));
			Out.Total = static_cast<int32>(R.Total());
			Node->OnSuccess.Broadcast(Out, FOWApiError());
		}
		else
		{
			Node->OnFailure.Broadcast(FOWFolderExportReport(),
				R.error ? OWClientShared::ErrorOf(*R.error) : OWClientShared::Refused(R.message));
		}
		Node->Release();
	});
}
