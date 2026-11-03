#include "OWClientNodes.h"

#include "UObject/WeakObjectPtrTemplates.h"

#include "OWConvert.h"
#include "OWHttpTransport.h"
#include "OWJson.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using OWConvert::Str;
using OWConvert::Utf8;

namespace
{
	using OnlyWorlds::OWJsonValue;

	// A value as JSON text, each number as it was served (an id past 2^53 keeps its digits), as UOWWorld::GetElementJson
	// writes an element.
	FString JsonText(const OWJsonValue& Value)
	{
		return Str(OnlyWorlds::OWJson::Stringify(Value, /*keep_number_text=*/true));
	}

	TArray<FString> JsonTexts(const std::vector<OWJsonValue>& Values)
	{
		TArray<FString> Out;
		Out.Reserve(static_cast<int32>(Values.size()));
		for (const OWJsonValue& V : Values) Out.Add(JsonText(V));
		return Out;
	}

	std::vector<std::string> Utf8s(const TArray<FString>& Values)
	{
		std::vector<std::string> Out;
		Out.reserve(Values.Num());
		for (const FString& V : Values) Out.push_back(Utf8(V));
		return Out;
	}

	FString TextOr(const std::optional<std::string>& Text)
	{
		return Text ? Str(*Text) : FString();
	}

	FOWApiError ErrorOf(const OnlyWorlds::OWApiError& E)
	{
		FOWApiError Out;
		switch (E.kind)
		{
		case OnlyWorlds::OWApiError::Kind::Api: Out.Kind = EOWApiErrorKind::Api; break;
		case OnlyWorlds::OWApiError::Kind::Transport: Out.Kind = EOWApiErrorKind::Transport; break;
		case OnlyWorlds::OWApiError::Kind::Refused: Out.Kind = EOWApiErrorKind::Refused; break;
		case OnlyWorlds::OWApiError::Kind::Answer: Out.Kind = EOWApiErrorKind::Answer; break;
		}
		Out.Status = E.status;
		Out.Type = Str(E.type);
		Out.Code = Str(E.code);
		Out.Message = Str(E.message);
		Out.Param = Str(E.param);
		Out.DocUrl = Str(E.doc_url);
		Out.RawBody = Str(E.raw_body);
		Out.bHasRetryAfter = E.retry_after.has_value();
		Out.RetryAfter = E.retry_after.value_or(0);
		Out.bRetryable = E.IsRetryable();
		Out.ResyncReason = TextOr(E.ResyncReason());
		Out.Line = Str(E.ToString());
		return Out;
	}

	OnlyWorlds::OWListOptions ListOptionsOf(const FOWListOptions& O)
	{
		OnlyWorlds::OWListOptions Out;
		if (O.Limit > 0) Out.limit = O.Limit;
		Out.cursor = Utf8(O.Cursor);
		Out.expand = Utf8s(O.Expand);
		Out.fields = Utf8s(O.Fields);
		Out.ids = Utf8s(O.Ids);
		Out.created_by = Utf8(O.CreatedBy);
		Out.name = Utf8(O.Name);
		Out.name_contains = Utf8(O.NameContains);
		Out.supertype = Utf8(O.Supertype);
		Out.subtype = Utf8(O.Subtype);
		for (const TPair<FString, FString>& F : O.Filter) Out.filter[Utf8(F.Key)] = Utf8(F.Value);
		Out.view_as = Utf8(O.ViewAs);
		return Out;
	}

