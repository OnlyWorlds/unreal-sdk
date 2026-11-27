// The client's write nodes (OWClientNodes.h: UOWClientWrite, UOWClientBulk), through the read nodes' fake transport
// (OWFakeTransport.h), which records each request and answers from a script: no test here touches the network. Each
// node is made by its factory, its OnSuccess and OnFailure bound to a listener (OWClientWriteNodesProbe.h) as a graph's
// async node binds them, given the fake (SetTransport, the C++ seam), and activated. What each node sends on each
// credential (method, URL, the headers in order, the body), its options (If-Match, Idempotency-Key, the client's name,
// the agent mark), what a success gives (an element as JSON text, a delete's nothing, a bulk's slots), keel's error
// envelope, a 412's current element and a 202 proposal reaching OnFailure, the core's refusals and the node's own
// reaching OnFailure with nothing sent, one answer only, one node made and activated through reflection as a graph
// does, and, in the editor, the pins a graph's node gets.
//
// Every helper of this file is in the namespace OWWriteNodesTest (OWW below), never at file scope: a file-scope name
// hides a template parameter of the same name in an engine header the file instantiates later (C4459, an error
// here: CppCompileWarnings.cs).

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
#include "OWClientWriteNodesProbe.h"
#include "OWConvert.h"
#include "OWFakeTransport.h"
#include "OWId.h"
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

namespace OWWriteNodesTest
{
	constexpr EAutomationTestFlags WriteTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// The frames a latent test waits for its answer before it fails.
	constexpr int32 FrameLimit = 200;

	const char* const KeelRoot = "http://keel.test/api/v2";

	// -- answers and requests -----------------------------------------------------------------------------------------

	OnlyWorlds::OWResponse Respond(int Code, const char* Text)
	{
		OnlyWorlds::OWResponse Out;
		Out.status = Code;
		Out.body = Text;
		return Out;
	}

	OnlyWorlds::OWJsonValue ParseText(const char* Text)
	{
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return Parsed.ok ? Parsed.value : OnlyWorlds::OWJsonValue();
	}

