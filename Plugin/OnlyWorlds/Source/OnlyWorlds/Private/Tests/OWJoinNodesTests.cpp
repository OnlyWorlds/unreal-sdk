// The join nodes (OWJoinNodes.h): UOWJoin::ParseJoinLink, and PreviewJoin, RedeemJoin and AcceptJoin through the client
// nodes' fake transport (OWFakeTransport.h), which records each request and answers from a script: no test here touches
// the network. Each node is made by its factory, its OnSuccess and OnFailure bound to a listener (OWJoinNodesProbe.h) as
// a graph's async node binds them, given the fake (SetTransport), and activated. Ported from the core's join tests
// (tests/test_join.cpp, their bodies keel's schemas field for field): what each sends (no credential on the two keyless
// calls, the Bearer alone on AcceptJoin, a "" pin not sent), every pin a success gives, keel's refusals reaching
// OnFailure by status, code and param, and on RedeemJoin and AcceptJoin no body in the error (it may hold the key), the
// refusals made before sending, one answer only, one node made and activated through reflection as a graph does, and,
// in the editor, the pins a graph's node gets. The key in no log line: OWJoinLogTests.cpp. Every key here is made up.
//
// Every helper is in the namespace OWJoinNodesTest, never at file scope (C4459: CppCompileWarnings.cs).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "OWBlueprintCall.h"
#include "OWConvert.h"
#include "OWFakeTransport.h"
#include "OWJoinNodes.h"
#include "OWJoinNodesProbe.h"
#include "OWJson.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWJoinNodesTest
{
	constexpr EAutomationTestFlags JoinTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	constexpr int32 FrameLimit = 200;

	const char* const Root = "http://keel.test/api/v2";
	const char* const CodeText = "ow_j_EoEUrAfQ9xumy-lbBJqXix3q1xl49-P4";

	const char* const PlayerJoined =
		"{\"kind\":\"player\",\"key\":\"ow_r_guest\","
		"\"world\":{\"id\":\"0196f0a2-0000-7000-8000-000000000001\",\"name\":\"Moppetopia\",\"description\":\"\"},"
		"\"character\":{\"id\":\"0196f0a2-0000-7000-8000-0000000000c1\",\"name\":\"Pip\"},\"role\":\"guest\"}";

	const char* const AgentJoined =
		"{\"kind\":\"agent\","
		"\"world\":{\"id\":\"0196f0a2-0000-7000-8000-000000000001\",\"name\":\"Moppetopia\",\"description\":\"\"},"
		"\"seat\":{\"id\":\"s1\",\"role\":\"contributor\",\"agent_name\":\"Scribe\"},"
		"\"with_character\":false,\"character\":null,\"key\":\"ow_w_seat\",\"pin\":\"ow_s_for_tests\","
		"\"base_url\":\"https://example.test/api/v2\",\"env\":\"OW_KEY=ow_w_seat\",\"next\":[],\"members\":[],"
		"\"front_door\":{\"sponsor\":{\"id\":\"m-sam\",\"kind\":\"person\",\"name\":\"The Mapmaker\"},"
		"\"sponsor_character\":\"c-map\",\"my_character\":null,\"charter\":\"law-1\",\"charter_skill\":[\"obj-1\",\"obj-2\"]},"
		"\"reading_aids\":{\"charter\":{\"id\":\"law-1\",\"name\":\"House rules\",\"rules\":[],\"skill\":[\"obj-1\",\"obj-2\"]},"
		"\"guide\":null,\"domains\":[{\"id\":\"con-d1\",\"name\":\"The Harbour\",\"sentence\":\"Where the ships come in.\","
		"\"hubs\":[]}]}}";

	const char* const PreviewBody =
		"{\"reader\":\"player\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
		"\"invited_by\":\"Sam\",\"note\":null,\"owner_character\":\"The Mapmaker\",\"role\":\"guest\","
		"\"with_character\":true,\"expires_at\":\"2026-10-26T14:00:00+00:00\",\"glimpse\":null}";

	// A person link's: its note with a line break, the Character it names, and a contributor's glimpse whose second link
	// holds a place that is no whole number.
	const char* const PersonPreview =
		"{\"reader\":\"person\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
		"\"invited_by\":\"Sam\",\"note\":\"Come and help with the harbour.\\nBring the maps.\",\"owner_character\":null,"
		"\"role\":\"contributor\",\"with_character\":false,\"expires_at\":\"2026-10-26T14:00:00+00:00\","
		"\"character_name\":\"Ada\","
		"\"glimpse\":{\"picked_by\":\"links\",\"things\":[{\"name\":\"Gela\",\"type\":\"location\"},{\"name\":\"Ada\",\"type\":"
		"\"character\"}],\"links\":[{\"from\":1,\"to\":0,\"field\":\"location\",\"label\":\"is at\"},"
		"{\"from\":1.5,\"to\":\"0\",\"field\":\"f\",\"label\":\"l\"}]}}";

	const char* const TablePreview =
		"{\"reader\":\"player\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
		"\"invited_by\":\"Sam\",\"note\":null,\"owner_character\":null,\"role\":\"guest\",\"with_character\":true,"
		"\"expires_at\":\"2026-10-26T14:00:00+00:00\",\"glimpse\":null,"
		"\"max_uses\":4,\"uses_left\":3,\"name_required\":false,"
		"\"characters\":[{\"id\":\"c-ana\",\"name\":\"Ana\",\"image_url\":null,\"taken\":false},"
		"{\"id\":\"c-bo\",\"name\":\"Bo\",\"image_url\":\"https://media.onlyworlds.com/bo.webp\",\"taken\":true}]}";

	const char* const Membership =
		"{\"id\":\"m7\",\"world_id\":\"w1\",\"world_name\":\"Ostvik\",\"role\":\"contributor\","
		"\"joined_at\":\"2026-10-31T01:00:00+00:00\",\"roster_character\":null}";

	const TCHAR* const WithheldWords = TEXT("the answer's body is withheld here, since it may hold the world's key");

	FString Code() { return OWConvert::Str(std::string(CodeText)); }
	FString RootText() { return OWConvert::Str(std::string(Root)); }

	OnlyWorlds::OWResponse Respond(int Status, const std::string& Text)
	{
		OnlyWorlds::OWResponse Out;
		Out.status = Status;
		Out.body = Text;
		return Out;
	}

	OnlyWorlds::OWResponse Refusing(int Status, const std::string& ErrorCode, const char* Param)
	{
		const std::string P = Param ? OnlyWorlds::OWJson::Quote(Param) : std::string("null");
		return Respond(Status, "{\"error\":{\"type\":\"invalid_request\",\"code\":" + OnlyWorlds::OWJson::Quote(ErrorCode) +
			",\"param\":" + P + ",\"message\":\"refused\"}}");
	}

	// A request as "<METHOD> <url, the root cut>|Name=Value; Name=Value", the headers in the order sent.
	FString Line(const OnlyWorlds::OWRequest& Sent)
	{
		const std::string Prefix(Root);
		std::string Out = Sent.method + " " + (Sent.url.rfind(Prefix, 0) == 0 ? Sent.url.substr(Prefix.size()) : Sent.url) + "|";
		for (size_t I = 0; I < Sent.headers.size(); ++I)
			Out += (I ? "; " : "") + Sent.headers[I].first + "=" + Sent.headers[I].second;
		return OWConvert::Str(Out);
	}
	FString BodyLine(const OnlyWorlds::OWRequest& Sent) { return Sent.body ? OWConvert::Str(*Sent.body) : FString(TEXT("<none>")); }

	// JSON text equal as a value to Want, every number to the digit, and parsed at all.
	bool SameJson(const FString& Text, const std::string& Want)
	{
		const std::string Got = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult A = OnlyWorlds::OWJson::Parse(std::string_view(Got));
		OnlyWorlds::OWJsonParseResult B = OnlyWorlds::OWJson::Parse(std::string_view(Want));
		return A.ok && B.ok && OnlyWorlds::OWJson::Stringify(A.value, true) == OnlyWorlds::OWJson::Stringify(B.value, true);
	}

	// An account token's config, with a PIN, a client name and the agent flag set, none of which AcceptJoin may send.
	FOWClientConfig Account(const TCHAR* Key = TEXT("ow_a_test"), const TCHAR* WorldId = TEXT(""))
	{
		FOWClientConfig Out;
		Out.ApiKey = Key;
		Out.ApiPin = TEXT("pin-for-tests");
		Out.World = WorldId;
		Out.BaseUrl = RootText();
		Out.Client = TEXT("atlas");
		Out.bAgent = true;
		return Out;
	}

	struct FRun
	{
		std::shared_ptr<OWClientNodesTest::FFakeTransport> Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		TStrongObjectPtr<UOWJoinNodesProbe> Probe;
		TWeakObjectPtr<UOWClientNode> Node;
		FRun() : Probe(NewObject<UOWJoinNodesProbe>(GetTransientPackage())) {}
	};

	using FMake = std::function<UOWClientNode*(UOWJoinNodesProbe*)>;

	template <typename TNode>
	UOWClientNode* Watched(TNode* N, UOWJoinNodesProbe* P)
	{
		P->Watch(N);
		return N;
	}

	// A node made by Make, watched, given the fake answering Answer (or held until Pump with bLater), and activated.
	std::shared_ptr<FRun> Launch(const FMake& Make, OnlyWorlds::OWResponse Answer, bool bLater = false, int32 Times = 1)
	{
		auto Into = std::make_shared<FRun>();
		Into->Fake->Fallback = std::move(Answer);
		Into->Fake->bLater = bLater;
		Into->Fake->Times = Times;
		UOWClientNode* Made = Make(Into->Probe.Get());
		Into->Node = Made;
		Made->SetTransport(Into->Fake);
		Made->Activate();
		return Into;
	}

	FMake Preview(const FString& C, const FString& Base = RootText())
	{
		return [C, Base](UOWJoinNodesProbe* P) { return Watched(UOWClientJoinPreview::PreviewJoin(C, Base), P); };
	}
	FMake Redeem(const FString& C, const FString& Name = TEXT(""), const FString& Agent = TEXT(""), const FString& Pick = TEXT(""),
		const FString& Base = RootText())
	{
		return [C, Name, Agent, Pick, Base](UOWJoinNodesProbe* P) { return Watched(UOWClientJoinRedeem::RedeemJoin(C, Name, Agent, Pick, Base), P); };
	}
	FMake Accept(const FOWClientConfig& With, const FString& C = TEXT("ow_j_abc"))
	{
		return [With, C](UOWJoinNodesProbe* P) { return Watched(UOWClientJoinAccept::AcceptJoin(With, C), P); };
	}
}

