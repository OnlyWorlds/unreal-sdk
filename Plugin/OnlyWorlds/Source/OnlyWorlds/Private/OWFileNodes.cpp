#include "OWFileNodes.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWMedia.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

using OWClientShared::ErrorOf;
using OWConvert::Str;
using OWConvert::Utf8;

namespace OWFileNodesImpl
{
	// The core's bytes from a graph's Array of Byte.
	OnlyWorlds::OWBytes BytesOf(const TArray<uint8>& In)
	{
		return OnlyWorlds::OWBytes(In.GetData(), In.GetData() + In.Num());
	}

	// A file read from disk for a FromDisk factory, or false and Why (in the core's refusal words: nothing was sent).
	bool ReadFrom(const FString& Path, const char* Node, TArray<uint8>& Out, std::string& Why)
	{
		if (!Path.IsEmpty() && FFileHelper::LoadFileToArray(Out, *Path)) return true;
		Why = std::string(Node) + ": the file at " + Utf8(Path) + " could not be read. Nothing was sent.";
		return false;
	}

	// A refusal as a core reply of any type, kind Refused.
	template <typename T>
	OnlyWorlds::OWReply<T> RefusedReply(const std::string& Why)
	{
		OnlyWorlds::OWReply<T> Out;
		Out.error = OnlyWorlds::OWApiError::Of(OnlyWorlds::OWApiError::Kind::Refused, Why);
		return Out;
	}

	// "" for none: a graph has no absent string.
	std::optional<std::string> TextOrNone(const FString& Text)
	{
		if (Text.IsEmpty()) return std::nullopt;
		return Utf8(Text);
	}

	FOWFileRow RowOf(const OnlyWorlds::OWFileRow& R)
	{
		FOWFileRow Out;
		Out.Id = Str(R.id);
		Out.Name = Str(R.name);
		Out.Kind = Str(R.kind);
		Out.Size = R.size;
		Out.Label = Str(R.label.value_or(""));
		Out.AddedByName = Str(R.added_by.name);
		Out.AddedByMembership = Str(R.added_by.membership.value_or(""));
		Out.bAddedByAgent = R.added_by.agent;
		Out.bAddedByOwner = R.added_by.owner;
		Out.AddedAt = Str(R.added_at);
		return Out;
	}
}

namespace OWFN = OWFileNodesImpl;

// -- UOWClientImage ---------------------------------------------------------------------------------------------------

UOWClientImage* UOWClientImage::UploadImage(const FOWClientConfig& Config, const TArray<uint8>& Image, const FString& Key)
{
	UOWClientImage* Node = NewObject<UOWClientImage>();
	Node->Settings = Config;
	Node->Bytes = Image;
	Node->KeyText = Key;
	return Node;
}

UOWClientImage* UOWClientImage::UploadImageFromDisk(const FOWClientConfig& Config, const FString& Path, const FString& Key)
{
	UOWClientImage* Node = NewObject<UOWClientImage>();
	Node->Settings = Config;
	Node->FromPath = Path;
	Node->bFromDisk = true;
	Node->KeyText = Key;
	return Node;
}

void UOWClientImage::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientImage> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWUploadedImage> Reply)
	{
		if (UOWClientImage* Node = Self.Get()) Node->Answer(std::move(Reply));
	};
	// The file is read only for a client the core would send with: its own refusal comes first.
	std::string Why;
	if (bFromDisk && C->Error().empty() && !OWFN::ReadFrom(FromPath, "UploadImageFromDisk", Bytes, Why))
		return Answer(OWFN::RefusedReply<OnlyWorlds::OWUploadedImage>(Why));
	OnlyWorlds::OWUploadImageOptions O;
	O.key = Utf8(KeyText);
	C->UploadImage(OWFN::BytesOf(Bytes), O, Done);
}