	// JSON text equal as a value to Want (key order and number texts kept), and parsed at all.
	bool SameJsonText(const FString& Text, const char* Want)
	{
		const std::string Got = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Got));
		return Parsed.ok && Parsed.value == ParseText(Want);
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

	// A request's body as sent, or "<none>".
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
		Out.BaseUrl = OWConvert::Str(std::string(KeelRoot) + "/");  // the trailing slash trimmed by the core
		return Out;
	}

	FOWClientConfig WriterConfig() { return MakeConfig(TEXT("ow_w_test"), TEXT("1234")); }

	// The id a keyed write gives an element at Index, as text.
	std::string KeyedId(const char* Key, int64 Index) { return OnlyWorlds::OWId::Keyed(Key, Index); }

	// -- the nodes, each made and watched -------------------------------------------------------------------------------

	using FMakeWrite = UOWClientNode* (*)(const FOWClientConfig&, UOWClientWriteNodesProbe*);

	struct FWriteRun
	{
		std::shared_ptr<OWClientNodesTest::FFakeTransport> Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		TStrongObjectPtr<UOWClientWriteNodesProbe> Probe;
		TWeakObjectPtr<UOWClientNode> Node;

		FWriteRun() : Probe(NewObject<UOWClientWriteNodesProbe>(GetTransientPackage())) {}
	};

	// A node made by Make, watched, given the fake, and activated.
	std::shared_ptr<FWriteRun> Launch(FMakeWrite Make, const FOWClientConfig& With, std::shared_ptr<FWriteRun> Into = nullptr)
	{
		if (!Into) Into = std::make_shared<FWriteRun>();
		UOWClientNode* Made = Make(With, Into->Probe.Get());
		Into->Node = Made;
		Made->SetTransport(Into->Fake);
		Made->Activate();
		return Into;
	}

	// A node already made (and watched): given the fake, and activated.
	std::shared_ptr<FWriteRun> Activated(UOWClientNode* Made, std::shared_ptr<FWriteRun> Into)
	{
		Into->Node = Made;
		Made->SetTransport(Into->Fake);
		Made->Activate();
		return Into;
	}

	// A run whose fake holds what it is sent, so an answer that came inside Activate is a refusal.
	std::shared_ptr<FWriteRun> HeldRun()
	{
		auto Out = std::make_shared<FWriteRun>();
		Out->Fake->bLater = true;
		return Out;
	}

	UOWClientNode* MakeCreate(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		UOWClientWrite* N = UOWClientWrite::CreateElement(With, TEXT("character"), TEXT("{\"id\": \"c1\", \"name\": \"Ana\"}"), TEXT(""));
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeUpsert(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		UOWClientWrite* N = UOWClientWrite::UpsertElement(With, TEXT("character"), TEXT("c1"), TEXT("{\"id\": \"c1\", \"name\": \"Ana\"}"));
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakePatch(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		UOWClientWrite* N = UOWClientWrite::PatchElement(With, TEXT("character"), TEXT("c1"), TEXT("{\"level\": 4}"), true, 17);
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeDelete(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		UOWClientWrite* N = UOWClientWrite::DeleteElement(With, TEXT("character"), TEXT("c1"), true, 17);
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeLinks(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		UOWClientWrite* N = UOWClientWrite::EditElementLinks(With, TEXT("character"), TEXT("c1"), TEXT("friends"), { TEXT("a1") },
			{ TEXT("a2") }, false, 0);
		P->Watch(N);
		return N;
	}
	UOWClientNode* MakeBulk(const FOWClientConfig& With, UOWClientWriteNodesProbe* P)
	{
		FOWBulkItem One;
		One.Type = TEXT("character");
		One.ElementJson = TEXT("{\"id\": \"c1\", \"name\": \"Ana\"}");
		UOWClientBulk* N = UOWClientBulk::BulkWrite(With, { One }, false, TEXT(""));
		P->Watch(N);
		return N;
	}

	FOWBulkItem BulkItem(const TCHAR* TypeWord, const TCHAR* Text)
	{
		FOWBulkItem Out;
		Out.Type = TypeWord;
		Out.ElementJson = Text;
		return Out;
	}

	const char* const ProposalRow = R"({"id": "p1", "status": "pending"})";
	const char* const ProposalBody = R"({"proposal": {"id": "p1", "status": "pending"}})";
}

namespace OWW = OWWriteNodesTest;

// -- what each write node sends, on each credential ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesRequestsTest, "OnlyWorlds.Client.WriteNodes.Requests", OWW::WriteTestFlags)
bool FOWClientWriteNodesRequestsTest::RunTest(const FString& Parameters)
{
	// The world keys the core sends a write with (the PIN when there is one; a read key too: keel says no, not the
	// core), then the four it refuses before sending.
	struct FCredential
	{
		const TCHAR* Label;
		const TCHAR* Key;
		const TCHAR* Pin;
		const TCHAR* WorldId;
		const TCHAR* Headers;  // the credential's headers, first in every request; nullptr: refused
		const TCHAR* Refusal;  // the refusal's words after the route, when refused
	};
	const FCredential Credentials[] = {
		{ TEXT("a write key"), TEXT("ow_w_test"), TEXT("1234"), TEXT(""), TEXT("API-Key=ow_w_test; API-Pin=1234"), nullptr },
		{ TEXT("a write key without a PIN"), TEXT("ow_w_test"), TEXT(""), TEXT(""), TEXT("API-Key=ow_w_test"), nullptr },
		{ TEXT("a read key"), TEXT("ow_r_test"), TEXT("1234"), TEXT(""), TEXT("API-Key=ow_r_test; API-Pin=1234"), nullptr },
		{ TEXT("a legacy key"), TEXT("0000000001"), TEXT("1234"), TEXT(""), TEXT("API-Key=0000000001; API-Pin=1234"), nullptr },
		{ TEXT("an account token with a world"), TEXT("ow_a_test"), TEXT("1234"), TEXT("w1"), nullptr, TEXT(" needs a world key") },
		{ TEXT("an account token without one"), TEXT("ow_a_test"), TEXT("1234"), TEXT(""), nullptr, TEXT(" needs a world key") },
		{ TEXT("no key, a world"), TEXT(""), TEXT(""), TEXT("w1"), nullptr, TEXT(" needs a key") },
		{ TEXT("no key, no world"), TEXT(""), TEXT(""), TEXT(""), nullptr, TEXT(" needs a key") },
	};

	// What each node sends after the credential's headers, and its body; what the fake answers.
	struct FWriteCase
	{
		const TCHAR* Label;
		OWW::FMakeWrite Make;
		const TCHAR* Method;
		const TCHAR* Path;
		const TCHAR* After;  // the headers after the credential's
		const TCHAR* Body;   // as sent, or "<none>"
		int AnswerStatus;
		const char* AnswerBody;
	};
	const TCHAR* const Json = TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk");
	const FWriteCase Cases[] = {
		{ TEXT("CreateElement"), &OWW::MakeCreate, TEXT("POST"), TEXT("/character/"), Json, TEXT("{\"id\":\"c1\",\"name\":\"Ana\"}"), 201, R"({"id": "c1"})" },
		{ TEXT("UpsertElement"), &OWW::MakeUpsert, TEXT("PUT"), TEXT("/character/c1/"), Json, TEXT("{\"id\":\"c1\",\"name\":\"Ana\"}"), 200, R"({"id": "c1"})" },
		{ TEXT("PatchElement"), &OWW::MakePatch, TEXT("PATCH"), TEXT("/character/c1/"),
			TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk; If-Match=\"17\""), TEXT("{\"level\":4}"), 200, R"({"id": "c1"})" },
		{ TEXT("DeleteElement"), &OWW::MakeDelete, TEXT("DELETE"), TEXT("/character/c1/"), TEXT("; X-OW-Client=unreal-sdk; If-Match=\"17\""),
			TEXT("<none>"), 204, "" },
		{ TEXT("EditElementLinks"), &OWW::MakeLinks, TEXT("POST"), TEXT("/character/c1/links/friends"), Json,
			TEXT("{\"add\":[\"a1\"],\"remove\":[\"a2\"]}"), 200, R"({"id": "c1"})" },
		{ TEXT("BulkWrite"), &OWW::MakeBulk, TEXT("POST"), TEXT("/bulk/"), Json,
			TEXT("{\"items\":[{\"type\":\"character\",\"element\":{\"id\":\"c1\",\"name\":\"Ana\"}}],\"atomic\":false}"), 200,
			R"({"errors": false, "items": [{"status": 201, "id": "c1"}]})" },
	};
	// The route a refusal names: the method and the path.
	auto Route = [](const FWriteCase& Case) { return FString(Case.Method) + TEXT(" ") + Case.Path; };

	int32 SentCount = 0, RefusedCount = 0;
	for (const FWriteCase& Case : Cases)
	{
		for (const FCredential& Cred : Credentials)
		{
			const FString What = FString::Printf(TEXT("%s on %s"), Case.Label, Cred.Label);
			auto Run = std::make_shared<OWW::FWriteRun>();
			Run->Fake->Fallback = OWW::Respond(Case.AnswerStatus, Case.AnswerBody);
			OWW::Launch(Case.Make, OWW::MakeConfig(Cred.Key, Cred.Pin, Cred.WorldId), Run);
			const UOWClientWriteNodesProbe& P = *Run->Probe;
			if (Cred.Headers)
			{
				++SentCount;
				if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) continue;
				const OnlyWorlds::OWRequest& R = Run->Fake->Sent[0];
				TestEqual(What + TEXT(": the method"), OWConvert::Str(R.method), FString(Case.Method));
				TestEqual(What + TEXT(": the URL and the headers"), OWW::RequestLine(R), FString(Case.Path) + TEXT("|") + Cred.Headers + Case.After);
				TestEqual(What + TEXT(": the body"), OWW::BodyLine(R), FString(Case.Body));
				TestEqual(What + TEXT(": OnSuccess once"), P.Successes, 1);
				TestEqual(What + TEXT(": no OnFailure"), P.Failures, 0);
			}
			else
			{
				++RefusedCount;
				TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
				TestEqual(What + TEXT(": OnFailure once"), P.Failures, 1);
				TestEqual(What + TEXT(": no OnSuccess"), P.Successes, 0);
				TestTrue(What + TEXT(": a refusal"), P.Error.Kind == EOWApiErrorKind::Refused);
				TestEqual(What + TEXT(": status 0"), P.Error.Status, 0);
				TestTrue(What + TEXT(": the core's words, got ") + P.Error.Message, P.Error.Message.StartsWith(Route(Case) + Cred.Refusal));
				TestTrue(What + TEXT(": the refusal's line"), P.Error.Line.StartsWith(TEXT("OnlyWorlds refused: ")));
				TestTrue(What + TEXT(": nothing was sent, it says"), P.Error.Message.EndsWith(TEXT("Nothing was sent.")));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("WRITE REQUESTS %d node calls: %d sent as the core sends them, %d refused before sending"),
		SentCount + RefusedCount, SentCount, RefusedCount));
	TestEqual(TEXT("the matrix: sent"), SentCount, 24);
	TestEqual(TEXT("the matrix: refused"), RefusedCount, 24);
	return true;
}

// -- each node's body and options -------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesBodiesTest, "OnlyWorlds.Client.WriteNodes.Bodies", OWW::WriteTestFlags)
bool FOWClientWriteNodesBodiesTest::RunTest(const FString& Parameters)
{
	auto Only = [this](const FString& What, const std::shared_ptr<OWW::FWriteRun>& Run, const FString& WantLine, const FString& WantBody)
	{
		if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) return;
		TestEqual(What + TEXT(": the URL and the headers"), OWW::RequestLine(Run->Fake->Sent[0]), WantLine);
		TestEqual(What + TEXT(": the body"), OWW::BodyLine(Run->Fake->Sent[0]), WantBody);
		TestEqual(What + TEXT(": OnSuccess once"), Run->Probe->Successes, 1);
	};
	const FString Writer = TEXT("API-Key=ow_w_test; API-Pin=1234");

	// A create under an idempotency key: the read-only keys left out, a null base string sent as "", an extension
	// key's null kept, the id derived from the key (index 0), the key sent as Idempotency-Key.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(201, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::CreateElement(OWW::WriterConfig(), TEXT("character"),
			TEXT("{\"name\": \"Ana\", \"world\": \"w9\", \"change_seq\": 3, \"description\": null, \"x_mood\": null}"), TEXT("k1"));
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("CreateElement, keyed"), Run,
			TEXT("/character/|") + Writer + TEXT("; Content-Type=application/json; Idempotency-Key=k1; X-OW-Client=unreal-sdk"),
			OWConvert::Str("{\"name\":\"Ana\",\"description\":\"\",\"x_mood\":null,\"id\":\"" + OWW::KeyedId("k1", 0) + "\"}"));
	}

	// A create without a key or an id: a UUIDv7 minted, no Idempotency-Key.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(201, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::CreateElement(OWW::WriterConfig(), TEXT("character"), TEXT("{\"name\": \"Ana\"}"), TEXT(""));
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		if (TestEqual(TEXT("CreateElement, unkeyed: one request"), static_cast<int32>(Run->Fake->Sent.size()), 1))
		{
			const OnlyWorlds::OWRequest& R = Run->Fake->Sent[0];
			TestNull(TEXT("CreateElement, unkeyed: no Idempotency-Key"), R.Header("Idempotency-Key"));
			const OnlyWorlds::OWJsonValue SentBody = R.body ? OWW::ParseText(R.body->c_str()) : OnlyWorlds::OWJsonValue();
			const OnlyWorlds::OWJsonValue* MintedId = SentBody.Find("id");
			const std::string IdText = MintedId && MintedId->IsString() ? MintedId->Text() : std::string();
			TestEqual(TEXT("CreateElement, unkeyed: an id in the 8-4-4-4-12 form"), static_cast<int32>(IdText.size()), 36);
			TestTrue(TEXT("CreateElement, unkeyed: a version 7 id, got ") + OWConvert::Str(IdText), IdText.size() == 36 && IdText[14] == '7');
			TestEqual(TEXT("CreateElement, unkeyed: the name, then the id"), static_cast<int32>(SentBody.Size()), 2);
		}
	}

	// The config's client name and agent mark: X-OW-Agent before X-OW-Client, on a write.
	{
		FOWClientConfig With = OWW::WriterConfig();
		With.Client = TEXT("my-game");
		With.bAgent = true;
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"id": "c1"})");
		OWW::Launch(&OWW::MakeUpsert, With, Run);
		Only(TEXT("UpsertElement, an agent named my-game"), Run,
			TEXT("/character/c1/|") + Writer + TEXT("; Content-Type=application/json; X-OW-Agent=1; X-OW-Client=my-game"),
			TEXT("{\"id\":\"c1\",\"name\":\"Ana\"}"));
	}

	// A patch: without bHasIfMatch no If-Match (IfMatch ignored, a negative one too); with it, 0 is sent; numbers in
	// JSON.stringify's spelling, an integer past 2^53 as written.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::PatchElement(OWW::WriterConfig(), TEXT("character"), TEXT("c1"),
			TEXT("{\"level\": 12345678901234567890, \"weight\": 1.50}"), false, -1);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("PatchElement, unconditional"), Run, TEXT("/character/c1/|") + Writer + TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"level\":12345678901234567890,\"weight\":1.5}"));
	}
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::PatchElement(OWW::WriterConfig(), TEXT("character"), TEXT("c1"), TEXT("{\"level\": 4}"), true, 0);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("PatchElement, If-Match 0"), Run,
			TEXT("/character/c1/|") + Writer + TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk; If-Match=\"0\""), TEXT("{\"level\":4}"));
	}

	// A delete without bHasIfMatch: no If-Match, no body.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(204, "");
		UOWClientWrite* Node = UOWClientWrite::DeleteElement(OWW::WriterConfig(), TEXT("character"), TEXT("c1"), false, 17);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("DeleteElement, unconditional"), Run, TEXT("/character/c1/|") + Writer + TEXT("; X-OW-Client=unreal-sdk"), TEXT("<none>"));
	}

	// A link edit: an empty list is not sent (both empty: {}); with If-Match.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::EditElementLinks(OWW::WriterConfig(), TEXT("character"), TEXT("c1"), TEXT("friends"),
			{ TEXT("a1"), TEXT("a2") }, {}, true, 9);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("EditElementLinks, add only"), Run,
			TEXT("/character/c1/links/friends|") + Writer + TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk; If-Match=\"9\""),
			TEXT("{\"add\":[\"a1\",\"a2\"]}"));
	}
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"id": "c1"})");
		UOWClientWrite* Node = UOWClientWrite::EditElementLinks(OWW::WriterConfig(), TEXT("character"), TEXT("c1"), TEXT("friends"), {}, {}, false, 0);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("EditElementLinks, both lists empty"), Run,
			TEXT("/character/c1/links/friends|") + Writer + TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk"), TEXT("{}"));
	}

	// A bulk under a key, atomic: an element without an id gets the key's id at its position, one with an id keeps it,
	// "" is the core's null, an empty element.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": false, "items": []})");
		UOWClientBulk* Node = UOWClientBulk::BulkWrite(OWW::WriterConfig(),
			{ OWW::BulkItem(TEXT("character"), TEXT("{\"name\": \"Ana\"}")), OWW::BulkItem(TEXT("location"), TEXT("{\"id\": \"l1\", \"name\": \"Port\"}")),
			  OWW::BulkItem(TEXT("character"), TEXT("")) },
			true, TEXT("bk"));
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("BulkWrite, keyed and atomic"), Run,
			TEXT("/bulk/|") + Writer + TEXT("; Content-Type=application/json; Idempotency-Key=bk; X-OW-Client=unreal-sdk"),
			OWConvert::Str("{\"items\":[{\"type\":\"character\",\"element\":{\"name\":\"Ana\",\"id\":\"" + OWW::KeyedId("bk", 0) +
				"\"}},{\"type\":\"location\",\"element\":{\"id\":\"l1\",\"name\":\"Port\"}},{\"type\":\"character\",\"element\":{\"id\":\"" +
				OWW::KeyedId("bk", 2) + "\"}}],\"atomic\":true}"));
	}

	// A bulk with bCreateOnly: create_only true after atomic; off (above and in every credential's case), not sent.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": false, "items": []})");
		UOWClientBulk* Node = UOWClientBulk::BulkWrite(OWW::WriterConfig(),
			{ OWW::BulkItem(TEXT("location"), TEXT("{\"id\": \"l1\", \"name\": \"Port\"}")) }, false, TEXT(""), true);
		Run->Probe->Watch(Node);
		OWW::Activated(Node, Run);
		Only(TEXT("BulkWrite, create only"), Run, TEXT("/bulk/|") + Writer + TEXT("; Content-Type=application/json; X-OW-Client=unreal-sdk"),
			TEXT("{\"items\":[{\"type\":\"location\",\"element\":{\"id\":\"l1\",\"name\":\"Port\"}}],\"atomic\":false,\"create_only\":true}"));
	}

	// The config's new members, by default: the core's own name, no agent mark.
	TestEqual(TEXT("FOWClientConfig's Client: \"\" (the core's unreal-sdk)"), FOWClientConfig().Client, FString());
	TestFalse(TEXT("FOWClientConfig's bAgent: off"), FOWClientConfig().bAgent);
	return true;
}

