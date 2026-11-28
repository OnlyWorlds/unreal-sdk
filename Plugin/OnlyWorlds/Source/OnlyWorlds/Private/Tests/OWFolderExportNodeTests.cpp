// The folder export's Blueprint node (OWFolderExportNode.h), through the client nodes' fake transport: no test here
// touches the network. The node is made by its factory, its OnSuccess and OnFailure bound to a listener as a graph's
// async node binds them, given the fake, and activated: what it writes, what it answers, its refusals, one answer only.
// The export's own rules are the core's, held by tests/test_folder_export.cpp.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWConvert.h"
#include "OWFiles.h"
#include "OWFolderExport.h"
#include "OWFolderExportNode.h"
#include "OWJson.h"
#include "OWFakeTransport.h"
#include "OWFolderExportNodeProbe.h"
#include "OWTestPaths.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWResponse;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	const char* const EmptyPage = R"({"data":[],"has_more":false,"next_cursor":null})";

	OWResponse Answer(int Status, const char* Body)
	{
		OWResponse R;
		R.status = Status;
		R.body = Body;
		return R;
	}

	FOWClientConfig Config()
	{
		FOWClientConfig C;
		C.ApiKey = TEXT("ow_r_test");
		C.ApiPin = TEXT("1234");
		C.BaseUrl = TEXT("http://keel.test/api/v2");
		return C;
	}

	struct FRun
	{
		std::shared_ptr<OWClientNodesTest::FFakeTransport> Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		TStrongObjectPtr<UOWFolderExportNodeProbe> Probe;
		FRun() : Probe(NewObject<UOWFolderExportNodeProbe>(GetTransientPackage())) { Fake->Fallback = Answer(200, EmptyPage); }

		void Go(const FString& Folder)
		{
			UOWClientFolderExport* Node = UOWClientFolderExport::ExportWorldToFolder(Config(), Folder);
			Probe->Watch(Node);
			Node->SetTransport(Fake);
			Node->Activate();
		}
	};

	FString Fresh()
	{
		const FString Root = OWTestPaths::Scratch(TEXT("folder-export"));
		OnlyWorlds::OWFiles::RemoveTree(OWConvert::Path(Root));
		OnlyWorlds::OWFiles::MakeDirs(OWConvert::Path(Root));
		return Root;
	}

	std::string Text(const FString& Path)
	{
		OnlyWorlds::OWBytes B;
		return OnlyWorlds::OWFiles::Read(OWConvert::Path(Path), B) ? std::string(B.begin(), B.end()) : std::string();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFolderExportNodeTest, "OnlyWorlds.Client.Nodes.FolderExport", OWTestFlags)
