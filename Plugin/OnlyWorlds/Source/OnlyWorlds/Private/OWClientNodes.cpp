#include "OWClientNodes.h"

#include "UObject/WeakObjectPtrTemplates.h"

#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWHttpTransport.h"
#include "OWJson.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using OWClientShared::ErrorOf;
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

	// A refusal the node makes itself, in the core's form: kind Refused, its line "OnlyWorlds refused: ...".
	template <typename T>
	OnlyWorlds::OWReply<T> RefusedReply(const std::string& Why)
	{
		OnlyWorlds::OWReply<T> Out;
		Out.error = OnlyWorlds::OWApiError::Of(OnlyWorlds::OWApiError::Kind::Refused, Why);
		return Out;
	}

	// An element's JSON text as the core's value: "" is null (the core then refuses it, or a bulk reads it as an empty
	// element); text that is not JSON leaves Why saying so, named by where and which pin, for the node to refuse.
	OWJsonValue BodyOf(const FString& Text, const std::string& Where, const char* Pin, std::string& Why)
	{
		if (Text.IsEmpty()) return OWJsonValue();
		const std::string Bytes = Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Bytes));
		if (Parsed.ok) return std::move(Parsed.value);
		Why = Where + ": " + Pin + " is not JSON text: " + Parsed.error + ". Nothing was sent.";
		return OWJsonValue();
	}

	FOWBulkResult BulkResultOf(const OnlyWorlds::OWBulkResult& R)
	{
		FOWBulkResult Out;
		Out.bErrors = R.errors;
		Out.bWasReplay = R.was_replay;
		for (const OnlyWorlds::OWBulkSlot& S : R.items)
		{
			FOWBulkSlot& Row = Out.Items.AddDefaulted_GetRef();
			Row.Status = S.status;
			Row.Id = TextOr(S.id);
			Row.CreatedAt = TextOr(S.created_at);
			Row.UpdatedAt = TextOr(S.updated_at);
			Row.bSucceeded = S.Succeeded();
			Row.bIsProposed = S.IsProposed();
			if (S.error) Row.Error = ErrorOf(*S.error);
			Row.Proposal = S.proposal ? JsonText(*S.proposal) : FString();
		}
		for (const size_t I : R.Failed()) Out.Failed.Add(static_cast<int32>(I));
		Out.SucceededCount = static_cast<int32>(R.SucceededCount());
		Out.bAllSucceeded = R.AllSucceeded();
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

	// A JSON value as text, or "" for none (a null fields or base).
	FString JsonOrEmpty(const OWJsonValue& Value)
	{
		return Value.IsNull() ? FString() : JsonText(Value);
	}

	FOWProposal ProposalOf(const OnlyWorlds::OWProposal& P)
	{
		FOWProposal Out;
		Out.Kind = Str(P.kind);
		Out.bIsNewElement = P.IsNewElement();
		Out.Id = Str(P.id);
		Out.ElementType = Str(P.element_type);
		Out.ElementId = Str(P.element_id);
		Out.House = TextOr(P.house);
		Out.State = Str(P.state);
		Out.FieldsJson = JsonOrEmpty(P.fields);
		if (P.omitted)
			for (const std::string& Name : *P.omitted) Out.Omitted.Add(Str(Name));
		Out.BaseJson = JsonOrEmpty(P.base);
		Out.BaseSeq = P.base_seq;
		// A string as itself, any other value its JSON text, "" for none.
		Out.Evidence = TextOr(P.EvidenceText());
		Out.Expires = TextOr(P.ExpiresText());
		Out.By = TextOr(P.by);
		Out.Via = TextOr(P.via);
		Out.Client = TextOr(P.client);
		Out.DecidedBy = TextOr(P.decided_by);
		Out.DecidedAt = TextOr(P.decided_at);
		Out.DeclineReason = TextOr(P.decline_reason);
		Out.CreatedAt = TextOr(P.created_at);
		Out.UpdatedAt = TextOr(P.updated_at);
		Out.Json = JsonText(P.ToJson());
		return Out;
	}

	FOWProposalPage ProposalPageOf(const OnlyWorlds::OWProposalPage& P)
	{
		FOWProposalPage Out;
		for (const OnlyWorlds::OWProposal& Row : P.data) Out.Proposals.Add(ProposalOf(Row));
		Out.bHasMore = P.has_more;
		Out.NextCursor = Str(P.next_cursor);
		Out.ETag = TextOr(P.etag);
		Out.bUnchanged = P.unchanged;
		return Out;
	}

	OnlyWorlds::OWProposalQuery ProposalQueryOf(const FOWProposalQuery& Q)
	{
		OnlyWorlds::OWProposalQuery Out;
		Out.state = Utf8(Q.State);
		Out.since = Utf8(Q.Since);
		Out.element = Utf8(Q.Element);
		if (Q.Limit > 0) Out.limit = Q.Limit;
		Out.cursor = Utf8(Q.Cursor);
		// "" is no tag on a graph, which has no absent string: none is sent.
		if (!Q.IfNoneMatch.IsEmpty()) Out.if_none_match = Utf8(Q.IfNoneMatch);
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

// -- OWClientShared -----------------------------------------------------------------------------------------------------

OnlyWorlds::OWClientConfig OWClientShared::CoreConfig(const FOWClientConfig& Config, std::shared_ptr<OnlyWorlds::OWTransport> Transport)
{
	OnlyWorlds::OWClientConfig Core;
	Core.api_key = Utf8(Config.ApiKey);
	Core.api_pin = Utf8(Config.ApiPin);
	// "" is no world; anything else goes to the core, which refuses white space.
	if (!Config.World.IsEmpty()) Core.world = Utf8(Config.World);
	Core.base_url = Config.BaseUrl.IsEmpty() ? std::string(OnlyWorlds::OWClient::DefaultBaseUrl) : Utf8(Config.BaseUrl);
	Core.page_size = Config.PageSize;
	// "" is the core's own name; anything else goes to the core, which refuses a name that is not keel's shape.
	if (!Config.Client.IsEmpty()) Core.client = Utf8(Config.Client);
	Core.agent = Config.bAgent;
	Core.transport = Transport ? std::move(Transport) : FOWHttpTransport::Make();
	return Core;
}

FOWApiError OWClientShared::ErrorOf(const OnlyWorlds::OWApiError& E)
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
	Out.Limit = TextOr(E.Limit());
	Out.Line = Str(E.ToString());
	Out.bIsProposed = E.IsProposed();
	Out.Proposal = E.proposal ? JsonText(*E.proposal) : FString();
	const std::optional<OWJsonValue> Now = E.Current();
	Out.Current = Now ? JsonText(*Now) : FString();
	for (const OnlyWorlds::OWProposalConflict& C : E.Conflicts())
	{
		FOWProposalConflict& Row = Out.Conflicts.AddDefaulted_GetRef();
		Row.Field = Str(C.field);
		Row.BaseJson = JsonText(C.base);
		Row.CurrentJson = JsonText(C.current);
	}
	return Out;
}

FOWApiError OWClientShared::Refused(const std::string& Why)
{
	return ErrorOf(OnlyWorlds::OWApiError::Of(OnlyWorlds::OWApiError::Kind::Refused, Why));
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

	Client = std::make_unique<OnlyWorlds::OWClient>(OWClientShared::CoreConfig(Settings, Transport));

	// Kept until it answered: nothing else may hold the node (a graph's async node holds it only while the graph that
	// made it lives), and its answer comes later. The keep comes first, since a refusal answers inside the call.
	AddToRoot();
	return Client.get();
}

std::shared_ptr<OnlyWorlds::OWTransport> UOWClientNode::BeginKeyless()
{
	if (bActivated) return nullptr;
	bActivated = true;
	AddToRoot();
	return Transport ? Transport : FOWHttpTransport::Make();
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

// -- UOWClientWrite ---------------------------------------------------------------------------------------------------

UOWClientWrite* UOWClientWrite::Make(const FOWClientConfig& Config, EWrite Which, const FString& Type, const FString& Id)
{
	UOWClientWrite* Node = NewObject<UOWClientWrite>();
	Node->Settings = Config;
	Node->Write = Which;
	Node->ElementType = Type;
	Node->ElementId = Id;
	return Node;
}

UOWClientWrite* UOWClientWrite::CreateElement(const FOWClientConfig& Config, const FString& Type, const FString& ElementJson,
	const FString& IdempotencyKey)
{
	UOWClientWrite* Node = Make(Config, EWrite::Create, Type, FString());
	Node->BodyText = ElementJson;
	Node->KeyText = IdempotencyKey;
	return Node;
}

UOWClientWrite* UOWClientWrite::UpsertElement(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& ElementJson)
{
	UOWClientWrite* Node = Make(Config, EWrite::Upsert, Type, Id);
	Node->BodyText = ElementJson;
	return Node;
}

UOWClientWrite* UOWClientWrite::PatchElement(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& PartialJson, bool bHasIfMatch, int64 IfMatch)
{
	UOWClientWrite* Node = Make(Config, EWrite::Patch, Type, Id);
	Node->BodyText = PartialJson;
	Node->bConditional = bHasIfMatch;
	Node->ChangeSeq = IfMatch;
	return Node;
}

UOWClientWrite* UOWClientWrite::DeleteElement(const FOWClientConfig& Config, const FString& Type, const FString& Id, bool bHasIfMatch,
	int64 IfMatch)
{
	UOWClientWrite* Node = Make(Config, EWrite::Delete, Type, Id);
	Node->bConditional = bHasIfMatch;
	Node->ChangeSeq = IfMatch;
	return Node;
}

UOWClientWrite* UOWClientWrite::EditElementLinks(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& Field, const TArray<FString>& Add, const TArray<FString>& Remove, bool bHasIfMatch, int64 IfMatch)
{
	UOWClientWrite* Node = Make(Config, EWrite::Links, Type, Id);
	Node->LinkField = Field;
	Node->AddIds = Add;
	Node->RemoveIds = Remove;
	Node->bConditional = bHasIfMatch;
	Node->ChangeSeq = IfMatch;
	return Node;
}

UOWClientWrite* UOWClientWrite::AcceptNewElement(const FOWClientConfig& Config, const FString& Type, const FString& Id)
{
	return Make(Config, EWrite::AcceptNew, Type, Id);
}

UOWClientWrite* UOWClientWrite::DeclineNewElement(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& Reason)
{
	UOWClientWrite* Node = Make(Config, EWrite::DeclineNew, Type, Id);
	Node->ReasonText = Reason;
	return Node;
}

void UOWClientWrite::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientWrite> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OWJsonValue> Reply)
	{
		if (UOWClientWrite* Node = Self.Get()) Node->Answer(std::move(Reply));
	};
	const std::string TypeWord = Utf8(ElementType);
	const std::string IdText = Utf8(ElementId);
	OnlyWorlds::OWWriteOptions When;
	if (bConditional) When.if_match = ChangeSeq;
	// The element's text is read here, unless the core refuses before it reads a body (a client it would not build, a
	// negative If-Match): its refusal then comes first, as it would in C++.
	const bool bParse = C->Error().empty() && !(bConditional && ChangeSeq < 0);
	std::string Why;
	switch (Write)
	{
	case EWrite::Create:
	{
		OWJsonValue Body = bParse ? BodyOf(BodyText, "create", "ElementJson", Why) : OWJsonValue();
		if (!Why.empty()) return Answer(RefusedReply<OWJsonValue>(Why));
		OnlyWorlds::OWCreateOptions O;
		O.idempotency_key = Utf8(KeyText);
		C->Create(TypeWord, Body, O, Done);
		break;
	}
	case EWrite::Upsert:
	{
		OWJsonValue Body = bParse ? BodyOf(BodyText, "upsert", "ElementJson", Why) : OWJsonValue();
		if (!Why.empty()) return Answer(RefusedReply<OWJsonValue>(Why));
		C->Upsert(TypeWord, IdText, Body, Done);
		break;
	}
	case EWrite::Patch:
	{
		OWJsonValue Body = bParse ? BodyOf(BodyText, "patch", "PartialJson", Why) : OWJsonValue();
		if (!Why.empty()) return Answer(RefusedReply<OWJsonValue>(Why));
		C->Patch(TypeWord, IdText, Body, When, Done);
		break;
	}
	case EWrite::Delete:
		C->Delete(TypeWord, IdText, When, Done);
		break;
	case EWrite::Links:
	{
		// An empty list is not sent: the core sends a list only when given.
		OnlyWorlds::OWLinkEdit Change;
		if (AddIds.Num() > 0) Change.add = Utf8s(AddIds);
		if (RemoveIds.Num() > 0) Change.remove = Utf8s(RemoveIds);
		C->EditLinks(TypeWord, IdText, Utf8(LinkField), Change, When, Done);
		break;
	}
	case EWrite::AcceptNew:
	case EWrite::DeclineNew:
	{
		// The review's answer as a write's: the element kept, or nothing after a decline (as after a delete). "" sends no
		// reason: a graph has no absent string.
		const bool bKeep = Write == EWrite::AcceptNew;
		std::optional<std::string> Reason;
		if (!bKeep && !ReasonText.IsEmpty()) Reason = Utf8(ReasonText);
		C->ReviewNewElement(TypeWord, IdText, bKeep ? OnlyWorlds::OWReviewed::Accept : OnlyWorlds::OWReviewed::Decline, Reason,
			[Done](OnlyWorlds::OWReply<OnlyWorlds::OWReviewed> Reviewed)
			{
				OnlyWorlds::OWReply<OWJsonValue> Out;
				Out.error = std::move(Reviewed.error);
				Out.response = std::move(Reviewed.response);
				if (!Out.error && Reviewed.value.element) Out.value = std::move(*Reviewed.value.element);
				Done(std::move(Out));
			});
		break;
	}
	}
}

