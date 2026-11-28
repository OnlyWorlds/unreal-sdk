// The proposal nodes (OWClientNodes.h: UOWClientProposal, UOWClientProposals, and UOWClientWrite's AcceptNewElement
// and DeclineNewElement), through the client nodes' fake transport (OWFakeTransport.h), which records each request and
// answers from a script: no test here touches the network. Each node is made by its factory, its OnSuccess and
// OnFailure bound to a listener (OWClientProposalNodesProbe.h) as a graph's async node binds them, given the fake
// (SetTransport, the C++ seam), and activated. What each node sends (method, URL, the headers in order, the body), the
// row and the page a success gives (every FOWProposal member, both kinds of row, an accept's element, a withdraw's
// nothing), the queue's poll (the tag sent as given, a 200's ETag, a 304 as bUnchanged), a stale_proposal's conflicts
// and keel's codes reaching OnFailure, the core's refusals and the node's own with nothing sent, one answer only, one
// node made and activated through reflection as a graph does, and, in the editor, the pins a graph's node gets.
//
// Every helper of this file is in the namespace OWProposalNodesTest (OWP below), never at file scope: a file-scope name
// hides a template parameter of the same name in an engine header the file instantiates later (C4459, an error here).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "OWBlueprintCall.h"
#include "OWClient.h"
#include "OWClientNodes.h"
#include "OWClientProposalNodesProbe.h"
#include "OWConvert.h"
#include "OWFakeTransport.h"
#include "OWJson.h"
#include "OWTransport.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AsyncAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#include <memory>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWProposalNodesTest
{
	constexpr EAutomationTestFlags ProposalTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// The frames a latent test waits for its answer before it fails.
	constexpr int32 FrameLimit = 200;

	const char* const KeelRoot = "http://keel.test/api/v2";

	// One row as keel's Proposal component gives it, the integer past 2^53 in its fields; a queue row for a new element
	// whose mark holds a list.
	const char* const ProposalRow =
		R"({"kind": "proposal", "id": "p1", "element_type": "location", "element_id": "e1", "house": "skill", "state": "open",)"
		R"( "fields": {"description": "A harbour town.", "x_count": 9007199254740993}, "omitted": ["supertype"],)"
		R"( "base": {"description": ""}, "base_seq": 41, "evidence": "Chapter 3 names the harbour.", "expires": "2026-11-21T16:00:00Z",)"
		R"( "by": "m1", "via": "api", "client": "my-game", "decided_by": null, "decided_at": null, "decline_reason": null,)"
		R"( "created_at": "2026-10-22T16:00:00Z", "updated_at": "2026-10-22T16:05:00Z"})";
	const char* const NewElementRow =
		R"({"kind": "new_element", "id": "e2", "element_type": "character", "element_id": "e2", "state": "lapsed", "by": "m2",)"
		R"( "evidence": ["e7", "e8"], "expires": 20991121, "created_at": "2026-10-23T07:00:00Z", "updated_at": "2026-10-23T07:00:00Z"})";
	const char* const ElementBody = R"({"id": "e1", "name": "Harbour", "type": "location", "change_seq": 42})";

	OnlyWorlds::OWResponse Respond(int Code, const std::string& Text, OnlyWorlds::OWHeaders Headers = {})
	{
		OnlyWorlds::OWResponse Out;
		Out.status = Code;
		Out.body = Text;
		Out.headers = std::move(Headers);
		return Out;
	}

	std::string Proposed(const char* Row) { return std::string("{\"proposal\": ") + Row + "}"; }

	OnlyWorlds::OWJsonValue ParseText(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return Parsed.ok ? Parsed.value : OnlyWorlds::OWJsonValue();
	}

	// JSON text equal as a value to Want, every number to the digit, and parsed at all.
	bool SameJsonText(const FString& Text, const std::string& Want)
	{
		const std::string Got = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Got));
		return Parsed.ok && OnlyWorlds::OWJson::Stringify(Parsed.value, true) == OnlyWorlds::OWJson::Stringify(ParseText(Want), true);
	}

	// A request as "<path after the root>|Name=Value; Name=Value", the headers in the order sent.
	FString RequestLine(const OnlyWorlds::OWRequest& Sent)
	{
		const std::string Prefix(KeelRoot);
		std::string Out = Sent.url.rfind(Prefix, 0) == 0 ? Sent.url.substr(Prefix.size()) : Sent.url;
		Out += "|";
		for (size_t I = 0; I < Sent.headers.size(); ++I)
			Out += (I ? "; " : "") + Sent.headers[I].first + "=" + Sent.headers[I].second;
		return OWConvert::Str(Out);
	}

	FString BodyLine(const OnlyWorlds::OWRequest& Sent)
	{
		return Sent.body ? OWConvert::Str(*Sent.body) : FString(TEXT("<none>"));
	}

	FOWClientConfig MakeConfig(const TCHAR* Key, const TCHAR* Pin = TEXT(""), const TCHAR* WorldId = TEXT(""))
	{
		FOWClientConfig Out;
		Out.ApiKey = Key;
		Out.ApiPin = Pin;
		Out.World = WorldId;
		Out.BaseUrl = OWConvert::Str(std::string(KeelRoot));
		return Out;
	}

	FOWClientConfig WriterConfig() { return MakeConfig(TEXT("ow_w_test"), TEXT("1234")); }

	using FMake = UOWClientNode* (*)(const FOWClientConfig&, UOWClientProposalNodesProbe*);

	struct FRun
	{
		std::shared_ptr<OWClientNodesTest::FFakeTransport> Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		TStrongObjectPtr<UOWClientProposalNodesProbe> Probe;
		TWeakObjectPtr<UOWClientNode> Node;

		FRun() : Probe(NewObject<UOWClientProposalNodesProbe>(GetTransientPackage())) {}
	};

	// A node made by Make, watched, given the fake (answering Fallback, after Script), and activated.
	std::shared_ptr<FRun> Launch(FMake Make, const FOWClientConfig& With, OnlyWorlds::OWResponse Fallback = Respond(200, "{}"),
		std::vector<OnlyWorlds::OWResponse> Script = {})
	{
		auto Into = std::make_shared<FRun>();
		Into->Fake->Fallback = std::move(Fallback);
		for (OnlyWorlds::OWResponse& R : Script) Into->Fake->Script.push_back(std::move(R));
		UOWClientNode* Made = Make(With, Into->Probe.Get());
		Into->Node = Made;
		Made->SetTransport(Into->Fake);
		Made->Activate();
		return Into;
	}

	// -- the nodes, each made and watched -------------------------------------------------------------------------------

	template <typename TNode>
	UOWClientNode* Watched(TNode* N, UOWClientProposalNodesProbe* P)
	{
		P->Watch(N);
		return N;
	}

	FOWProposalQuery EveryQuery(bool bWorld)
	{
		FOWProposalQuery Q;
		Q.State = TEXT("all");
		Q.Since = TEXT("2026-10-22T00:00:00Z");
		if (bWorld) Q.Element = TEXT("e1");
		Q.Limit = 5;
		Q.Cursor = TEXT("c1");
		Q.IfNoneMatch = TEXT("W/\"2026-10-22:1:0\"");
		return Q;
	}

	UOWClientNode* MakePropose(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::ProposeChange(With, TEXT("location"), TEXT("e1"), TEXT("{\"description\": \"A harbour town.\", \"world\": \"w\"}"),
			TEXT("Chapter 3 names the harbour."), TEXT("2026-11-21T16:00:00Z"), TEXT("k1")), P);
	}
	UOWClientNode* MakeProposeBare(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::ProposeChange(With, TEXT("location"), TEXT("e1"), TEXT("{\"name\": \"H\"}"), TEXT("x"), TEXT(""), TEXT("")), P);
	}
	UOWClientNode* MakeAccept(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::AcceptProposal(With, TEXT("location"), TEXT("e1"), TEXT("p1")), P);
	}
	UOWClientNode* MakeDecline(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::DeclineProposal(With, TEXT("location"), TEXT("e1"), TEXT("p1"), TEXT("Not canon.")), P);
	}
	UOWClientNode* MakeDeclineBare(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::DeclineProposal(With, TEXT("location"), TEXT("e1"), TEXT("p1"), TEXT("")), P);
	}
	UOWClientNode* MakeWithdraw(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposal::WithdrawProposal(With, TEXT("location"), TEXT("e1"), TEXT("p1")), P);
	}
	UOWClientNode* MakeList(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposals::ListElementProposals(With, TEXT("location"), TEXT("e1"), EveryQuery(false)), P);
	}
	UOWClientNode* MakeListBare(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposals::ListElementProposals(With, TEXT("location"), TEXT("e1"), FOWProposalQuery()), P);
	}
	UOWClientNode* MakeQueue(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposals::ListWorldProposals(With, EveryQuery(true)), P);
	}
	UOWClientNode* MakeQueueBare(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientProposals::ListWorldProposals(With, FOWProposalQuery()), P);
	}
	UOWClientNode* MakeKeep(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientWrite::AcceptNewElement(With, TEXT("character"), TEXT("e2")), P);
	}
	UOWClientNode* MakeDrop(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientWrite::DeclineNewElement(With, TEXT("character"), TEXT("e2"), TEXT("Not canon.")), P);
	}
	UOWClientNode* MakeDropBare(const FOWClientConfig& With, UOWClientProposalNodesProbe* P)
	{
		return Watched(UOWClientWrite::DeclineNewElement(With, TEXT("character"), TEXT("e2"), TEXT("")), P);
	}
}