bool FOWFolderExportNodeTest::RunTest(const FString& Parameters)
{
	// A world with one Character, read by GETs only, written as a folder: the report, the files, the producer line.
	{
		const FString Root = Fresh();
		const FString Folder = Root / TEXT("world");
		FRun Run;
		Run.Fake->Script.push_back(Answer(200, R"({"id":"w-1","name":"Moor","time_range_current":3})"));
		Run.Fake->Script.push_back(Answer(200, R"({"kind":"owner","role":"owner","character":null})"));
		Run.Fake->Script.push_back(Answer(200, EmptyPage));  // ability
		Run.Fake->Script.push_back(Answer(200, EmptyPage));  // collective
		Run.Fake->Script.push_back(Answer(200, R"({"data":[{"id":"c0000000-0000-7000-8000-00000000000a","type":"character","name":"Mara","level":2.50}],"has_more":false,"next_cursor":null})"));
		Run.Go(Folder);

		TestEqual(TEXT("OnSuccess once"), Run.Probe->Successes, 1);
		TestEqual(TEXT("no OnFailure"), Run.Probe->Failures, 0);
		TestTrue(TEXT("Error is empty on OnSuccess"), Run.Probe->Error.Kind == EOWApiErrorKind::None);
		TestEqual(TEXT("24 GETs: the world, who the key is, the 22 types"), static_cast<int32>(Run.Fake->Sent.size()), 24);
		bool bGets = true;
		for (const OnlyWorlds::OWRequest& R : Run.Fake->Sent) bGets = bGets && R.method == "GET";
		TestTrue(TEXT("GETs only"), bGets);
		TestEqual(TEXT("Report.WorldId"), Run.Probe->Report.WorldId, FString(TEXT("w-1")));
		TestEqual(TEXT("Report.WorldName"), Run.Probe->Report.WorldName, FString(TEXT("Moor")));
		TestEqual(TEXT("Report.Total"), Run.Probe->Report.Total, 1);
		TestEqual(TEXT("Report.Counts holds the one type"), Run.Probe->Report.Counts.Num(), 1);
		const int32* Characters = Run.Probe->Report.Counts.Find(TEXT("character"));
		TestTrue(TEXT("Report.Counts[character] is 1"), Characters && *Characters == 1);
		TestEqual(TEXT("Report.Folder, absolute and '/' separated"), Run.Probe->Report.Folder, Str(OnlyWorlds::OWFiles::Absolute(OWConvert::Path(Folder))));

		TestEqual(TEXT("the Character as the folder format writes it"), Str(Text(Folder / TEXT("elements/character/mara--0000000a.json"))),
			FString(TEXT("{\n  \"id\": \"c0000000-0000-7000-8000-00000000000a\",\n  \"type\": \"character\",\n  \"name\": \"Mara\",\n  \"level\": 2.5\n}\n")));
		OnlyWorlds::OWJsonValue World = OnlyWorlds::OWJson::Parse(Text(Folder / TEXT("world.json"))).value;
		const OnlyWorlds::OWJsonValue* Producer = World.Find("producer");
		TestTrue(TEXT("world.json's producer is this plugin's line"), Producer && *Producer == OnlyWorlds::OWFolderExport::Producer());
		TestTrue(TEXT("the now as the standard names it"), World.Has("time_current") && !World.Has("time_range_current"));
		const OnlyWorlds::OWJsonValue* Slice = World.Find("exported_as");
		const OnlyWorlds::OWJsonValue* Kind = Slice ? Slice->Find("kind") : nullptr;
		TestTrue(TEXT("exported_as names the owner's slice"), Kind && Kind->Text() == "owner");
		OnlyWorlds::OWFiles::RemoveTree(OWConvert::Path(Root));
	}

	// A folder that holds anything: refused before the first read, the reason in Error.Line, the folder as it was.
	{
		const FString Root = Fresh();
		const FString Folder = Root / TEXT("world");
		OnlyWorlds::OWFiles::Write(OWConvert::Path(Folder / TEXT("notes.txt")), OnlyWorlds::OWBytes{'m', 'i', 'n', 'e'});
		FRun Run;
		Run.Go(Folder);
		TestEqual(TEXT("occupied: OnFailure once"), Run.Probe->Failures, 1);
		TestEqual(TEXT("occupied: no OnSuccess"), Run.Probe->Successes, 0);
		TestTrue(TEXT("occupied: kind Refused"), Run.Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestTrue(TEXT("occupied: the line says why, got ") + Run.Probe->Error.Line,
			Run.Probe->Error.Line.Contains(TEXT("exists and is not an empty directory; a world folder is written whole, once.")));
		TestEqual(TEXT("occupied: nothing sent"), static_cast<int32>(Run.Fake->Sent.size()), 0);
		TestEqual(TEXT("occupied: the folder as it was"), Str(Text(Folder / TEXT("notes.txt"))), FString(TEXT("mine")));
		TestTrue(TEXT("occupied: an empty report"), Run.Probe->Report.Total == 0 && Run.Probe->Report.Folder.IsEmpty());
		OnlyWorlds::OWFiles::RemoveTree(OWConvert::Path(Root));
	}

	// The server's refusal: kind Api, its status, and nothing left beside the folder.
	{
		const FString Root = Fresh();
		FRun Run;
		Run.Fake->Script.push_back(Answer(401, R"({"error":{"type":"authentication_error","code":"invalid_credentials","message":"Invalid API key or PIN."}})"));
		Run.Go(Root / TEXT("world"));
		TestEqual(TEXT("refused read: OnFailure once"), Run.Probe->Failures, 1);
		TestTrue(TEXT("refused read: kind Api, 401"), Run.Probe->Error.Kind == EOWApiErrorKind::Api && Run.Probe->Error.Status == 401);
		TestEqual(TEXT("refused read: keel's code"), Run.Probe->Error.Code, FString(TEXT("invalid_credentials")));
		TestTrue(TEXT("refused read: nothing written"), OnlyWorlds::OWFiles::List(OWConvert::Path(Root), true).empty()
			&& OnlyWorlds::OWFiles::List(OWConvert::Path(Root), false).empty());
		OnlyWorlds::OWFiles::RemoveTree(OWConvert::Path(Root));
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