void UOWClientWrite::Answer(OnlyWorlds::OWReply<OWJsonValue> Reply)
{
	if (!Answering()) return;
	// A delete answers null: no element, "".
	if (Reply.Ok()) OnSuccess.Broadcast(Reply.value.IsNull() ? FString() : JsonText(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FString(), ErrorOf(*Reply.error));
	Release();
}

// -- UOWClientBulk ----------------------------------------------------------------------------------------------------

UOWClientBulk* UOWClientBulk::BulkWrite(const FOWClientConfig& Config, const TArray<FOWBulkItem>& Items, bool bAtomic,
	const FString& IdempotencyKey, bool bCreateOnly)
{
	UOWClientBulk* Node = NewObject<UOWClientBulk>();
	Node->Settings = Config;
	Node->BulkItems = Items;
	Node->bAllOrNothing = bAtomic;
	Node->bNeverReplace = bCreateOnly;
	Node->KeyText = IdempotencyKey;
	return Node;
}

void UOWClientBulk::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientBulk> Self(this);
	// Each element's text is read here, in order, until an item the core refuses on its own (no type, not an object,
	// a known-of stub) or a client it would not build: from there the core's refusal comes first, as it would in C++.
	bool bParse = C->Error().empty();
	std::vector<OnlyWorlds::OWBulkItem> Writes;
	Writes.reserve(BulkItems.Num());
	for (int32 I = 0; I < BulkItems.Num(); ++I)
	{
		OnlyWorlds::OWBulkItem& One = Writes.emplace_back();
		One.type = Utf8(BulkItems[I].Type);
		if (One.type.empty()) bParse = false;
		if (!bParse) continue;
		std::string Why;
		One.element = BodyOf(BulkItems[I].ElementJson, "bulk: items[" + std::to_string(I) + "]", "ElementJson", Why);
		if (!Why.empty()) return Answer(RefusedReply<OnlyWorlds::OWBulkResult>(Why));
		if ((!One.element.IsNull() && !One.element.IsObject()) || OnlyWorlds::OWClient::IsKnownOf(One.element)) bParse = false;
	}
	OnlyWorlds::OWBulkOptions O;
	O.atomic = bAllOrNothing;
	O.create_only = bNeverReplace;
	O.idempotency_key = Utf8(KeyText);
	C->Bulk(Writes, O, [Self](OnlyWorlds::OWReply<OnlyWorlds::OWBulkResult> Reply)
	{
		if (UOWClientBulk* Node = Self.Get()) Node->Answer(std::move(Reply));
	});
}