// -- what a success gives -----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesAnswersTest, "OnlyWorlds.Client.WriteNodes.Answers", OWW::WriteTestFlags)
bool FOWClientWriteNodesAnswersTest::RunTest(const FString& Parameters)
{
	auto NoError = [this](const FString& What, const FOWApiError& E)
	{
		TestTrue(What + TEXT(": Error is empty on OnSuccess"), E.Kind == EOWApiErrorKind::None && E.Status == 0 && E.Line.IsEmpty() && !E.bIsProposed);
	};

	// The element as keel wrote it, as JSON text, its numbers as served.
	{
		const char* Served = R"({"id": "c1", "name": "Ana", "change_seq": 18, "big": 12345678901234567890, "half": 0.50})";
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(201, Served);
		OWW::Launch(&OWW::MakeCreate, OWW::WriterConfig(), Run);
		TestEqual(TEXT("CreateElement: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("CreateElement: no OnFailure"), Run->Probe->Failures, 0);
		TestTrue(TEXT("CreateElement: the element as JSON text, got ") + Run->Probe->Json, OWW::SameJsonText(Run->Probe->Json, Served));
		TestTrue(TEXT("CreateElement: a number past 2^53 as served"), Run->Probe->Json.Contains(TEXT("12345678901234567890")));
		TestTrue(TEXT("CreateElement: a number's text kept"), Run->Probe->Json.Contains(TEXT("0.50")));
		NoError(TEXT("CreateElement"), Run->Probe->Error);
	}

	// A delete answers no element: Json "", on a 204 and on a 200 with a body alike.
	for (const int Code : { 204, 200 })
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(Code, Code == 204 ? "" : R"({"deleted": true})");
		OWW::Launch(&OWW::MakeDelete, OWW::WriterConfig(), Run);
		const FString What = FString::Printf(TEXT("DeleteElement, %d"), Code);
		TestEqual(What + TEXT(": OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(What + TEXT(": no element"), Run->Probe->Json, FString());
		NoError(What, Run->Probe->Error);
	}

	// A bulk with a created slot, a failed one and a proposed one, replayed: each slot read back, the failed indices,
	// the count, and keel's flag. keel answers 200, so it is OnSuccess: the slots say what landed.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		OnlyWorlds::OWResponse Served = OWW::Respond(200, R"({"errors": true, "items": [
			{"status": 201, "id": "c1", "created_at": "2026-11-03T07:00:00Z", "updated_at": "2026-11-03T07:00:01Z"},
			{"status": 422, "id": "c2", "error": {"type": "invalid_request", "code": "invalid_field", "message": "level is a number.", "param": "level"}},
			{"status": 202, "id": "c3", "proposal": {"id": "p1", "status": "pending"}}]})");
		Served.headers.emplace_back("idempotent-replay", "True");
		Run->Fake->Fallback = Served;
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		const FOWBulkResult& B = Run->Probe->Bulk;
		TestEqual(TEXT("BulkWrite: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("BulkWrite: no OnFailure"), Run->Probe->Failures, 0);
		NoError(TEXT("BulkWrite"), Run->Probe->Error);
		TestTrue(TEXT("BulkWrite: bErrors, keel's flag"), B.bErrors);
		TestTrue(TEXT("BulkWrite: bWasReplay, from Idempotent-Replay by any case"), B.bWasReplay);
		TestFalse(TEXT("BulkWrite: not bAllSucceeded"), B.bAllSucceeded);
		TestEqual(TEXT("BulkWrite: SucceededCount, the proposal counted"), B.SucceededCount, 2);
		if (TestEqual(TEXT("BulkWrite: one failed index"), B.Failed.Num(), 1)) TestEqual(TEXT("BulkWrite: the failed index"), B.Failed[0], 1);
		if (TestEqual(TEXT("BulkWrite: three slots, in the order sent"), B.Items.Num(), 3))
		{
			const FOWBulkSlot& Made = B.Items[0];
			TestEqual(TEXT("slot 0: Status"), Made.Status, 201);
			TestEqual(TEXT("slot 0: Id"), Made.Id, FString(TEXT("c1")));
			TestEqual(TEXT("slot 0: CreatedAt"), Made.CreatedAt, FString(TEXT("2026-11-03T07:00:00Z")));
			TestEqual(TEXT("slot 0: UpdatedAt"), Made.UpdatedAt, FString(TEXT("2026-11-03T07:00:01Z")));
			TestTrue(TEXT("slot 0: bSucceeded"), Made.bSucceeded);
			TestFalse(TEXT("slot 0: not bIsProposed"), Made.bIsProposed);
			TestTrue(TEXT("slot 0: no error"), Made.Error.Kind == EOWApiErrorKind::None);
			TestEqual(TEXT("slot 0: no proposal"), Made.Proposal, FString());

			const FOWBulkSlot& Bad = B.Items[1];
			TestEqual(TEXT("slot 1: Status"), Bad.Status, 422);
			TestEqual(TEXT("slot 1: Id"), Bad.Id, FString(TEXT("c2")));
			TestFalse(TEXT("slot 1: not bSucceeded"), Bad.bSucceeded);
			TestFalse(TEXT("slot 1: not bIsProposed"), Bad.bIsProposed);
			TestTrue(TEXT("slot 1: an error of kind Api"), Bad.Error.Kind == EOWApiErrorKind::Api);
			TestEqual(TEXT("slot 1: the error's Status, the slot's"), Bad.Error.Status, 422);
			TestEqual(TEXT("slot 1: Code"), Bad.Error.Code, FString(TEXT("invalid_field")));
			TestEqual(TEXT("slot 1: Param"), Bad.Error.Param, FString(TEXT("level")));
			TestEqual(TEXT("slot 1: Message"), Bad.Error.Message, FString(TEXT("level is a number.")));
			TestEqual(TEXT("slot 1: CreatedAt \"\""), Bad.CreatedAt, FString());

			const FOWBulkSlot& Asked = B.Items[2];
			TestEqual(TEXT("slot 2: Status"), Asked.Status, 202);
			TestTrue(TEXT("slot 2: bSucceeded (a proposal is a 2xx)"), Asked.bSucceeded);
			TestTrue(TEXT("slot 2: bIsProposed"), Asked.bIsProposed);
			TestTrue(TEXT("slot 2: the proposal row as JSON text, got ") + Asked.Proposal, OWW::SameJsonText(Asked.Proposal, OWW::ProposalRow));
			TestTrue(TEXT("slot 2: no error"), Asked.Error.Kind == EOWApiErrorKind::None);
			TestEqual(TEXT("slot 1: no Limit"), Bad.Error.Limit, FString());
		}
	}

	// A slot refused for a size limit carries the limit on its Error.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": true, "items": [
			{"status": 422, "error": {"type": "invalid_request", "code": "invalid_request", "message": "Too many links.", "limit": "element_link_ids"}}]})");
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		const FOWBulkResult& B = Run->Probe->Bulk;
		TestEqual(TEXT("BulkWrite, a size refusal: OnSuccess once"), Run->Probe->Successes, 1);
		if (TestEqual(TEXT("BulkWrite, a size refusal: one slot"), B.Items.Num(), 1))
		{
			TestEqual(TEXT("BulkWrite, a size refusal: Code"), B.Items[0].Error.Code, FString(TEXT("invalid_request")));
			TestEqual(TEXT("BulkWrite, a size refusal: Limit"), B.Items[0].Error.Limit, FString(TEXT("element_link_ids")));
		}
	}

	// A bulk where every slot landed, not replayed.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": false, "items": [{"status": 201, "id": "c1"}]})");
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		const FOWBulkResult& B = Run->Probe->Bulk;
		TestEqual(TEXT("BulkWrite, all landed: OnSuccess once"), Run->Probe->Successes, 1);
		TestTrue(TEXT("BulkWrite, all landed: bAllSucceeded"), B.bAllSucceeded);
		TestFalse(TEXT("BulkWrite, all landed: not bErrors"), B.bErrors);
		TestFalse(TEXT("BulkWrite, all landed: not bWasReplay"), B.bWasReplay);
		TestEqual(TEXT("BulkWrite, all landed: no failed index"), B.Failed.Num(), 0);
		TestEqual(TEXT("BulkWrite, all landed: SucceededCount"), B.SucceededCount, 1);
	}
	return true;
}