namespace OWJ = OWJoinNodesTest;

// -- the pasted link ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesParseLinkTest, "OnlyWorlds.Client.JoinNodes.ParseLink", OWJ::JoinTestFlags)
bool FOWJoinNodesParseLinkTest::RunTest(const FString& Parameters)
{
	const FString C = OWJ::Code();
	const FString Ideographic = OWConvert::Str(std::string("\xE3\x80\x80"));  // U+3000
	const FString NoBreak = OWConvert::Str(std::string("\xC2\xA0"));  // U+00A0
	const TPair<FString, const TCHAR*> Finds[] = {
		{ TEXT("https://www.onlyworlds.com/join#") + C, TEXT("the site's link") },
		{ TEXT("https://www.onlyworlds.com/join/0196f0a2-0000-7000-8000-000000000001#") + C, TEXT("the link with the preview on") },
		{ TEXT("  http://localhost:8000/join#") + C + TEXT("\n"), TEXT("any host, white space round it") },
		{ C, TEXT("a bare code") },
		{ Ideographic + C + NoBreak, TEXT("Unicode's white space trimmed, through the FString") },
	};
	for (const TPair<FString, const TCHAR*>& F : Finds) TestEqual(F.Value, UOWJoin::ParseJoinLink(F.Key), C);
	const FString None[] = { TEXT(""), TEXT("https://www.onlyworlds.com/join"), TEXT("ow_j_"), TEXT("ow_r_readkey"),
		TEXT("ow_j_two") + NoBreak + TEXT("words"), TEXT("OW_J_upper") };
	for (const FString& Text : None) TestEqual(TEXT("no code in '") + Text + TEXT("'"), UOWJoin::ParseJoinLink(Text), FString());
	return true;
}

