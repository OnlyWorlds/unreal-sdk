// The held world on Blueprints (UOWWorldCache, OWWorldCacheObject.h) and its sync nodes (OWSyncNodes.h), through the
// client nodes' fake transport (OWFakeTransport.h): no OnlyWorlds.* test here touches the network. Each case runs the
// core's OWSync on a core cache and the node on a UOWWorldCache, each over its own fake scripted the same, and holds
// the node's answer, the requests it sent and the cache it leaves to the core's: every count is the core run's, read
// at run time.
//
// - OnlyWorlds.Sync.Answers: a baseline, an incremental walk (upserts, a delete, a bodiless upsert), a 409 for view
//   and for role, a rewound server, a walk that fails.
// - OnlyWorlds.Sync.HasChanges: two polls on one cache, the second sending the first tip's ETag, a 304 answered false.
// - OnlyWorlds.Sync.Refusals: a read-only cache, no cache, a sync while one is in flight (held for a later Pump).
// - OnlyWorlds.Sync.File: WriteFile and OpenWorldCache against the core's ReadFile; no file, a file that is no cache.
// - OnlyWorlds.Sync.World: GetHeldWorld is the core's ToWorld, the same object across syncs, read by UOWElements.
// - OnlyWorldsLive.Sync.HeadTag: Moppetopia over its public key and Unreal's HTTP: a SyncWorld, then HasWorldChanges
//   twice, the second's If-None-Match and its answer recorded as they come (a 304, or a 200 if the tag is ignored).
//
// Every helper is in the namespace OWSyncTest, never at file scope (C4459, an error here).

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWAddress.h"
#include "OWClient.h"
#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWHttpTransport.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWSync.h"
#include "OWSyncNodes.h"
#include "OWWorldCache.h"
#include "OWWorldCacheObject.h"
#include "OWWorldObject.h"
#include "OWFakeTransport.h"
#include "OWSyncProbe.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWSyncTest
{
	using OnlyWorlds::OWJsonValue;
	using OnlyWorlds::OWRequest;
	using OnlyWorlds::OWResponse;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	const char* const KeelBase = "http://keel.test/api/v2";
	const char* const EmptyPage = R"({"data":[],"has_more":false,"next_cursor":null})";

	using FFake = OWClientNodesTest::FFakeTransport;
	using FScript = std::vector<OWResponse>;
	using FSeed = void (*)(OnlyWorlds::OWWorldCache&);

	OWResponse Answer(int Status, const std::string& Body, OnlyWorlds::OWHeaders Headers = {})
	{
		OWResponse R;
		R.status = Status;
		R.body = Body;
		R.headers = std::move(Headers);
		return R;
	}

	OWJsonValue J(const std::string& Text) { return OnlyWorlds::OWJson::Parse(std::string_view(Text)).value; }

	std::string Head(int64 HeadSeq)
	{
		const std::string H = std::to_string(HeadSeq);
		return "{\"cursor\":" + H + ",\"head\":" + H + ",\"changes\":[],\"has_more\":false,\"epoch\":null}";
	}

	std::string GuestHead(int64 HeadSeq, int64 Epoch)
	{
		const std::string H = std::to_string(HeadSeq), E = std::to_string(Epoch);
		return "{\"cursor\":\"" + H + ":ffffffff-ffff-ffff-ffff-ffffffffffff:" + E + "\",\"head\":" + H +
			",\"changes\":[],\"has_more\":false,\"epoch\":" + E + "}";
	}

	// A tip, then one list per type: Character holds c1 and c2 (two pages), Pin holds p1, the rest are empty.
	void Baseline(FScript& S, const std::string& Tip, const char* Named = nullptr)
	{
		S.push_back(Answer(200, Tip));
		for (const char* Type : OnlyWorlds::OWAddress::TypeWords)
		{
			const std::string T(Type);
			if (T == "character" && Named)
			{
				S.push_back(Answer(200, std::string("{\"data\":[{\"id\":\"") + Named + "\",\"name\":\"Fresh\"}],\"has_more\":false}"));
			}
			else if (T == "character")
			{
				S.push_back(Answer(200, R"({"data":[{"id":"c1","type":"character","name":"Ama","x_k":9007199254740993}],"has_more":true,"next_cursor":"n1"})"));
				S.push_back(Answer(200, R"({"data":[{"id":"c2","type":"character","name":"Bo"}],"has_more":false})"));
			}
			else if (T == "pin")
			{
				S.push_back(Answer(200, R"({"data":[{"id":"p1","type":"pin","name":"P"}],"has_more":false})"));
			}
			else
			{
				S.push_back(Answer(200, EmptyPage));
			}
		}
	}

	std::string Resync(const char* Reason)
	{
		return std::string(R"({"error":{"type":"invalid_request","code":"resync_required","param":"since","message":"m","reason":")") + Reason + "\"}}";
	}

	void SeedNone(OnlyWorlds::OWWorldCache&) {}

	void SeedTwo(OnlyWorlds::OWWorldCache& C)
	{
		C.Upsert("character", J(R"({"id":"a","name":"Old A"})"));
		C.Upsert("character", J(R"({"id":"b","name":"Out of view"})"));
		C.SetCursor(100, "100:ffffffff-ffff-ffff-ffff-ffffffffffff:2", 2, "t");
	}

	void SeedAt100(OnlyWorlds::OWWorldCache& C)
	{
		C.Upsert("character", J(R"({"id":"d1","name":"Doomed"})"));
		C.SetCursor(100, "", std::nullopt, "t");
	}

	void SeedAhead(OnlyWorlds::OWWorldCache& C)
	{
		C.Upsert("character", J(R"({"id":"stale","name":"Old"})"));
		C.SetCursor(9999, "", std::nullopt, "t");
	}

	struct FCase
	{
		const TCHAR* Name;
		FSeed Seed;
		bool bBaseline;
		std::function<FScript()> Script;
	};

	std::vector<FCase> Cases()
	{
		std::vector<FCase> Out;
		Out.push_back({TEXT("a baseline"), &SeedNone, true, []() { FScript S; Baseline(S, GuestHead(40, 3)); return S; }});
		Out.push_back({TEXT("the first sync walks it all"), &SeedNone, false, []() { FScript S; Baseline(S, Head(7)); return S; }});
		Out.push_back({TEXT("an incremental walk"), &SeedAt100, false, []()
		{
			FScript S;
			S.push_back(Answer(200, Head(104)));
			S.push_back(Answer(200, R"({"cursor":"102:x","has_more":true,"head":104,"changes":[
				{"op":"upsert","type":"character","id":"u1","change_seq":101,"element":{"id":"u1","name":"New","level":3}},
				{"op":"delete","type":"character","id":"d1","change_seq":102}]})"));
			S.push_back(Answer(200, R"({"cursor":"104:y","has_more":false,"head":104,"changes":[
				{"op":"upsert","type":"location","id":"l1","change_seq":103},
				{"op":"upsert","type":"pin","id":"p9","change_seq":104,"element":{"name":"Takes its row's id"}}]})"));
			S.push_back(Answer(200, R"({"id":"l1","name":"Read by id"})"));
			return S;
		}});
		Out.push_back({TEXT("a 409 for view"), &SeedTwo, false, []()
		{
			FScript S;
			S.push_back(Answer(200, GuestHead(101, 3)));
			S.push_back(Answer(409, Resync("view")));
			Baseline(S, GuestHead(101, 3), "a");
			return S;
		}});
		Out.push_back({TEXT("a 409 for role"), &SeedTwo, false, []()
		{
			FScript S;
			S.push_back(Answer(200, GuestHead(101, 3)));
			S.push_back(Answer(409, Resync("role")));
			Baseline(S, GuestHead(101, 3), "a");
			return S;
		}});
		Out.push_back({TEXT("a rewound server"), &SeedAhead, false, []()
		{
			FScript S;
			S.push_back(Answer(200, Head(100)));
			Baseline(S, Head(100));
			return S;
		}});
		Out.push_back({TEXT("a walk that fails"), &SeedTwo, false, []()
		{
			FScript S;
			S.push_back(Answer(200, GuestHead(101, 3)));
			S.push_back(Answer(409, Resync("view")));
			S.push_back(Answer(200, GuestHead(101, 3)));
			S.push_back(Answer(503, R"({"error":{"type":"api_error","code":"server_busy","message":"busy"}})"));
			return S;
		}});
		return Out;
	}

	OnlyWorlds::OWClientConfig CoreConfig(std::shared_ptr<FFake> Fake)
	{
		OnlyWorlds::OWClientConfig C;
		C.api_key = "ow_w_test";
		C.api_pin = "1234";
		C.base_url = KeelBase;
		C.transport = std::move(Fake);
		return C;
	}

	FOWClientConfig NodeConfig()
	{
		FOWClientConfig C;
		C.ApiKey = TEXT("ow_w_test");
		C.ApiPin = TEXT("1234");
		C.BaseUrl = Str(KeelBase);
		return C;
	}

	struct FCoreRun
	{
		std::shared_ptr<FFake> Fake = std::make_shared<FFake>();
		std::unique_ptr<OnlyWorlds::OWClient> Client;
		OnlyWorlds::OWWorldCache Cache{"w-1", "Test World"};
		int32 Calls = 0;
		OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult> Reply;
	};

	std::unique_ptr<FCoreRun> RunCore(const FCase& Case)
	{
		auto Run = std::make_unique<FCoreRun>();
		for (OWResponse& R : Case.Script()) Run->Fake->Script.push_back(std::move(R));
		Run->Client = std::make_unique<OnlyWorlds::OWClient>(CoreConfig(Run->Fake));
		Case.Seed(Run->Cache);
		FCoreRun* Raw = Run.get();
		auto Done = [Raw](OnlyWorlds::OWReply<OnlyWorlds::OWSyncResult> R)
		{
			++Raw->Calls;
			Raw->Reply = std::move(R);
		};
		if (Case.bBaseline) OnlyWorlds::OWSync::Baseline(*Run->Client, Run->Cache, Done);
		else OnlyWorlds::OWSync::Incremental(*Run->Client, Run->Cache, Done);
		return Run;
	}

	struct FNodeRun
	{
		std::shared_ptr<FFake> Fake = std::make_shared<FFake>();
		TStrongObjectPtr<UOWWorldCache> Cache;
		TStrongObjectPtr<UOWSyncProbe> Probe;
	};

	std::unique_ptr<FNodeRun> RunNode(const FCase& Case)
	{
		auto Run = std::make_unique<FNodeRun>();
		for (OWResponse& R : Case.Script()) Run->Fake->Script.push_back(std::move(R));
		Run->Cache.Reset(UOWWorldCache::MakeWorldCache(NodeConfig(), TEXT("w-1"), TEXT("Test World"), false));
		Run->Probe.Reset(NewObject<UOWSyncProbe>(GetTransientPackage()));
		Run->Cache->SetTransport(Run->Fake);
		Case.Seed(Run->Cache->Native());
		Run->Cache->RefreshWorld();
		UOWSyncWorld* Node = Case.bBaseline ? UOWSyncWorld::BaselineWorld(Run->Cache.Get()) : UOWSyncWorld::SyncWorld(Run->Cache.Get());
		Run->Probe->Watch(Node);
		Node->Activate();
		return Run;
	}

	// The two caches hold the same elements, in the same order, of the same types and bodies, and stop at one place.
	void SameCache(FAutomationTestBase& Test, const FString& Name, const OnlyWorlds::OWWorldCache& Want, const UOWWorldCache& Got)
	{
		const TArray<FString> Ids = Got.GetElementIds(TEXT(""));
		std::vector<const OnlyWorlds::OWCachedElement*> Held = Want.Elements();
		if (!Test.TestEqual(Name + TEXT(": the elements"), Ids.Num(), static_cast<int32>(Held.size()))) return;
		for (int32 I = 0; I < Ids.Num(); ++I)
		{
			const OnlyWorlds::OWCachedElement* E = Held[I];
			Test.TestEqual(Name + TEXT(": id ") + FString::FromInt(I), Ids[I], Str(E->id));
			Test.TestEqual(Name + TEXT(": type of ") + Str(E->id), Got.GetElementType(Ids[I]), Str(E->type));
			Test.TestEqual(Name + TEXT(": body of ") + Str(E->id), Got.GetElementJson(Ids[I]),
				Str(OnlyWorlds::OWJson::Stringify(E->body, /*keep_number_text=*/true)));
		}
		Test.TestEqual(Name + TEXT(": cursor"), Got.GetCursor(), static_cast<int64>(Want.Cursor()));
		Test.TestEqual(Name + TEXT(": token"), Got.GetCursorToken(), Str(Want.CursorToken()));
		bool bHasEpoch = false;
		const int64 Epoch = Got.GetEpoch(bHasEpoch);
		Test.TestEqual(Name + TEXT(": has an epoch"), bHasEpoch, Want.Epoch().has_value());
		Test.TestEqual(Name + TEXT(": epoch"), Epoch, static_cast<int64>(Want.Epoch().value_or(0)));
		Test.TestFalse(Name + TEXT(": not syncing"), Got.IsSyncing());
	}

	void SameResult(FAutomationTestBase& Test, const FString& Name, const FOWSyncResult& Got, const OnlyWorlds::OWSyncResult& Want)
	{
		const FOWSyncResult W = OWClientShared::SyncResultOf(Want);
		Test.TestEqual(Name + TEXT(": Fetched"), Got.Fetched, W.Fetched);
		Test.TestEqual(Name + TEXT(": Removed"), Got.Removed, W.Removed);
		Test.TestEqual(Name + TEXT(": Cursor"), Got.Cursor, W.Cursor);
		Test.TestEqual(Name + TEXT(": bWasRebaselined"), Got.bWasRebaselined, W.bWasRebaselined);
		Test.TestEqual(Name + TEXT(": RebaselineReason"), Got.RebaselineReason, W.RebaselineReason);
		Test.TestEqual(Name + TEXT(": ResyncReason"), Got.ResyncReason, W.ResyncReason);
		Test.TestEqual(Name + TEXT(": Kept"), Got.Kept, W.Kept);
		Test.TestEqual(Name + TEXT(": bChangedAnything"), Got.bChangedAnything, W.bChangedAnything);
	}

	void SameRequests(FAutomationTestBase& Test, const FString& Name, const std::vector<OWRequest>& Got, const std::vector<OWRequest>& Want)
	{
		if (!Test.TestEqual(Name + TEXT(": the requests"), static_cast<int32>(Got.size()), static_cast<int32>(Want.size()))) return;
		for (size_t I = 0; I < Got.size(); ++I)
			Test.TestEqual(Name + TEXT(": request ") + FString::FromInt(static_cast<int32>(I)), Str(Got[I].url), Str(Want[I].url));
	}

	FString TempPath(const TCHAR* Leaf)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OWSyncTests"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = Dir / Leaf;
		IFileManager::Get().Delete(*Path);
		return Path;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncAnswersTest, "OnlyWorlds.Sync.Answers", OWSyncTest::Flags)

