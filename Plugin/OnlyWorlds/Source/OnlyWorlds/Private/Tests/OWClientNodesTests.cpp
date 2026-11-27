// The client's async Blueprint nodes (OWClientNodes.h), through a fake transport that records each request and answers
// from a script: no test here touches the network. Each node is made by its factory, its OnSuccess and OnFailure bound
// to a listener (OWClientNodesProbe.h) as a graph's async node binds them, given the fake (SetTransport, the C++ seam),
// and activated. What each node sends on each credential, its options, the shape of each answer, keel's error envelope
// and the core's refusals reaching OnFailure, one answer only, the node kept alive while its request is in flight and
// let go after, one node made and activated through reflection as a graph does (its members text then read by
// UOWMessages), and, in the editor, the pins a graph's node gets.
//
// A fake that answers inside Send drives the request tests; the latent tests hold each answer for a later frame and
// wait a frame at a time, as the HTTP module completes on the game thread (OWHttpTransportTests.cpp waits the same way).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "OWClient.h"
#include "OWClientNodes.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWMessagesLibrary.h"
#include "OWTransport.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWClientNodesProbe.h"
#include "OWFakeTransport.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AsyncAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OnlyWorlds::OWRequest;
	using OnlyWorlds::OWResponse;
	using OnlyWorlds::OWResponseHandler;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// The frames a latent test waits for its answers before it fails.
	constexpr int32 WaitFrames = 200;

	const char* const KeelBase = "http://keel.test/api/v2";

	// -- the fake transport (OWFakeTransport.h, shared with the write nodes' tests) ----------------------------------

	using FFake = OWClientNodesTest::FFakeTransport;

	OWResponse Answer(int Status, const char* Body)
	{
		OWResponse R;
		R.status = Status;
		R.body = Body;
		return R;
	}

	OWResponse Ok(const char* Body) { return Answer(200, Body); }

	// -- reading what came back ------------------------------------------------------------------------------------------

	OWJsonValue J(const char* Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	// JSON text equal as a value to Want (key order and number texts kept), and parsed at all.
	bool SameJson(const FString& Text, const char* Want)
	{
		const std::string Got = Utf8(Text);
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Got));
		return R.ok && R.value == J(Want);
	}

	// A request as "<path after the base>|Name=Value; Name=Value", the headers in the order sent.
	FString Shown(const OWRequest& R)
	{
		const std::string Prefix(KeelBase);
		std::string Out = R.url.rfind(Prefix, 0) == 0 ? R.url.substr(Prefix.size()) : R.url;
		Out += "|";
		for (size_t I = 0; I < R.headers.size(); ++I) Out += (I ? "; " : "") + R.headers[I].first + "=" + R.headers[I].second;
		return Str(Out);
	}

	FOWClientConfig Config(const TCHAR* Key, const TCHAR* Pin = TEXT(""), const TCHAR* World = TEXT(""))
	{
		FOWClientConfig C;
		C.ApiKey = Key;
		C.ApiPin = Pin;
		C.World = World;
		C.BaseUrl = Str(std::string(KeelBase) + "/");  // the trailing slash trimmed by the core
		return C;
	}

	FOWClientConfig ReadKey() { return Config(TEXT("ow_r_test"), TEXT("1234")); }

	// -- the nodes, each made and watched -----------------------------------------------------------------------------

	using FMake = UOWClientNode* (*)(const FOWClientConfig&, UOWClientNodesProbe*);

	struct FRun
	{
		std::shared_ptr<FFake> Fake = std::make_shared<FFake>();
		TStrongObjectPtr<UOWClientNodesProbe> Probe;
		TWeakObjectPtr<UOWClientNode> Node;

		FRun() : Probe(NewObject<UOWClientNodesProbe>(GetTransientPackage())) {}
	};

	// A node made by Make, watched, given the fake (unless bOwnTransport: the node's default), and activated.
	std::shared_ptr<FRun> Start(FMake Make, const FOWClientConfig& C, std::shared_ptr<FRun> Run = nullptr, bool bOwnTransport = false)
	{
		if (!Run) Run = std::make_shared<FRun>();
		UOWClientNode* Node = Make(C, Run->Probe.Get());
		Run->Node = Node;
		if (!bOwnTransport) Node->SetTransport(Run->Fake);
		Node->Activate();
		return Run;
	}

	UOWClientNode* MakeHealth(const FOWClientConfig& C, UOWClientNodesProbe* P) { UOWClientRead* N = UOWClientRead::ReadHealth(C); P->Watch(N); return N; }
	UOWClientNode* MakeWorld(const FOWClientConfig& C, UOWClientNodesProbe* P) { UOWClientRead* N = UOWClientRead::ReadWorld(C); P->Watch(N); return N; }
	UOWClientNode* MakeMe(const FOWClientConfig& C, UOWClientNodesProbe* P) { UOWClientRead* N = UOWClientRead::ReadMe(C); P->Watch(N); return N; }
	UOWClientNode* MakeMembers(const FOWClientConfig& C, UOWClientNodesProbe* P) { UOWClientRead* N = UOWClientRead::ReadMembers(C); P->Watch(N); return N; }
	UOWClientNode* MakeElement(const FOWClientConfig& C, UOWClientNodesProbe* P)
	{
		UOWClientRead* N = UOWClientRead::ReadElement(C, TEXT("character"), TEXT("c1"), {}, {}, TEXT(""));
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeList(const FOWClientConfig& C, UOWClientNodesProbe* P)
	{
		UOWClientList* N = UOWClientList::ListElements(C, TEXT("character"), FOWListOptions());
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeListAll(const FOWClientConfig& C, UOWClientNodesProbe* P)
	{
		UOWClientListAll* N = UOWClientListAll::ListAllElements(C, TEXT("character"), FOWListOptions());
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeChanges(const FOWClientConfig& C, UOWClientNodesProbe* P)
	{
		UOWClientChanges* N = UOWClientChanges::ReadChanges(C, TEXT(""), TEXT(""));
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeHead(const FOWClientConfig& C, UOWClientNodesProbe* P) { UOWClientChanges* N = UOWClientChanges::ReadChangesHead(C); P->Watch(N); return N; }

	// -- the hand-made world, for the members text ------------------------------------------------------------------------

	const TCHAR* const Mara = TEXT("019a3b10-0000-7000-8000-0000000000a2");  // membership m2
	const TCHAR* const Wren = TEXT("019a3b10-0000-7000-8000-0000000000a3");  // membership m3

	const char* const MembersRows = R"([{"id": null, "kind": "owner", "character": "019a3b10-0000-7000-8000-0000000000a1"},
		{"id": "m2", "kind": "person", "character": "019a3b10-0000-7000-8000-0000000000a2"},
		{"id": "m3", "kind": "agent", "character": "019a3b10-0000-7000-8000-0000000000a3"}])";

	std::string MembersBody() { return std::string("{\"data\": ") + MembersRows + "}"; }

	// c3 written by m2 as Mara; c2 written by m2 claiming Wren, then changed by m3.
	UOWWorld* MessagesWorld()
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f000"));
		Native->Add("narrative", J(R"({"id": "c3", "type": "narrative", "supertype": "Message", "name": "Third",
			"narrator": "019a3b10-0000-7000-8000-0000000000a2", "characters": [], "created_at": "2026-10-26T09:03:00Z",
			"created_by": "m2"})"));
		Native->Add("narrative", J(R"({"id": "c2", "type": "narrative", "supertype": "Message", "name": "Second",
			"narrator": "019a3b10-0000-7000-8000-0000000000a3", "characters": [], "created_at": "2026-10-26T09:02:00Z",
			"created_by": "m2", "changed_by": "m3"})"));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}
}

// -- what each node sends, on each credential ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesRequestsTest, "OnlyWorlds.Client.Nodes.Requests", OWTestFlags)
bool FOWClientNodesRequestsTest::RunTest(const FString& Parameters)
{
	struct FCredential
	{
		const TCHAR* Name;
		const TCHAR* Key;
		const TCHAR* Pin;
		const TCHAR* World;
	};
	constexpr int32 N = 7;
	const FCredential Credentials[N] = {
		{ TEXT("a write key"), TEXT("ow_w_test"), TEXT("1234"), TEXT("") },
		{ TEXT("a read key"), TEXT("ow_r_test"), TEXT("1234"), TEXT("") },
		{ TEXT("a legacy key"), TEXT("0000000001"), TEXT("1234"), TEXT("") },
		{ TEXT("an account token with a world"), TEXT("ow_a_test"), TEXT("1234"), TEXT("w1") },
		{ TEXT("an account token without one"), TEXT("ow_a_test"), TEXT("1234"), TEXT("") },
		{ TEXT("no key, a world"), TEXT(""), TEXT(""), TEXT("w1") },
		{ TEXT("no key, no world"), TEXT(""), TEXT(""), TEXT("") },
	};

	// What each node sends on each credential, in Credentials' order: the path after the base and the headers in order;
	// nullptr where the core refuses before sending.
	struct FNodeCase
	{
		const TCHAR* Name;
		FMake Make;
		const char* Body;  // the 200 the fake answers
		const TCHAR* Want[N];
	};
	const FNodeCase Cases[] = {
		{ TEXT("ReadHealth"), &MakeHealth, R"({"status": "ok"})",
			{ TEXT("/health/|"), TEXT("/health/|"), TEXT("/health/|"), TEXT("/health/|"), TEXT("/health/|"), TEXT("/health/|"), TEXT("/health/|") } },
		{ TEXT("ReadWorld"), &MakeWorld, R"({"name": "W"})",
			{ TEXT("/world/|API-Key=ow_w_test"), TEXT("/world/|API-Key=ow_r_test"), TEXT("/world/|API-Key=0000000001; API-Pin=1234"),
			  TEXT("/world/?world=w1|Authorization=Bearer ow_a_test"), nullptr, TEXT("/world/?world=w1|"), nullptr } },
		{ TEXT("ReadMe"), &MakeMe, R"({"scope": "read"})",
			{ TEXT("/me/|API-Key=ow_w_test; API-Pin=1234"), TEXT("/me/|API-Key=ow_r_test; API-Pin=1234"),
			  TEXT("/me/|API-Key=0000000001; API-Pin=1234"), nullptr, nullptr, nullptr, nullptr } },
		{ TEXT("ReadMembers"), &MakeMembers, R"({"data": []})",
			{ TEXT("/members/|API-Key=ow_w_test"), TEXT("/members/|API-Key=ow_r_test"), TEXT("/members/|API-Key=0000000001; API-Pin=1234"),
			  TEXT("/members/?world=w1|Authorization=Bearer ow_a_test"), nullptr, nullptr, nullptr } },
		{ TEXT("ReadElement"), &MakeElement, R"({"id": "c1"})",
			{ TEXT("/character/c1/|API-Key=ow_w_test"), TEXT("/character/c1/|API-Key=ow_r_test"),
			  TEXT("/character/c1/|API-Key=0000000001; API-Pin=1234"), TEXT("/character/c1/?world=w1|Authorization=Bearer ow_a_test"),
			  nullptr, TEXT("/character/c1/?world=w1|"), nullptr } },
		{ TEXT("ListElements"), &MakeList, R"({"data": [], "has_more": false})",
			{ TEXT("/character/?limit=100|API-Key=ow_w_test"), TEXT("/character/?limit=100|API-Key=ow_r_test"),
			  TEXT("/character/?limit=100|API-Key=0000000001; API-Pin=1234"),
			  TEXT("/character/?limit=100&world=w1|Authorization=Bearer ow_a_test"), nullptr, TEXT("/character/?limit=100&world=w1|"), nullptr } },
		{ TEXT("ListAllElements"), &MakeListAll, R"({"data": [], "has_more": false})",
			{ TEXT("/character/?limit=100|API-Key=ow_w_test"), TEXT("/character/?limit=100|API-Key=ow_r_test"),
			  TEXT("/character/?limit=100|API-Key=0000000001; API-Pin=1234"),
			  TEXT("/character/?limit=100&world=w1|Authorization=Bearer ow_a_test"), nullptr, TEXT("/character/?limit=100&world=w1|"), nullptr } },
		{ TEXT("ReadChanges"), &MakeChanges, R"({"head": 1, "cursor": "1", "changes": []})",
			{ TEXT("/changes/?limit=100|API-Key=ow_w_test"), TEXT("/changes/?limit=100|API-Key=ow_r_test"),
			  TEXT("/changes/?limit=100|API-Key=0000000001; API-Pin=1234"), nullptr, nullptr, nullptr, nullptr } },
		{ TEXT("ReadChangesHead"), &MakeHead, R"({"head": 1, "cursor": "1"})",
			{ TEXT("/changes/?head=true|API-Key=ow_w_test"), TEXT("/changes/?head=true|API-Key=ow_r_test"),
			  TEXT("/changes/?head=true|API-Key=0000000001; API-Pin=1234"), nullptr, nullptr, nullptr, nullptr } },
	};

	int32 Sent = 0, Refused = 0;
	for (const FNodeCase& Case : Cases)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const FCredential& Cred = Credentials[I];
			const FString What = FString::Printf(TEXT("%s on %s"), Case.Name, Cred.Name);
			auto Run = std::make_shared<FRun>();
			Run->Fake->Fallback = Ok(Case.Body);
			Start(Case.Make, Config(Cred.Key, Cred.Pin, Cred.World), Run);
			UOWClientNodesProbe& P = *Run->Probe;
			if (Case.Want[I])
			{
				++Sent;
				if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) continue;
				const OWRequest& R = Run->Fake->Sent[0];
				TestEqual(What + TEXT(": GET"), Str(R.method), FString(TEXT("GET")));
				TestFalse(What + TEXT(": no body"), R.body.has_value());
				TestEqual(What + TEXT(": the URL and the headers"), Shown(R), FString(Case.Want[I]));
				TestEqual(What + TEXT(": OnSuccess once"), P.Successes, 1);
				TestEqual(What + TEXT(": no OnFailure"), P.Failures, 0);
			}
			else
			{
				++Refused;
				TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
				TestEqual(What + TEXT(": OnFailure once"), P.Failures, 1);
				TestEqual(What + TEXT(": no OnSuccess"), P.Successes, 0);
				TestTrue(What + TEXT(": a refusal"), P.Error.Kind == EOWApiErrorKind::Refused);
				TestEqual(What + TEXT(": status 0"), P.Error.Status, 0);
				TestTrue(What + TEXT(": the refusal's line, got ") + P.Error.Line, P.Error.Line.StartsWith(TEXT("OnlyWorlds refused: ")));
				TestTrue(What + TEXT(": the core's words"), P.Error.Message.EndsWith(TEXT("Nothing was sent.")));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("REQUESTS %d node calls: %d sent as the core sends them, %d refused before sending"), Sent + Refused, Sent, Refused));
	TestEqual(TEXT("the matrix: sent"), Sent, 40);
	TestEqual(TEXT("the matrix: refused"), Refused, 23);
	return true;
}

