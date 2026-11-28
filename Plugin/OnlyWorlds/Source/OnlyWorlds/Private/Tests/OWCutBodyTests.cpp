// The core client over Unreal's own HTTP (FOWHttpTransport) against a real socket on 127.0.0.1: an answer cut off while
// its body is read (a body shorter than its Content-Length, a chunked body with no last chunk) is kind Transport, never
// a value or kind Answer; a whole answer read to the end of the connection is the value. Mirrors the TypeScript SDK's
// 4.223.1 test of the same name. No network: the server is the test's own listener (FOneAnswer), served a step a frame
// from the latent command that waits for the answer, and closed when the test lets go of it.

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Misc/AutomationTest.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#include "OWClient.h"
#include "OWHttpTransport.h"
#include "OWJson.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWApiError;
	using OnlyWorlds::OWJsonValue;
	using OnlyWorlds::OWReply;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	constexpr double WaitSeconds = 20.0;

	const FString Head = TEXT("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n");

	// One connection answered with the bytes given, as they are, then shut for writing (FIN). Listens on a port the
	// system picks; non-blocking, stepped from the game thread. The destructor closes both sockets.
	class FOneAnswer
	{
	public:
		explicit FOneAnswer(const FString& InAnswer)
		{
			const FTCHARToUTF8 Utf8(*InAnswer);
			Answer.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			Listener = Subsystem->CreateSocket(NAME_Stream, TEXT("OWOneAnswer"), false);
			TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr();
			bool bValid = false;
			Addr->SetIp(TEXT("127.0.0.1"), bValid);
			Addr->SetPort(0);
			if (Listener && bValid && Listener->Bind(*Addr) && Listener->Listen(1) && Listener->SetNonBlocking(true))
			{
				Port = Listener->GetPortNo();
			}
		}

		~FOneAnswer()
		{
			Drop(Peer);
			Drop(Listener);
		}

		int32 Port = 0;

		// Accepts, reads the request's head, writes the answer and shuts the connection for writing; once a frame.
		void Step()
		{
			if (bServed || Port == 0) return;
			if (!Peer)
			{
				bool bPending = false;
				if (Listener->HasPendingConnection(bPending) && bPending)
				{
					Peer = Listener->Accept(TEXT("OWOneAnswerPeer"));
					if (Peer) Peer->SetNonBlocking(true);
				}
				return;
			}
			uint32 Size = 0;
			while (Peer->HasPendingData(Size) && Size > 0)
			{
				TArray<uint8> Buf;
				Buf.SetNumUninitialized(static_cast<int32>(Size));
				int32 Read = 0;
				if (!Peer->Recv(Buf.GetData(), Buf.Num(), Read) || Read <= 0) break;
				Got.Append(Buf.GetData(), Read);
			}
			const std::string Seen(reinterpret_cast<const char*>(Got.GetData()), static_cast<size_t>(Got.Num()));
			if (Seen.find("\r\n\r\n") == std::string::npos) return;
			int32 Sent = 0;
			Peer->Send(Answer.GetData(), Answer.Num(), Sent);
			Peer->Shutdown(ESocketShutdownMode::Write);
			bServed = true;
		}

	private:
		void Drop(FSocket*& Socket)
		{
			if (!Socket) return;
			Socket->Close();
			Subsystem->DestroySocket(Socket);
			Socket = nullptr;
		}

		ISocketSubsystem* Subsystem = nullptr;
		FSocket* Listener = nullptr;
		FSocket* Peer = nullptr;
		TArray<uint8> Answer;
		TArray<uint8> Got;
		bool bServed = false;
	};

	struct FAnswer
	{
		int32 Calls = 0;
		OWReply<OWJsonValue> Value;
	};

	// Sends GET or DELETE /character/x/ to a server answering Answer, steps the server a frame at a time until the
	// client answered or the wait ran out, then hands the reply to Check.
	template <typename FCheck>
	void Send(FAutomationTestBase& Test, const FString& Answer, bool bDelete, FCheck Check)
	{
		auto Server = std::make_shared<FOneAnswer>(Answer);
		if (!Test.TestTrue(TEXT("the server listens"), Server->Port != 0)) return;
		OnlyWorlds::OWClientConfig Config;
		Config.api_key = "ow_w_test";
		Config.api_pin = "1234";
		Config.base_url = "http://127.0.0.1:" + std::to_string(Server->Port) + "/api/v2";
		Config.transport = FOWHttpTransport::Make(10.f);
		auto Client = std::make_shared<OnlyWorlds::OWClient>(std::move(Config));
		auto A = std::make_shared<FAnswer>();
		auto Done = [A](OWReply<OWJsonValue> R)
		{
			++A->Calls;
			A->Value = std::move(R);
		};
		if (bDelete) Client->Delete("character", "x", {}, Done);
		else Client->Get("character", "x", {}, Done);

		const double Start = FPlatformTime::Seconds();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Server, Client, A, Start, Check]()
		{
			Server->Step();
			if (A->Calls == 0 && FPlatformTime::Seconds() - Start < WaitSeconds) return false;
			Check(*A);
			return true;
		}));
	}

	FString Words(const OWReply<OWJsonValue>& R)
	{
		return R.error ? FString(UTF8_TO_TCHAR(R.error->ToString().c_str())) : FString(TEXT("a value"));
	}

	// The reply is kind Transport, status 0, its words the client's for this request.
	void Lost(FAutomationTestBase& Test, const FAnswer& Got, const TCHAR* Label, const char* Route)
	{
		Test.AddInfo(FString(Label) + TEXT(": ") + Words(Got.Value));
		Test.TestEqual(FString(Label) + TEXT(": answered once"), Got.Calls, 1);
		if (!Test.TestTrue(FString(Label) + TEXT(": not a value"), Got.Value.error.has_value())) return;
		Test.TestTrue(FString(Label) + TEXT(": kind Transport"), Got.Value.error->kind == OWApiError::Kind::Transport);
		Test.TestEqual(FString(Label) + TEXT(": status 0"), Got.Value.error->status, 0);
		const std::string Prefix = std::string("OnlyWorlds request failed: ") + Route + ": ";
		Test.TestTrue(FString(Label) + TEXT(": the client's words"), Got.Value.error->message.rfind(Prefix, 0) == 0);
		Test.TestTrue(FString(Label) + TEXT(": an answer had begun, and the words say so"),
			Got.Value.error->message.find("the answer was lost while it was read") != std::string::npos);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCutBodyLengthTest, "OnlyWorlds.Client.Transport.CutBody.Length", OWTestFlags)