// -- keel's error envelope, a 412's current element, a 202 proposal ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesErrorsTest, "OnlyWorlds.Client.WriteNodes.Errors", OWW::WriteTestFlags)
bool FOWClientWriteNodesErrorsTest::RunTest(const FString& Parameters)
{
	auto Failed = [this](const FString& What, const std::shared_ptr<OWW::FWriteRun>& Run)
	{
		TestEqual(What + TEXT(": OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), Run->Probe->Successes, 0);
		TestEqual(What + TEXT(": no Json"), Run->Probe->Json, FString());
	};

	// keel's envelope on a create.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(422, R"({"error": {"type": "invalid_request", "code": "invalid_field", "message": "level is a number.", "param": "level"}})");
		OWW::Launch(&OWW::MakeCreate, OWW::WriterConfig(), Run);
		const FOWApiError& E = Run->Probe->Error;
		Failed(TEXT("422"), Run);
		TestTrue(TEXT("422: kind Api"), E.Kind == EOWApiErrorKind::Api);
		TestEqual(TEXT("422: Status"), E.Status, 422);
		TestEqual(TEXT("422: Code"), E.Code, FString(TEXT("invalid_field")));
		TestEqual(TEXT("422: Param"), E.Param, FString(TEXT("level")));
		TestEqual(TEXT("422: Line"), E.Line, FString(TEXT("OnlyWorlds API 422 [invalid_field] (field: level): level is a number.")));
		TestFalse(TEXT("422: not bIsProposed"), E.bIsProposed);
		TestEqual(TEXT("422: no proposal"), E.Proposal, FString());
		TestEqual(TEXT("422: no current element"), E.Current, FString());
	}

	// A stale If-Match: a 412, the element as keel holds it now in Current.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(412, R"({"error": {"type": "conflict", "code": "precondition_failed", "message": "Changed since."},
			"current": {"id": "c1", "change_seq": 19, "name": "Ana"}})");
		OWW::Launch(&OWW::MakePatch, OWW::WriterConfig(), Run);
		const FOWApiError& E = Run->Probe->Error;
		Failed(TEXT("412"), Run);
		TestTrue(TEXT("412: kind Api"), E.Kind == EOWApiErrorKind::Api);
		TestEqual(TEXT("412: Status"), E.Status, 412);
		TestEqual(TEXT("412: Code"), E.Code, FString(TEXT("precondition_failed")));
		TestTrue(TEXT("412: Current, the element now, got ") + E.Current, OWW::SameJsonText(E.Current, R"({"id": "c1", "change_seq": 19, "name": "Ana"})"));
		TestFalse(TEXT("412: not bIsProposed"), E.bIsProposed);
	}

	// A 202 on each write that may become a proposal: OnFailure, kind Api, status 202, bIsProposed, the row.
	struct FProposable
	{
		const TCHAR* Label;
		OWW::FMakeWrite Make;
		const TCHAR* Route;
	};
	for (const FProposable& W : { FProposable{ TEXT("UpsertElement"), &OWW::MakeUpsert, TEXT("PUT /character/c1/") },
		FProposable{ TEXT("PatchElement"), &OWW::MakePatch, TEXT("PATCH /character/c1/") },
		FProposable{ TEXT("EditElementLinks"), &OWW::MakeLinks, TEXT("POST /character/c1/links/friends") } })
	{
		const FString What = FString(W.Label) + TEXT(", 202");
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(202, OWW::ProposalBody);
		OWW::Launch(W.Make, OWW::WriterConfig(), Run);
		const FOWApiError& E = Run->Probe->Error;
		Failed(What, Run);
		TestTrue(What + TEXT(": kind Api"), E.Kind == EOWApiErrorKind::Api);
		TestEqual(What + TEXT(": Status"), E.Status, 202);
		TestTrue(What + TEXT(": bIsProposed"), E.bIsProposed);
		TestTrue(What + TEXT(": the proposal row as JSON text, got ") + E.Proposal, OWW::SameJsonText(E.Proposal, OWW::ProposalRow));
		TestTrue(What + TEXT(": the core's words, got ") + E.Message, E.Message.StartsWith(FString(W.Route) + TEXT(" became a proposal (202)")));
		TestFalse(What + TEXT(": not retryable"), E.bRetryable);
	}

	// A 202 with no proposal row: kind Answer, never the element, never a made-up proposal.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(202, R"({"id": "c1"})");
		OWW::Launch(&OWW::MakeUpsert, OWW::WriterConfig(), Run);
		Failed(TEXT("202 without a proposal"), Run);
		TestTrue(TEXT("202 without a proposal: kind Answer"), Run->Probe->Error.Kind == EOWApiErrorKind::Answer);
		TestEqual(TEXT("202 without a proposal: Status"), Run->Probe->Error.Status, 202);
		TestFalse(TEXT("202 without a proposal: not bIsProposed"), Run->Probe->Error.bIsProposed);
		TestEqual(TEXT("202 without a proposal: no proposal"), Run->Probe->Error.Proposal, FString());
	}

	// No answer came, on a delete.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		OnlyWorlds::OWResponse NoAnswer;
		NoAnswer.failure = "no answer: ConnectionError";
		Run->Fake->Fallback = NoAnswer;
		OWW::Launch(&OWW::MakeDelete, OWW::WriterConfig(), Run);
		Failed(TEXT("no answer"), Run);
		TestTrue(TEXT("no answer: kind Transport"), Run->Probe->Error.Kind == EOWApiErrorKind::Transport);
		TestEqual(TEXT("no answer: Message"), Run->Probe->Error.Message,
			FString(TEXT("OnlyWorlds request failed: DELETE /character/c1/: no answer: ConnectionError")));
	}

	// A bulk refused whole by keel: OnFailure with the code, an empty outcome.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(400, R"({"error": {"type": "invalid_request", "code": "bulk_too_large", "message": "At most 500 items."}})");
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		TestEqual(TEXT("bulk 400: OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(TEXT("bulk 400: no OnSuccess"), Run->Probe->Successes, 0);
		TestEqual(TEXT("bulk 400: Code"), Run->Probe->Error.Code, FString(TEXT("bulk_too_large")));
		TestEqual(TEXT("bulk 400: Status"), Run->Probe->Error.Status, 400);
		TestEqual(TEXT("bulk 400: no slots"), Run->Probe->Bulk.Items.Num(), 0);
	}

	// A bulk's 200 that is not the answer: kind Answer.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": false, "items": {}})");
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		TestEqual(TEXT("bulk, items not a list: OnFailure once"), Run->Probe->Failures, 1);
		TestTrue(TEXT("bulk, items not a list: kind Answer"), Run->Probe->Error.Kind == EOWApiErrorKind::Answer);
		TestEqual(TEXT("bulk, items not a list: no slots"), Run->Probe->Bulk.Items.Num(), 0);
	}
	return true;
}