// -- the options each node sends ----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesOptionsTest, "OnlyWorlds.Client.Nodes.Options", OWTestFlags)
bool FOWClientNodesOptionsTest::RunTest(const FString& Parameters)
{
	auto Only = [this](const FString& What, const std::shared_ptr<FRun>& Run, const TCHAR* Want)
	{
		if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) return;
		TestEqual(What, Shown(Run->Fake->Sent[0]), FString(Want));
	};

	// Every list option, in the core's order: the typed ones, the filters by field name in byte order (an empty value
	// lists the rows with none), view as last; a list joined by commas, escaped.
	{
		FOWListOptions O;
		O.Limit = 5;
		O.Cursor = TEXT("abc");
		O.Expand = { TEXT("a"), TEXT("b") };
		O.Fields = { TEXT("name") };
		O.Ids = { TEXT("i1"), TEXT("i2") };
		O.CreatedBy = TEXT("owner");
		O.Name = TEXT("Ada");
		O.NameContains = TEXT("x y");
		O.Supertype = TEXT("Hero");
		O.Subtype = TEXT("Old");
		O.Filter.Add(TEXT("parent_location"), TEXT("l1"));
		O.Filter.Add(TEXT("birthplace"), TEXT(""));
		O.ViewAs = TEXT("public");
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": [], "has_more": false})");
		UOWClientList* Node = UOWClientList::ListElements(ReadKey(), TEXT("character"), O);
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Only(TEXT("ListElements, every option"), Run,
			TEXT("/character/?limit=5&cursor=abc&expand=a%2Cb&fields=name&ids=i1%2Ci2&created_by=owner&name=Ada&name__icontains=x%20y")
			TEXT("&supertype=Hero&subtype=Old&birthplace=&parent_location=l1&as=public|API-Key=ow_r_test"));
	}

	// Limit 0 or less is the config's page size; the page size 0 or less is 100.
	for (const int64 Limit : { int64(0), int64(-3) })
	{
		FOWListOptions O;
		O.Limit = Limit;
		FOWClientConfig C = ReadKey();
		C.PageSize = 50;
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": []})");
		UOWClientList* Node = UOWClientList::ListElements(C, TEXT("character"), O);
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Only(FString::Printf(TEXT("ListElements, Limit %lld: the page size"), Limit), Run, TEXT("/character/?limit=50|API-Key=ow_r_test"));
	}
	{
		FOWClientConfig C = ReadKey();
		C.PageSize = 0;
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": []})");
		Start(&MakeList, C, Run);
		Only(TEXT("ListElements, PageSize 0: 100"), Run, TEXT("/character/?limit=100|API-Key=ow_r_test"));
	}

	// ListAllElements starts at the options' cursor.
	{
		FOWListOptions O;
		O.Cursor = TEXT("start");
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": [], "has_more": false})");
		UOWClientListAll* Node = UOWClientListAll::ListAllElements(ReadKey(), TEXT("character"), O);
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Only(TEXT("ListAllElements, a starting cursor"), Run, TEXT("/character/?limit=100&cursor=start|API-Key=ow_r_test"));
	}

	// ReadElement's options, in the core's order.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"id": "c1"})");
		UOWClientRead* Node = UOWClientRead::ReadElement(ReadKey(), TEXT("character"), TEXT("c1"), { TEXT("a"), TEXT("b") },
			{ TEXT("name") }, TEXT("public"));
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Only(TEXT("ReadElement, every option"), Run, TEXT("/character/c1/?expand=a%2Cb&fields=name&as=public|API-Key=ow_r_test"));
	}

	// ReadChanges: since, the limit (the page size, at most 1000), after, escaped.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"head": 1, "cursor": "1", "changes": []})");
		UOWClientChanges* Node = UOWClientChanges::ReadChanges(ReadKey(), TEXT("17"), TEXT("2026-10-22T00:00:00Z"));
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Only(TEXT("ReadChanges, since and after"), Run, TEXT("/changes/?since=17&limit=100&after=2026-10-22T00%3A00%3A00Z|API-Key=ow_r_test"));
	}
	{
		FOWClientConfig C = ReadKey();
		C.PageSize = 5000;
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"head": 1, "cursor": "1", "changes": []})");
		Start(&MakeChanges, C, Run);
		Only(TEXT("ReadChanges, PageSize 5000: at most 1000"), Run, TEXT("/changes/?limit=1000|API-Key=ow_r_test"));
	}

	// BaseUrl "" is the core's default address.
	{
		FOWClientConfig C;
		C.BaseUrl = TEXT("");
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"status": "ok"})");
		Start(&MakeHealth, C, Run);
		if (TestEqual(TEXT("BaseUrl \"\": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1))
			TestEqual(TEXT("BaseUrl \"\": the default address"), Str(Run->Fake->Sent[0].url), FString(TEXT("https://www.onlyworlds.com/api/v2/health/")));
	}
	// The config's own default is that address too.
	TestEqual(TEXT("FOWClientConfig's BaseUrl"), FOWClientConfig().BaseUrl, FString(UTF8_TO_TCHAR(OnlyWorlds::OWClient::DefaultBaseUrl)));
	TestEqual(TEXT("FOWClientConfig's PageSize"), FOWClientConfig().PageSize, int64(100));
	return true;
}

// -- what a success gives ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesAnswersTest, "OnlyWorlds.Client.Nodes.Answers", OWTestFlags)
bool FOWClientNodesAnswersTest::RunTest(const FString& Parameters)
{
	auto NoError = [this](const FString& What, const FOWApiError& E)
	{
		TestTrue(What + TEXT(": Error is empty on OnSuccess"), E.Kind == EOWApiErrorKind::None && E.Status == 0 && E.Line.IsEmpty());
	};

	// A body as JSON text, its numbers as written (past 2^53 too).
	{
		const char* Body = R"({"id": "w1", "name": "Moppetopia", "big": 12345678901234567890, "half": 0.50})";
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(Body);
		Start(&MakeWorld, ReadKey(), Run);
		TestEqual(TEXT("ReadWorld: OnSuccess once"), Run->Probe->Successes, 1);
		TestTrue(TEXT("ReadWorld: the body as JSON text, got ") + Run->Probe->Json, SameJson(Run->Probe->Json, Body));
		TestTrue(TEXT("ReadWorld: a number past 2^53 as written"), Run->Probe->Json.Contains(TEXT("12345678901234567890")));
		TestTrue(TEXT("ReadWorld: a number's text kept"), Run->Probe->Json.Contains(TEXT("0.50")));
		NoError(TEXT("ReadWorld"), Run->Probe->Error);
	}

	// The members: the rows of keel's {data}, as the JSON array UOWMessages takes; no rows, an empty array.
	{
		const std::string Body = MembersBody();
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(Body.c_str());
		Start(&MakeMembers, ReadKey(), Run);
		TestEqual(TEXT("ReadMembers: OnSuccess once"), Run->Probe->Successes, 1);
		TestTrue(TEXT("ReadMembers: the rows as a JSON array, got ") + Run->Probe->Json, SameJson(Run->Probe->Json, MembersRows));
		NoError(TEXT("ReadMembers"), Run->Probe->Error);
	}
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"note": "no rows"})");
		Start(&MakeMembers, ReadKey(), Run);
		TestTrue(TEXT("ReadMembers, no data: an empty array, got ") + Run->Probe->Json, SameJson(Run->Probe->Json, "[]"));
	}

	// One page: each row its body as JSON text, has_more, the cursor.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": [{"id": "c1", "name": "Ada"}, {"id": "c2", "name": "Mara"}], "has_more": true, "next_cursor": "p2"})");
		Start(&MakeList, ReadKey(), Run);
		const UOWClientNodesProbe& P = *Run->Probe;
		TestEqual(TEXT("ListElements: OnSuccess once"), P.Successes, 1);
		if (TestEqual(TEXT("ListElements: two rows"), P.Elements.Num(), 2))
		{
			TestTrue(TEXT("ListElements: row 1"), SameJson(P.Elements[0], R"({"id": "c1", "name": "Ada"})"));
			TestTrue(TEXT("ListElements: row 2"), SameJson(P.Elements[1], R"({"id": "c2", "name": "Mara"})"));
		}
		TestTrue(TEXT("ListElements: has more"), P.bMore);
		TestEqual(TEXT("ListElements: the cursor"), P.Cursor, FString(TEXT("p2")));
		NoError(TEXT("ListElements"), P.Error);
	}

	// Every page: the walk by has_more, each page on the last one's cursor, one list.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c1"}, {"id": "c2"}], "has_more": true, "next_cursor": "p2"})"));
		Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c3"}], "has_more": false, "next_cursor": null})"));
		Start(&MakeListAll, ReadKey(), Run);
		const UOWClientNodesProbe& P = *Run->Probe;
		TestEqual(TEXT("ListAllElements: OnSuccess once"), P.Successes, 1);
		if (TestEqual(TEXT("ListAllElements: two pages asked"), static_cast<int32>(Run->Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("ListAllElements: page 1"), Shown(Run->Fake->Sent[0]), FString(TEXT("/character/?limit=100|API-Key=ow_r_test")));
			TestEqual(TEXT("ListAllElements: page 2 on its cursor"), Shown(Run->Fake->Sent[1]), FString(TEXT("/character/?limit=100&cursor=p2|API-Key=ow_r_test")));
		}
		if (TestEqual(TEXT("ListAllElements: three rows"), P.Elements.Num(), 3))
		{
			TestTrue(TEXT("ListAllElements: in order"), SameJson(P.Elements[0], R"({"id": "c1"})") && SameJson(P.Elements[2], R"({"id": "c3"})"));
		}
		NoError(TEXT("ListAllElements"), P.Error);
	}

	// A change feed page: each row's fields, an upsert's element as JSON text, a delete's marks.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"cursor": "c-18", "has_more": true, "head": 20, "epoch": 3, "epoch_reason": "role", "changes": [
			{"op": "upsert", "type": "character", "id": "c1", "change_seq": 17, "updated_at": "2026-10-26T09:00:00Z",
			 "element": {"id": "c1", "name": "Ada"}},
			{"op": "delete", "type": "character", "id": "c2", "change_seq": 18, "deleted_at": "2026-10-26T10:00:00Z",
			 "deleted_by": "m2", "deleted_via": "api", "deleted_client": "atlas", "reason": "hidden"}]})");
		Start(&MakeChanges, ReadKey(), Run);
		const FOWChangesPage& Page = Run->Probe->Page;
		TestEqual(TEXT("ReadChanges: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("ReadChanges: Cursor"), Page.Cursor, FString(TEXT("c-18")));
		TestTrue(TEXT("ReadChanges: bHasMore"), Page.bHasMore);
		TestEqual(TEXT("ReadChanges: Head"), Page.Head, int64(20));
		TestTrue(TEXT("ReadChanges: bHasEpoch"), Page.bHasEpoch);
		TestEqual(TEXT("ReadChanges: Epoch"), Page.Epoch, int64(3));
		TestEqual(TEXT("ReadChanges: EpochReason"), Page.EpochReason, FString(TEXT("role")));
		TestFalse(TEXT("ReadChanges: bUnchanged"), Page.bUnchanged);
		if (TestEqual(TEXT("ReadChanges: two rows"), Page.Changes.Num(), 2))
		{
			const FOWChange& U = Page.Changes[0];
			TestEqual(TEXT("upsert: Op"), U.Op, FString(TEXT("upsert")));
			TestEqual(TEXT("upsert: Type"), U.Type, FString(TEXT("character")));
			TestEqual(TEXT("upsert: Id"), U.Id, FString(TEXT("c1")));
			TestEqual(TEXT("upsert: ChangeSeq"), U.ChangeSeq, int64(17));
			TestEqual(TEXT("upsert: UpdatedAt"), U.UpdatedAt, FString(TEXT("2026-10-26T09:00:00Z")));
			TestTrue(TEXT("upsert: Element as JSON text"), SameJson(U.Element, R"({"id": "c1", "name": "Ada"})"));
			TestEqual(TEXT("upsert: DeletedAt"), U.DeletedAt, FString());
			const FOWChange& D = Page.Changes[1];
			TestEqual(TEXT("delete: Op"), D.Op, FString(TEXT("delete")));
			TestEqual(TEXT("delete: ChangeSeq"), D.ChangeSeq, int64(18));
			TestEqual(TEXT("delete: Element"), D.Element, FString());
			TestEqual(TEXT("delete: DeletedAt"), D.DeletedAt, FString(TEXT("2026-10-26T10:00:00Z")));
			TestEqual(TEXT("delete: DeletedBy"), D.DeletedBy, FString(TEXT("m2")));
			TestEqual(TEXT("delete: DeletedVia"), D.DeletedVia, FString(TEXT("api")));
			TestEqual(TEXT("delete: DeletedClient"), D.DeletedClient, FString(TEXT("atlas")));
			TestEqual(TEXT("delete: Reason"), D.Reason, FString(TEXT("hidden")));
		}
		NoError(TEXT("ReadChanges"), Run->Probe->Error);
	}

	// The tip: a number cursor read as its text, no epoch. Each node is its own client, so no node holds the last
	// tip's ETag: a second tip read sends no If-None-Match, and bUnchanged is never set.
	{
		auto Run = std::make_shared<FRun>();
		OWResponse Tip = Ok(R"({"head": 17, "cursor": 17})");
		Tip.headers.emplace_back("ETag", "\"t17\"");
		Run->Fake->Fallback = Tip;
		Start(&MakeHead, ReadKey(), Run);
		const FOWChangesPage Page = Run->Probe->Page;
		TestEqual(TEXT("ReadChangesHead: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("ReadChangesHead: Head"), Page.Head, int64(17));
		TestEqual(TEXT("ReadChangesHead: a number cursor as its text"), Page.Cursor, FString(TEXT("17")));
		TestFalse(TEXT("ReadChangesHead: no epoch"), Page.bHasEpoch);
		TestFalse(TEXT("ReadChangesHead: bUnchanged"), Page.bUnchanged);
		TestEqual(TEXT("ReadChangesHead: no rows"), Page.Changes.Num(), 0);
		Start(&MakeHead, ReadKey(), Run);
		if (TestEqual(TEXT("ReadChangesHead twice: two requests"), static_cast<int32>(Run->Fake->Sent.size()), 2))
			TestNull(TEXT("ReadChangesHead, a second node: no If-None-Match"), Run->Fake->Sent[1].Header("If-None-Match"));
	}
	return true;
}

// -- keel's error envelope, and the client's own errors ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesErrorsTest, "OnlyWorlds.Client.Nodes.Errors", OWTestFlags)
bool FOWClientNodesErrorsTest::RunTest(const FString& Parameters)
{
	auto Failed = [this](const FString& What, const std::shared_ptr<FRun>& Run)
	{
		TestEqual(What + TEXT(": OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), Run->Probe->Successes, 0);
	};

	// keel's envelope, every member of it.
	{
		const char* Body = R"({"error": {"type": "invalid_request", "code": "not_found", "message": "No such character.", "param": "id",
			"doc_url": "https://www.onlyworlds.com/docs/errors#not_found"}})";
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Answer(404, Body);
		Start(&MakeElement, ReadKey(), Run);
		const FOWApiError& E = Run->Probe->Error;
		Failed(TEXT("404"), Run);
		TestTrue(TEXT("404: kind Api"), E.Kind == EOWApiErrorKind::Api);
		TestEqual(TEXT("404: Status"), E.Status, 404);
		TestEqual(TEXT("404: Type"), E.Type, FString(TEXT("invalid_request")));
		TestEqual(TEXT("404: Code"), E.Code, FString(TEXT("not_found")));
		TestEqual(TEXT("404: Message"), E.Message, FString(TEXT("No such character.")));
		TestEqual(TEXT("404: Param"), E.Param, FString(TEXT("id")));
		TestEqual(TEXT("404: DocUrl"), E.DocUrl, FString(TEXT("https://www.onlyworlds.com/docs/errors#not_found")));
		TestEqual(TEXT("404: RawBody"), E.RawBody, Str(std::string(Body)));
		TestFalse(TEXT("404: no Retry-After"), E.bHasRetryAfter);
		TestFalse(TEXT("404: not retryable"), E.bRetryable);
		TestEqual(TEXT("404: Line"), E.Line, FString(TEXT("OnlyWorlds API 404 [not_found] (field: id): No such character.")));
		TestEqual(TEXT("404: no Json"), Run->Probe->Json, FString());
	}

	// Retry-After, and a retryable status.
	{
		auto Run = std::make_shared<FRun>();
		OWResponse Busy = Answer(429, R"({"error": {"code": "rate_limited", "message": "Slow down."}})");
		Busy.headers.emplace_back("retry-after", "30");
		Run->Fake->Fallback = Busy;
		Start(&MakeWorld, ReadKey(), Run);
		const FOWApiError& E = Run->Probe->Error;
		Failed(TEXT("429"), Run);
		TestEqual(TEXT("429: Code"), E.Code, FString(TEXT("rate_limited")));
		TestTrue(TEXT("429: bHasRetryAfter"), E.bHasRetryAfter);
		TestEqual(TEXT("429: RetryAfter"), E.RetryAfter, 30);
		TestTrue(TEXT("429: retryable"), E.bRetryable);
	}

	// The feed's resync, with its reason.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Answer(409, R"({"error": {"code": "resync_required", "message": "Walk again.", "reason": "view"}})");
		Start(&MakeChanges, ReadKey(), Run);
		Failed(TEXT("409 resync_required"), Run);
		TestEqual(TEXT("409: Code"), Run->Probe->Error.Code, FString(TEXT("resync_required")));
		TestEqual(TEXT("409: ResyncReason"), Run->Probe->Error.ResyncReason, FString(TEXT("view")));
		TestEqual(TEXT("409: an empty page"), Run->Probe->Page.Changes.Num(), 0);
		TestEqual(TEXT("409: no Limit"), Run->Probe->Error.Limit, FString());
	}

	// A size refusal names its limit; another 422 has none.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Answer(422, R"({"error": {"type": "invalid_request", "code": "invalid_request", "message": "Too large.", "limit": "element_bytes"}})");
		Start(&MakeWorld, ReadKey(), Run);
		Failed(TEXT("422 element_bytes"), Run);
		TestEqual(TEXT("422: Code"), Run->Probe->Error.Code, FString(TEXT("invalid_request")));
		TestEqual(TEXT("422: Limit"), Run->Probe->Error.Limit, FString(TEXT("element_bytes")));
	}
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Answer(422, R"({"error": {"type": "invalid_request", "code": "invalid_request", "message": "No."}})");
		Start(&MakeWorld, ReadKey(), Run);
		Failed(TEXT("422 without a limit"), Run);
		TestEqual(TEXT("422 without a limit: Limit"), Run->Probe->Error.Limit, FString());
	}

	// No answer came: kind Transport, status 0, the transport's words in the core's.
	{
		auto Run = std::make_shared<FRun>();
		OWResponse NoAnswer;
		NoAnswer.failure = "no answer: ConnectionError";
		Run->Fake->Fallback = NoAnswer;
		Start(&MakeMe, ReadKey(), Run);
		Failed(TEXT("no answer"), Run);
		TestTrue(TEXT("no answer: kind Transport"), Run->Probe->Error.Kind == EOWApiErrorKind::Transport);
		TestEqual(TEXT("no answer: Status"), Run->Probe->Error.Status, 0);
		TestEqual(TEXT("no answer: Message"), Run->Probe->Error.Message,
			FString(TEXT("OnlyWorlds request failed: GET /me/: no answer: ConnectionError")));
	}

	// A 2xx whose body is not the answer: kind Answer.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok("<html>a gateway</html>");
		Start(&MakeWorld, ReadKey(), Run);
		Failed(TEXT("not JSON"), Run);
		TestTrue(TEXT("not JSON: kind Answer"), Run->Probe->Error.Kind == EOWApiErrorKind::Answer);
		TestEqual(TEXT("not JSON: Status"), Run->Probe->Error.Status, 200);
		TestEqual(TEXT("not JSON: Message"), Run->Probe->Error.Message, FString(TEXT("GET /world/ answered 200 with a body that is not JSON")));
	}
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"data": {}})");
		Start(&MakeList, ReadKey(), Run);
		Failed(TEXT("data not a list"), Run);
		TestTrue(TEXT("data not a list: kind Answer"), Run->Probe->Error.Kind == EOWApiErrorKind::Answer);
		TestEqual(TEXT("data not a list: no rows"), Run->Probe->Elements.Num(), 0);
	}

	// The walk stops at a page's error, answering no half list.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c1"}], "has_more": true, "next_cursor": "p2"})"));
		Run->Fake->Script.push_back(Answer(404, R"({"error": {"code": "not_found", "message": "Gone."}})"));
		Start(&MakeListAll, ReadKey(), Run);
		Failed(TEXT("ListAllElements, page 2 refused"), Run);
		TestEqual(TEXT("ListAllElements, page 2 refused: two pages asked"), static_cast<int32>(Run->Fake->Sent.size()), 2);
		TestEqual(TEXT("ListAllElements, page 2 refused: Code"), Run->Probe->Error.Code, FString(TEXT("not_found")));
		TestEqual(TEXT("ListAllElements, page 2 refused: no half list"), Run->Probe->Elements.Num(), 0);
	}
	return true;
}

