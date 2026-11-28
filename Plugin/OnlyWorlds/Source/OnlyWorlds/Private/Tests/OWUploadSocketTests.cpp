// An image's two steps over Unreal's own HTTP (FOWHttpTransport) against two real sockets on 127.0.0.1, one standing in
// for keel and one for the upload host: the ticket goes to keel with the key and the PIN; the bytes go to the URL the
// ticket names, with the ticket as their Bearer and nothing of the client's, as application/octet-stream (Unreal's curl
// request asserts that a POST with bytes that are not URL-encoded names its type: OWHttpTransport.cpp), every byte
// whole, a NUL and a 0xFF among them; and a keel write's JSON body still goes as application/json. No network: each
// server is the test's own listener (OWOneAnswer.h), stepped a frame at a time from the latent command that waits.

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include "OWClient.h"
#include "OWHttpTransport.h"
#include "OWJson.h"
#include "OWMedia.h"

#include "OWOneAnswer.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWUploadSocketTest
{
	using OWOneAnswerTest::FOneAnswer;

	constexpr EAutomationTestFlags UploadTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	constexpr double WaitSeconds = 20.0;

	FString Answer201(const FString& Json)
	{
		const FTCHARToUTF8 Utf8(*Json);
		return FString::Printf(TEXT("HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n"),
			Utf8.Length()) + Json;
	}

	std::shared_ptr<OnlyWorlds::OWClient> ClientOn(int32 Port)
	{
		OnlyWorlds::OWClientConfig Config;
		Config.api_key = "ow_w_test";
		Config.api_pin = "1234";
		Config.base_url = "http://127.0.0.1:" + std::to_string(Port) + "/api/v2";
		Config.transport = FOWHttpTransport::Make(10.f);
		return std::make_shared<OnlyWorlds::OWClient>(std::move(Config));
	}

	bool Has(const std::string& Text, const char* Part) { return Text.find(Part) != std::string::npos; }

	FString Words(const std::optional<OnlyWorlds::OWApiError>& E)
	{
		return E ? FString(UTF8_TO_TCHAR(E->ToString().c_str())) : FString(TEXT("no error"));
	}
}