// -- the refusals, before anything is sent ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesRefusalsTest, "OnlyWorlds.Client.WriteNodes.Refusals", OWW::WriteTestFlags)
bool FOWClientWriteNodesRefusalsTest::RunTest(const FString& Parameters)
{
	// Each answered inside Activate (the fake holds what it is sent), nothing sent.
	int32 Count = 0;
	auto Refused = [this, &Count](const FString& What, const std::shared_ptr<OWW::FWriteRun>& Run, const TCHAR* Starts)
	{
		++Count;
		const UOWClientWriteNodesProbe& P = *Run->Probe;
		TestEqual(What + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
		TestEqual(What + TEXT(": OnFailure once, inside Activate"), P.Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), P.Successes, 0);
		TestTrue(What + TEXT(": kind Refused"), P.Error.Kind == EOWApiErrorKind::Refused);
		TestEqual(What + TEXT(": status 0"), P.Error.Status, 0);
		TestTrue(What + TEXT(": the words, got ") + P.Error.Message, P.Error.Message.StartsWith(Starts));
		TestTrue(What + TEXT(": nothing was sent, it says"), P.Error.Message.EndsWith(TEXT("Nothing was sent.")));
		TestTrue(What + TEXT(": the line"), P.Error.Line.StartsWith(TEXT("OnlyWorlds refused: ")));
	};
	auto Write = [](UOWClientWrite* Node)
	{
		auto Run = OWW::HeldRun();
		Run->Probe->Watch(Node);
		return OWW::Activated(Node, Run);
	};
	auto Bulk = [](UOWClientBulk* Node)
	{
		auto Run = OWW::HeldRun();
		Run->Probe->Watch(Node);
		return OWW::Activated(Node, Run);
	};
	const FOWClientConfig Writer = OWW::WriterConfig();

	// The credential: an account token and no key write nothing.
	Refused(TEXT("CreateElement on an account token"), OWW::Launch(&OWW::MakeCreate, OWW::MakeConfig(TEXT("ow_a_test"), TEXT(""), TEXT("w1")), OWW::HeldRun()),
		TEXT("POST /character/ needs a world key"));
	Refused(TEXT("BulkWrite on an account token"), OWW::Launch(&OWW::MakeBulk, OWW::MakeConfig(TEXT("ow_a_test"), TEXT(""), TEXT("w1")), OWW::HeldRun()),
		TEXT("POST /bulk/ needs a world key"));
	Refused(TEXT("UpsertElement with no key, a world"), OWW::Launch(&OWW::MakeUpsert, OWW::MakeConfig(TEXT(""), TEXT(""), TEXT("w1")), OWW::HeldRun()),
		TEXT("PUT /character/c1/ needs a key"));

	// A config the core will not build: a client name that is not keel's shape; it wins over a body that is not JSON.
	{
		FOWClientConfig Named = Writer;
		Named.Client = TEXT("My Game");
		Refused(TEXT("a client name with capitals and a space"), OWW::Launch(&OWW::MakeDelete, Named, OWW::HeldRun()),
			TEXT("OWClient was not built: client must be lower case letters"));
		Refused(TEXT("a client name refused, and a body that is not JSON"),
			Write(UOWClientWrite::CreateElement(Named, TEXT("character"), TEXT("{oops"), TEXT(""))), TEXT("OWClient was not built: "));
	}

	// The body: not JSON text (the node's own word), not an object, "" (the core's null), a known-of stub.
	Refused(TEXT("CreateElement, not JSON text"), Write(UOWClientWrite::CreateElement(Writer, TEXT("character"), TEXT("{oops"), TEXT(""))),
		TEXT("create: ElementJson is not JSON text"));
	Refused(TEXT("PatchElement, not JSON text"), Write(UOWClientWrite::PatchElement(Writer, TEXT("character"), TEXT("c1"), TEXT("{"), false, 0)),
		TEXT("patch: PartialJson is not JSON text"));
	Refused(TEXT("UpsertElement, an array"), Write(UOWClientWrite::UpsertElement(Writer, TEXT("character"), TEXT("c1"), TEXT("[1]"))),
		TEXT("upsert: the element is a JSON object; got an array."));
	Refused(TEXT("CreateElement, \"\""), Write(UOWClientWrite::CreateElement(Writer, TEXT("character"), TEXT(""), TEXT(""))),
		TEXT("create: the element is a JSON object; got null."));
	Refused(TEXT("PatchElement, a known-of stub"),
		Write(UOWClientWrite::PatchElement(Writer, TEXT("character"), TEXT("c1"), TEXT("{\"name\": \"\", \"known_of\": true}"), false, 0)),
		TEXT("patch: this body is a known-of stub"));

	// A value that is not one path segment.
	Refused(TEXT("CreateElement, a type in capitals"), Write(UOWClientWrite::CreateElement(Writer, TEXT("Character"), TEXT("{\"name\": \"Ana\"}"), TEXT(""))),
		TEXT("type \"Character\" is not one path segment."));
	Refused(TEXT("DeleteElement, an id with a slash"), Write(UOWClientWrite::DeleteElement(Writer, TEXT("character"), TEXT("c1/hidden"), false, 0)),
		TEXT("id \"c1/hidden\" is not one path segment."));
	Refused(TEXT("EditElementLinks, a field with a slash"),
		Write(UOWClientWrite::EditElementLinks(Writer, TEXT("character"), TEXT("c1"), TEXT("friends/x"), { TEXT("a1") }, {}, false, 0)),
		TEXT("field \"friends/x\" is not one path segment."));

	// A negative If-Match, given.
	Refused(TEXT("PatchElement, If-Match -1"), Write(UOWClientWrite::PatchElement(Writer, TEXT("character"), TEXT("c1"), TEXT("{\"level\": 4}"), true, -1)),
		TEXT("patch: if_match must be a whole number"));
	Refused(TEXT("DeleteElement, If-Match -1"), Write(UOWClientWrite::DeleteElement(Writer, TEXT("character"), TEXT("c1"), true, -1)),
		TEXT("delete: if_match must be a whole number"));
	Refused(TEXT("EditElementLinks, If-Match -1"),
		Write(UOWClientWrite::EditElementLinks(Writer, TEXT("character"), TEXT("c1"), TEXT("friends"), { TEXT("a1") }, {}, true, -1)),
		TEXT("edit links: if_match must be a whole number"));

	// A bulk: no items, an item without a type, an item not JSON text, not an object, a known-of stub.
	Refused(TEXT("BulkWrite, no items"), Bulk(UOWClientBulk::BulkWrite(Writer, {}, false, TEXT(""))), TEXT("bulk: needs at least one item."));
	Refused(TEXT("BulkWrite, an item without a type"),
		Bulk(UOWClientBulk::BulkWrite(Writer, { OWW::BulkItem(TEXT("character"), TEXT("{}")), OWW::BulkItem(TEXT(""), TEXT("{}")) }, false, TEXT(""))),
		TEXT("bulk: items[1] needs a type."));
	Refused(TEXT("BulkWrite, an item not JSON text"),
		Bulk(UOWClientBulk::BulkWrite(Writer, { OWW::BulkItem(TEXT("character"), TEXT("{oops")) }, false, TEXT(""))),
		TEXT("bulk: items[0]: ElementJson is not JSON text"));
	Refused(TEXT("BulkWrite, an item a string"),
		Bulk(UOWClientBulk::BulkWrite(Writer, { OWW::BulkItem(TEXT("character"), TEXT("\"Ana\"")) }, false, TEXT(""))),
		TEXT("bulk: items[0]: the element is a JSON object; got a string."));
	Refused(TEXT("BulkWrite, an item a known-of stub"),
		Bulk(UOWClientBulk::BulkWrite(Writer, { OWW::BulkItem(TEXT("character"), TEXT("{\"id\": \"c1\", \"known_of\": true}")) }, false, TEXT(""))),
		TEXT("bulk: items[0]: this body is a known-of stub"));

	TestEqual(TEXT("the refusals counted"), Count, 21);
	return true;
}