	FOWChangesPage PageOf(const OnlyWorlds::OWChangesPage& P)
	{
		FOWChangesPage Out;
		Out.Cursor = Str(P.cursor);
		Out.bHasMore = P.has_more;
		Out.Head = P.head;
		Out.bHasEpoch = P.epoch.has_value();
		Out.Epoch = P.epoch.value_or(0);
		Out.EpochReason = TextOr(P.epoch_reason);
		Out.bUnchanged = P.unchanged;
		for (const OnlyWorlds::OWChange& C : P.changes)
		{
			FOWChange& Row = Out.Changes.AddDefaulted_GetRef();
			Row.Op = Str(C.op);
			Row.Type = Str(C.type);
			Row.Id = Str(C.id);
			Row.ChangeSeq = C.change_seq;
			Row.UpdatedAt = TextOr(C.updated_at);
			Row.ChangedAt = TextOr(C.changed_at);
			Row.ChangedBy = TextOr(C.changed_by);
			Row.ChangedVia = TextOr(C.changed_via);
			Row.Element = C.element ? JsonText(*C.element) : FString();
			Row.DeletedAt = TextOr(C.deleted_at);
			Row.DeletedBy = TextOr(C.deleted_by);
			Row.DeletedVia = TextOr(C.deleted_via);
			Row.DeletedClient = TextOr(C.deleted_client);
			Row.Reason = TextOr(C.reason);
		}
		return Out;
	}
}

// -- UOWClientNode ----------------------------------------------------------------------------------------------------

void UOWClientNode::SetTransport(std::shared_ptr<OnlyWorlds::OWTransport> InTransport)
{
	Transport = std::move(InTransport);
}

OnlyWorlds::OWClient* UOWClientNode::Begin()
{
	if (bActivated) return nullptr;
	bActivated = true;

	OnlyWorlds::OWClientConfig Core;
	Core.api_key = Utf8(Settings.ApiKey);
	Core.api_pin = Utf8(Settings.ApiPin);
	// "" is no world; anything else goes to the core, which refuses white space.
	if (!Settings.World.IsEmpty()) Core.world = Utf8(Settings.World);
	Core.base_url = Settings.BaseUrl.IsEmpty() ? std::string(OnlyWorlds::OWClient::DefaultBaseUrl) : Utf8(Settings.BaseUrl);
	Core.page_size = Settings.PageSize;
	Core.transport = Transport ? Transport : FOWHttpTransport::Make();
	Client = std::make_unique<OnlyWorlds::OWClient>(std::move(Core));

	// Kept until it answered: nothing else may hold the node (a graph's async node holds it only while the graph that
	// made it lives), and its answer comes later. The keep comes first, since a refusal answers inside the call.
	AddToRoot();
	return Client.get();
}

bool UOWClientNode::Answering()
{
	if (bAnswered) return false;
	bAnswered = true;
	return true;
}

void UOWClientNode::Release()
{
	if (IsRooted()) RemoveFromRoot();
	SetReadyToDestroy();
}

// -- UOWClientRead ----------------------------------------------------------------------------------------------------

UOWClientRead* UOWClientRead::Make(const FOWClientConfig& Config, ECall Which)
{
	UOWClientRead* Node = NewObject<UOWClientRead>();
	Node->Settings = Config;
	Node->Call = Which;
	return Node;
}

UOWClientRead* UOWClientRead::ReadHealth(const FOWClientConfig& Config) { return Make(Config, ECall::Health); }
UOWClientRead* UOWClientRead::ReadWorld(const FOWClientConfig& Config) { return Make(Config, ECall::World); }
UOWClientRead* UOWClientRead::ReadMe(const FOWClientConfig& Config) { return Make(Config, ECall::Me); }
UOWClientRead* UOWClientRead::ReadMembers(const FOWClientConfig& Config) { return Make(Config, ECall::Members); }

UOWClientRead* UOWClientRead::ReadElement(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const TArray<FString>& Expand, const TArray<FString>& Fields, const FString& ViewAs)
{
	UOWClientRead* Node = Make(Config, ECall::Element);
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->ExpandFields = Expand;
	Node->SparseFields = Fields;
	Node->ReadAs = ViewAs;
	return Node;
}

