// The image and file nodes (OWFileNodes.h), through the client nodes' fake transport (OWFakeTransport.h), which
// records each request and answers from a script: no test here touches the network. Each node is made by its factory,
// its OnSuccess and OnFailure bound to a listener (OWFileNodesProbe.h) as a graph's async node binds them, given the
// fake (SetTransport), and activated. What each sends (to keel and to the upload host), what its pins hold, the
// FromDisk factories reading a file (its name the path's) and refusing one they cannot read with nothing sent, a
// refusal and an error reaching OnFailure, one answer only.
//
// Every helper is in the namespace OWFileNodesTest, never at file scope (C4459: CppCompileWarnings.cs).

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWConvert.h"
#include "OWFakeTransport.h"
#include "OWFileNodes.h"
#include "OWFileNodesProbe.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWFileNodesTest
{
	constexpr EAutomationTestFlags FileTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	const char* const Root = "http://keel.test/api/v2";

	OnlyWorlds::OWResponse Respond(int Code, const std::string& Text)
	{
		OnlyWorlds::OWResponse Out;
		Out.status = Code;
		Out.body = Text;
		return Out;
	}

	FOWClientConfig Writer()
	{
		FOWClientConfig Out;
		Out.ApiKey = TEXT("ow_w_test");
		Out.ApiPin = TEXT("1234");
		Out.BaseUrl = OWConvert::Str(std::string(Root));
		return Out;
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

	const std::string ImageTicket =
		R"({"ticket":"t.sig","upload_url":"https://upload.test/v1/upload","prefix":"u/w1/","max_bytes":100,"exp":1900000000,"uses":1})";
	const std::string Uploaded =
		R"({"url":"https://media.test/u/w1/x.png","key":"u/w1/x.png","bytes":3,"type":"image/png","etag":"e"})";
	const std::string FileTicket =
		R"({"ticket":"f.sig","upload_url":"https://upload.test/v1/file","prefix":"w/w1/l1/","key":"w/w1/l1/f1.pdf","max_bytes":100,)"
		R"("exp":1900000000,"uses":1,"kind":"file","file_id":"f1"})";
	const std::string Stored = R"({"key":"w/w1/l1/f1.pdf","fid":"f1","bytes":5,"type":"application/pdf","etag":"e"})";
	const std::string Row =
		R"({"id":"f1","name":"Map.pdf","kind":"pdf","size":5,"label":"description","added_by":{"name":"Ada","membership":null,)"
		R"("agent":false,"owner":true},"added_at":"2026-10-19T18:00:00Z"})";

	struct FRun
	{
		std::shared_ptr<OWClientNodesTest::FFakeTransport> Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		TStrongObjectPtr<UOWFileNodesProbe> Probe;
		FRun() : Probe(NewObject<UOWFileNodesProbe>(GetTransientPackage())) {}
	};

	template <typename TNode>
	void Go(FRun& Run, TNode* Node)
	{
		Run.Probe->Watch(Node);
		Node->SetTransport(Run.Fake);
		Node->Activate();
	}

	// A scratch file under the host's Saved folder, its bytes as given; removed by the test.
	FString Scratch(const TCHAR* Name, const TArray<uint8>& Bytes)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OWFileNodesTest") / Name);
		FFileHelper::SaveArrayToFile(Bytes, *Path);
		return Path;
	}
}