void UOWClientBulk::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWBulkResult> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(BulkResultOf(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FOWBulkResult(), ErrorOf(*Reply.error));
	Release();
}

// -- UOWClientProposal ------------------------------------------------------------------------------------------------

UOWClientProposal* UOWClientProposal::Make(const FOWClientConfig& Config, EAct Which, const FString& Type, const FString& Id,
	const FString& ProposalId)
{
	UOWClientProposal* Node = NewObject<UOWClientProposal>();
	Node->Settings = Config;
	Node->Act = Which;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->RowId = ProposalId;
	return Node;
}

UOWClientProposal* UOWClientProposal::ProposeChange(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& FieldsJson, const FString& Evidence, const FString& Expires, const FString& IdempotencyKey)
{
	UOWClientProposal* Node = Make(Config, EAct::Propose, Type, Id, FString());
	Node->BodyText = FieldsJson;
	Node->EvidenceText = Evidence;
	Node->ExpiresText = Expires;
	Node->KeyText = IdempotencyKey;
	return Node;
}

UOWClientProposal* UOWClientProposal::AcceptProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& ProposalId)
{
	return Make(Config, EAct::Accept, Type, Id, ProposalId);
}

UOWClientProposal* UOWClientProposal::DeclineProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& ProposalId, const FString& Reason)
{
	UOWClientProposal* Node = Make(Config, EAct::Decline, Type, Id, ProposalId);
	Node->ReasonText = Reason;
	return Node;
}