void UOWClientImage::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWUploadedImage> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		FOWUploadedImage Out;
		Out.Url = Str(Reply.value.url);
		Out.Key = Str(Reply.value.key);
		Out.Bytes = Reply.value.bytes;
		Out.Type = Str(Reply.value.type);
		Out.Etag = Str(Reply.value.etag);
		OnSuccess.Broadcast(Out, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(FOWUploadedImage(), ErrorOf(*Reply.error));
	}
	Release();
}

// -- UOWClientImageRemoval --------------------------------------------------------------------------------------------

UOWClientImageRemoval* UOWClientImageRemoval::RemoveImage(const FOWClientConfig& Config, const FString& KeyOrUrl)
{
	UOWClientImageRemoval* Node = NewObject<UOWClientImageRemoval>();
	Node->Settings = Config;
	Node->KeyText = KeyOrUrl;
	return Node;
}

void UOWClientImageRemoval::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientImageRemoval> Self(this);
	C->RemoveImage(Utf8(KeyText), std::nullopt, [Self](OnlyWorlds::OWReply<OnlyWorlds::OWRemovedImage> Reply)
	{
		if (UOWClientImageRemoval* Node = Self.Get()) Node->Answer(std::move(Reply));
	});
}

void UOWClientImageRemoval::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWRemovedImage> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		FOWRemovedImage Out;
		Out.Removed = Str(Reply.value.removed);
		Out.Bytes = Reply.value.bytes;
		Out.Cache = Str(Reply.value.cache);
		OnSuccess.Broadcast(Out, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(FOWRemovedImage(), ErrorOf(*Reply.error));
	}
	Release();
}

// -- UOWClientFiles ---------------------------------------------------------------------------------------------------

UOWClientFiles* UOWClientFiles::ListElementFiles(const FOWClientConfig& Config, const FString& Type, const FString& Id)
{
	UOWClientFiles* Node = NewObject<UOWClientFiles>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	return Node;
}

UOWClientFiles* UOWClientFiles::ListWorldFiles(const FOWClientConfig& Config)
{
	UOWClientFiles* Node = NewObject<UOWClientFiles>();
	Node->Settings = Config;
	Node->bWorld = true;
	return Node;
}

void UOWClientFiles::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientFiles> Self(this);
	if (bWorld)
	{
		C->WorldFiles([Self](OnlyWorlds::OWReply<OnlyWorlds::OWWorldFiles> Reply)
		{
			if (UOWClientFiles* Node = Self.Get()) Node->AnswerWorld(std::move(Reply));
		});
		return;
	}
	C->ListFiles(Utf8(ElementType), Utf8(ElementId), [Self](OnlyWorlds::OWReply<std::vector<OnlyWorlds::OWFileRow>> Reply)
	{
		if (UOWClientFiles* Node = Self.Get()) Node->Answer(std::move(Reply));
	});
}

void UOWClientFiles::Answer(OnlyWorlds::OWReply<std::vector<OnlyWorlds::OWFileRow>> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		TArray<FOWFileRow> Rows;
		for (const OnlyWorlds::OWFileRow& R : Reply.value) Rows.Add(OWFN::RowOf(R));
		OnSuccess.Broadcast(Rows, 0, 0, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(TArray<FOWFileRow>(), 0, 0, ErrorOf(*Reply.error));
	}
	Release();
}

void UOWClientFiles::AnswerWorld(OnlyWorlds::OWReply<OnlyWorlds::OWWorldFiles> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		TArray<FOWFileRow> Rows;
		for (const OnlyWorlds::OWWorldFileRow& R : Reply.value.data)
		{
			FOWFileRow Row = OWFN::RowOf(R);
			Row.ElementType = Str(R.element_type);
			Row.ElementId = Str(R.element_id);
			Rows.Add(Row);
		}
		OnSuccess.Broadcast(Rows, Reply.value.used_bytes, Reply.value.cap_bytes, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(TArray<FOWFileRow>(), 0, 0, ErrorOf(*Reply.error));
	}
	Release();
}

// -- UOWClientFileUpload ----------------------------------------------------------------------------------------------