// -- what each node sends -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesRequestsTest, "OnlyWorlds.Client.JoinNodes.Requests", OWJ::JoinTestFlags)
bool FOWJoinNodesRequestsTest::RunTest(const FString& Parameters)
{
	const FString C = OWJ::Code();
	const FString CodeOnly = TEXT("{\"code\":\"") + C + TEXT("\"");
	struct FCase
	{
		const TCHAR* Label;
		OWJ::FMake Make;
		FString Line;
		FString Body;
		OnlyWorlds::OWResponse Answer;
	};
	const FCase Cases[] = {
		{ TEXT("PreviewJoin"), OWJ::Preview(C), TEXT("POST /join/preview/|Content-Type=application/json"), CodeOnly + TEXT("}"),
			OWJ::Respond(200, OWJ::PreviewBody) },
		{ TEXT("PreviewJoin, BaseUrl \"\" is production's"), OWJ::Preview(C, TEXT("")),
			TEXT("POST https://www.onlyworlds.com/api/v2/join/preview/|Content-Type=application/json"), CodeOnly + TEXT("}"),
			OWJ::Respond(200, OWJ::PreviewBody) },
		{ TEXT("RedeemJoin, a name"), OWJ::Redeem(C, TEXT("Ana")), TEXT("POST /join/|Content-Type=application/json"),
			CodeOnly + TEXT(",\"name\":\"Ana\"}"), OWJ::Respond(201, OWJ::PlayerJoined) },
		{ TEXT("RedeemJoin, every pin \"\": the code alone"), OWJ::Redeem(C), TEXT("POST /join/|Content-Type=application/json"),
			CodeOnly + TEXT("}"), OWJ::Respond(201, OWJ::PlayerJoined) },
		{ TEXT("RedeemJoin, an agent's name"), OWJ::Redeem(C, TEXT(""), TEXT("Scribe")), TEXT("POST /join/|Content-Type=application/json"),
			CodeOnly + TEXT(",\"agent_name\":\"Scribe\"}"), OWJ::Respond(201, OWJ::AgentJoined) },
		{ TEXT("RedeemJoin, the pick"), OWJ::Redeem(C, TEXT(""), TEXT(""), TEXT("c-ana")), TEXT("POST /join/|Content-Type=application/json"),
			CodeOnly + TEXT(",\"character\":\"c-ana\"}"), OWJ::Respond(201, OWJ::PlayerJoined) },
		{ TEXT("RedeemJoin, every option, in keel's order"), OWJ::Redeem(C, TEXT("N"), TEXT("A"), TEXT("c1")),
			TEXT("POST /join/|Content-Type=application/json"), CodeOnly + TEXT(",\"name\":\"N\",\"agent_name\":\"A\",\"character\":\"c1\"}"),
			OWJ::Respond(201, OWJ::PlayerJoined) },
		{ TEXT("RedeemJoin, BaseUrl \"\" is production's"), OWJ::Redeem(C, TEXT(""), TEXT(""), TEXT(""), TEXT("")),
			TEXT("POST https://www.onlyworlds.com/api/v2/join/|Content-Type=application/json"), CodeOnly + TEXT("}"),
			OWJ::Respond(201, OWJ::PlayerJoined) },
		{ TEXT("AcceptJoin: the Bearer, the code alone, no PIN, no X-OW-*, no world="), OWJ::Accept(OWJ::Account(TEXT("ow_a_test"), TEXT("w1"))),
			TEXT("POST /account/join/|Authorization=Bearer ow_a_test; Content-Type=application/json"), TEXT("{\"code\":\"ow_j_abc\"}"),
			OWJ::Respond(201, OWJ::Membership) },
	};
	for (const FCase& Case : Cases)
	{
		auto Run = OWJ::Launch(Case.Make, Case.Answer);
		const FString What(Case.Label);
		if (!TestEqual(What + TEXT(": one request"), static_cast<int32>(Run->Fake->Sent.size()), 1)) continue;
		TestEqual(What + TEXT(": the line"), OWJ::Line(Run->Fake->Sent[0]), Case.Line);
		TestEqual(What + TEXT(": the body"), OWJ::BodyLine(Run->Fake->Sent[0]), Case.Body);
		TestEqual(What + TEXT(": OnSuccess once, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(What + TEXT(": no OnFailure"), Run->Probe->Failures, 0);
	}
	return true;
}

// -- what a success gives -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesAnswersTest, "OnlyWorlds.Client.JoinNodes.Answers", OWJ::JoinTestFlags)
bool FOWJoinNodesAnswersTest::RunTest(const FString& Parameters)
{
	const FString C = OWJ::Code();
	// A player's: a read key and the Character it reads as.
	{
		auto Run = OWJ::Launch(OWJ::Redeem(C, TEXT("Ana")), OWJ::Respond(201, OWJ::PlayerJoined));
		const FOWJoined& J = Run->Probe->Joined;
		TestEqual(TEXT("player: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("Kind"), J.Kind, FString(TEXT("player")));
		TestTrue(TEXT("bIsPlayer, not bIsAgent"), J.bIsPlayer && !J.bIsAgent);
		TestEqual(TEXT("Key"), J.Key, FString(TEXT("ow_r_guest")));
		TestEqual(TEXT("no Pin"), J.Pin, FString());
		TestEqual(TEXT("World.Id, the code spent"), J.World.Id, FString(TEXT("0196f0a2-0000-7000-8000-000000000001")));
		TestEqual(TEXT("World.Name"), J.World.Name, FString(TEXT("Moppetopia")));
		TestEqual(TEXT("World.Description"), J.World.Description, FString());
		TestEqual(TEXT("Character.Id"), J.Character.Id, FString(TEXT("0196f0a2-0000-7000-8000-0000000000c1")));
		TestEqual(TEXT("Character.Name"), J.Character.Name, FString(TEXT("Pip")));
		TestEqual(TEXT("Role, on the answer"), J.Role, FString(TEXT("guest")));
		TestEqual(TEXT("no Seat"), J.Seat.Id, FString());
		TestEqual(TEXT("no BaseUrl"), J.BaseUrl, FString());
		TestEqual(TEXT("no front door"), J.FrontDoorJson, FString());
		TestFalse(TEXT("no reading aids"), J.bHasReadingAids);
		TestTrue(TEXT("Error empty"), Run->Probe->Error.Kind == EOWApiErrorKind::None);
	}
	// An agent's: the seat's key and secret, the seat, the front door and the reading aids.
	{
		auto Run = OWJ::Launch(OWJ::Redeem(C, TEXT(""), TEXT("Scribe")), OWJ::Respond(201, OWJ::AgentJoined));
		const FOWJoined& J = Run->Probe->Joined;
		TestEqual(TEXT("agent: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestTrue(TEXT("bIsAgent, not bIsPlayer"), J.bIsAgent && !J.bIsPlayer);
		TestEqual(TEXT("Key, the seat's write key"), J.Key, FString(TEXT("ow_w_seat")));
		TestEqual(TEXT("Pin, its secret"), J.Pin, FString(TEXT("ow_s_for_tests")));
		TestEqual(TEXT("Role, from the seat"), J.Role, FString(TEXT("contributor")));
		TestEqual(TEXT("Seat.Id"), J.Seat.Id, FString(TEXT("s1")));
		TestEqual(TEXT("Seat.Role"), J.Seat.Role, FString(TEXT("contributor")));
		TestEqual(TEXT("Seat.AgentName"), J.Seat.AgentName, FString(TEXT("Scribe")));
		TestEqual(TEXT("no Character (null as served)"), J.Character.Id, FString());
		TestEqual(TEXT("BaseUrl"), J.BaseUrl, FString(TEXT("https://example.test/api/v2")));
		TestTrue(TEXT("FrontDoorJson, as served: ") + J.FrontDoorJson,
			OWJ::SameJson(J.FrontDoorJson, "{\"sponsor\":{\"id\":\"m-sam\",\"kind\":\"person\",\"name\":\"The Mapmaker\"},"
				"\"sponsor_character\":\"c-map\",\"my_character\":null,\"charter\":\"law-1\",\"charter_skill\":[\"obj-1\",\"obj-2\"]}"));
		TestTrue(TEXT("bHasReadingAids"), J.bHasReadingAids);
		TestTrue(TEXT("the Charter"), J.ReadingAids.bHasCharter);
		TestEqual(TEXT("its id"), J.ReadingAids.Charter.Id, FString(TEXT("law-1")));
		TestTrue(TEXT("its skill"), J.ReadingAids.Charter.Skill == TArray<FString>{ TEXT("obj-1"), TEXT("obj-2") });
		TestFalse(TEXT("no Guide"), J.ReadingAids.bHasGuide);
		if (TestEqual(TEXT("one Domain"), J.ReadingAids.Domains.Num(), 1))
			TestEqual(TEXT("its name"), J.ReadingAids.Domains[0].Name, FString(TEXT("The Harbour")));
	}
	// A preview: every field of a player's link for one.
	{
		auto Run = OWJ::Launch(OWJ::Preview(C), OWJ::Respond(200, OWJ::PreviewBody));
		const FOWJoinPreview& P = Run->Probe->Preview;
		TestEqual(TEXT("preview: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("Reader"), P.Reader, FString(TEXT("player")));
		TestEqual(TEXT("World.Name"), P.World.Name, FString(TEXT("Moppetopia")));
		TestEqual(TEXT("World.Description"), P.World.Description, FString(TEXT("A demo")));
		TestEqual(TEXT("no World.Id: the code is unspent"), P.World.Id, FString());
		TestEqual(TEXT("InvitedBy"), P.InvitedBy, FString(TEXT("Sam")));
		TestEqual(TEXT("OwnerCharacter"), P.OwnerCharacter, FString(TEXT("The Mapmaker")));
		TestEqual(TEXT("Role"), P.Role, FString(TEXT("guest")));
		TestTrue(TEXT("bWithCharacter"), P.bWithCharacter);
		TestEqual(TEXT("ExpiresAt, as sent"), P.ExpiresAt, FString(TEXT("2026-10-26T14:00:00+00:00")));
		TestEqual(TEXT("no Note"), P.Note, FString());
		TestEqual(TEXT("no CharacterName"), P.CharacterName, FString());
		TestEqual(TEXT("no glimpse"), P.Glimpse.PickedBy, FString());
		TestFalse(TEXT("not a table"), P.bIsTable);
		TestTrue(TEXT("no table keys"), P.MaxUses == 0 && P.UsesLeft == 0 && !P.bNameRequired && P.Characters.Num() == 0);
		TestEqual(TEXT("Json, the answer as served"), P.Json, OWConvert::Str(std::string(OWJ::PreviewBody)));
	}
	// A person's link: the note's line break, the Character it names, the glimpse and its places.
	{
		auto Run = OWJ::Launch(OWJ::Preview(C), OWJ::Respond(200, OWJ::PersonPreview));
		const FOWJoinPreview& P = Run->Probe->Preview;
		TestEqual(TEXT("person: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("Reader"), P.Reader, FString(TEXT("person")));
		TestEqual(TEXT("Note, its line break kept"), P.Note, FString(TEXT("Come and help with the harbour.\nBring the maps.")));
		TestEqual(TEXT("CharacterName"), P.CharacterName, FString(TEXT("Ada")));
		TestEqual(TEXT("OwnerCharacter, null"), P.OwnerCharacter, FString());
		TestFalse(TEXT("no Character for a person"), P.bWithCharacter);
		TestEqual(TEXT("Glimpse.PickedBy"), P.Glimpse.PickedBy, FString(TEXT("links")));
		if (TestEqual(TEXT("two things"), P.Glimpse.Things.Num(), 2))
		{
			TestEqual(TEXT("the things, in order"), P.Glimpse.Things[0].Name + TEXT(",") + P.Glimpse.Things[1].Name, FString(TEXT("Gela,Ada")));
			TestEqual(TEXT("their types"), P.Glimpse.Things[0].Type + TEXT(",") + P.Glimpse.Things[1].Type, FString(TEXT("location,character")));
		}
		if (TestEqual(TEXT("two links"), P.Glimpse.Links.Num(), 2))
		{
			const FOWJoinGlimpseLink& L = P.Glimpse.Links[0];
			TestTrue(TEXT("From 1, To 0"), L.From == 1 && L.To == 0);
			TestEqual(TEXT("Field"), L.Field, FString(TEXT("location")));
			TestEqual(TEXT("Label"), L.Label, FString(TEXT("is at")));
			TestTrue(TEXT("1.5 and \"0\" are no place: -1"), P.Glimpse.Links[1].From == -1 && P.Glimpse.Links[1].To == -1);
		}
	}
	// A table: its places, and its Characters in the owner's order.
	{
		auto Run = OWJ::Launch(OWJ::Preview(C), OWJ::Respond(200, OWJ::TablePreview));
		const FOWJoinPreview& P = Run->Probe->Preview;
		TestEqual(TEXT("table: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestTrue(TEXT("bIsTable"), P.bIsTable);
		TestEqual(TEXT("MaxUses"), P.MaxUses, int64(4));
		TestEqual(TEXT("UsesLeft"), P.UsesLeft, int64(3));
		TestFalse(TEXT("bNameRequired"), P.bNameRequired);
		if (TestEqual(TEXT("two to pick from"), P.Characters.Num(), 2))
		{
			TestEqual(TEXT("in the owner's order"), P.Characters[0].Id + TEXT(",") + P.Characters[1].Id, FString(TEXT("c-ana,c-bo")));
			TestEqual(TEXT("a name"), P.Characters[1].Name, FString(TEXT("Bo")));
			TestEqual(TEXT("no picture"), P.Characters[0].ImageUrl, FString());
			TestEqual(TEXT("a picture"), P.Characters[1].ImageUrl, FString(TEXT("https://media.onlyworlds.com/bo.webp")));
			TestTrue(TEXT("bTaken"), !P.Characters[0].bTaken && P.Characters[1].bTaken);
		}
	}
	// AcceptJoin: the membership.
	{
		auto Run = OWJ::Launch(OWJ::Accept(OWJ::Account()), OWJ::Respond(201, OWJ::Membership));
		const FOWJoinedMembership& M = Run->Probe->Membership;
		TestEqual(TEXT("membership: OnSuccess, got ") + Run->Probe->Error.Line, Run->Probe->Successes, 1);
		TestEqual(TEXT("the membership"), M.Id + TEXT("|") + M.WorldId + TEXT("|") + M.WorldName + TEXT("|") + M.Role,
			FString(TEXT("m7|w1|Ostvik|contributor")));
		TestEqual(TEXT("JoinedAt"), M.JoinedAt, FString(TEXT("2026-10-31T01:00:00+00:00")));
		TestEqual(TEXT("no RosterCharacter"), M.RosterCharacter, FString());
	}
	return true;
}

// -- keel's refusals, and an error with no body ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesErrorsTest, "OnlyWorlds.Client.JoinNodes.Errors", OWJ::JoinTestFlags)
bool FOWJoinNodesErrorsTest::RunTest(const FString& Parameters)
{
	const FString C = OWJ::Code();
	struct FCase
	{
		const TCHAR* Label;
		OWJ::FMake Make;
		OnlyWorlds::OWResponse Answer;
		int32 Status;
		const TCHAR* ErrorCode;
		const TCHAR* Param;
	};
	const std::string NotFound = "{\"error\":{\"type\":\"invalid_request\",\"code\":\"not_found\","
		"\"message\":\"This join code is not valid. It may have been used, revoked, or expired.\"}}";
	const FCase Cases[] = {
		{ TEXT("RedeemJoin, a bad code"), OWJ::Redeem(C), OWJ::Respond(404, NotFound), 404, TEXT("not_found"), TEXT("") },
		{ TEXT("RedeemJoin, no agent name"), OWJ::Redeem(C), OWJ::Refusing(422, "invalid_request", "agent_name"), 422, TEXT("invalid_request"), TEXT("agent_name") },
		{ TEXT("RedeemJoin, a taken pick"), OWJ::Redeem(C, TEXT(""), TEXT(""), TEXT("c-bo")), OWJ::Refusing(422, "character_taken", "character"), 422,
			TEXT("character_taken"), TEXT("character") },
		{ TEXT("RedeemJoin, a pick off the list"), OWJ::Redeem(C, TEXT(""), TEXT(""), TEXT("c-zed")), OWJ::Refusing(422, "invalid_request", "character"), 422,
			TEXT("invalid_request"), TEXT("character") },
		{ TEXT("RedeemJoin, a full world"), OWJ::Redeem(C), OWJ::Refusing(422, "world_full", nullptr), 422, TEXT("world_full"), TEXT("") },
		{ TEXT("AcceptJoin, already a member"), OWJ::Accept(OWJ::Account()), OWJ::Refusing(409, "already_member", nullptr), 409,
			TEXT("already_member"), TEXT("") },
		{ TEXT("AcceptJoin, a bad code"), OWJ::Accept(OWJ::Account()), OWJ::Respond(404, NotFound), 404, TEXT("not_found"), TEXT("") },
	};
	for (const FCase& Case : Cases)
	{
		auto Run = OWJ::Launch(Case.Make, Case.Answer);
		const FString What(Case.Label);
		const FOWApiError& E = Run->Probe->Error;
		TestEqual(What + TEXT(": OnFailure once"), Run->Probe->Failures, 1);
		TestEqual(What + TEXT(": no OnSuccess"), Run->Probe->Successes, 0);
		TestTrue(What + TEXT(": kind Api"), E.Kind == EOWApiErrorKind::Api);
		TestEqual(What + TEXT(": Status"), E.Status, Case.Status);
		TestEqual(What + TEXT(": Code"), E.Code, FString(Case.ErrorCode));
		TestEqual(What + TEXT(": Param"), E.Param, FString(Case.Param));
		TestEqual(What + TEXT(": no RawBody"), E.RawBody, FString());
		TestTrue(What + TEXT(": the message says the body was withheld: ") + E.Message, E.Message.Contains(OWJ::WithheldWords));
		TestTrue(What + TEXT(": and so does the line: ") + E.Line, E.Line.Contains(OWJ::WithheldWords));
		TestEqual(What + TEXT(": no key in the answer"), Run->Probe->Joined.Key, FString());
	}

	// keel's message is kept, the clause after it.
	{
		auto Run = OWJ::Launch(OWJ::Redeem(C), OWJ::Respond(404, NotFound));
		TestEqual(TEXT("the message, then the clause"), Run->Probe->Error.Message,
			FString(TEXT("This join code is not valid. It may have been used, revoked, or expired. (")) + OWJ::WithheldWords + TEXT(")"));
	}

	// PreviewJoin keeps its body: a preview holds no secret.
	{
		auto Run = OWJ::Launch(OWJ::Preview(C), OWJ::Respond(404, NotFound));
		const FOWApiError& E = Run->Probe->Error;
		TestEqual(TEXT("preview: OnFailure"), Run->Probe->Failures, 1);
		TestEqual(TEXT("preview: not_found"), E.Code, FString(TEXT("not_found")));
		TestEqual(TEXT("preview: its RawBody kept"), E.RawBody, OWConvert::Str(NotFound));
		TestFalse(TEXT("preview: no clause"), E.Message.Contains(OWJ::WithheldWords));
	}

	// A 2xx that is not the answer: kind Answer, and none of the body anywhere in the error (the core's own vector, a
	// key-shaped string). A non-2xx that is no envelope: its message was the body's start, now the clause alone.
	const struct
	{
		const TCHAR* Label;
		OWJ::FMake Make;
		int Status;
		const char* Body;
		EOWApiErrorKind Kind;
	} Bodies[] = {
		{ TEXT("RedeemJoin, a 201 that is a string"), OWJ::Redeem(C), 201, "\"ow_r_planted_in_body\"", EOWApiErrorKind::Answer },
		{ TEXT("RedeemJoin, a 201 that is a list"), OWJ::Redeem(C), 201, "[\"ow_r_planted_in_body\"]", EOWApiErrorKind::Answer },
		{ TEXT("RedeemJoin, a 502 page"), OWJ::Redeem(C), 502, "<html>ow_r_planted_in_body</html>", EOWApiErrorKind::Api },
		{ TEXT("AcceptJoin, a 201 that is a list"), OWJ::Accept(OWJ::Account()), 201, "[\"ow_r_planted_in_body\"]", EOWApiErrorKind::Answer },
		{ TEXT("AcceptJoin, a 500 page"), OWJ::Accept(OWJ::Account()), 500, "ow_r_planted_in_body", EOWApiErrorKind::Api },
	};
	for (const auto& B : Bodies)
	{
		auto Run = OWJ::Launch(B.Make, OWJ::Respond(B.Status, B.Body));
		const FString What(B.Label);
		const FOWApiError& E = Run->Probe->Error;
		TestEqual(What + TEXT(": OnFailure"), Run->Probe->Failures, 1);
		TestTrue(What + TEXT(": its kind"), E.Kind == B.Kind);
		TestEqual(What + TEXT(": its status"), E.Status, B.Status);
		TestEqual(What + TEXT(": no RawBody"), E.RawBody, FString());
		TestFalse(What + TEXT(": the body in no message: ") + E.Message, E.Message.Contains(TEXT("planted")));
		TestFalse(What + TEXT(": nor in the line: ") + E.Line, E.Line.Contains(TEXT("planted")));
		TestTrue(What + TEXT(": the message says so: ") + E.Message, E.Message.Contains(OWJ::WithheldWords));
	}

	// No answer at all: kind Transport, no body to withhold and no clause.
	{
		OnlyWorlds::OWResponse NoAnswer;
		NoAnswer.failure = "no answer: can't connect";
		auto Run = OWJ::Launch(OWJ::Redeem(C), NoAnswer);
		const FOWApiError& E = Run->Probe->Error;
		TestEqual(TEXT("no answer: OnFailure"), Run->Probe->Failures, 1);
		TestTrue(TEXT("no answer: kind Transport"), E.Kind == EOWApiErrorKind::Transport);
		TestTrue(TEXT("no answer: the transport's words: ") + E.Message, E.Message.Contains(TEXT("can't connect")));
		TestFalse(TEXT("no answer: no clause"), E.Message.Contains(OWJ::WithheldWords));
	}
	return true;
}

// -- the refusals made before sending -------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesRefusalsTest, "OnlyWorlds.Client.JoinNodes.Refusals", OWJ::JoinTestFlags)
bool FOWJoinNodesRefusalsTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		FString Label;
		OWJ::FMake Make;
		const TCHAR* Words;
	};
	TArray<FCase> Cases;
	for (const FString& Blank : { FString(), FString(TEXT(" ")), FString(TEXT("\t\n")), OWConvert::Str(std::string("\xC2\xA0")) })
	{
		Cases.Add({ TEXT("PreviewJoin, code '") + Blank + TEXT("'"), OWJ::Preview(Blank), TEXT("OWJoin::Preview: a join code is required") });
		Cases.Add({ TEXT("RedeemJoin, code '") + Blank + TEXT("'"), OWJ::Redeem(Blank, TEXT("Ana")), TEXT("OWJoin::Redeem: a join code is required") });
		Cases.Add({ TEXT("AcceptJoin, code '") + Blank + TEXT("'"), OWJ::Accept(OWJ::Account(), Blank), TEXT("AcceptJoin: a join code is required") });
	}
	Cases.Add({ TEXT("AcceptJoin, a write key"), OWJ::Accept(OWJ::Account(TEXT("ow_w_test"))), TEXT("AcceptJoin needs an account token (ow_a_)") });
	Cases.Add({ TEXT("AcceptJoin, a read key"), OWJ::Accept(OWJ::Account(TEXT("ow_r_test"))), TEXT("AcceptJoin needs an account token (ow_a_)") });
	Cases.Add({ TEXT("AcceptJoin, no key, a world"), OWJ::Accept(OWJ::Account(TEXT(""), TEXT("w1"))), TEXT("AcceptJoin needs an account token (ow_a_)") });
	for (const FCase& Case : Cases)
	{
		auto Run = OWJ::Launch(Case.Make, OWJ::Respond(201, OWJ::PlayerJoined), /*bLater=*/true);
		const FOWApiError& E = Run->Probe->Error;
		TestEqual(Case.Label + TEXT(": answered inside Activate, on OnFailure"), Run->Probe->Failures, 1);
		TestEqual(Case.Label + TEXT(": no OnSuccess"), Run->Probe->Successes, 0);
		TestTrue(Case.Label + TEXT(": kind Refused"), E.Kind == EOWApiErrorKind::Refused);
		TestTrue(Case.Label + TEXT(": its words: ") + E.Message, E.Message.Contains(Case.Words));
		TestTrue(Case.Label + TEXT(": nothing was sent, it says"), E.Message.EndsWith(TEXT("Nothing was sent.")));
		TestEqual(Case.Label + TEXT(": nothing sent"), static_cast<int32>(Run->Fake->Sent.size()), 0);
		TestTrue(Case.Label + TEXT(": let go"), Run->Node.IsValid() && !Run->Node->IsRooted());
	}
	return true;
}

// -- one answer only ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesOnceTest, "OnlyWorlds.Client.JoinNodes.Once", OWJ::JoinTestFlags)
bool FOWJoinNodesOnceTest::RunTest(const FString& Parameters)
{
	const FString C = OWJ::Code();
	// The HTTP module answers on a later tick: nothing before, one answer after, the node kept until then.
	{
		auto Run = OWJ::Launch(OWJ::Redeem(C), OWJ::Respond(201, OWJ::PlayerJoined), /*bLater=*/true);
		TestEqual(TEXT("later: not answered inside Activate"), Run->Probe->Answers(), 0);
		TestTrue(TEXT("later: kept while it waits"), Run->Node.IsValid() && Run->Node->IsRooted());
		TestEqual(TEXT("later: one held"), Run->Fake->Pump(), 1);
		TestEqual(TEXT("later: OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("later: the key"), Run->Probe->Joined.Key, FString(TEXT("ow_r_guest")));
		TestTrue(TEXT("later: let go"), Run->Node.IsValid() && !Run->Node->IsRooted());
	}
	// A transport that answers twice: the second is dropped, on each node.
	const TPair<const TCHAR*, OWJ::FMake> Twice[] = {
		{ TEXT("PreviewJoin"), OWJ::Preview(C) },
		{ TEXT("RedeemJoin"), OWJ::Redeem(C) },
		{ TEXT("AcceptJoin"), OWJ::Accept(OWJ::Account()) },
	};
	for (const TPair<const TCHAR*, OWJ::FMake>& T : Twice)
	{
		auto Run = OWJ::Launch(T.Value, OWJ::Respond(404, "{\"error\":{\"code\":\"not_found\",\"message\":\"no\"}}"), false, /*Times=*/2);
		TestEqual(FString(T.Key) + TEXT(": answered twice, heard once"), Run->Probe->Answers(), 1);
	}
	// Activated again: no second request, no second answer.
	{
		auto Run = OWJ::Launch(OWJ::Preview(C), OWJ::Respond(200, OWJ::PreviewBody));
		if (UOWClientNode* N = Run->Node.Get()) N->Activate();
		TestEqual(TEXT("again: one request"), static_cast<int32>(Run->Fake->Sent.size()), 1);
		TestEqual(TEXT("again: one answer"), Run->Probe->Answers(), 1);
	}
	return true;
}

// -- one node as a graph makes it -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesReflectionTest, "OnlyWorlds.Client.JoinNodes.Reflection", OWJ::JoinTestFlags)
bool FOWJoinNodesReflectionTest::RunTest(const FString& Parameters)
{
	// RedeemJoin found by name, its frame from the UFunction, each parameter by its property, ProcessEvent on the class
	// default object; the node read from the return; the delegates bound; Activate found by name and called by
	// ProcessEvent, as UK2Node_AsyncAction's expansion calls it.
	UFunction* Factory = UOWClientJoinRedeem::StaticClass()->FindFunctionByName(TEXT("RedeemJoin"));
	if (!TestNotNull(TEXT("UFUNCTION RedeemJoin"), Factory)) return false;
	TestTrue(TEXT("RedeemJoin: a static, public, native Blueprint function"),
		Factory->HasAllFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable));
#if WITH_METADATA
	TestTrue(TEXT("RedeemJoin: the async node's, not a plain call"), Factory->HasMetaData(TEXT("BlueprintInternalUseOnly")));
	TestEqual(TEXT("RedeemJoin: BaseUrl in the advanced pins"), Factory->GetMetaData(TEXT("AdvancedDisplay")), FString(TEXT("BaseUrl")));
#endif
	UFunction* Parse = UOWJoin::StaticClass()->FindFunctionByName(TEXT("ParseJoinLink"));
	if (TestNotNull(TEXT("UFUNCTION ParseJoinLink"), Parse))
		TestTrue(TEXT("ParseJoinLink: a pure static Blueprint function"),
			Parse->HasAllFunctionFlags(FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable | FUNC_BlueprintPure));

	uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Factory->ParmsSize, 1), Factory->GetMinAlignment()));
	Factory->InitializeStruct(Frame);
	UOWClientJoinRedeem* Made = nullptr;
	bool bAll = true;
	auto SetText = [this, Factory, Frame, &bAll](const TCHAR* Param, const FString& Value)
	{
		FStrProperty* P = CastField<FStrProperty>(Factory->FindPropertyByName(Param));
		if (TestNotNull(FString::Printf(TEXT("RedeemJoin has an FString parameter %s"), Param), P))
			*P->ContainerPtrToValuePtr<FString>(Frame) = Value;
		else
			bAll = false;
	};
	SetText(TEXT("Code"), OWJ::Code());
	SetText(TEXT("Name"), TEXT("Ana"));
	SetText(TEXT("AgentName"), TEXT(""));
	SetText(TEXT("Character"), TEXT("c-ana"));
	SetText(TEXT("BaseUrl"), OWJ::RootText());
	if (bAll)
	{
		UOWClientJoinRedeem::StaticClass()->GetDefaultObject()->ProcessEvent(Factory, Frame);
		const FObjectProperty* Ret = CastField<FObjectProperty>(Factory->GetReturnProperty());
		if (TestTrue(TEXT("RedeemJoin returns a UOWClientJoinRedeem"), Ret && Ret->PropertyClass == UOWClientJoinRedeem::StaticClass()))
			Made = Cast<UOWClientJoinRedeem>(Ret->GetObjectPropertyValue_InContainer(Frame));
	}
	Factory->DestroyStruct(Frame);
	FMemory::Free(Frame);
	if (!TestNotNull(TEXT("the node"), Made)) return false;

	const FMulticastDelegateProperty* Success = CastField<FMulticastDelegateProperty>(UOWClientJoinRedeem::StaticClass()->FindPropertyByName(TEXT("OnSuccess")));
	if (TestNotNull(TEXT("OnSuccess, a multicast delegate"), Success))
	{
		const UFunction* Sig = Success->SignatureFunction;
		const FStructProperty* Pin = Sig ? CastField<FStructProperty>(Sig->FindPropertyByName(TEXT("Joined"))) : nullptr;
		TestTrue(TEXT("OnSuccess's Joined, an FOWJoined"), Pin && Pin->Struct == FOWJoined::StaticStruct());
	}

	auto Run = std::make_shared<OWJ::FRun>();
	Run->Fake->bLater = true;
	Run->Fake->Fallback = OWJ::Respond(201, OWJ::PlayerJoined);
	Run->Node = Made;
	Made->SetTransport(Run->Fake);
	Run->Probe->Watch(Made);
	UFunction* Activate = Made->FindFunction(TEXT("Activate"));
	if (!TestNotNull(TEXT("UFUNCTION Activate"), Activate)) return false;
	Made->ProcessEvent(Activate, nullptr);
	TestEqual(TEXT("not answered inside Activate"), Run->Probe->Answers(), 0);
	if (TestEqual(TEXT("one request"), static_cast<int32>(Run->Fake->Sent.size()), 1))
	{
		TestEqual(TEXT("the request"), OWJ::Line(Run->Fake->Sent[0]), FString(TEXT("POST /join/|Content-Type=application/json")));
		TestEqual(TEXT("the body"), OWJ::BodyLine(Run->Fake->Sent[0]),
			TEXT("{\"code\":\"") + OWJ::Code() + TEXT("\",\"name\":\"Ana\",\"character\":\"c-ana\"}"));
	}

	auto Frames = std::make_shared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Frames]()
	{
		++*Frames;
		Run->Fake->Pump();
		if (Run->Probe->Answers() == 0 && *Frames < OWJ::FrameLimit) return false;
		TestEqual(TEXT("OnSuccess once"), Run->Probe->Successes, 1);
		TestEqual(TEXT("no OnFailure"), Run->Probe->Failures, 0);
		TestEqual(TEXT("the key"), Run->Probe->Joined.Key, FString(TEXT("ow_r_guest")));
		TestTrue(TEXT("let go once answered"), Run->Node.IsValid() && !Run->Node->IsRooted());
		return true;
	}));
	return true;
}

// -- the pins a graph's node gets (editor only) ---------------------------------------------------------------------------

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinNodesPinsTest, "OnlyWorlds.Client.JoinNodes.Pins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FOWJoinNodesPinsTest::RunTest(const FString& Parameters)
{
	const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(TEXT("OWJoinNodesProbe")));
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
		const TCHAR* Title;  // the title a graph shows (the README names it so)
		TArray<FPin> In;
		TArray<FPin> Out;
	};
	const FPin ErrorPin{ TEXT("Error"), PinStruct, FOWApiError::StaticStruct() };
	const FPin CodePin{ TEXT("Code"), PinString, nullptr };
	const FPin BasePin{ TEXT("BaseUrl"), PinString, nullptr };
	const TArray<FNode> Nodes = {
		{ UOWClientJoinPreview::StaticClass(), TEXT("PreviewJoin"), TEXT("PreviewJoin"), { CodePin, BasePin },
			{ { TEXT("Preview"), PinStruct, FOWJoinPreview::StaticStruct() }, ErrorPin } },
		{ UOWClientJoinRedeem::StaticClass(), TEXT("RedeemJoin"), TEXT("RedeemJoin"),
			{ CodePin, { TEXT("Name"), PinString, nullptr }, { TEXT("AgentName"), PinString, nullptr }, { TEXT("Character"), PinString, nullptr }, BasePin },
			{ { TEXT("Joined"), PinStruct, FOWJoined::StaticStruct() }, ErrorPin } },
		{ UOWClientJoinAccept::StaticClass(), TEXT("AcceptJoin"), TEXT("AcceptJoin"), { { TEXT("Config"), PinStruct, FOWClientConfig::StaticStruct() }, CodePin },
			{ { TEXT("Membership"), PinStruct, FOWJoinedMembership::StaticStruct() }, ErrorPin } },
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
		// An async node's title is its factory's name as written: UK2Node_BaseAsyncTask asks for no friendly name
		// (ObjectTools::GetUserFacingFunctionName, bAllowFriendlyNames false), in the editor as here.
		TestEqual(FString::Printf(TEXT("%s: its title"), N.Factory), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), FString(N.Title));

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

	// The pure call, as a graph places it.
	if (UFunction* Parse = UOWJoin::StaticClass()->FindFunctionByName(TEXT("ParseJoinLink")))
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->SetFromFunction(Parse);
		Creator.Finalize();
		// A call's full title is the friendly name (K2Node_CallFunction asks for it on FullTitle).
		TestEqual(TEXT("ParseJoinLink: its title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), FString(TEXT("Parse Join Link")));
		TestTrue(TEXT("ParseJoinLink: pure, no exec pin"), Node->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
		Check(TEXT("ParseJoinLink in Text"), Node->FindPin(FName(TEXT("Text")), EGPD_Input), FPin{ TEXT("Text"), PinString, nullptr });
		Check(TEXT("ParseJoinLink out ReturnValue"), Node->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), FPin{ TEXT("ReturnValue"), PinString, nullptr });
	}
	else
	{
		AddError(TEXT("UFUNCTION ParseJoinLink"));
	}
	return true;
}

#endif  // WITH_EDITOR

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