// -- the core's refusals, before anything is sent ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesRefusalsTest, "OnlyWorlds.Client.Nodes.Refusals", OWTestFlags)
bool FOWClientNodesRefusalsTest::RunTest(const FString& Parameters)
{
	// Each answered inside Activate (the fake holds what it is sent, so an answer that came is a refusal), nothing sent.
	auto Refused = [this](const FString& What, const std::shared_ptr<FRun>& Run, const TCHAR* Starts)
	{
		const UOWClientNodesProbe& P = *Run->Probe;
		TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
		TestEqual(What + TEXT(": OnFailure once, inside Activate"), P.Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), P.Successes, 0);
		TestTrue(What + TEXT(": kind Refused"), P.Error.Kind == EOWApiErrorKind::Refused);
		TestTrue(What + TEXT(": the core's words, got ") + P.Error.Message, P.Error.Message.StartsWith(Starts));
		TestTrue(What + TEXT(": the line"), P.Error.Line.StartsWith(TEXT("OnlyWorlds refused: ")));
	};
	auto Held = []()
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->bLater = true;
		return Run;
	};

	// A config the core refuses: every call answers it, the health pulse too.
	Refused(TEXT("a world key with a world"), Start(&MakeHealth, Config(TEXT("ow_w_test"), TEXT(""), TEXT("w1")), Held()),
		TEXT("OWClient was not built: A world key reads its own world"));
	Refused(TEXT("a world of white space"), Start(&MakeWorld, Config(TEXT("ow_a_test"), TEXT(""), TEXT("  ")), Held()),
		TEXT("OWClient was not built: world is a world's id"));

	// A value that is not one path segment.
	{
		auto Run = Held();
		UOWClientRead* Node = UOWClientRead::ReadElement(ReadKey(), TEXT("character"), TEXT("c1/hidden"), {}, {}, TEXT(""));
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Refused(TEXT("an id with a slash"), Run, TEXT("id \"c1/hidden\" is not one path segment."));
	}
	{
		auto Run = Held();
		UOWClientList* Node = UOWClientList::ListElements(ReadKey(), TEXT("Character"), FOWListOptions());
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Refused(TEXT("a type that is not a lower-case word"), Run, TEXT("type \"Character\" is not one path segment."));
	}

	// The list's filters.
	struct FFilter
	{
		const TCHAR* Key;
		const TCHAR* Starts;
	};
	for (const FFilter& F : { FFilter{ TEXT("world"), TEXT("List: world is not a filter") },
		FFilter{ TEXT("limit"), TEXT("List: 'limit' is sent by its own option") },
		FFilter{ TEXT("Bad-Key"), TEXT("List: 'Bad-Key' is not a field name to filter on.") } })
	{
		FOWListOptions O;
		O.Filter.Add(F.Key, TEXT("x"));
		auto Run = Held();
		UOWClientList* Node = UOWClientList::ListElements(ReadKey(), TEXT("character"), O);
		Run->Probe->Watch(Node);
		Node->SetTransport(Run->Fake);
		Node->Activate();
		Refused(FString(TEXT("a filter on ")) + F.Key, Run, F.Starts);
	}

	// A call the credential may not send.
	Refused(TEXT("Me without a key"), Start(&MakeMe, Config(TEXT("")), Held()), TEXT("GET /me/ needs a key"));
	Refused(TEXT("Changes on an account token"), Start(&MakeChanges, Config(TEXT("ow_a_test"), TEXT(""), TEXT("w1")), Held()),
		TEXT("GET /changes/ needs a world key"));

	// On the node's own transport (FOWHttpTransport): a refusal is answered before anything could reach it.
	{
		auto Run = std::make_shared<FRun>();
		Start(&MakeMembers, Config(TEXT("")), Run, /*bOwnTransport=*/true);
		TestEqual(TEXT("the default transport, a refusal: OnFailure once, inside Activate"), Run->Probe->Failures, 1);
		TestTrue(TEXT("the default transport, a refusal: kind Refused"), Run->Probe->Error.Kind == EOWApiErrorKind::Refused);
	}
	return true;
}