void UOWClientRead::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	// Weak: the node is kept until it answers, and a node gone (an engine shutting down) is never called.
	TWeakObjectPtr<UOWClientRead> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OWJsonValue> Reply)
	{
		if (UOWClientRead* Node = Self.Get()) Node->Answer(std::move(Reply));
	};
	switch (Call)
	{
	case ECall::Health: C->Health(Done); break;
	case ECall::World: C->GetWorld(Done); break;
	case ECall::Me: C->Me(Done); break;
	case ECall::Members: C->Members(Done); break;
	case ECall::Element:
	{
		OnlyWorlds::OWGetOptions O;
		O.expand = Utf8s(ExpandFields);
		O.fields = Utf8s(SparseFields);
		O.view_as = Utf8(ReadAs);
		C->Get(Utf8(ElementType), Utf8(ElementId), O, Done);
		break;
	}
	}
}

void UOWClientRead::Answer(OnlyWorlds::OWReply<OWJsonValue> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(JsonText(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FString(), ErrorOf(*Reply.error));
	Release();
}

// -- UOWClientList ----------------------------------------------------------------------------------------------------

UOWClientList* UOWClientList::ListElements(const FOWClientConfig& Config, const FString& Type, const FOWListOptions& Options)
{
	UOWClientList* Node = NewObject<UOWClientList>();
	Node->Settings = Config;
	Node->ListType = Type;
	Node->ListOptions = Options;
	return Node;
}

void UOWClientList::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientList> Self(this);
	C->List(Utf8(ListType), ListOptionsOf(ListOptions), [Self](OnlyWorlds::OWReply<OnlyWorlds::OWPage> Reply)
	{
		UOWClientList* Node = Self.Get();
		if (!Node || !Node->Answering()) return;
		if (Reply.Ok())
			Node->OnSuccess.Broadcast(JsonTexts(Reply.value.data), Reply.value.has_more, Str(Reply.value.next_cursor), FOWApiError());
		else Node->OnFailure.Broadcast(TArray<FString>(), false, FString(), ErrorOf(*Reply.error));
		Node->Release();
	});
}

// -- UOWClientListAll -------------------------------------------------------------------------------------------------

UOWClientListAll* UOWClientListAll::ListAllElements(const FOWClientConfig& Config, const FString& Type,
	const FOWListOptions& Options)
{
	UOWClientListAll* Node = NewObject<UOWClientListAll>();
	Node->Settings = Config;
	Node->ListType = Type;
	Node->ListOptions = Options;
	return Node;
}

void UOWClientListAll::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientListAll> Self(this);
	C->ListAll(Utf8(ListType), ListOptionsOf(ListOptions), [Self](OnlyWorlds::OWReply<std::vector<OWJsonValue>> Reply)
	{
		UOWClientListAll* Node = Self.Get();
		if (!Node || !Node->Answering()) return;
		if (Reply.Ok()) Node->OnSuccess.Broadcast(JsonTexts(Reply.value), FOWApiError());
		else Node->OnFailure.Broadcast(TArray<FString>(), ErrorOf(*Reply.error));
		Node->Release();
	});
}

// -- UOWClientChanges -------------------------------------------------------------------------------------------------

UOWClientChanges* UOWClientChanges::ReadChanges(const FOWClientConfig& Config, const FString& Since, const FString& After)
{
	UOWClientChanges* Node = NewObject<UOWClientChanges>();
	Node->Settings = Config;
	Node->SinceCursor = Since;
	Node->AfterTime = After;
	return Node;
}

UOWClientChanges* UOWClientChanges::ReadChangesHead(const FOWClientConfig& Config)
{
	UOWClientChanges* Node = NewObject<UOWClientChanges>();
	Node->Settings = Config;
	Node->bHead = true;
	return Node;
}

void UOWClientChanges::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientChanges> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWChangesPage> Reply)
	{
		if (UOWClientChanges* Node = Self.Get()) Node->Answer(std::move(Reply));
	};
	if (bHead)
	{
		C->ChangesHead(Done);
		return;
	}
	OnlyWorlds::OWChangesOptions O;
	O.since = Utf8(SinceCursor);
	O.after = Utf8(AfterTime);
	C->Changes(O, Done);
}

void UOWClientChanges::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWChangesPage> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(PageOf(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FOWChangesPage(), ErrorOf(*Reply.error));
	Release();
}