UOWClientProposal* UOWClientProposal::WithdrawProposal(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FString& ProposalId)
{
	return Make(Config, EAct::Withdraw, Type, Id, ProposalId);
}

void UOWClientProposal::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientProposal> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWProposal> Reply)
	{
		if (UOWClientProposal* Node = Self.Get()) Node->Answer(std::move(Reply), FString());
	};
	const std::string TypeWord = Utf8(ElementType);
	const std::string IdText = Utf8(ElementId);
	const std::string Pid = Utf8(RowId);
	switch (Act)
	{
	case EAct::Propose:
	{
		// The fields' text is read here, unless the core refuses before it reads a body (a client it would not build):
		// its refusal then comes first, as it would in C++.
		std::string Why;
		OWJsonValue Fields = C->Error().empty() ? BodyOf(BodyText, "propose", "FieldsJson", Why) : OWJsonValue();
		if (!Why.empty()) return Answer(RefusedReply<OnlyWorlds::OWProposal>(Why), FString());
		OnlyWorlds::OWProposeOptions O;
		O.expires = Utf8(ExpiresText);
		O.idempotency_key = Utf8(KeyText);
		C->Propose(TypeWord, IdText, Fields, Utf8(EvidenceText), O, Done);
		break;
	}
	case EAct::Accept:
		C->AcceptProposal(TypeWord, IdText, Pid, [Self](OnlyWorlds::OWReply<OnlyWorlds::OWProposalAccepted> Reply)
		{
			UOWClientProposal* Node = Self.Get();
			if (!Node) return;
			OnlyWorlds::OWReply<OnlyWorlds::OWProposal> Row;
			Row.error = std::move(Reply.error);
			Row.response = std::move(Reply.response);
			FString Element;
			if (!Row.error)
			{
				Row.value = std::move(Reply.value.proposal);
				Element = JsonText(Reply.value.element);
			}
			Node->Answer(std::move(Row), Element);
		});
		break;
	case EAct::Decline:
	{
		// "" sends no reason: a graph has no absent string.
		std::optional<std::string> Reason;
		if (!ReasonText.IsEmpty()) Reason = Utf8(ReasonText);
		C->DeclineProposal(TypeWord, IdText, Pid, Reason, Done);
		break;
	}
	case EAct::Withdraw:
		C->WithdrawProposal(TypeWord, IdText, Pid, [Self](OnlyWorlds::OWReply<OWJsonValue> Reply)
		{
			UOWClientProposal* Node = Self.Get();
			if (!Node) return;
			OnlyWorlds::OWReply<OnlyWorlds::OWProposal> Row;
			Row.error = std::move(Reply.error);
			Row.response = std::move(Reply.response);
			Node->Answer(std::move(Row), FString());
		});
		break;
	}
}