// -- one answer only ----------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesOnceTest, "OnlyWorlds.Client.Nodes.Once", OWTestFlags)
bool FOWClientNodesOnceTest::RunTest(const FString& Parameters)
{
	// A transport that answers every request twice: one OnSuccess, one OnFailure.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = Ok(R"({"name": "W"})");
		Start(&MakeWorld, ReadKey(), Run);
		TestEqual(TEXT("answered twice, a success: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("answered twice, a success: no OnFailure"), Run->Probe->Failures, 0);
	}
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = Answer(500, R"({"error": {"code": "server_error"}})");
		Start(&MakeWorld, ReadKey(), Run);
		TestEqual(TEXT("answered twice, a failure: OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(TEXT("answered twice, a failure: no OnSuccess"), Run->Probe->Successes, 0);
	}
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Times = 2;
		Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c1"}], "has_more": true, "next_cursor": "p2"})"));
		Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c2"}], "has_more": false})"));
		Start(&MakeListAll, ReadKey(), Run);
		TestEqual(TEXT("a walk answered twice a page: two pages asked"), static_cast<int32>(Run->Fake->Sent.size()), 2);
		TestEqual(TEXT("a walk answered twice a page: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("a walk answered twice a page: two rows"), Run->Probe->Elements.Num(), 2);
	}

	// Activate twice: one request, one answer.
	{
		auto Run = std::make_shared<FRun>();
		Run->Fake->Fallback = Ok(R"({"name": "W"})");
		Start(&MakeWorld, ReadKey(), Run);
		if (UOWClientNode* Node = Run->Node.Get()) Node->Activate();
		TestEqual(TEXT("activated twice: one request"), static_cast<int32>(Run->Fake->Sent.size()), 1);
		TestEqual(TEXT("activated twice: OnSuccess once"), Run->Probe->Successes, 1);
	}
	return true;
}