namespace OWP = OWProposalNodesTest;

// -- what each node sends -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesRequestsTest, "OnlyWorlds.Client.ProposalNodes.Requests", OWP::ProposalTestFlags)
bool FOWClientProposalNodesRequestsTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Label;
		OWP::FMake Make;
		const TCHAR* Method;
		const TCHAR* Line;  // the path and query, then the headers in order
		const TCHAR* Body;  // as sent, or "<none>"
		int AnswerStatus;
		std::string AnswerBody;
	};
	const std::string Row = OWP::Proposed(OWP::ProposalRow);
	const std::string Page = std::string("{\"data\": [") + OWP::ProposalRow + "], \"has_more\": false}";
	const FCase Cases[] = {
		{ TEXT("ProposeChange"), &OWP::MakePropose, TEXT("POST"),
			TEXT("/location/e1/proposals|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; Idempotency-Key=k1; X-OW-Client=unreal-sdk"),
			TEXT("{\"fields\":{\"description\":\"A harbour town.\"},\"evidence\":\"Chapter 3 names the harbour.\",\"expires\":\"2026-11-21T16:00:00Z\"}"),
			201, Row },
		{ TEXT("ProposeChange, no options"), &OWP::MakeProposeBare, TEXT("POST"),
			TEXT("/location/e1/proposals|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"fields\":{\"name\":\"H\"},\"evidence\":\"x\"}"), 201, Row },
		{ TEXT("AcceptProposal"), &OWP::MakeAccept, TEXT("POST"),
			TEXT("/location/e1/proposals/p1/accept|API-Key=ow_w_test; API-Pin=1234; X-OW-Client=unreal-sdk"), TEXT("<none>"), 200,
			std::string("{\"proposal\": ") + OWP::ProposalRow + ", \"element\": " + OWP::ElementBody + "}" },
		{ TEXT("DeclineProposal"), &OWP::MakeDecline, TEXT("POST"),
			TEXT("/location/e1/proposals/p1/decline|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json"),
			TEXT("{\"reason\":\"Not canon.\"}"), 200, Row },
		{ TEXT("DeclineProposal, no reason"), &OWP::MakeDeclineBare, TEXT("POST"),
			TEXT("/location/e1/proposals/p1/decline|API-Key=ow_w_test; API-Pin=1234"), TEXT("<none>"), 200, Row },
		{ TEXT("WithdrawProposal"), &OWP::MakeWithdraw, TEXT("DELETE"), TEXT("/location/e1/proposals/p1|API-Key=ow_w_test; API-Pin=1234"),
			TEXT("<none>"), 204, "" },
		{ TEXT("ListElementProposals"), &OWP::MakeList, TEXT("GET"),
			TEXT("/location/e1/proposals?state=all&since=2026-10-22T00%3A00%3A00Z&limit=5&cursor=c1|API-Key=ow_w_test; If-None-Match=W/\"2026-10-22:1:0\""),
			TEXT("<none>"), 200, Page },
		{ TEXT("ListElementProposals, no query"), &OWP::MakeListBare, TEXT("GET"), TEXT("/location/e1/proposals?limit=100|API-Key=ow_w_test"),
			TEXT("<none>"), 200, Page },
		{ TEXT("ListWorldProposals"), &OWP::MakeQueue, TEXT("GET"),
			TEXT("/world/proposals?state=all&since=2026-10-22T00%3A00%3A00Z&element=e1&limit=5&cursor=c1|API-Key=ow_w_test; If-None-Match=W/\"2026-10-22:1:0\""),
			TEXT("<none>"), 200, Page },
		{ TEXT("ListWorldProposals, no query"), &OWP::MakeQueueBare, TEXT("GET"), TEXT("/world/proposals?limit=100|API-Key=ow_w_test"),
			TEXT("<none>"), 200, Page },
		{ TEXT("AcceptNewElement"), &OWP::MakeKeep, TEXT("POST"),
			TEXT("/character/e2/review|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"decision\":\"accept\"}"), 200, R"({"element": {"id": "e2", "name": "Ana"}})" },
		{ TEXT("DeclineNewElement"), &OWP::MakeDrop, TEXT("POST"),
			TEXT("/character/e2/review|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"decision\":\"decline\",\"reason\":\"Not canon.\"}"), 200, R"({"deleted": "e2"})" },
		{ TEXT("DeclineNewElement, no reason"), &OWP::MakeDropBare, TEXT("POST"),
			TEXT("/character/e2/review|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"decision\":\"decline\"}"), 200, R"({"deleted": "e2"})" },
	};
	for (const FCase& Case : Cases)
	{
		auto Run = OWP::Launch(Case.Make, OWP::WriterConfig(), OWP::Respond(Case.AnswerStatus, Case.AnswerBody));
		const FString What(Case.Label);
		if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) continue;
		const OnlyWorlds::OWRequest& R = Run->Fake->Sent[0];
		TestEqual(What + TEXT(": the method"), OWConvert::Str(R.method), FString(Case.Method));
		TestEqual(What + TEXT(": the URL and the headers"), OWP::RequestLine(R), FString(Case.Line));
		TestEqual(What + TEXT(": the body"), OWP::BodyLine(R), FString(Case.Body));
		TestEqual(What + TEXT(": OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(What + TEXT(": no OnFailure"), Run->Probe->Failures, 0);
	}

	// An agent's config with its own name: X-OW-Client on the propose, the accept and the review, X-OW-Agent on none.
	FOWClientConfig Agent = OWP::WriterConfig();
	Agent.Client = TEXT("my-game");
	Agent.bAgent = true;
	for (OWP::FMake Make : { &OWP::MakeProposeBare, &OWP::MakeAccept, &OWP::MakeKeep })
	{
		auto Run = OWP::Launch(Make, Agent);
		if (!TestEqual(TEXT("an agent's call: one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) continue;
		const OnlyWorlds::OWRequest& R = Run->Fake->Sent[0];
		const FString Line = OWP::RequestLine(R);
		TestFalse(TEXT("no X-OW-Agent: ") + Line, Line.Contains(TEXT("X-OW-Agent")));
		TestTrue(TEXT("its own name: ") + Line, Line.EndsWith(TEXT("X-OW-Client=my-game")));
	}
	return true;
}

// -- what a success gives -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesAnswersTest, "OnlyWorlds.Client.ProposalNodes.Answers", OWP::ProposalTestFlags)
bool FOWClientProposalNodesAnswersTest::RunTest(const FString& Parameters)
{
	// The row, every member.
	{
		auto Run = OWP::Launch(&OWP::MakeProposeBare, OWP::WriterConfig(), OWP::Respond(201, OWP::Proposed(OWP::ProposalRow)));
		const FOWProposal& P = Run->Probe->Proposal;
		TestEqual(TEXT("propose: OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("Kind"), P.Kind, FString(TEXT("proposal")));
		TestFalse(TEXT("bIsNewElement"), P.bIsNewElement);
		TestEqual(TEXT("Id"), P.Id, FString(TEXT("p1")));
		TestEqual(TEXT("ElementType"), P.ElementType, FString(TEXT("location")));
		TestEqual(TEXT("ElementId"), P.ElementId, FString(TEXT("e1")));
		TestEqual(TEXT("House"), P.House, FString(TEXT("skill")));
		TestEqual(TEXT("State"), P.State, FString(TEXT("open")));
		TestTrue(TEXT("FieldsJson, to the digit: ") + P.FieldsJson,
			OWP::SameJsonText(P.FieldsJson, R"({"description": "A harbour town.", "x_count": 9007199254740993})"));
		TestTrue(TEXT("FieldsJson keeps the digit as text"), P.FieldsJson.Contains(TEXT("9007199254740993")));
		TestTrue(TEXT("Omitted"), P.Omitted.Num() == 1 && P.Omitted[0].Equals(TEXT("supertype"), ESearchCase::CaseSensitive));
		TestTrue(TEXT("BaseJson: ") + P.BaseJson, OWP::SameJsonText(P.BaseJson, R"({"description": ""})"));
		TestEqual(TEXT("BaseSeq"), P.BaseSeq, int64(41));
		TestEqual(TEXT("Evidence"), P.Evidence, FString(TEXT("Chapter 3 names the harbour.")));
		TestEqual(TEXT("Expires"), P.Expires, FString(TEXT("2026-11-21T16:00:00Z")));
		TestEqual(TEXT("By"), P.By, FString(TEXT("m1")));
		TestEqual(TEXT("Via"), P.Via, FString(TEXT("api")));
		TestEqual(TEXT("Client"), P.Client, FString(TEXT("my-game")));
		TestEqual(TEXT("DecidedBy, null"), P.DecidedBy, FString());
		TestEqual(TEXT("DecidedAt, null"), P.DecidedAt, FString());
		TestEqual(TEXT("DeclineReason, null"), P.DeclineReason, FString());
		TestEqual(TEXT("CreatedAt"), P.CreatedAt, FString(TEXT("2026-10-22T16:00:00Z")));
		TestEqual(TEXT("UpdatedAt"), P.UpdatedAt, FString(TEXT("2026-10-22T16:05:00Z")));
		TestTrue(TEXT("Json, the row as read: ") + P.Json, OWP::SameJsonText(P.Json, OWP::ProposalRow));
		TestEqual(TEXT("ElementJson, \"\" on a propose"), Run->Probe->ElementJson, FString());
	}
	// The accept's element; the withdraw's nothing.
	{
		auto Run = OWP::Launch(&OWP::MakeAccept, OWP::WriterConfig(),
			OWP::Respond(200, std::string("{\"proposal\": ") + OWP::ProposalRow + ", \"element\": " + OWP::ElementBody + "}"));
		TestEqual(TEXT("accept: OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("accept: the row"), Run->Probe->Proposal.Id, FString(TEXT("p1")));
		TestTrue(TEXT("accept: the element as written: ") + Run->Probe->ElementJson, OWP::SameJsonText(Run->Probe->ElementJson, OWP::ElementBody));
		auto Gone = OWP::Launch(&OWP::MakeWithdraw, OWP::WriterConfig(), OWP::Respond(204, ""));
		TestEqual(TEXT("withdraw: OnSuccess once, got ") + Gone->Probe->Error.Line, Gone->Probe->Successes, 1);
		TestEqual(TEXT("withdraw: no row"), Gone->Probe->Proposal.Id, FString());
		TestEqual(TEXT("withdraw: no element"), Gone->Probe->ElementJson, FString());
	}
	// A page of both kinds, in keel's order, with its tag.
	{
		const std::string Both = std::string("{\"data\": [") + OWP::NewElementRow + ", " + OWP::ProposalRow +
			"], \"has_more\": true, \"next_cursor\": \"1761202800000000.e2\"}";
		auto Run = OWP::Launch(&OWP::MakeQueueBare, OWP::WriterConfig(), OWP::Respond(200, Both, { { "etag", "W/\"t:2:1\"" } }));
		const FOWProposalPage& Page = Run->Probe->Page;
		TestEqual(TEXT("queue: OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		if (TestEqual(TEXT("queue: two rows"), Page.Proposals.Num(), 2))
		{
			const FOWProposal& Marked = Page.Proposals[0];
			TestTrue(TEXT("the new element first"), Marked.bIsNewElement && Marked.Kind == TEXT("new_element"));
			TestEqual(TEXT("its id"), Marked.Id, FString(TEXT("e2")));
			TestEqual(TEXT("its state"), Marked.State, FString(TEXT("lapsed")));
			TestEqual(TEXT("its mark's evidence, a list, as JSON text"), Marked.Evidence, FString(TEXT("[\"e7\",\"e8\"]")));
			TestEqual(TEXT("its mark's expires, a number, as JSON text"), Marked.Expires, FString(TEXT("20991121")));
			TestEqual(TEXT("no fields"), Marked.FieldsJson, FString());
			TestEqual(TEXT("no omitted"), Marked.Omitted.Num(), 0);
			TestEqual(TEXT("no house"), Marked.House, FString());
			TestFalse(TEXT("the proposal second"), Page.Proposals[1].bIsNewElement);
		}
		TestTrue(TEXT("bHasMore"), Page.bHasMore);
		TestEqual(TEXT("NextCursor, opaque"), Page.NextCursor, FString(TEXT("1761202800000000.e2")));
		TestEqual(TEXT("ETag, as received"), Page.ETag, FString(TEXT("W/\"t:2:1\"")));
		TestFalse(TEXT("a 200 is never unchanged"), Page.bUnchanged);
	}
	// The poll: a 304 to a tagged call is unchanged, no rows.
	{
		auto Run = OWP::Launch(&OWP::MakeQueue, OWP::WriterConfig(), OWP::Respond(304, "", { { "ETag", "W/\"2026-10-22:1:0\"" } }));
		TestEqual(TEXT("a 304 to a poll: OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestTrue(TEXT("bUnchanged"), Run->Probe->Page.bUnchanged);
		TestEqual(TEXT("no rows"), Run->Probe->Page.Proposals.Num(), 0);
		TestEqual(TEXT("its ETag"), Run->Probe->Page.ETag, FString(TEXT("W/\"2026-10-22:1:0\"")));
		auto List = OWP::Launch(&OWP::MakeList, OWP::WriterConfig(), OWP::Respond(304, ""));
		TestTrue(TEXT("an element's list: bUnchanged"), List->Probe->Successes == 1 && List->Probe->Page.bUnchanged);
	}
	// The review's answer as a write's: the element kept, or nothing after a decline.
	{
		auto Keep = OWP::Launch(&OWP::MakeKeep, OWP::WriterConfig(), OWP::Respond(200, R"({"element": {"id": "e2", "name": "Ana", "x_count": 9007199254740993}})"));
		TestEqual(TEXT("AcceptNewElement: OnSuccess once, got ") + Keep->Probe->Error.Line, Keep->Probe->Successes, 1);
		TestTrue(TEXT("AcceptNewElement: the element: ") + Keep->Probe->Json,
			OWP::SameJsonText(Keep->Probe->Json, R"({"id": "e2", "name": "Ana", "x_count": 9007199254740993})"));
		auto Drop = OWP::Launch(&OWP::MakeDrop, OWP::WriterConfig(), OWP::Respond(200, R"({"deleted": "e2"})"));
		TestEqual(TEXT("DeclineNewElement: OnSuccess once, got ") + Drop->Probe->Error.Line, Drop->Probe->Successes, 1);
		TestEqual(TEXT("DeclineNewElement: \"\", as after a delete"), Drop->Probe->Json, FString());
	}
	return true;
}

// -- keel's refusals ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesErrorsTest, "OnlyWorlds.Client.ProposalNodes.Errors", OWP::ProposalTestFlags)
bool FOWClientProposalNodesErrorsTest::RunTest(const FString& Parameters)
{
	// A stale_proposal: its conflicts and the element now.
	const std::string Stale =
		R"({"error": {"type": "invalid_request", "code": "stale_proposal", "message": "moved"},)"
		R"( "conflicts": [{"field": "description", "base": "", "current": "A fishing village."}, {"field": 3}],)"
		R"( "current": {"id": "e1", "description": "A fishing village.", "change_seq": 43}})";
	auto Run = OWP::Launch(&OWP::MakeAccept, OWP::WriterConfig(), OWP::Respond(409, Stale));
	const FOWApiError& E = Run->Probe->Error;
	TestEqual(TEXT("stale: OnFailure once"), Run->Probe->Failures, 1);
	TestTrue(TEXT("stale: kind Api, 409"), E.Kind == EOWApiErrorKind::Api && E.Status == 409);
	TestEqual(TEXT("stale: its code"), E.Code, FString(TEXT("stale_proposal")));
	if (TestEqual(TEXT("stale: one conflict (one with no string field left out)"), E.Conflicts.Num(), 1))
	{
		TestEqual(TEXT("the field"), E.Conflicts[0].Field, FString(TEXT("description")));
		TestTrue(TEXT("its value then, as JSON text: ") + E.Conflicts[0].BaseJson, OWP::SameJsonText(E.Conflicts[0].BaseJson, "\"\""));
		TestTrue(TEXT("its value now, as JSON text: ") + E.Conflicts[0].CurrentJson,
			OWP::SameJsonText(E.Conflicts[0].CurrentJson, "\"A fishing village.\""));
	}
	TestTrue(TEXT("stale: Current, the element now: ") + E.Current,
		OWP::SameJsonText(E.Current, R"({"id": "e1", "description": "A fishing village.", "change_seq": 43})"));
	TestEqual(TEXT("stale: no row"), Run->Probe->Proposal.Id, FString());

	// Another 409 carries no conflicts.
	auto Other = OWP::Launch(&OWP::MakeAccept, OWP::WriterConfig(),
		OWP::Respond(409, R"({"error": {"code": "id_conflict"}, "conflicts": [{"field": "description"}]})"));
	TestEqual(TEXT("another 409: no conflicts"), Other->Probe->Error.Conflicts.Num(), 0);

	// keel's codes reach OnFailure as they came.
	struct FCode
	{
		OWP::FMake Make;
		int Status;
		const char* Code;
	};
	for (const FCode& C : { FCode{ &OWP::MakeProposeBare, 422, "too_many_open" }, FCode{ &OWP::MakeProposeBare, 422, "field_not_visible" },
			 FCode{ &OWP::MakeDecline, 403, "own_proposal" }, FCode{ &OWP::MakeKeep, 422, "not_proposed" }, FCode{ &OWP::MakeDrop, 403, "proposed_only" } })
	{
		auto Coded = OWP::Launch(C.Make, OWP::WriterConfig(), OWP::Respond(C.Status, std::string("{\"error\": {\"code\": \"") + C.Code + "\"}}"));
		const FString Code = OWConvert::Str(C.Code);
		TestEqual(Code + TEXT(": OnFailure once"), Coded->Probe->Failures, 1);
		TestTrue(Code + TEXT(": its status and code"), Coded->Probe->Error.Status == C.Status && Coded->Probe->Error.Code == Code);
	}

	// A 304 to a list that sent no tag is an error, never an unchanged page.
	auto Untagged = OWP::Launch(&OWP::MakeQueueBare, OWP::WriterConfig(), OWP::Respond(304, ""));
	TestEqual(TEXT("an untagged 304: OnFailure once"), Untagged->Probe->Failures, 1);
	TestTrue(TEXT("an untagged 304: kind Api, status 304"), Untagged->Probe->Error.Kind == EOWApiErrorKind::Api && Untagged->Probe->Error.Status == 304);
	TestFalse(TEXT("an untagged 304: never unchanged"), Untagged->Probe->Page.bUnchanged);

	// A review's 200 without the key its decision settles is never read as settled.
	auto Unsettled = OWP::Launch(&OWP::MakeKeep, OWP::WriterConfig(), OWP::Respond(200, R"({"deleted": "e2"})"));
	TestTrue(TEXT("an accept answered with deleted: kind Answer"), Unsettled->Probe->Failures == 1 && Unsettled->Probe->Error.Kind == EOWApiErrorKind::Answer);
	return true;
}

// -- refused, nothing sent ----------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesRefusalsTest, "OnlyWorlds.Client.ProposalNodes.Refusals", OWP::ProposalTestFlags)
bool FOWClientProposalNodesRefusalsTest::RunTest(const FString& Parameters)
{
	auto Refused = [this](const FString& What, const std::shared_ptr<OWP::FRun>& Run, const TCHAR* Starts)
	{
		TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
		TestEqual(What + TEXT(": OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), Run->Probe->Successes, 0);
		TestTrue(What + TEXT(": kind Refused"), Run->Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestTrue(What + TEXT(": the words, got ") + Run->Probe->Error.Message, Run->Probe->Error.Message.StartsWith(Starts));
		TestTrue(What + TEXT(": it says nothing was sent"), Run->Probe->Error.Message.EndsWith(TEXT("Nothing was sent.")));
	};
	// An account token and no key, on every node.
	const OWP::FMake All[] = { &OWP::MakePropose, &OWP::MakeAccept, &OWP::MakeDecline, &OWP::MakeWithdraw, &OWP::MakeListBare,
		&OWP::MakeQueueBare, &OWP::MakeKeep, &OWP::MakeDrop };
	int32 Count = 0;
	for (const OWP::FMake Make : All)
	{
		for (const TCHAR* Key : { TEXT("ow_a_test"), TEXT("") })
		{
			auto Run = OWP::Launch(Make, OWP::MakeConfig(Key, TEXT(""), TEXT("w1")));
			const FString What = FString::Printf(TEXT("node %d on %s"), Count / 2, *Key ? Key : TEXT("no key"));
			TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
			TestTrue(What + TEXT(": refused"), Run->Probe->Failures == 1 && Run->Probe->Error.Kind == EOWApiErrorKind::Refused);
			++Count;
		}
	}
	TestEqual(TEXT("eight nodes, two credentials"), Count, 16);

	// A ProposalId that is not one path segment.
	Refused(TEXT("a ProposalId with a slash"),
		OWP::Launch([](const FOWClientConfig& With, UOWClientProposalNodesProbe* P) -> UOWClientNode*
		{
			return OWP::Watched(UOWClientProposal::AcceptProposal(With, TEXT("location"), TEXT("e1"), TEXT("p1/../hide")), P);
		}, OWP::WriterConfig()),
		TEXT("pid \"p1/../hide\" is not one path segment."));
	// Element on an element's own list.
	Refused(TEXT("Element on an element's list"),
		OWP::Launch([](const FOWClientConfig& With, UOWClientProposalNodesProbe* P) -> UOWClientNode*
		{
			FOWProposalQuery Q;
			Q.Element = TEXT("e1");
			return OWP::Watched(UOWClientProposals::ListElementProposals(With, TEXT("location"), TEXT("e1"), Q), P);
		}, OWP::WriterConfig()),
		TEXT("ListProposals: element filters the world's queue"));
	// The node's own word: FieldsJson that is not JSON; the core's: a known-of stub, a list, a value of another kind.
	struct FBad
	{
		const TCHAR* Fields;
		const TCHAR* Starts;
	};
	for (const FBad& B : { FBad{ TEXT("{\"name\": "), TEXT("propose: FieldsJson is not JSON text") },
			 FBad{ TEXT("{\"name\": \"\", \"known_of\": true}"), TEXT("propose: this body is a known-of stub") },
			 FBad{ TEXT("[]"), TEXT("propose: fields must be an object") }, FBad{ TEXT(""), TEXT("propose: fields must be an object") },
			 FBad{ TEXT("{\"height\": 1.5}"), TEXT("propose: character.height is an integer field; 1.5 would be stored") } })
	{
		auto Run = std::make_shared<OWP::FRun>();
		UOWClientProposal* N = UOWClientProposal::ProposeChange(OWP::WriterConfig(), TEXT("character"), TEXT("e1"), B.Fields, TEXT("x"), TEXT(""), TEXT(""));
		Run->Probe->Watch(N);
		Run->Node = N;
		N->SetTransport(Run->Fake);
		N->Activate();
		Refused(FString(TEXT("FieldsJson ")) + B.Fields, Run, B.Starts);
	}
	return true;
}

// -- one answer only ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesOnceTest, "OnlyWorlds.Client.ProposalNodes.Once", OWP::ProposalTestFlags)
bool FOWClientProposalNodesOnceTest::RunTest(const FString& Parameters)
{
	// A transport that answers each request twice: each node answers once, is let go, and a second activation sends
	// nothing.
	const OWP::FMake All[] = { &OWP::MakePropose, &OWP::MakeAccept, &OWP::MakeDecline, &OWP::MakeWithdraw, &OWP::MakeList, &OWP::MakeQueue,
		&OWP::MakeKeep, &OWP::MakeDrop };
	int32 I = 0;
	for (const OWP::FMake Make : All)
	{
		const FString What = FString::Printf(TEXT("node %d"), I++);
		auto Run = std::make_shared<OWP::FRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = OWP::Respond(200, "{}");
		UOWClientNode* Made = Make(OWP::WriterConfig(), Run->Probe.Get());
		Run->Node = Made;
		Made->SetTransport(Run->Fake);
		Made->Activate();
		TestEqual(What + TEXT(": one answer"), Run->Probe->Answers(), 1);
		TestFalse(What + TEXT(": let go"), Made->IsRooted());
		Made->Activate();
		TestEqual(What + TEXT(": a second activation sends nothing"), static_cast<int32>(Run->Fake->Sent.size()), 1);
		TestEqual(What + TEXT(": and answers nothing"), Run->Probe->Answers(), 1);
	}
	return true;
}

// -- one node as a graph makes it ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesReflectionTest, "OnlyWorlds.Client.ProposalNodes.Reflection", OWP::ProposalTestFlags)
bool FOWClientProposalNodesReflectionTest::RunTest(const FString& Parameters)
{
	// ProposeChange found by name, its flags, its frame from the UFunction, the config set member by member, each other
	// parameter by its property, ProcessEvent on the class default object; the node read from the return; the delegates
	// bound; Activate found by name and called by ProcessEvent, as UK2Node_AsyncAction's expansion calls it.
	UFunction* Factory = UOWClientProposal::StaticClass()->FindFunctionByName(TEXT("ProposeChange"));
	if (!TestNotNull(TEXT("UFUNCTION ProposeChange"), Factory)) return false;
	TestTrue(TEXT("ProposeChange: a static, public, native Blueprint function"),
		Factory->HasAllFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable));
#if WITH_METADATA
	TestTrue(TEXT("ProposeChange: the async node's, not a plain call"), Factory->HasMetaData(TEXT("BlueprintInternalUseOnly")));
#endif

	uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Factory->ParmsSize, 1), Factory->GetMinAlignment()));
	Factory->InitializeStruct(Frame);
	UOWClientProposal* Made = nullptr;
	FStructProperty* ConfigParam = CastField<FStructProperty>(Factory->FindPropertyByName(TEXT("Config")));
	auto SetText = [this, Factory, Frame](const TCHAR* Param, const FString& Value)
	{
		FStrProperty* P = CastField<FStrProperty>(Factory->FindPropertyByName(Param));
		if (TestNotNull(FString::Printf(TEXT("ProposeChange has an FString parameter %s"), Param), P))
			*P->ContainerPtrToValuePtr<FString>(Frame) = Value;
	};
	if (TestTrue(TEXT("ProposeChange has an FOWClientConfig parameter Config"), ConfigParam && ConfigParam->Struct == FOWClientConfig::StaticStruct()))
	{
		void* Data = ConfigParam->ContainerPtrToValuePtr<void>(Frame);
		const UScriptStruct* S = FOWClientConfig::StaticStruct();
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("ApiKey"), TEXT("ow_r_test"));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("BaseUrl"), OWConvert::Str(std::string(OWP::KeelRoot)));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("Client"), TEXT("my-game"));
		SetText(TEXT("Type"), TEXT("location"));
		SetText(TEXT("Id"), TEXT("e1"));
		SetText(TEXT("FieldsJson"), TEXT("{\"name\": \"Harbour\"}"));
		SetText(TEXT("Evidence"), TEXT("a source"));
		SetText(TEXT("Expires"), TEXT(""));
		SetText(TEXT("IdempotencyKey"), TEXT(""));
		UOWClientProposal::StaticClass()->GetDefaultObject()->ProcessEvent(Factory, Frame);
		const FObjectProperty* Ret = CastField<FObjectProperty>(Factory->GetReturnProperty());
		if (TestTrue(TEXT("ProposeChange returns a UOWClientProposal"), Ret && Ret->PropertyClass == UOWClientProposal::StaticClass()))
			Made = Cast<UOWClientProposal>(Ret->GetObjectPropertyValue_InContainer(Frame));
	}
	Factory->DestroyStruct(Frame);
	FMemory::Free(Frame);
	if (!TestNotNull(TEXT("the node"), Made)) return false;

	const FMulticastDelegateProperty* Success = CastField<FMulticastDelegateProperty>(UOWClientProposal::StaticClass()->FindPropertyByName(TEXT("OnSuccess")));
	if (TestNotNull(TEXT("OnSuccess, a multicast delegate"), Success))
	{
		const UFunction* Sig = Success->SignatureFunction;
		const FStructProperty* RowPin = Sig ? CastField<FStructProperty>(Sig->FindPropertyByName(TEXT("Proposal"))) : nullptr;
		TestTrue(TEXT("OnSuccess's Proposal, an FOWProposal"), RowPin && RowPin->Struct == FOWProposal::StaticStruct());
		TestTrue(TEXT("OnSuccess's ElementJson, a string"), Sig && CastField<FStrProperty>(Sig->FindPropertyByName(TEXT("ElementJson"))) != nullptr);
	}
	const FStructProperty* ConflictsOf = nullptr;
	if (const FArrayProperty* A = CastField<FArrayProperty>(FOWApiError::StaticStruct()->FindPropertyByName(TEXT("Conflicts"))))
		ConflictsOf = CastField<FStructProperty>(A->Inner);
	TestTrue(TEXT("FOWApiError's Conflicts, a list of FOWProposalConflict"), ConflictsOf && ConflictsOf->Struct == FOWProposalConflict::StaticStruct());

	auto Run = std::make_shared<OWP::FRun>();
	Run->Fake->bLater = true;
	Run->Fake->Fallback = OWP::Respond(201, OWP::Proposed(OWP::ProposalRow));
	Run->Node = Made;
	Made->SetTransport(Run->Fake);
	Run->Probe->Watch(Made);
	UFunction* Activate = Made->FindFunction(TEXT("Activate"));
	if (!TestNotNull(TEXT("UFUNCTION Activate"), Activate)) return false;
	Made->ProcessEvent(Activate, nullptr);
	TestEqual(TEXT("not answered inside Activate"), Run->Probe->Answers(), 0);
	if (TestEqual(TEXT("one request"), static_cast<int32>(Run->Fake->Sent.size()), 1))
	{
		const OnlyWorlds::OWRequest& R = Run->Fake->Sent[0];
		TestEqual(TEXT("the request"), OWP::RequestLine(R),
			FString(TEXT("/location/e1/proposals|API-Key=ow_r_test; Content-Type=application/json; X-OW-Client=my-game")));
		TestEqual(TEXT("the body"), OWP::BodyLine(R), FString(TEXT("{\"fields\":{\"name\":\"Harbour\"},\"evidence\":\"a source\"}")));
	}

	auto Frames = std::make_shared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Frames]()
	{
		++*Frames;
		Run->Fake->Pump();
		if (Run->Probe->Answers() == 0 && *Frames < OWP::FrameLimit) return false;

		TestEqual(TEXT("OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("no OnFailure"), Run->Probe->Failures, 0);
		TestEqual(TEXT("the row"), Run->Probe->Proposal.Id, FString(TEXT("p1")));
		TestTrue(TEXT("let go once answered"), Run->Node.IsValid() && !Run->Node->IsRooted());
		return true;
	}));
	return true;
}

// -- the pins a graph's node gets (editor only) -----------------------------------------------------------------------

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientProposalNodesPinsTest, "OnlyWorlds.Client.ProposalNodes.Pins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FOWClientProposalNodesPinsTest::RunTest(const FString& Parameters)
{
	const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(TEXT("OWClientProposalNodesProbe")));
	TStrongObjectPtr<UBlueprint> BP(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Unique,
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None));
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP.Get()) : nullptr;
	if (!TestNotNull(TEXT("a Blueprint with an event graph"), Graph)) return false;

	const FName PinString = UEdGraphSchema_K2::PC_String, PinStruct = UEdGraphSchema_K2::PC_Struct, PinExec = UEdGraphSchema_K2::PC_Exec;
	struct FPin
	{
		const TCHAR* Name;
		FName Category;
		const UScriptStruct* Of;
	};
	struct FNode
	{
		UClass* Class;
		const TCHAR* Factory;
		TArray<FPin> In;
		TArray<FPin> Out;
	};
	const FPin ConfigPin{ TEXT("Config"), PinStruct, FOWClientConfig::StaticStruct() };
	const FPin ErrorPin{ TEXT("Error"), PinStruct, FOWApiError::StaticStruct() };
	const FPin TypePin{ TEXT("Type"), PinString, nullptr };
	const FPin IdPin{ TEXT("Id"), PinString, nullptr };
	const FPin RowIdPin{ TEXT("ProposalId"), PinString, nullptr };
	const FPin ReasonPin{ TEXT("Reason"), PinString, nullptr };
	const FPin QueryPin{ TEXT("Query"), PinStruct, FOWProposalQuery::StaticStruct() };
	const TArray<FPin> ActOut = { { TEXT("Proposal"), PinStruct, FOWProposal::StaticStruct() }, { TEXT("ElementJson"), PinString, nullptr }, ErrorPin };
	const TArray<FPin> PageOut = { { TEXT("Page"), PinStruct, FOWProposalPage::StaticStruct() }, ErrorPin };
	const TArray<FPin> WriteOut = { { TEXT("Json"), PinString, nullptr }, ErrorPin };
	const TArray<FNode> Nodes = {
		{ UOWClientProposal::StaticClass(), TEXT("ProposeChange"),
			{ ConfigPin, TypePin, IdPin, { TEXT("FieldsJson"), PinString, nullptr }, { TEXT("Evidence"), PinString, nullptr },
			  { TEXT("Expires"), PinString, nullptr }, { TEXT("IdempotencyKey"), PinString, nullptr } }, ActOut },
		{ UOWClientProposal::StaticClass(), TEXT("AcceptProposal"), { ConfigPin, TypePin, IdPin, RowIdPin }, ActOut },
		{ UOWClientProposal::StaticClass(), TEXT("DeclineProposal"), { ConfigPin, TypePin, IdPin, RowIdPin, ReasonPin }, ActOut },
		{ UOWClientProposal::StaticClass(), TEXT("WithdrawProposal"), { ConfigPin, TypePin, IdPin, RowIdPin }, ActOut },
		{ UOWClientProposals::StaticClass(), TEXT("ListElementProposals"), { ConfigPin, TypePin, IdPin, QueryPin }, PageOut },
		{ UOWClientProposals::StaticClass(), TEXT("ListWorldProposals"), { ConfigPin, QueryPin }, PageOut },
		{ UOWClientWrite::StaticClass(), TEXT("AcceptNewElement"), { ConfigPin, TypePin, IdPin }, WriteOut },
		{ UOWClientWrite::StaticClass(), TEXT("DeclineNewElement"), { ConfigPin, TypePin, IdPin, ReasonPin }, WriteOut },
	};

	auto Check = [this](const FString& What, const UEdGraphPin* Pin, const FPin& Want)
	{
		if (!TestNotNull(What + TEXT(": a pin"), Pin)) return;
		TestEqual(What + TEXT(": its category"), Pin->PinType.PinCategory, Want.Category);
		TestTrue(What + TEXT(": not a list"), Pin->PinType.ContainerType == EPinContainerType::None);
		if (Want.Of) TestTrue(What + TEXT(": its struct"), Pin->PinType.PinSubCategoryObject.Get() == Want.Of);
	};

	for (const FNode& N : Nodes)
	{
		UFunction* F = N.Class->FindFunctionByName(N.Factory);
		if (!TestNotNull(FString::Printf(TEXT("UFUNCTION %s"), N.Factory), F)) continue;
		FGraphNodeCreator<UK2Node_AsyncAction> Creator(*Graph);
		UK2Node_AsyncAction* Node = Creator.CreateNode(false);
		Node->InitializeProxyFromFunction(F);
		Creator.Finalize();

		for (const FPin& P : N.In) Check(FString::Printf(TEXT("%s in %s"), N.Factory, P.Name), Node->FindPin(FName(P.Name), EGPD_Input), P);
		for (const FPin& P : N.Out) Check(FString::Printf(TEXT("%s out %s"), N.Factory, P.Name), Node->FindPin(FName(P.Name), EGPD_Output), P);
		for (const TCHAR* Then : { TEXT("OnSuccess"), TEXT("OnFailure") })
		{
			const UEdGraphPin* Pin = Node->FindPin(FName(Then), EGPD_Output);
			TestTrue(FString::Printf(TEXT("%s: an exec pin %s"), N.Factory, Then), Pin && Pin->PinType.PinCategory == PinExec);
		}
		int32 Data = 0;
		for (const UEdGraphPin* Pin : Node->Pins) Data += Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != PinExec;
		TestEqual(FString::Printf(TEXT("%s: its data out pins, no more"), N.Factory), Data, N.Out.Num());
	}
	return true;
}

#endif  // WITH_EDITOR

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