bool FOWSyncAnswersTest::RunTest(const FString& Parameters)
{
	for (const OWSyncTest::FCase& Case : OWSyncTest::Cases())
	{
		const FString Name = Case.Name;
		std::unique_ptr<OWSyncTest::FCoreRun> Core = OWSyncTest::RunCore(Case);
		std::unique_ptr<OWSyncTest::FNodeRun> Node = OWSyncTest::RunNode(Case);
		TestEqual(Name + TEXT(": the core answered once"), Core->Calls, 1);
		TestEqual(Name + TEXT(": the node answered once"), Node->Probe->Answers(), 1);
		if (Core->Reply.Ok())
		{
			TestEqual(Name + TEXT(": OnSuccess"), Node->Probe->Successes, 1);
			TestTrue(Name + TEXT(": no error"), Node->Probe->Error.Kind == EOWApiErrorKind::None);
			OWSyncTest::SameResult(*this, Name, Node->Probe->Result, Core->Reply.value);
		}
		else
		{
			TestEqual(Name + TEXT(": OnFailure"), Node->Probe->Failures, 1);
			const FOWApiError Want = OWClientShared::ErrorOf(*Core->Reply.error);
			TestEqual(Name + TEXT(": its status"), Node->Probe->Error.Status, Want.Status);
			TestEqual(Name + TEXT(": its code"), Node->Probe->Error.Code, Want.Code);
			TestEqual(Name + TEXT(": its line"), Node->Probe->Error.Line, Want.Line);
		}
		OWSyncTest::SameRequests(*this, Name, Node->Fake->Sent, Core->Fake->Sent);
		OWSyncTest::SameCache(*this, Name, Core->Cache, *Node->Cache.Get());
		AddInfo(FString::Printf(TEXT("%s: %d requests, %d elements held"), *Name, static_cast<int32>(Core->Fake->Sent.size()),
			static_cast<int32>(Core->Cache.Count())));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncHasChangesTest, "OnlyWorlds.Sync.HasChanges", OWSyncTest::Flags)

bool FOWSyncHasChangesTest::RunTest(const FString& Parameters)
{
	using namespace OWSyncTest;
	auto Script = []()
	{
		FScript S;
		S.push_back(Answer(200, Head(9), {{"ETag", "W/\"9:-\""}}));
		S.push_back(Answer(304, ""));
		S.push_back(Answer(200, Head(10), {{"ETag", "\"10:-\""}}));
		return S;
	};

	// The core, on one client.
	auto CoreFake = std::make_shared<FFake>();
	for (OWResponse& R : Script()) CoreFake->Script.push_back(std::move(R));
	OnlyWorlds::OWClient CoreClient(CoreConfig(CoreFake));
	OnlyWorlds::OWWorldCache CoreCache("w-1", "Test World");
	CoreCache.SetCursor(9, "9", std::nullopt, "t");
	std::vector<OnlyWorlds::OWReply<bool>> CoreAnswers;
	for (int32 I = 0; I < 3; ++I)
		OnlyWorlds::OWSync::HasChanges(CoreClient, CoreCache, [&CoreAnswers](OnlyWorlds::OWReply<bool> R) { CoreAnswers.push_back(std::move(R)); });

	// The node, on one cache.
	auto NodeFake = std::make_shared<FFake>();
	for (OWResponse& R : Script()) NodeFake->Script.push_back(std::move(R));
	TStrongObjectPtr<UOWWorldCache> Cache(UOWWorldCache::MakeWorldCache(NodeConfig(), TEXT("w-1"), TEXT("Test World"), false));
	Cache->SetTransport(NodeFake);
	Cache->Native().SetCursor(9, "9", std::nullopt, "t");
	TStrongObjectPtr<UOWSyncProbe> Probe(NewObject<UOWSyncProbe>(GetTransientPackage()));
	TArray<bool> NodeAnswers;
	for (int32 I = 0; I < 3; ++I)
	{
		UOWHasWorldChanges* Node = UOWHasWorldChanges::HasWorldChanges(Cache.Get());
		Probe->Watch(Node);
		Node->Activate();
		NodeAnswers.Add(Probe->bHasChanges);
	}

	TestEqual(TEXT("the core answered three times"), static_cast<int32>(CoreAnswers.size()), 3);
	TestEqual(TEXT("the node answered three times, OnSuccess"), Probe->Successes, 3);
	if (!TestEqual(TEXT("three requests"), static_cast<int32>(NodeFake->Sent.size()), 3) || CoreAnswers.size() != 3) return true;
	TestNull(TEXT("the first: nothing held, no tag"), NodeFake->Sent[0].Header("If-None-Match"));
	const std::string* Tag = NodeFake->Sent[1].Header("If-None-Match");
	TestEqual(TEXT("the second: the first tip's tag, exactly as received"), Tag ? Str(*Tag) : FString(TEXT("<absent>")), FString(TEXT("W/\"9:-\"")));
	const std::string* CoreTag = CoreFake->Sent[1].Header("If-None-Match");
	TestEqual(TEXT("the core sends the same"), CoreTag ? Str(*CoreTag) : FString(TEXT("<absent>")), Tag ? Str(*Tag) : FString(TEXT("<absent>")));
	for (int32 I = 0; I < 3; ++I)
		TestEqual(FString::Printf(TEXT("answer %d is the core's"), I), NodeAnswers[I], CoreAnswers[I].Ok() && CoreAnswers[I].value);
	TestFalse(TEXT("at the cursor: false"), NodeAnswers[0]);
	TestFalse(TEXT("a 304: false"), NodeAnswers[1]);
	TestTrue(TEXT("a moved head: true"), NodeAnswers[2]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncRefusalsTest, "OnlyWorlds.Sync.Refusals", OWSyncTest::Flags)

bool FOWSyncRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace OWSyncTest;
	TStrongObjectPtr<UOWSyncProbe> Probe(NewObject<UOWSyncProbe>(GetTransientPackage()));

	// A read-only cache.
	{
		auto Fake = std::make_shared<FFake>();
		TStrongObjectPtr<UOWWorldCache> Frozen(UOWWorldCache::MakeWorldCache(NodeConfig(), TEXT("w-1"), TEXT("Chapter 1"), true));
		Frozen->SetTransport(Fake);
		TestTrue(TEXT("IsReadOnly"), Frozen->IsReadOnly());
		for (bool bBase : {false, true})
		{
			UOWSyncWorld* Node = bBase ? UOWSyncWorld::BaselineWorld(Frozen.Get()) : UOWSyncWorld::SyncWorld(Frozen.Get());
			Probe->Watch(Node);
			const int32 Before = Probe->Failures;
			Node->Activate();
			TestEqual(TEXT("read-only: OnFailure at once"), Probe->Failures, Before + 1);
			TestTrue(TEXT("read-only: kind Refused"), Probe->Error.Kind == EOWApiErrorKind::Refused);
			TestTrue(TEXT("read-only: says so"), Probe->Error.Message.Contains(TEXT("read-only")));
		}
		TestEqual(TEXT("read-only: nothing sent"), static_cast<int32>(Fake->Sent.size()), 0);
	}

	// No cache.
	{
		UOWSyncWorld* Node = UOWSyncWorld::SyncWorld(nullptr);
		Probe->Watch(Node);
		const int32 Before = Probe->Failures;
		Node->Activate();
		TestEqual(TEXT("no cache: OnFailure"), Probe->Failures, Before + 1);
		TestTrue(TEXT("no cache: kind Refused"), Probe->Error.Kind == EOWApiErrorKind::Refused);
		UOWHasWorldChanges* Has = UOWHasWorldChanges::HasWorldChanges(nullptr);
		Probe->Watch(Has);
		Has->Activate();
		TestEqual(TEXT("no cache, HasWorldChanges: OnFailure"), Probe->Failures, Before + 2);
		TestTrue(TEXT("no cache, HasWorldChanges: kind Refused"), Probe->Error.Kind == EOWApiErrorKind::Refused);
	}

	// One in flight: its answers held for a later Pump, as the HTTP module answers on a later tick.
	{
		auto Fake = std::make_shared<FFake>();
		Fake->bLater = true;
		FScript S;
		Baseline(S, Head(5));
		for (OWResponse& R : S) Fake->Script.push_back(std::move(R));
		TStrongObjectPtr<UOWWorldCache> Cache(UOWWorldCache::MakeWorldCache(NodeConfig(), TEXT("w-1"), TEXT("Test World"), false));
		Cache->SetTransport(Fake);
		TStrongObjectPtr<UOWSyncProbe> First(NewObject<UOWSyncProbe>(GetTransientPackage()));
		UOWSyncWorld* Node = UOWSyncWorld::SyncWorld(Cache.Get());
		First->Watch(Node);
		Node->Activate();
		TestEqual(TEXT("in flight: not answered yet"), First->Answers(), 0);
		TestTrue(TEXT("in flight: IsSyncing"), Cache->IsSyncing());
		TestTrue(TEXT("in flight: the node is kept"), Node->IsRooted());
		UOWSyncWorld* Second = UOWSyncWorld::SyncWorld(Cache.Get());
		Probe->Watch(Second);
		const int32 Before = Probe->Failures;
		Second->Activate();
		TestEqual(TEXT("a second: OnFailure at once"), Probe->Failures, Before + 1);
		TestTrue(TEXT("a second: kind Refused"), Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestEqual(TEXT("a second: nothing sent"), static_cast<int32>(Fake->Sent.size()), 1);
		int32 Pumps = 0;
		while (Fake->Pump() > 0 && Pumps < 1000) ++Pumps;
		TestEqual(TEXT("in flight: answered once, OnSuccess"), First->Successes, 1);
		TestFalse(TEXT("then: not syncing"), Cache->IsSyncing());
		TestFalse(TEXT("then: let go"), Node->IsRooted());
		TestEqual(TEXT("then: the requests"), static_cast<int32>(Fake->Sent.size()), static_cast<int32>(S.size()));
		AddInfo(FString::Printf(TEXT("in flight: %d pumps, %d elements held"), Pumps, Cache->GetElementCount()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncFileTest, "OnlyWorlds.Sync.File", OWSyncTest::Flags)

bool FOWSyncFileTest::RunTest(const FString& Parameters)
{
	using namespace OWSyncTest;
	const FCase Case = Cases()[0];
	std::unique_ptr<FNodeRun> Node = RunNode(Case);
	const FString Path = TempPath(TEXT("held.owcache"));
	FString Error;
	TestTrue(TEXT("WriteFile"), Node->Cache->WriteFile(Path, Error));
	TestTrue(TEXT("no error: ") + Error, Error.IsEmpty());
	std::string Why;
	std::unique_ptr<OnlyWorlds::OWWorldCache> Core = OnlyWorlds::OWWorldCache::ReadFile(Utf8(Path), &Why);
	if (!TestTrue(TEXT("the core reads it: ") + Str(Why), Core != nullptr)) return true;
	TStrongObjectPtr<UOWWorldCache> Back(UOWWorldCache::OpenWorldCache(NodeConfig(), Path, Error));
	if (!TestTrue(TEXT("OpenWorldCache: ") + Error, Back.IsValid())) return true;
	SameCache(*this, TEXT("read back"), *Core, *Back.Get());
	SameCache(*this, TEXT("as written"), *Core, *Node->Cache.Get());
	TestEqual(TEXT("its world id"), Back->GetWorldId(), FString(TEXT("w-1")));
	TestEqual(TEXT("its world name"), Back->GetWorldName(), FString(TEXT("Test World")));
	TestEqual(TEXT("its view holds it"), Back->GetHeldWorld()->GetElementCount(), static_cast<int32>(Core->Count()));
	FString Text;
	FFileHelper::LoadFileToString(Text, *Path);
	TestFalse(TEXT("the key is not in the file"), Text.Contains(TEXT("ow_w_test")));
	TestFalse(TEXT("nor the PIN"), Text.Contains(TEXT("1234")));

	const FString Missing = TempPath(TEXT("none.owcache"));
	TStrongObjectPtr<UOWWorldCache> Empty(UOWWorldCache::OpenWorldCache(NodeConfig(), Missing, Error));
	TestTrue(TEXT("no file: an empty cache"), Empty.IsValid() && Empty->GetElementCount() == 0 && Error.IsEmpty());

	const FString Bad = TempPath(TEXT("bad.owcache"));
	FFileHelper::SaveStringToFile(TEXT("{\"format\":\"something else\"}"), *Bad);
	UOWWorldCache* Refused = UOWWorldCache::OpenWorldCache(NodeConfig(), Bad, Error);
	TestNull(TEXT("a file that is no cache: none"), Refused);
	TestFalse(TEXT("and why"), Error.IsEmpty());
	IFileManager::Get().Delete(*Path);
	IFileManager::Get().Delete(*Bad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncWorldTest, "OnlyWorlds.Sync.World", OWSyncTest::Flags)

bool FOWSyncWorldTest::RunTest(const FString& Parameters)
{
	using namespace OWSyncTest;
	const FCase Case = Cases()[0];
	std::unique_ptr<FCoreRun> Core = RunCore(Case);
	std::unique_ptr<FNodeRun> Node = RunNode(Case);
	UOWWorld* View = Node->Cache->GetHeldWorld();
	if (!TestNotNull(TEXT("a view"), View)) return true;
	std::unique_ptr<OnlyWorlds::OWWorld> Want = Core->Cache.ToWorld();
	TestEqual(TEXT("its id"), View->GetId(), Str(Want->Id()));
	const TArray<FString> Ids = View->GetElementIds(TEXT(""));
	if (TestEqual(TEXT("its elements"), Ids.Num(), static_cast<int32>(Want->elements.size())))
	{
		for (int32 I = 0; I < Ids.Num(); ++I)
		{
			TestEqual(FString::Printf(TEXT("element %d"), I), Ids[I], Str(Want->elements[I].Id()));
			TestEqual(TEXT("its text"), View->GetElementJson(Ids[I]), Str(OnlyWorlds::OWJson::Stringify(Want->elements[I].body, true)));
		}
	}
	const TArray<FOWCharacter> Characters = UOWElements::GetAllCharacters(View);
	TestEqual(TEXT("UOWElements reads the synced Characters"), Characters.Num(), static_cast<int32>(Core->Cache.OfType("character").size()));
	TestTrue(TEXT("some Characters"), Characters.Num() > 0);

	// A second sync that changes it: the same object, its contents the held world's.
	auto Fake = std::make_shared<FFake>();
	Fake->Script.push_back(Answer(200, GuestHead(41, 3)));
	Fake->Script.push_back(Answer(200, R"({"cursor":"41:z:3","has_more":false,"head":41,"epoch":3,"changes":[
		{"op":"upsert","type":"character","id":"c9","change_seq":41,"element":{"id":"c9","name":"Later"}}]})"));
	Node->Cache->SetTransport(Fake);
	TStrongObjectPtr<UOWSyncProbe> Probe(NewObject<UOWSyncProbe>(GetTransientPackage()));
	UOWSyncWorld* Again = UOWSyncWorld::SyncWorld(Node->Cache.Get());
	Probe->Watch(Again);
	Again->Activate();
	TestEqual(TEXT("the second sync: OnSuccess"), Probe->Successes, 1);
	TestTrue(TEXT("the same object"), Node->Cache->GetHeldWorld() == View);
	TestTrue(TEXT("holding the new element"), View->HasElement(TEXT("c9")));
	TestEqual(TEXT("as many as the cache"), View->GetElementCount(), Node->Cache->GetElementCount());
	return true;
}

namespace OWSyncTest
{
	// Unreal's HTTP, every request and every answer's status kept.
	struct FRecording : OnlyWorlds::OWTransport
	{
		std::shared_ptr<OnlyWorlds::OWTransport> Inner = FOWHttpTransport::Make();
		std::vector<OWRequest> Sent;
		std::vector<int> Statuses;

		void Send(OWRequest Request, OnlyWorlds::OWResponseHandler Done) override
		{
			Sent.push_back(Request);
			const size_t At = Statuses.size();
			Statuses.push_back(-1);
			Inner->Send(std::move(Request), [this, At, Done](OWResponse R)
			{
				Statuses[At] = R.status;
				Done(std::move(R));
			});
		}
	};

	struct FLive
	{
		std::shared_ptr<FRecording> Wire = std::make_shared<FRecording>();
		TStrongObjectPtr<UOWWorldCache> Cache;
		TStrongObjectPtr<UOWSyncProbe> Probe;
		int32 Step = 0;
		int32 Want = 0;
		TArray<bool> Polls;
		double Start = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWSyncLiveHeadTagTest, "OnlyWorldsLive.Sync.HeadTag", OWSyncTest::Flags)

bool FOWSyncLiveHeadTagTest::RunTest(const FString& Parameters)
{
	using namespace OWSyncTest;
	auto L = std::make_shared<FLive>();
	FOWClientConfig Moppetopia;
	Moppetopia.ApiKey = TEXT("0000000001");
	L->Cache.Reset(UOWWorldCache::MakeWorldCache(Moppetopia, TEXT(""), TEXT("Moppetopia"), false));
	L->Cache->SetTransport(L->Wire);
	L->Probe.Reset(NewObject<UOWSyncProbe>(GetTransientPackage()));
	L->Start = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, L]()
	{
		if (L->Probe->Answers() < L->Want)
		{
			if (FPlatformTime::Seconds() - L->Start < 120.0) return false;
			AddError(FString::Printf(TEXT("no answer to step %d within 120 s"), L->Step));
			return true;
		}
		if (L->Probe->Failures > 0)
		{
			AddError(FString::Printf(TEXT("step %d failed: %s"), L->Step, *L->Probe->Error.Line));
			return true;
		}
		if (L->Step >= 2) L->Polls.Add(L->Probe->bHasChanges);
		if (L->Step == 0)
		{
			UOWSyncWorld* Node = UOWSyncWorld::SyncWorld(L->Cache.Get());
			L->Probe->Watch(Node);
			L->Want = 1;
			L->Step = 1;
			Node->Activate();
			return false;
		}
		if (L->Step == 1) AddInfo(FString::Printf(TEXT("SyncWorld: %d elements, cursor %lld, %d requests"),
			L->Cache->GetElementCount(), L->Cache->GetCursor(), static_cast<int32>(L->Wire->Sent.size())));
		if (L->Step == 1 || L->Step == 2)
		{
			UOWHasWorldChanges* Node = UOWHasWorldChanges::HasWorldChanges(L->Cache.Get());
			L->Probe->Watch(Node);
			L->Want = L->Probe->Answers() + 1;
			++L->Step;
			Node->Activate();
			return false;
		}
		// Both polls answered: read what the second sent and what came back.
		const int32 N = static_cast<int32>(L->Wire->Sent.size());
		if (!TestTrue(TEXT("two polls after the sync"), N >= 2 && L->Polls.Num() == 2)) return true;
		const OWRequest& First = L->Wire->Sent[N - 2];
		const OWRequest& Second = L->Wire->Sent[N - 1];
		TestTrue(TEXT("the polls are tip reads"), First.url.find("head=true") != std::string::npos && Second.url.find("head=true") != std::string::npos);
		const std::string* Tag = Second.Header("If-None-Match");
		TestNotNull(TEXT("the second poll carries If-None-Match"), Tag);
		const int Status = L->Wire->Statuses[N - 1];
		AddInfo(FString::Printf(TEXT("second poll: If-None-Match %s, answered %d, bHasChanges %s; first answered %d"),
			Tag ? *Str(*Tag) : TEXT("<absent>"), Status, L->Polls[1] ? TEXT("true") : TEXT("false"), L->Wire->Statuses[N - 2]));
		if (Status == 304) TestFalse(TEXT("a 304: no change"), L->Polls[1]);
		else AddInfo(FString::Printf(TEXT("NOT 304: the second poll answered %d, so the tag was ignored on the way or by keel"), Status));
		return true;
	}));
	return true;
}

#endif