// -- one answer only ----------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesOnceTest, "OnlyWorlds.Client.WriteNodes.Once", OWW::WriteTestFlags)
bool FOWClientWriteNodesOnceTest::RunTest(const FString& Parameters)
{
	// A transport that answers every request twice: one answer each.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = OWW::Respond(201, R"({"id": "c1"})");
		OWW::Launch(&OWW::MakeCreate, OWW::WriterConfig(), Run);
		TestEqual(TEXT("a create answered twice: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("a create answered twice: no OnFailure"), Run->Probe->Failures, 0);
	}
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = OWW::Respond(202, OWW::ProposalBody);
		OWW::Launch(&OWW::MakePatch, OWW::WriterConfig(), Run);
		TestEqual(TEXT("a proposal answered twice: OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(TEXT("a proposal answered twice: no OnSuccess"), Run->Probe->Successes, 0);
	}
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Times = 2;
		Run->Fake->Fallback = OWW::Respond(200, R"({"errors": false, "items": [{"status": 201, "id": "c1"}]})");
		OWW::Launch(&OWW::MakeBulk, OWW::WriterConfig(), Run);
		TestEqual(TEXT("a bulk answered twice: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("a bulk answered twice: no OnFailure"), Run->Probe->Failures, 0);
	}

	// Activate twice: one request, one answer.
	{
		auto Run = std::make_shared<OWW::FWriteRun>();
		Run->Fake->Fallback = OWW::Respond(204, "");
		OWW::Launch(&OWW::MakeDelete, OWW::WriterConfig(), Run);
		if (UOWClientNode* Node = Run->Node.Get()) Node->Activate();
		TestEqual(TEXT("a delete activated twice: one request"), static_cast<int32>(Run->Fake->Sent.size()), 1);
		TestEqual(TEXT("a delete activated twice: OnSuccess once"), Run->Probe->Successes, 1);
	}
	{
		auto Run = OWW::HeldRun();
		OWW::Launch(&OWW::MakeCreate, OWW::MakeConfig(TEXT("")), Run);
		if (UOWClientNode* Node = Run->Node.Get()) Node->Activate();
		TestEqual(TEXT("a refusal activated twice: OnFailure once"), Run->Probe->Failures, 1);
	}
	return true;
}

// -- a write node made and activated as a graph does ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesReflectionTest, "OnlyWorlds.Client.WriteNodes.Reflection", OWW::WriteTestFlags)
bool FOWClientWriteNodesReflectionTest::RunTest(const FString& Parameters)
{
	// CreateElement found by name, its flags, its frame from the UFunction, the config set member by member (a Make
	// node: the client's name and the agent mark among them), each other parameter by its property, ProcessEvent on the
	// class default object; the node read from the return; the delegates bound; Activate found by name and called by
	// ProcessEvent, as UK2Node_AsyncAction's expansion calls it.
	UFunction* Factory = UOWClientWrite::StaticClass()->FindFunctionByName(TEXT("CreateElement"));
	if (!TestNotNull(TEXT("UFUNCTION CreateElement"), Factory)) return false;
	TestTrue(TEXT("CreateElement: a static, public, native Blueprint function"),
		Factory->HasAllFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable));
	TestFalse(TEXT("CreateElement: not pure"), Factory->HasAnyFunctionFlags(FUNC_BlueprintPure));
#if WITH_METADATA
	TestTrue(TEXT("CreateElement: the async node's, not a plain call"), Factory->HasMetaData(TEXT("BlueprintInternalUseOnly")));
#endif

	uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Factory->ParmsSize, 1), Factory->GetMinAlignment()));
	Factory->InitializeStruct(Frame);
	UOWClientWrite* Made = nullptr;
	FStructProperty* ConfigParam = CastField<FStructProperty>(Factory->FindPropertyByName(TEXT("Config")));
	auto SetText = [this, Factory, Frame](const TCHAR* Param, const FString& Value)
	{
		FStrProperty* P = CastField<FStrProperty>(Factory->FindPropertyByName(Param));
		if (TestNotNull(FString::Printf(TEXT("CreateElement has an FString parameter %s"), Param), P))
			*P->ContainerPtrToValuePtr<FString>(Frame) = Value;
	};
	if (TestTrue(TEXT("CreateElement has an FOWClientConfig parameter Config"), ConfigParam && ConfigParam->Struct == FOWClientConfig::StaticStruct()))
	{
		void* Data = ConfigParam->ContainerPtrToValuePtr<void>(Frame);
		const UScriptStruct* S = FOWClientConfig::StaticStruct();
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("ApiKey"), TEXT("ow_w_test"));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("ApiPin"), TEXT("1234"));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("BaseUrl"), OWConvert::Str(std::string(OWW::KeelRoot)));
		OWBlueprintTest::SetMember<FStrProperty>(*this, S, Data, TEXT("Client"), TEXT("my-game"));
		OWBlueprintTest::SetBoolMember(*this, S, Data, TEXT("bAgent"), true);
		SetText(TEXT("Type"), TEXT("character"));
		SetText(TEXT("ElementJson"), TEXT("{\"name\": \"Ana\"}"));
		SetText(TEXT("IdempotencyKey"), TEXT("k1"));
		UOWClientWrite::StaticClass()->GetDefaultObject()->ProcessEvent(Factory, Frame);
		const FObjectProperty* Ret = CastField<FObjectProperty>(Factory->GetReturnProperty());
		if (TestTrue(TEXT("CreateElement returns a UOWClientWrite"), Ret && Ret->PropertyClass == UOWClientWrite::StaticClass()))
			Made = Cast<UOWClientWrite>(Ret->GetObjectPropertyValue_InContainer(Frame));
	}
	Factory->DestroyStruct(Frame);
	FMemory::Free(Frame);
	if (!TestNotNull(TEXT("the node"), Made)) return false;

	// The delegates a graph binds: Json and Error on a write, Outcome and Error on a bulk; the error's proposal mark.
	const FMulticastDelegateProperty* Success = CastField<FMulticastDelegateProperty>(UOWClientWrite::StaticClass()->FindPropertyByName(TEXT("OnSuccess")));
	if (TestNotNull(TEXT("OnSuccess, a multicast delegate"), Success))
	{
		const UFunction* Sig = Success->SignatureFunction;
		TestTrue(TEXT("OnSuccess's Json, a string"), Sig && CastField<FStrProperty>(Sig->FindPropertyByName(TEXT("Json"))) != nullptr);
		const FStructProperty* ErrorPin = Sig ? CastField<FStructProperty>(Sig->FindPropertyByName(TEXT("Error"))) : nullptr;
		TestTrue(TEXT("OnSuccess's Error, an FOWApiError"), ErrorPin && ErrorPin->Struct == FOWApiError::StaticStruct());
	}
	TestNotNull(TEXT("FOWApiError's bIsProposed, a bool"), CastField<FBoolProperty>(FOWApiError::StaticStruct()->FindPropertyByName(TEXT("bIsProposed"))));
	const FMulticastDelegateProperty* BulkSuccess = CastField<FMulticastDelegateProperty>(UOWClientBulk::StaticClass()->FindPropertyByName(TEXT("OnSuccess")));
	if (TestNotNull(TEXT("UOWClientBulk's OnSuccess, a multicast delegate"), BulkSuccess))
	{
		const UFunction* Sig = BulkSuccess->SignatureFunction;
		const FStructProperty* OutcomePin = Sig ? CastField<FStructProperty>(Sig->FindPropertyByName(TEXT("Outcome"))) : nullptr;
		TestTrue(TEXT("UOWClientBulk's Outcome, an FOWBulkResult"), OutcomePin && OutcomePin->Struct == FOWBulkResult::StaticStruct());
	}

	auto Run = OWW::HeldRun();
	Run->Fake->Fallback = OWW::Respond(201, R"({"id": "c9", "name": "Ana", "change_seq": 1})");
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
		TestEqual(TEXT("the method"), OWConvert::Str(R.method), FString(TEXT("POST")));
		TestEqual(TEXT("the request"), OWW::RequestLine(R),
			FString(TEXT("/character/|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json; Idempotency-Key=k1; X-OW-Agent=1; X-OW-Client=my-game")));
		TestEqual(TEXT("the body"), OWW::BodyLine(R), OWConvert::Str("{\"name\":\"Ana\",\"id\":\"" + OWW::KeyedId("k1", 0) + "\"}"));
	}

	auto Frames = std::make_shared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Frames]()
	{
		++*Frames;
		Run->Fake->Pump();
		if (Run->Probe->Answers() == 0 && *Frames < OWW::FrameLimit) return false;

		TestEqual(TEXT("OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("no OnFailure"), Run->Probe->Failures, 0);
		TestTrue(TEXT("the element as keel wrote it, got ") + Run->Probe->Json,
			OWW::SameJsonText(Run->Probe->Json, R"({"id": "c9", "name": "Ana", "change_seq": 1})"));
		TestTrue(TEXT("let go once answered"), Run->Node.IsValid() && !Run->Node->IsRooted());
		return true;
	}));
	return true;
}