void UOWClientProposal::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWProposal> Reply, const FString& Element)
{
	if (!Answering()) return;
	// A withdraw answers no row: an empty one, as a delete's Json is "".
	if (Reply.Ok() && Act == EAct::Withdraw) OnSuccess.Broadcast(FOWProposal(), FString(), FOWApiError());
	else if (Reply.Ok()) OnSuccess.Broadcast(ProposalOf(Reply.value), Element, FOWApiError());
	else OnFailure.Broadcast(FOWProposal(), FString(), ErrorOf(*Reply.error));
	Release();
}

// -- UOWClientProposals -----------------------------------------------------------------------------------------------

UOWClientProposals* UOWClientProposals::ListElementProposals(const FOWClientConfig& Config, const FString& Type, const FString& Id,
	const FOWProposalQuery& Query)
{
	UOWClientProposals* Node = NewObject<UOWClientProposals>();
	Node->Settings = Config;
	Node->ElementType = Type;
	Node->ElementId = Id;
	Node->ListQuery = Query;
	return Node;
}

UOWClientProposals* UOWClientProposals::ListWorldProposals(const FOWClientConfig& Config, const FOWProposalQuery& Query)
{
	UOWClientProposals* Node = NewObject<UOWClientProposals>();
	Node->Settings = Config;
	Node->bWorld = true;
	Node->ListQuery = Query;
	return Node;
}

void UOWClientProposals::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientProposals> Self(this);
	auto Done = [Self](OnlyWorlds::OWReply<OnlyWorlds::OWProposalPage> Reply)
	{
		UOWClientProposals* Node = Self.Get();
		if (!Node || !Node->Answering()) return;
		if (Reply.Ok()) Node->OnSuccess.Broadcast(ProposalPageOf(Reply.value), FOWApiError());
		else Node->OnFailure.Broadcast(FOWProposalPage(), ErrorOf(*Reply.error));
		Node->Release();
	};
	const OnlyWorlds::OWProposalQuery Q = ProposalQueryOf(ListQuery);
	if (bWorld) C->WorldProposals(Q, Done);
	else C->ListProposals(Utf8(ElementType), Utf8(ElementId), Q, Done);
}
