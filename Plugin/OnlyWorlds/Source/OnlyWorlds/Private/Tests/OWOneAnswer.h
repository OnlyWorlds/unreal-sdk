#pragma once
// A test's own server on 127.0.0.1, for the tests that run the core client over Unreal's HTTP against a real socket
// (OWCutBodyTests.cpp, OWUploadSocketTests.cpp): one connection, answered with the bytes given, as they are, then shut
// for writing (FIN). It listens on a port the system picks, non-blocking, stepped from the game thread; it answers once
// it has read the request's head and the body its Content-Length names, and keeps what it read (Got) for the test to
// look at. The destructor closes both sockets. Tests only.

#include "CoreMinimal.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWOneAnswerTest
{
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

		FOneAnswer(const FOneAnswer&) = delete;
		FOneAnswer& operator=(const FOneAnswer&) = delete;

		int32 Port = 0;

		// Accepts, reads the request's head and its body, writes the answer and shuts the connection for writing; once a
		// frame.
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
			const std::string Seen = Request();
			const size_t End = Seen.find("\r\n\r\n");
			if (End == std::string::npos) return;
			if (Seen.size() < End + 4 + BodyLength(Seen.substr(0, End))) return;
			int32 Sent = 0;
			Peer->Send(Answer.GetData(), Answer.Num(), Sent);
			Peer->Shutdown(ESocketShutdownMode::Write);
			bServed = true;
		}

		bool Served() const { return bServed; }

		// The request as read: its head and its body, byte for byte.
		std::string Request() const
		{
			return std::string(reinterpret_cast<const char*>(Got.GetData()), static_cast<size_t>(Got.Num()));
		}
		// The request's head, lower-cased (header names are case-insensitive), without the blank line.
		std::string HeadLower() const
		{
			std::string Seen = Request();
			Seen = Seen.substr(0, Seen.find("\r\n\r\n"));
			for (char& C : Seen)
				if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');
			return Seen;
		}
		// The request's body: what follows the head.
		std::string Body() const
		{
			const std::string Seen = Request();
			const size_t End = Seen.find("\r\n\r\n");
			return End == std::string::npos ? std::string() : Seen.substr(End + 4);
		}

	private:
		// The Content-Length a head names, 0 for none.
		static size_t BodyLength(std::string Head)
		{
			for (char& C : Head)
				if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');
			const size_t At = Head.find("\r\ncontent-length:");
			if (At == std::string::npos) return 0;
			size_t I = At + 17;
			while (I < Head.size() && Head[I] == ' ') ++I;
			size_t N = 0;
			while (I < Head.size() && Head[I] >= '0' && Head[I] <= '9') N = N * 10 + static_cast<size_t>(Head[I++] - '0');
			return N;
		}

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
}

#endif