// -- the pins a graph's node gets (editor only) -----------------------------------------------------------------------

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWClientWriteNodesPinsTest, "OnlyWorlds.Client.WriteNodes.Pins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FOWClientWriteNodesPinsTest::RunTest(const FString& Parameters)
{
	const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(TEXT("OWClientWriteNodesProbe")));
	TStrongObjectPtr<UBlueprint> BP(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Unique,
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None));
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP.Get()) : nullptr;
	if (!TestNotNull(TEXT("a Blueprint with an event graph"), Graph)) return false;

	const FName PinString = UEdGraphSchema_K2::PC_String, PinStruct = UEdGraphSchema_K2::PC_Struct,
		PinBool = UEdGraphSchema_K2::PC_Boolean, PinInt64 = UEdGraphSchema_K2::PC_Int64, PinExec = UEdGraphSchema_K2::PC_Exec;
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
	const FPin TypePin{ TEXT("Type"), PinString, nullptr, false };
	const FPin IdPin{ TEXT("Id"), PinString, nullptr, false };
	const FPin HasIfMatchPin{ TEXT("bHasIfMatch"), PinBool, nullptr, false };
	const FPin IfMatchPin{ TEXT("IfMatch"), PinInt64, nullptr, false };
	const FPin KeyPin{ TEXT("IdempotencyKey"), PinString, nullptr, false };
	const TArray<FNode> Nodes = {
		{ UOWClientWrite::StaticClass(), TEXT("CreateElement"), { ConfigPin, TypePin, { TEXT("ElementJson"), PinString, nullptr, false }, KeyPin },
			{ JsonPin, ErrorPin } },
		{ UOWClientWrite::StaticClass(), TEXT("UpsertElement"), { ConfigPin, TypePin, IdPin, { TEXT("ElementJson"), PinString, nullptr, false } },
			{ JsonPin, ErrorPin } },
		{ UOWClientWrite::StaticClass(), TEXT("PatchElement"),
			{ ConfigPin, TypePin, IdPin, { TEXT("PartialJson"), PinString, nullptr, false }, HasIfMatchPin, IfMatchPin }, { JsonPin, ErrorPin } },
		{ UOWClientWrite::StaticClass(), TEXT("DeleteElement"), { ConfigPin, TypePin, IdPin, HasIfMatchPin, IfMatchPin }, { JsonPin, ErrorPin } },
		{ UOWClientWrite::StaticClass(), TEXT("EditElementLinks"),
			{ ConfigPin, TypePin, IdPin, { TEXT("Field"), PinString, nullptr, false }, { TEXT("Add"), PinString, nullptr, true },
			  { TEXT("Remove"), PinString, nullptr, true }, HasIfMatchPin, IfMatchPin },
			{ JsonPin, ErrorPin } },
		{ UOWClientBulk::StaticClass(), TEXT("BulkWrite"),
			{ ConfigPin, { TEXT("Items"), PinStruct, FOWBulkItem::StaticStruct(), true }, { TEXT("bAtomic"), PinBool, nullptr, false }, KeyPin,
			  { TEXT("bCreateOnly"), PinBool, nullptr, false } },
			{ { TEXT("Outcome"), PinStruct, FOWBulkResult::StaticStruct(), false }, ErrorPin } },
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