// -- in flight: a later frame, the node kept until it answers ----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesLatentTest, "OnlyWorlds.Client.Nodes.Latent", OWTestFlags)
bool FOWClientNodesLatentTest::RunTest(const FString& Parameters)
{
	// Two pages, each answered on a later frame. Between Activate and the answer only the node's own keep holds it: a
	// garbage collection leaves it; once it answered, one collects it.
	auto Run = std::make_shared<FRun>();
	Run->Fake->bLater = true;
	Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c1"}, {"id": "c2"}], "has_more": true, "next_cursor": "p2"})"));
	Run->Fake->Script.push_back(Ok(R"({"data": [{"id": "c3"}], "has_more": false})"));
	Start(&MakeListAll, ReadKey(), Run);

	TestEqual(TEXT("not answered inside Activate"), Run->Probe->Answers(), 0);
	TestEqual(TEXT("page 1 sent"), static_cast<int32>(Run->Fake->Sent.size()), 1);
	TestTrue(TEXT("the node is kept while in flight"), Run->Node.IsValid() && Run->Node->IsRooted());
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("a collection in flight leaves the node"), Run->Node.IsValid());

	auto Frames = std::make_shared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Frames]()
	{
		++*Frames;
		Run->Fake->Pump();
		if (Run->Probe->Answers() == 0 && *Frames < WaitFrames) return false;

		TestEqual(TEXT("answered once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("no OnFailure"), Run->Probe->Failures, 0);
		TestEqual(TEXT("a frame a page"), *Frames, 2);
		TestEqual(TEXT("two pages asked"), static_cast<int32>(Run->Fake->Sent.size()), 2);
		TestEqual(TEXT("three rows"), Run->Probe->Elements.Num(), 3);
		TestTrue(TEXT("let go once answered"), Run->Node.IsValid() && !Run->Node->IsRooted());
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TestFalse(TEXT("collected after"), Run->Node.IsValid());
		return true;
	}));
	return true;
}

