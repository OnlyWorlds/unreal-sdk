// The core client over Unreal's own HTTP (FOWHttpTransport). Each test sends, then waits a frame at a time (a latent
// command) for the one answer, as a game would: FHttpModule completes on the game thread, never inside Send.
//
// OnlyWorlds.Client.Transport.* needs no network (a port nothing listens on). OnlyWorldsLive.Client.* reads production
// (the health pulse, Moppetopia by its public read key 0000000001, the apex address's redirect), so it sits outside the
// OnlyWorlds prefix the suite runs by default: run it by name (Automation RunTests OnlyWorldsLive).

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include "OWClient.h"
#include "OWHttpTransport.h"
#include "OWJson.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	constexpr double WaitSeconds = 40.0;

	// What one call answered, and how many times it was answered.
	template <typename T>
	struct FAnswer
	{
		int32 Calls = 0;
		T Value{};
	};

	// Waits a frame at a time until the answer came or the wait ran out, then hands it to Check.
	template <typename T, typename FCheck>
	void AwaitThen(std::shared_ptr<FAnswer<T>> A, FCheck Check)
	{
		const double Start = FPlatformTime::Seconds();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([A, Start, Check]()
		{
			if (A->Calls == 0 && FPlatformTime::Seconds() - Start < WaitSeconds) return false;
			Check(*A);
			return true;
		}));
	}

	std::string Text(const OWJsonValue& Body, const char* Key)
	{
		const OWJsonValue* V = Body.Find(Key);
		return V && V->IsString() ? V->Text() : std::string();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWHttpTransportNoConnectionTest, "OnlyWorlds.Client.Transport.NoConnection", OWTestFlags)
bool FOWHttpTransportNoConnectionTest::RunTest(const FString& Parameters)
{
	// Port 1 on loopback: nothing listens, so the request fails, and the failure still comes back once, as status 0.
	auto A = std::make_shared<FAnswer<OnlyWorlds::OWResponse>>();
	OnlyWorlds::OWRequest Request;
	Request.url = "http://127.0.0.1:1/api/v2/health/";
	FOWHttpTransport(5.f).Send(std::move(Request), [A](OnlyWorlds::OWResponse R)
	{
		++A->Calls;
		A->Value = std::move(R);
	});
	TestEqual(TEXT("not answered inside Send"), A->Calls, 0);

	AwaitThen(A, [this](const FAnswer<OnlyWorlds::OWResponse>& Got)
	{
		TestEqual(TEXT("answered once"), Got.Calls, 1);
		TestEqual(TEXT("status 0: no answer"), Got.Value.status, 0);
		TestFalse(TEXT("the failure says why"), Got.Value.failure.empty());
		AddInfo(FString(TEXT("failure: ")) + UTF8_TO_TCHAR(Got.Value.failure.c_str()));
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLiveHealthTest, "OnlyWorldsLive.Client.Health", OWTestFlags)
bool FOWLiveHealthTest::RunTest(const FString& Parameters)
{
	OnlyWorlds::OWClientConfig Config;
	Config.transport = FOWHttpTransport::Make();
	auto Client = std::make_shared<OnlyWorlds::OWClient>(std::move(Config));
	TestEqual(TEXT("a bare client is built"), FString(UTF8_TO_TCHAR(Client->Error().c_str())), FString());

	auto A = std::make_shared<FAnswer<OnlyWorlds::OWReply<OWJsonValue>>>();
	Client->Health([A](OnlyWorlds::OWReply<OWJsonValue> R)
	{
		++A->Calls;
		A->Value = std::move(R);
	});

	AwaitThen(A, [this, Client](const FAnswer<OnlyWorlds::OWReply<OWJsonValue>>& Got)
	{
		TestEqual(TEXT("answered once"), Got.Calls, 1);
		if (!TestTrue(TEXT("ok"), Got.Value.Ok())) AddError(UTF8_TO_TCHAR(Got.Value.error->ToString().c_str()));
		TestEqual(TEXT("status"), FString(UTF8_TO_TCHAR(Text(Got.Value.value, "status").c_str())), FString(TEXT("ok")));
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLiveMoppetopiaTest, "OnlyWorldsLive.Client.Moppetopia", OWTestFlags)
bool FOWLiveMoppetopiaTest::RunTest(const FString& Parameters)
{
	OnlyWorlds::OWClientConfig Config;
	Config.api_key = "0000000001";
	Config.transport = FOWHttpTransport::Make();
	auto Client = std::make_shared<OnlyWorlds::OWClient>(std::move(Config));

	auto A = std::make_shared<FAnswer<OnlyWorlds::OWReply<OWJsonValue>>>();
	Client->GetWorld([A](OnlyWorlds::OWReply<OWJsonValue> R)
	{
		++A->Calls;
		A->Value = std::move(R);
	});

	AwaitThen(A, [this, Client](const FAnswer<OnlyWorlds::OWReply<OWJsonValue>>& Got)
	{
		TestEqual(TEXT("answered once"), Got.Calls, 1);
		if (!TestTrue(TEXT("ok"), Got.Value.Ok())) AddError(UTF8_TO_TCHAR(Got.Value.error->ToString().c_str()));
		TestEqual(TEXT("the world's name"), FString(UTF8_TO_TCHAR(Text(Got.Value.value, "name").c_str())), FString(TEXT("Moppetopia")));
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLiveRedirectTest, "OnlyWorldsLive.Client.Redirect", OWTestFlags)
bool FOWLiveRedirectTest::RunTest(const FString& Parameters)
{
	// The apex address answers 301 to www. The engine follows it (curl, CURLOPT_FOLLOWLOCATION); the transport refuses
	// the followed answer, so the client never reads it as keel's.
	auto A = std::make_shared<FAnswer<OnlyWorlds::OWResponse>>();
	OnlyWorlds::OWRequest Request;
	Request.url = "https://onlyworlds.com/api/v2/health/";
	FOWHttpTransport().Send(std::move(Request), [A](OnlyWorlds::OWResponse R)
	{
		++A->Calls;
		A->Value = std::move(R);
	});

	AwaitThen(A, [this](const FAnswer<OnlyWorlds::OWResponse>& Got)
	{
		TestEqual(TEXT("answered once"), Got.Calls, 1);
		TestEqual(TEXT("status 0: the followed answer refused"), Got.Value.status, 0);
		const FString Failure = UTF8_TO_TCHAR(Got.Value.failure.c_str());
		TestTrue(TEXT("the failure names where it went"), Failure.Contains(TEXT("redirected to https://www.onlyworlds.com/")));
		AddInfo(TEXT("failure: ") + Failure);
	});
	return true;
}

#endif