namespace OWFNT = OWFileNodesTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFileNodesImageTest, "OnlyWorlds.Client.FileNodes.Image", OWFNT::FileTestFlags)
bool FOWFileNodesImageTest::RunTest(const FString& Parameters)
{
	{
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(201, OWFNT::ImageTicket), OWFNT::Respond(201, OWFNT::Uploaded)};
		OWFNT::Go(Run, UOWClientImage::UploadImage(OWFNT::Writer(), {0x89, 0x50, 0x00}, TEXT("u/w1/x.png")));
		TestEqual(TEXT("UploadImage: one success"), Run.Probe->Successes, 1);
		TestEqual(TEXT("UploadImage: the url"), Run.Probe->Image.Url, FString(TEXT("https://media.test/u/w1/x.png")));
		TestEqual(TEXT("UploadImage: the key"), Run.Probe->Image.Key, FString(TEXT("u/w1/x.png")));
		TestEqual(TEXT("UploadImage: the bytes"), Run.Probe->Image.Bytes, int64(3));
		TestEqual(TEXT("UploadImage: the type"), Run.Probe->Image.Type, FString(TEXT("image/png")));
		if (TestEqual(TEXT("UploadImage: two requests"), static_cast<int32>(Run.Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("the ticket"), OWFNT::Line(Run.Fake->Sent[0]), FString(TEXT("POST /media/ticket|API-Key=ow_w_test; API-Pin=1234")));
			TestEqual(TEXT("the host"), OWFNT::Line(Run.Fake->Sent[1]),
				FString(TEXT("POST https://upload.test/v1/upload|Authorization=Bearer t.sig; X-Key=u/w1/x.png")));
			TestTrue(TEXT("the bytes whole"), Run.Fake->Sent[1].body && *Run.Fake->Sent[1].body == std::string("\x89P\0", 3));
		}
	}
	{
		// From disk: the file's bytes; no key, no X-Key.
		const FString Path = OWFNT::Scratch(TEXT("shot.png"), {0x89, 0x50, 0x4e, 0x47});
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(201, OWFNT::ImageTicket), OWFNT::Respond(201, OWFNT::Uploaded)};
		OWFNT::Go(Run, UOWClientImage::UploadImageFromDisk(OWFNT::Writer(), Path, TEXT("")));
		TestEqual(TEXT("UploadImageFromDisk: one success"), Run.Probe->Successes, 1);
		if (TestEqual(TEXT("UploadImageFromDisk: two requests"), static_cast<int32>(Run.Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("the host, no X-Key"), OWFNT::Line(Run.Fake->Sent[1]),
				FString(TEXT("POST https://upload.test/v1/upload|Authorization=Bearer t.sig")));
			TestTrue(TEXT("the file's bytes"), Run.Fake->Sent[1].body && *Run.Fake->Sent[1].body == "\x89PNG");
		}
		IFileManager::Get().Delete(*Path);
	}
	{
		// A file that cannot be read: refused, nothing sent.
		OWFNT::FRun Run;
		OWFNT::Go(Run, UOWClientImage::UploadImageFromDisk(OWFNT::Writer(),
			FPaths::ProjectSavedDir() / TEXT("OWFileNodesTest/none.png"), TEXT("")));
		TestEqual(TEXT("a missing file: one failure"), Run.Probe->Failures, 1);
		TestTrue(TEXT("a missing file: kind Refused"), Run.Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestTrue(TEXT("a missing file: named"), Run.Probe->Error.Message.Contains(TEXT("could not be read")));
		TestEqual(TEXT("a missing file: nothing sent"), static_cast<int32>(Run.Fake->Sent.size()), 0);
	}
	IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("OWFileNodesTest")), false, true);
	{
		// The host's refusal reaches OnFailure with its code; the ticket is not in the words.
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(201, OWFNT::ImageTicket), OWFNT::Respond(401, R"({"error":"ticket_used"})")};
		OWFNT::Go(Run, UOWClientImage::UploadImage(OWFNT::Writer(), {1, 2, 3}, TEXT("")));
		TestEqual(TEXT("the host refuses: one failure"), Run.Probe->Failures, 1);
		TestEqual(TEXT("the host's code"), Run.Probe->Error.Code, FString(TEXT("ticket_used")));
		TestEqual(TEXT("the host's status"), Run.Probe->Error.Status, 401);
		TestFalse(TEXT("the ticket is not in the words"), Run.Probe->Error.Line.Contains(TEXT("t.sig")));
	}
	{
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(200, R"({"ticket":"d.sig","remove_url":"https://upload.test/v1/remove","exp":1,"key":"u/w1/x.png","referenced":2})"),
			OWFNT::Respond(200, R"({"removed":"u/w1/x.png","bytes":3,"cache":"may_linger"})")};
		OWFNT::Go(Run, UOWClientImageRemoval::RemoveImage(OWFNT::Writer(), TEXT("https://media.test/u/w1/x.png")));
		TestEqual(TEXT("RemoveImage: one success"), Run.Probe->Successes, 1);
		TestEqual(TEXT("RemoveImage: removed"), Run.Probe->Removal.Removed, FString(TEXT("u/w1/x.png")));
		TestEqual(TEXT("RemoveImage: bytes"), Run.Probe->Removal.Bytes, int64(3));
		TestEqual(TEXT("RemoveImage: cache"), Run.Probe->Removal.Cache, FString(TEXT("may_linger")));
		if (TestEqual(TEXT("RemoveImage: two requests"), static_cast<int32>(Run.Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("the removal ticket"), OWFNT::Line(Run.Fake->Sent[0]),
				FString(TEXT("POST /media/remove-ticket|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json")));
			TestEqual(TEXT("its body, the URL cut to its key"), OWFNT::BodyLine(Run.Fake->Sent[0]), FString(TEXT("{\"key\":\"u/w1/x.png\"}")));
			TestEqual(TEXT("the host"), OWFNT::Line(Run.Fake->Sent[1]),
				FString(TEXT("POST https://upload.test/v1/remove|Authorization=Bearer d.sig")));
			TestEqual(TEXT("no body"), OWFNT::BodyLine(Run.Fake->Sent[1]), FString(TEXT("<none>")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFileNodesFilesTest, "OnlyWorlds.Client.FileNodes.Files", OWFNT::FileTestFlags)
bool FOWFileNodesFilesTest::RunTest(const FString& Parameters)
{
	{
		OWFNT::FRun Run;
		Run.Fake->Fallback = OWFNT::Respond(200, "{\"data\":[" + OWFNT::Row + "]}");
		OWFNT::Go(Run, UOWClientFiles::ListElementFiles(OWFNT::Writer(), TEXT("location"), TEXT("l1")));
		TestEqual(TEXT("ListElementFiles: one success"), Run.Probe->Successes, 1);
		if (TestEqual(TEXT("ListElementFiles: one request"), static_cast<int32>(Run.Fake->Sent.size()), 1))
			TestEqual(TEXT("ListElementFiles: the key alone"), OWFNT::Line(Run.Fake->Sent[0]), FString(TEXT("GET /location/l1/files|API-Key=ow_w_test")));
		if (TestEqual(TEXT("ListElementFiles: one row"), Run.Probe->Files.Num(), 1))
		{
			const FOWFileRow& R = Run.Probe->Files[0];
			TestEqual(TEXT("Id"), R.Id, FString(TEXT("f1")));
			TestEqual(TEXT("Name"), R.Name, FString(TEXT("Map.pdf")));
			TestEqual(TEXT("Kind"), R.Kind, FString(TEXT("pdf")));
			TestEqual(TEXT("Size"), R.Size, int64(5));
			TestEqual(TEXT("Label"), R.Label, FString(TEXT("description")));
			TestEqual(TEXT("AddedByName"), R.AddedByName, FString(TEXT("Ada")));
			TestEqual(TEXT("AddedByMembership, null"), R.AddedByMembership, FString());
			TestTrue(TEXT("bAddedByOwner as served, bAddedByAgent"), R.bAddedByOwner && !R.bAddedByAgent);
			TestEqual(TEXT("AddedAt"), R.AddedAt, FString(TEXT("2026-10-19T18:00:00Z")));
			TestEqual(TEXT("no element on an element's list"), R.ElementType + R.ElementId, FString());
		}
		TestEqual(TEXT("no total on an element's list"), Run.Probe->UsedBytes + Run.Probe->CapBytes, int64(0));
	}
	{
		OWFNT::FRun Run;
		std::string WorldRow = OWFNT::Row;
		WorldRow.insert(WorldRow.size() - 1, R"(,"element_type":"location","element_id":"l1")");
		Run.Fake->Fallback = OWFNT::Respond(200, "{\"data\":[" + WorldRow + "],\"used_bytes\":42991616,\"cap_bytes\":262144000}");
		OWFNT::Go(Run, UOWClientFiles::ListWorldFiles(OWFNT::Writer()));
		TestEqual(TEXT("ListWorldFiles: one success"), Run.Probe->Successes, 1);
		if (TestEqual(TEXT("ListWorldFiles: one request"), static_cast<int32>(Run.Fake->Sent.size()), 1))
			TestEqual(TEXT("ListWorldFiles: the route"), OWFNT::Line(Run.Fake->Sent[0]), FString(TEXT("GET /world/files|API-Key=ow_w_test")));
		TestEqual(TEXT("UsedBytes"), Run.Probe->UsedBytes, int64(42991616));
		TestEqual(TEXT("CapBytes"), Run.Probe->CapBytes, int64(262144000));
		if (TestEqual(TEXT("ListWorldFiles: one row"), Run.Probe->Files.Num(), 1))
		{
			TestEqual(TEXT("ElementType"), Run.Probe->Files[0].ElementType, FString(TEXT("location")));
			TestEqual(TEXT("ElementId"), Run.Probe->Files[0].ElementId, FString(TEXT("l1")));
		}
	}
	{
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(201, OWFNT::FileTicket), OWFNT::Respond(201, OWFNT::Stored)};
		OWFNT::Go(Run, UOWClientFileUpload::UploadElementFile(OWFNT::Writer(), TEXT("location"), TEXT("l1"),
			{0x25, 0x50, 0x44, 0x46, 0x2d}, TEXT("Map.pdf"), TEXT("description")));
		TestEqual(TEXT("UploadElementFile: one success"), Run.Probe->Successes, 1);
		TestEqual(TEXT("UploadElementFile: Fid"), Run.Probe->File.Fid, FString(TEXT("f1")));
		TestEqual(TEXT("UploadElementFile: Key"), Run.Probe->File.Key, FString(TEXT("w/w1/l1/f1.pdf")));
		TestEqual(TEXT("UploadElementFile: Bytes"), Run.Probe->File.Bytes, int64(5));
		if (TestEqual(TEXT("UploadElementFile: two requests"), static_cast<int32>(Run.Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("the ticket"), OWFNT::Line(Run.Fake->Sent[0]),
				FString(TEXT("POST /location/l1/files|API-Key=ow_w_test; API-Pin=1234; Content-Type=application/json")));
			TestEqual(TEXT("its body"), OWFNT::BodyLine(Run.Fake->Sent[0]),
				FString(TEXT("{\"name\":\"Map.pdf\",\"kind\":\"pdf\",\"size\":5,\"label\":\"description\"}")));
			TestEqual(TEXT("the host"), OWFNT::Line(Run.Fake->Sent[1]), FString(TEXT("POST https://upload.test/v1/file|Authorization=Bearer f.sig")));
			TestEqual(TEXT("the bytes"), OWFNT::BodyLine(Run.Fake->Sent[1]), FString(TEXT("%PDF-")));
		}
	}
	{
		// From disk: the name is the path's file name, the kind read from it; Label "" sends none.
		const FString Path = OWFNT::Scratch(TEXT("Ledger.PDF"), {0x25, 0x50, 0x44, 0x46, 0x2d});
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(201, OWFNT::FileTicket), OWFNT::Respond(201, OWFNT::Stored)};
		OWFNT::Go(Run, UOWClientFileUpload::UploadElementFileFromDisk(OWFNT::Writer(), TEXT("location"), TEXT("l1"), Path, TEXT("")));
		TestEqual(TEXT("UploadElementFileFromDisk: one success"), Run.Probe->Successes, 1);
		if (TestEqual(TEXT("UploadElementFileFromDisk: two requests"), static_cast<int32>(Run.Fake->Sent.size()), 2))
		{
			TestEqual(TEXT("the path's name, its kind, no label"), OWFNT::BodyLine(Run.Fake->Sent[0]),
				FString(TEXT("{\"name\":\"Ledger.PDF\",\"kind\":\"pdf\",\"size\":5}")));
			TestEqual(TEXT("the file's bytes"), OWFNT::BodyLine(Run.Fake->Sent[1]), FString(TEXT("%PDF-")));
		}
		IFileManager::Get().Delete(*Path);
		IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("OWFileNodesTest")), false, true);
	}
	{
		// A name keel takes no file with: refused by the core, nothing sent.
		OWFNT::FRun Run;
		OWFNT::Go(Run, UOWClientFileUpload::UploadElementFile(OWFNT::Writer(), TEXT("location"), TEXT("l1"), {1}, TEXT("report.docx"), TEXT("")));
		TestEqual(TEXT("a .docx: one failure"), Run.Probe->Failures, 1);
		TestTrue(TEXT("a .docx: kind Refused"), Run.Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestEqual(TEXT("a .docx: nothing sent"), static_cast<int32>(Run.Fake->Sent.size()), 0);
	}
	{
		// keel's 403 files_full reaches OnFailure with its code; the host is never called.
		OWFNT::FRun Run;
		Run.Fake->Script = {OWFNT::Respond(403, R"({"error":{"type":"permission_error","code":"files_full","message":"Full."}})")};
		OWFNT::Go(Run, UOWClientFileUpload::UploadElementFile(OWFNT::Writer(), TEXT("location"), TEXT("l1"), {1}, TEXT("a.pdf"), TEXT("")));
		TestEqual(TEXT("files_full: one failure"), Run.Probe->Failures, 1);
		TestEqual(TEXT("files_full: the code"), Run.Probe->Error.Code, FString(TEXT("files_full")));
		TestEqual(TEXT("files_full: the host never called"), static_cast<int32>(Run.Fake->Sent.size()), 1);
	}
	{
		OWFNT::FRun Run;
		Run.Fake->Fallback = OWFNT::Respond(200, R"({"url":"https://files.test/v1/get?t=g.sig","exp":1900000300,"name":"Map.pdf","kind":"pdf","size":5})");
		OWFNT::Go(Run, UOWClientFileLink::ReadFileDownloadLink(OWFNT::Writer(), TEXT("location"), TEXT("l1"), TEXT("f1")));
		TestEqual(TEXT("ReadFileDownloadLink: one success"), Run.Probe->Successes, 1);
		if (TestEqual(TEXT("ReadFileDownloadLink: one request"), static_cast<int32>(Run.Fake->Sent.size()), 1))
			TestEqual(TEXT("the route"), OWFNT::Line(Run.Fake->Sent[0]), FString(TEXT("GET /location/l1/files/f1/download|API-Key=ow_w_test")));
		TestEqual(TEXT("Url"), Run.Probe->Link.Url, FString(TEXT("https://files.test/v1/get?t=g.sig")));
		TestEqual(TEXT("Exp"), Run.Probe->Link.Exp, int64(1900000300));
		TestEqual(TEXT("Name"), Run.Probe->Link.Name, FString(TEXT("Map.pdf")));
		TestEqual(TEXT("Size"), Run.Probe->Link.Size, int64(5));
	}
	{
		OWFNT::FRun Run;
		Run.Fake->Fallback = OWFNT::Respond(204, "");
		OWFNT::Go(Run, UOWClientFileRemoval::RemoveElementFile(OWFNT::Writer(), TEXT("location"), TEXT("l1"), TEXT("f1")));
		TestEqual(TEXT("RemoveElementFile: one success"), Run.Probe->Successes, 1);
		TestEqual(TEXT("RemoveElementFile: Json \"\""), Run.Probe->Json, FString());
		if (TestEqual(TEXT("RemoveElementFile: one request"), static_cast<int32>(Run.Fake->Sent.size()), 1))
			TestEqual(TEXT("the route"), OWFNT::Line(Run.Fake->Sent[0]), FString(TEXT("DELETE /location/l1/files/f1|API-Key=ow_w_test; API-Pin=1234")));
	}
	{
		// An account token: every node refused, nothing sent.
		FOWClientConfig Token = OWFNT::Writer();
		Token.ApiKey = TEXT("ow_a_test");
		Token.ApiPin = TEXT("");
		Token.World = TEXT("w1");
		OWFNT::FRun Run;
		OWFNT::Go(Run, UOWClientFiles::ListElementFiles(Token, TEXT("location"), TEXT("l1")));
		OWFNT::Go(Run, UOWClientFileRemoval::RemoveElementFile(Token, TEXT("location"), TEXT("l1"), TEXT("f1")));
		OWFNT::Go(Run, UOWClientImage::UploadImage(Token, {1}, TEXT("")));
		TestEqual(TEXT("an account token: three failures"), Run.Probe->Failures, 3);
		TestTrue(TEXT("an account token: kind Refused"), Run.Probe->Error.Kind == EOWApiErrorKind::Refused);
		TestEqual(TEXT("an account token: nothing sent"), static_cast<int32>(Run.Fake->Sent.size()), 0);
	}
	return true;
}

#endif