// -- a node made and activated as a graph does, its members read by UOWMessages ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesReflectionTest, "OnlyWorlds.Client.Nodes.Reflection", OWTestFlags)
bool FOWClientNodesReflectionTest::RunTest(const FString& Parameters)
{
	// The factory found by name, its flags, its frame from the UFunction, the config set member by member (a Make
	// node), ProcessEvent on the class default object; the node read from the return; the delegates bound; Activate
	// found by name and called by ProcessEvent, as UK2Node_AsyncAction's expansion calls it.
	UFunction* Factory = UOWClientRead::StaticClass()->FindFunctionByName(TEXT("ReadMembers"));
	if (!TestNotNull(TEXT("UFUNCTION ReadMembers"), Factory)) return false;
	TestTrue(TEXT("ReadMembers: a static, public, native Blueprint function"),
		Factory->HasAllFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable));
	TestFalse(TEXT("ReadMembers: not pure"), Factory->HasAnyFunctionFlags(FUNC_BlueprintPure));
#if WITH_METADATA
	TestTrue(TEXT("ReadMembers: the async node's, not a plain call"), Factory->HasMetaData(TEXT("BlueprintInternalUseOnly")));
#endif

	uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Factory->ParmsSize, 1), Factory->GetMinAlignment()));
	Factory->InitializeStruct(Frame);
	UOWClientRead* Node = nullptr;
	FStructProperty* ConfigParam = CastField<FStructProperty>(Factory->FindPropertyByName(TEXT("Config")));
	if (TestTrue(TEXT("ReadMembers has an FOWClientConfig parameter Config"), ConfigParam && ConfigParam->Struct == FOWClientConfig::StaticStruct()))
	{
		void* Data = ConfigParam->ContainerPtrToValuePtr<void>(Frame);
		const UScriptStruct* S = FOWClientConfig::StaticStruct();
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("ApiKey"), TEXT("ow_r_test"));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("ApiPin"), TEXT("1234"));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("BaseUrl"), Str(std::string(KeelBase)));
		OWBlueprintTest::SetMember<FInt64Property>(*this, S, Data, TEXT("PageSize"), int64(100));
		UOWClientRead::StaticClass()->GetDefaultObject()->ProcessEvent(Factory, Frame);
		const FObjectProperty* Ret = CastField<FObjectProperty>(Factory->GetReturnProperty());
		if (TestTrue(TEXT("ReadMembers returns a UOWClientRead"), Ret && Ret->PropertyClass == UOWClientRead::StaticClass()))
			Node = Cast<UOWClientRead>(Ret->GetObjectPropertyValue_InContainer(Frame));
	}
	Factory->DestroyStruct(Frame);
	FMemory::Free(Frame);
	if (!TestNotNull(TEXT("the node"), Node)) return false;

	// The delegate a graph binds: a multicast delegate whose signature gives the pins Json and Error.
	const FMulticastDelegateProperty* Success = CastField<FMulticastDelegateProperty>(UOWClientRead::StaticClass()->FindPropertyByName(TEXT("OnSuccess")));
	if (TestNotNull(TEXT("OnSuccess, a multicast delegate"), Success))
	{
		const UFunction* Sig = Success->SignatureFunction;
		TestTrue(TEXT("OnSuccess's Json, a string"), Sig && CastField<FStrProperty>(Sig->FindPropertyByName(TEXT("Json"))) != nullptr);
		const FStructProperty* ErrorPin = Sig ? CastField<FStructProperty>(Sig->FindPropertyByName(TEXT("Error"))) : nullptr;
		TestTrue(TEXT("OnSuccess's Error, an FOWApiError"), ErrorPin && ErrorPin->Struct == FOWApiError::StaticStruct());
	}

	auto Run = std::make_shared<FRun>();
	Run->Fake->bLater = true;
	const std::string Body = MembersBody();
	Run->Fake->Fallback = Ok(Body.c_str());
	Run->Node = Node;
	Node->SetTransport(Run->Fake);
	Run->Probe->Watch(Node);
	UFunction* Activate = Node->FindFunction(TEXT("Activate"));
	if (!TestNotNull(TEXT("UFUNCTION Activate"), Activate)) return false;
	Node->ProcessEvent(Activate, nullptr);
	TestEqual(TEXT("not answered inside Activate"), Run->Probe->Answers(), 0);
	if (TestEqual(TEXT("one request"), static_cast<int32>(Run->Fake->Sent.size()), 1))
		TestEqual(TEXT("the request"), Shown(Run->Fake->Sent[0]), FString(TEXT("/members/|API-Key=ow_r_test")));

	struct FKeep
	{
		TStrongObjectPtr<UOWWorld> World;
	};
	auto Keep = std::make_shared<FKeep>();
	Keep->World.Reset(MessagesWorld());
	auto Frames = std::make_shared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Keep, Frames]()
	{
		++*Frames;
		Run->Fake->Pump();
		if (Run->Probe->Answers() == 0 && *Frames < WaitFrames) return false;

		TestEqual(TEXT("OnSuccess once"), Run->Probe->Successes, 1);
		const FString Members = Run->Probe->Json;
		TestTrue(TEXT("the members text, got ") + Members, SameJson(Members, MembersRows));

		// The text wired into UOWMessages as a graph wires it.
		{
			OWBlueprintTest::FCall Call(*this, UOWMessages::StaticClass(), TEXT("SenderStatus"));
			Call.SetWorld(Keep->World.Get());
			Call.SetString(TEXT("MessageId"), TEXT("c3"));
			Call.SetString(TEXT("MembersJson"), Members);
			Call.Invoke();
			TestTrue(TEXT("SenderStatus takes it"), Call.ReturnBool());
			TestEqual(TEXT("SenderStatus: no reason"), Call.Out<FStrProperty>(TEXT("Reason")), FString());
			const UScriptStruct* S = FOWSender::StaticStruct();
			const void* Data = Call.OutStruct(TEXT("Sender"), S);
			const FEnumProperty* Status = Data ? CastField<FEnumProperty>(S->FindPropertyByName(TEXT("Status"))) : nullptr;
			const int64 V = Status ? Status->GetUnderlyingProperty()->GetSignedIntPropertyValue(Status->ContainerPtrToValuePtr<void>(Data)) : -1;
			TestEqual(TEXT("SenderStatus c3: Verified"), V, static_cast<int64>(EOWSenderStatus::Verified));
			TestEqual(TEXT("SenderStatus c3: Claimed"), OWBlueprintTest::Member<FStrProperty>(*this, S, Data, TEXT("Claimed")), FString(Mara));
		}
		{
			OWBlueprintTest::FCall Call(*this, UOWMessages::StaticClass(), TEXT("EditedBy"));
			Call.SetWorld(Keep->World.Get());
			Call.SetString(TEXT("MessageId"), TEXT("c2"));
			Call.SetString(TEXT("MembersJson"), Members);
			Call.Invoke();
			TestTrue(TEXT("EditedBy takes it"), Call.ReturnBool());
			const UScriptStruct* S = FOWEdited::StaticStruct();
			const void* Data = Call.OutStruct(TEXT("Edited"), S);
			TestTrue(TEXT("EditedBy c2: edited"), OWBlueprintTest::BoolMember(*this, S, Data, TEXT("bEdited")));
			TestEqual(TEXT("EditedBy c2: by Wren"), OWBlueprintTest::Member<FStrProperty>(*this, S, Data, TEXT("By")), FString(Wren));
		}
		return true;
	}));
	return true;
}