namespace OWUS = OWUploadSocketTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWUploadSocketImageTest, "OnlyWorlds.Client.Transport.Upload.Image", OWUS::UploadTestFlags)
bool FOWUploadSocketImageTest::RunTest(const FString& Parameters)
{
	auto Host = std::make_shared<OWUS::FOneAnswer>(OWUS::Answer201(
		TEXT("{\"url\":\"https://media.example.test/u/w1/x.png\",\"key\":\"u/w1/x.png\",\"bytes\":6,\"type\":\"image/png\",\"etag\":\"e\"}")));
	if (!TestTrue(TEXT("the host listens"), Host->Port != 0)) return true;
	const FString UploadUrl = FString::Printf(TEXT("http://127.0.0.1:%d/v1/upload"), Host->Port);
	auto Keel = std::make_shared<OWUS::FOneAnswer>(OWUS::Answer201(FString::Printf(
		TEXT("{\"ticket\":\"t.sig\",\"upload_url\":\"%s\",\"prefix\":\"u/w1/\",\"max_bytes\":100,\"exp\":1900000000,\"uses\":1}"),
		*UploadUrl)));
	if (!TestTrue(TEXT("keel listens"), Keel->Port != 0)) return true;

	auto Client = OWUS::ClientOn(Keel->Port);
	const OnlyWorlds::OWBytes Png = {0x89, 'P', 'N', 'G', 0x00, 0xFF};
	struct FGot
	{
		int32 Calls = 0;
		OnlyWorlds::OWReply<OnlyWorlds::OWUploadedImage> Reply;
	};
	auto Got = std::make_shared<FGot>();
	OnlyWorlds::OWUploadImageOptions O;
	O.key = "u/w1/x.png";
	Client->UploadImage(Png, O, [Got](OnlyWorlds::OWReply<OnlyWorlds::OWUploadedImage> R)
	{
		++Got->Calls;
		Got->Reply = std::move(R);
	});

	const double Start = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Keel, Host, Client, Got, Png, Start]()
	{
		Keel->Step();
		Host->Step();
		if (Got->Calls == 0 && FPlatformTime::Seconds() - Start < OWUS::WaitSeconds) return false;
		AddInfo(TEXT("the upload: ") + OWUS::Words(Got->Reply.error));
		TestEqual(TEXT("answered once"), Got->Calls, 1);
		TestTrue(TEXT("the host's answer"), Got->Reply.Ok() && Got->Reply.value.url == "https://media.example.test/u/w1/x.png");

		const std::string KeelHead = Keel->HeadLower();
		TestTrue(TEXT("keel: the ticket's route"), KeelHead.rfind("post /api/v2/media/ticket ", 0) == 0);
		TestTrue(TEXT("keel: the key"), OWUS::Has(KeelHead, "\r\napi-key: ow_w_test"));
		TestTrue(TEXT("keel: the PIN"), OWUS::Has(KeelHead, "\r\napi-pin: 1234"));
		TestTrue(TEXT("keel: no body"), Keel->Body().empty());

		const std::string HostHead = Host->HeadLower();
		TestTrue(TEXT("the host: the ticket's own URL"), HostHead.rfind("post /v1/upload ", 0) == 0);
		TestTrue(TEXT("the host: the ticket as the Bearer"), OWUS::Has(HostHead, "\r\nauthorization: bearer t.sig"));
		TestTrue(TEXT("the host: the object key"), OWUS::Has(HostHead, "\r\nx-key: u/w1/x.png"));
		TestTrue(TEXT("the host: application/octet-stream"),
			OWUS::Has(HostHead, "\r\ncontent-type: application/octet-stream"));
		TestFalse(TEXT("the host: no API key"), OWUS::Has(HostHead, "api-key"));
		TestFalse(TEXT("the host: no PIN"), OWUS::Has(HostHead, "api-pin"));
		TestFalse(TEXT("the host: no X-OW-*"), OWUS::Has(HostHead, "x-ow-"));
		TestTrue(TEXT("the host: every byte whole"), Host->Body() == std::string(Png.begin(), Png.end()));
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWUploadSocketJsonTest, "OnlyWorlds.Client.Transport.Upload.KeelJson", OWUS::UploadTestFlags)
bool FOWUploadSocketJsonTest::RunTest(const FString& Parameters)
{
	// The control: a keel write's body names application/json, and the transport adds no type of its own.
	auto Keel = std::make_shared<OWUS::FOneAnswer>(OWUS::Answer201(TEXT("{\"id\":\"c1\",\"name\":\"Ana\"}")));
	if (!TestTrue(TEXT("keel listens"), Keel->Port != 0)) return true;
	auto Client = OWUS::ClientOn(Keel->Port);
	struct FGot
	{
		int32 Calls = 0;
		OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply;
	};
	auto Got = std::make_shared<FGot>();
	Client->Create("character", OnlyWorlds::OWJson::Parse("{\"id\":\"c1\",\"name\":\"Ana\"}").value, {},
		[Got](OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> R)
		{
			++Got->Calls;
			Got->Reply = std::move(R);
		});
	const double Start = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Keel, Client, Got, Start]()
	{
		Keel->Step();
		if (Got->Calls == 0 && FPlatformTime::Seconds() - Start < OWUS::WaitSeconds) return false;
		AddInfo(TEXT("the create: ") + OWUS::Words(Got->Reply.error));
		TestEqual(TEXT("answered once"), Got->Calls, 1);
		TestTrue(TEXT("the element"), Got->Reply.Ok());
		const std::string Head = Keel->HeadLower();
		TestTrue(TEXT("the create's route"), Head.rfind("post /api/v2/character/ ", 0) == 0);
		TestTrue(TEXT("application/json"), OWUS::Has(Head, "\r\ncontent-type: application/json"));
		TestFalse(TEXT("never octet-stream"), OWUS::Has(Head, "octet-stream"));
		TestTrue(TEXT("the body whole"), Keel->Body() == "{\"id\":\"c1\",\"name\":\"Ana\"}");
		return true;
	}));
	return true;
}

#endif