bool FOWCutBodyLengthTest::RunTest(const FString& Parameters)
{
	Send(*this, Head + TEXT("Content-Length: 100\r\n\r\n{\"id\":\"x\"}"), false,
		[this](const FAnswer& Got) { Lost(*this, Got, TEXT("cut, parses"), "GET /character/x/"); });
	Send(*this, Head + TEXT("Content-Length: 100\r\n\r\n{\"id\":\"x\",\"na"), false,
		[this](const FAnswer& Got) { Lost(*this, Got, TEXT("cut, broken"), "GET /character/x/"); });
	Send(*this, Head + TEXT("Content-Length: 100\r\n\r\n"), true,
		[this](const FAnswer& Got) { Lost(*this, Got, TEXT("no bytes, DELETE"), "DELETE /character/x/"); });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCutBodyChunkedTest, "OnlyWorlds.Client.Transport.CutBody.Chunked", OWTestFlags)
bool FOWCutBodyChunkedTest::RunTest(const FString& Parameters)
{
	Send(*this, Head + TEXT("Transfer-Encoding: chunked\r\n\r\na\r\n{\"id\":\"x\"}\r\n"), false,
		[this](const FAnswer& Got) { Lost(*this, Got, TEXT("after a chunk"), "GET /character/x/"); });
	Send(*this, Head + TEXT("Transfer-Encoding: chunked\r\n\r\n14\r\n{\"id\":\"x\"}"), false,
		[this](const FAnswer& Got) { Lost(*this, Got, TEXT("inside a chunk"), "GET /character/x/"); });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCutBodyWholeTest, "OnlyWorlds.Client.Transport.CutBody.Whole", OWTestFlags)
bool FOWCutBodyWholeTest::RunTest(const FString& Parameters)
{
	// The control: a whole answer read to the end of the connection (no length, not chunked) is the value.
	Send(*this, Head + TEXT("Connection: close\r\n\r\n{\"id\":\"x\",\"name\":\"whole\"}"), false, [this](const FAnswer& Got)
	{
		AddInfo(TEXT("whole, no length: ") + Words(Got.Value));
		TestEqual(TEXT("answered once"), Got.Calls, 1);
		if (!TestTrue(TEXT("the value"), Got.Value.Ok())) return;
		const OWJsonValue* Name = Got.Value.value.Find("name");
		TestTrue(TEXT("its name"), Name && Name->IsString() && Name->Text() == "whole");
	});
	// A whole body that is not JSON keeps kind Answer: the answer's fault, not the line's.
	Send(*this, Head + TEXT("Content-Length: 5\r\n\r\n<html"), false, [this](const FAnswer& Got)
	{
		AddInfo(TEXT("whole, not JSON: ") + Words(Got.Value));
		TestTrue(TEXT("kind Answer"), Got.Value.error && Got.Value.error->kind == OWApiError::Kind::Answer);
	});
	return true;
}

#endif