// -- the pins a graph's node gets (editor only) -----------------------------------------------------------------------

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientNodesPinsTest, "OnlyWorlds.Client.Nodes.Pins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FOWClientNodesPinsTest::RunTest(const FString& Parameters)
{
	const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(TEXT("OWClientNodesProbe")));
	TStrongObjectPtr<UBlueprint> BP(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Unique,
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None));
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP.Get()) : nullptr;
	if (!TestNotNull(TEXT("a Blueprint with an event graph"), Graph)) return false;

	const FName PinString = UEdGraphSchema_K2::PC_String, PinStruct = UEdGraphSchema_K2::PC_Struct,
		PinBool = UEdGraphSchema_K2::PC_Boolean, PinExec = UEdGraphSchema_K2::PC_Exec;
	struct FPin
	{
		const TCHAR* Name;
		FName Category;
		const UScriptStruct* Of;  // a struct pin's struct
		bool bArray;
	};
	struct FNode
	{
		UClass* Class;
		const TCHAR* Factory;
		TArray<FPin> In;
		TArray<FPin> Out;
	};
	const FPin ConfigPin{ TEXT("Config"), PinStruct, FOWClientConfig::StaticStruct(), false };
	const FPin ErrorPin{ TEXT("Error"), PinStruct, FOWApiError::StaticStruct(), false };
	const FPin JsonPin{ TEXT("Json"), PinString, nullptr, false };
	const FPin ElementsPin{ TEXT("Elements"), PinString, nullptr, true };
	const FPin PagePin{ TEXT("Page"), PinStruct, FOWChangesPage::StaticStruct(), false };
	const TArray<FNode> Nodes = {
		{ UOWClientRead::StaticClass(), TEXT("ReadHealth"), { ConfigPin }, { JsonPin, ErrorPin } },
		{ UOWClientRead::StaticClass(), TEXT("ReadWorld"), { ConfigPin }, { JsonPin, ErrorPin } },
		{ UOWClientRead::StaticClass(), TEXT("ReadMe"), { ConfigPin }, { JsonPin, ErrorPin } },
		{ UOWClientRead::StaticClass(), TEXT("ReadMembers"), { ConfigPin }, { JsonPin, ErrorPin } },
		{ UOWClientRead::StaticClass(), TEXT("ReadElement"),
			{ ConfigPin, { TEXT("Type"), PinString, nullptr, false }, { TEXT("Id"), PinString, nullptr, false }, { TEXT("Expand"), PinString, nullptr, true },
			  { TEXT("Fields"), PinString, nullptr, true }, { TEXT("ViewAs"), PinString, nullptr, false } },
			{ JsonPin, ErrorPin } },
		{ UOWClientList::StaticClass(), TEXT("ListElements"),
			{ ConfigPin, { TEXT("Type"), PinString, nullptr, false }, { TEXT("Options"), PinStruct, FOWListOptions::StaticStruct(), false } },
			{ ElementsPin, { TEXT("bHasMore"), PinBool, nullptr, false }, { TEXT("NextCursor"), PinString, nullptr, false }, ErrorPin } },
		{ UOWClientListAll::StaticClass(), TEXT("ListAllElements"),
			{ ConfigPin, { TEXT("Type"), PinString, nullptr, false }, { TEXT("Options"), PinStruct, FOWListOptions::StaticStruct(), false } },
			{ ElementsPin, ErrorPin } },
		{ UOWClientChanges::StaticClass(), TEXT("ReadChanges"),
			{ ConfigPin, { TEXT("Since"), PinString, nullptr, false }, { TEXT("After"), PinString, nullptr, false } }, { PagePin, ErrorPin } },
		{ UOWClientChanges::StaticClass(), TEXT("ReadChangesHead"), { ConfigPin }, { PagePin, ErrorPin } },
	};

	auto Check = [this](const FString& What, const UEdGraphPin* Pin, const FPin& Want)
	{
		if (!TestNotNull(What + TEXT(": a pin"), Pin)) return;
		TestEqual(What + TEXT(": its category"), Pin->PinType.PinCategory, Want.Category);
		TestTrue(What + TEXT(": an array exactly when it is a list"), (Pin->PinType.ContainerType == EPinContainerType::Array) == Want.bArray);
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