UOWClientFileUpload* UOWClientFileUpload::UploadElementFile(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const TArray<uint8>& File, const FString& Name, const FString& Label)
{
	UOWClientFileUpload* Node = NewObject<UOWClientFileUpload>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->Bytes = File;
	Node->NameText = Name;
	Node->LabelText = Label;
	return Node;
}

UOWClientFileUpload* UOWClientFileUpload::UploadElementFileFromDisk(const FOWClientConfig& Config, const FString& Type,
	const FString& Id, const FString& Path, const FString& Label)
{
	UOWClientFileUpload* Node = NewObject<UOWClientFileUpload>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->FromPath = Path;
	Node->bFromDisk = true;
	Node->NameText = FPaths::GetCleanFilename(Path);
	Node->LabelText = Label;
	return Node;
}

void UOWClientFileUpload::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientFileUpload> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWUploadedFile> Reply)
	{
		if (UOWClientFileUpload* Node = Self.Get()) Node->Answer(std::move(Reply));
	};
	std::string Why;
	if (bFromDisk && C->Error().empty() && !OWFN::ReadFrom(FromPath, "UploadElementFileFromDisk", Bytes, Why))
		return Answer(OWFN::RefusedReply<OnlyWorlds::OWUploadedFile>(Why));
	OnlyWorlds::OWUploadFileOptions O;
	O.name = Utf8(NameText);
	O.label = OWFN::TextOrNone(LabelText);
	C->UploadFile(Utf8(ElementType), Utf8(ElementId), OWFN::BytesOf(Bytes), O, Done);
}

void UOWClientFileUpload::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWUploadedFile> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		FOWUploadedFile Out;
		Out.Key = Str(Reply.value.key);
		Out.Fid = Str(Reply.value.fid);
		Out.Bytes = Reply.value.bytes;
		Out.Type = Str(Reply.value.type);
		Out.Etag = Str(Reply.value.etag);
		OnSuccess.Broadcast(Out, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(FOWUploadedFile(), ErrorOf(*Reply.error));
	}
	Release();
}

// -- UOWClientFileLink ------------------------------------------------------------------------------------------------

UOWClientFileLink* UOWClientFileLink::ReadFileDownloadLink(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& FileId)
{
	UOWClientFileLink* Node = NewObject<UOWClientFileLink>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->FileText = FileId;
	return Node;
}

void UOWClientFileLink::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientFileLink> Self(this);
	C->FileDownloadLink(Utf8(ElementType), Utf8(ElementId), Utf8(FileText),
		[Self](OnlyWorlds::OWReply<OnlyWorlds::OWFileDownload> Reply)
		{
			if (UOWClientFileLink* Node = Self.Get()) Node->Answer(std::move(Reply));
		});
}

void UOWClientFileLink::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWFileDownload> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok())
	{
		FOWFileDownload Out;
		Out.Url = Str(Reply.value.url);
		Out.Exp = Reply.value.exp;
		Out.Name = Str(Reply.value.name);
		Out.Kind = Str(Reply.value.kind);
		Out.Size = Reply.value.size;
		OnSuccess.Broadcast(Out, FOWApiError());
	}
	else
	{
		OnFailure.Broadcast(FOWFileDownload(), ErrorOf(*Reply.error));
	}
	Release();
}

// -- UOWClientFileRemoval ---------------------------------------------------------------------------------------------

UOWClientFileRemoval* UOWClientFileRemoval::RemoveElementFile(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& FileId)
{
	UOWClientFileRemoval* Node = NewObject<UOWClientFileRemoval>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->FileText = FileId;
	return Node;
}

void UOWClientFileRemoval::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientFileRemoval> Self(this);
	C->RemoveFile(Utf8(ElementType), Utf8(ElementId), Utf8(FileText), [Self](OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply)
	{
		if (UOWClientFileRemoval* Node = Self.Get()) Node->Answer(std::move(Reply));
	});
}

void UOWClientFileRemoval::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(FString(), FOWApiError());
	else OnFailure.Broadcast(FString(), ErrorOf(*Reply.error));
	Release();
}
