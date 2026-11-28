// The key RedeemJoin answers, in no log line: the node over Unreal's own HTTP (FOWHttpTransport) against a real socket
// on 127.0.0.1, the server the test's own listener (FOneAnswer, OWOneAnswer.h) answering a player's 201, every line the
// log takes in the meantime recorded by a device of the test's own on GLog. At LogHttp's default verbosity the key and
// the join code are in no line: that is asserted. Then the same redeem with LogHttp raised to Verbose and to
// VeryVerbose, and an AcceptJoin at Verbose (its account token goes out as the Bearer): what Unreal's own logging writes
// there is the engine's, not the plugin's, so those are read and reported (AddInfo), never failed. LogHttp's verbosity is
// set back when the test ends. Two control lines in each phase prove the recording hears the log, and hears LogHttp's
// Verbose lines exactly when it is raised. No network. Every key here is made up.

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Http.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "OWHttpTransport.h"
#include "OWJoinNodes.h"
#include "OWJoinNodesProbe.h"
#include "OWOneAnswer.h"

#include <memory>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWJoinLogTest
{
	constexpr EAutomationTestFlags LogTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	constexpr double WaitSeconds = 20.0;
	// Frames the recording stays open after the answer, for what the HTTP module logs once it has answered.
	constexpr int32 AfterFrames = 10;

	const TCHAR* const Key = TEXT("ow_r_logprobe_key_9c41");
	const TCHAR* const Token = TEXT("ow_a_logprobe_token_9c41");
	const TCHAR* const Code = TEXT("ow_j_logprobe_code_9c41");

	// Every line the log takes while it is added, from any thread.
	struct FRecord : public FOutputDevice
	{
		FCriticalSection Lock;
		TArray<FString> Lines;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			FScopeLock Hold(&Lock);
			Lines.Add(Category.ToString() + TEXT(" ") + ::ToString(ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask)) + TEXT(": ") + V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		// The lines holding Text, and how many lines in all.
		int32 Holding(const TCHAR* Text, FString* First = nullptr)
		{
			FScopeLock Hold(&Lock);
			int32 N = 0;
			for (const FString& L : Lines)
			{
				if (!L.Contains(Text)) continue;
				if (N++ == 0 && First) *First = L;
			}
			return N;
		}
		// The first N lines of a category, for the report.
		TArray<FString> FirstOf(const TCHAR* Category, int32 N)
		{
			FScopeLock Hold(&Lock);
			TArray<FString> Out;
			const FString Prefix = FString(Category) + TEXT(" ");
			for (const FString& L : Lines)
				if (Out.Num() < N && L.StartsWith(Prefix) && !L.Contains(TEXT("OWJoinLogTest control"))) Out.Add(L);
			return Out;
		}
		int32 Count()
		{
			FScopeLock Hold(&Lock);
			return Lines.Num();
		}
	};

	FString Answer(const FString& Status, const FString& Body)
	{
		const FTCHARToUTF8 Bytes(*Body);
		return TEXT("HTTP/1.1 ") + Status + TEXT("\r\nContent-Type: application/json\r\nContent-Length: ") +
			FString::FromInt(Bytes.Length()) + TEXT("\r\nConnection: close\r\n\r\n") + Body;
	}

	FString PlayerJoined()
	{
		return FString::Printf(TEXT("{\"kind\":\"player\",\"key\":\"%s\",\"world\":{\"id\":\"w1\",\"name\":\"Moppetopia\",")
			TEXT("\"description\":\"\"},\"character\":{\"id\":\"c1\",\"name\":\"Pip\"},\"role\":\"guest\"}"), Key);
	}

	const TCHAR* const Membership = TEXT("{\"id\":\"m7\",\"world_id\":\"w1\",\"world_name\":\"Ostvik\",\"role\":\"contributor\",")
		TEXT("\"joined_at\":\"2026-10-31T01:00:00+00:00\",\"roster_character\":null}");

	// A line with the made-up secrets marked, for the report.
	FString Shown(FString Line)
	{
		Line.ReplaceInline(Key, TEXT("<key>"));
		Line.ReplaceInline(Token, TEXT("<token>"));
		Line.ReplaceInline(Code, TEXT("<code>"));
		return Line.Left(300);
	}

	struct FPhase
	{
		const TCHAR* Label;
		ELogVerbosity::Type Verbosity;
		bool bAccept;
		bool bAssert;  // the default's: the key and the code in no line, failed if they are
	};

	struct FState
	{
		int32 Phase = 0;
		bool bRunning = false;
		double Start = 0.0;
		int32 After = 0;
		ELogVerbosity::Type Was = ELogVerbosity::Log;
		std::shared_ptr<OWOneAnswerTest::FOneAnswer> Server;
		TStrongObjectPtr<UOWJoinNodesProbe> Probe;
		std::unique_ptr<FRecord> Record;
	};
}

namespace OWJL = OWJoinLogTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWJoinLogKeyTest, "OnlyWorlds.Client.JoinNodes.KeyInNoLog", OWJL::LogTestFlags)
bool FOWJoinLogKeyTest::RunTest(const FString& Parameters)
{
	auto S = std::make_shared<OWJL::FState>();
	S->Was = LogHttp.GetVerbosity();
	AddInfo(FString(TEXT("LogHttp's verbosity at the start: ")) + ::ToString(S->Was));
	const TArray<OWJL::FPhase> Phases = {
		{ TEXT("RedeemJoin, LogHttp as it is"), S->Was, false, true },
		{ TEXT("RedeemJoin, LogHttp Verbose"), ELogVerbosity::Verbose, false, false },
		{ TEXT("RedeemJoin, LogHttp VeryVerbose"), ELogVerbosity::VeryVerbose, false, false },
		{ TEXT("AcceptJoin, LogHttp Verbose"), ELogVerbosity::Verbose, true, false },
	};

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Phases]()
	{
		if (!S->bRunning)
		{
			if (S->Phase >= Phases.Num())
			{
				LogHttp.SetVerbosity(S->Was);
				return true;
			}
			const OWJL::FPhase& P = Phases[S->Phase];
			LogHttp.SetVerbosity(P.Verbosity);
			S->Server = std::make_shared<OWOneAnswerTest::FOneAnswer>(
				P.bAccept ? OWJL::Answer(TEXT("201 Created"), OWJL::Membership) : OWJL::Answer(TEXT("201 Created"), OWJL::PlayerJoined()));
			if (!TestTrue(FString(P.Label) + TEXT(": the server listens"), S->Server->Port != 0))
			{
				++S->Phase;
				return false;
			}
			const FString Base = FString::Printf(TEXT("http://127.0.0.1:%d/api/v2"), S->Server->Port);
			S->Probe.Reset(NewObject<UOWJoinNodesProbe>(GetTransientPackage()));
			S->Record = std::make_unique<OWJL::FRecord>();
			GLog->AddOutputDevice(S->Record.get());
			// The controls: a line the recorder must hear, and one of LogHttp's own at Verbose, heard only when raised.
			UE_LOG(LogTemp, Display, TEXT("OWJoinLogTest control Display"));
			UE_LOG(LogHttp, Verbose, TEXT("OWJoinLogTest control Verbose"));
			UOWClientNode* Node = nullptr;
			if (P.bAccept)
			{
				FOWClientConfig Config;
				Config.ApiKey = OWJL::Token;
				Config.BaseUrl = Base;
				UOWClientJoinAccept* A = UOWClientJoinAccept::AcceptJoin(Config, OWJL::Code);
				S->Probe->Watch(A);
				Node = A;
			}
			else
			{
				UOWClientJoinRedeem* R = UOWClientJoinRedeem::RedeemJoin(OWJL::Code, TEXT("Ana"), TEXT(""), TEXT(""), Base);
				S->Probe->Watch(R);
				Node = R;
			}
			Node->SetTransport(FOWHttpTransport::Make(10.f));
			Node->Activate();
			S->Start = FPlatformTime::Seconds();
			S->After = 0;
			S->bRunning = true;
			return false;
		}

		S->Server->Step();
		if (S->Probe->Answers() == 0 && FPlatformTime::Seconds() - S->Start < OWJL::WaitSeconds) return false;
		if (S->Probe->Answers() > 0 && S->After++ < OWJL::AfterFrames) return false;
		GLog->Flush();
		GLog->RemoveOutputDevice(S->Record.get());

		const OWJL::FPhase& P = Phases[S->Phase];
		const FString What(P.Label);
		TestEqual(What + TEXT(": answered once, on OnSuccess, got ") + S->Probe->Error.Line, S->Probe->Successes, 1);
		if (!P.bAccept) TestEqual(What + TEXT(": the key reached OnSuccess"), S->Probe->Joined.Key, FString(OWJL::Key));
		FString First;
		const int32 WithKey = S->Record->Holding(OWJL::Key, &First);
		const FString KeyFirst = First;
		const int32 WithCode = S->Record->Holding(OWJL::Code, &First);
		const FString CodeFirst = First;
		const int32 WithToken = S->Record->Holding(OWJL::Token, &First);
		const FString TokenFirst = First;
		AddInfo(FString::Printf(TEXT("%s: %d lines recorded; the key in %d, the join code in %d, the account token in %d"),
			P.Label, S->Record->Count(), WithKey, WithCode, WithToken));
		if (WithKey) AddInfo(What + TEXT(": the first with the key: ") + OWJL::Shown(KeyFirst));
		if (WithCode) AddInfo(What + TEXT(": the first with the code: ") + OWJL::Shown(CodeFirst));
		if (WithToken) AddInfo(What + TEXT(": the first with the token: ") + OWJL::Shown(TokenFirst));
		TestEqual(What + TEXT(": the recorder hears the log (the control line)"), S->Record->Holding(TEXT("OWJoinLogTest control Display")), 1);
		TestEqual(What + TEXT(": LogHttp's Verbose lines heard exactly when raised (the control line)"),
			S->Record->Holding(TEXT("OWJoinLogTest control Verbose")), P.Verbosity >= ELogVerbosity::Verbose ? 1 : 0);
		if (!P.bAssert)
			for (const FString& L : S->Record->FirstOf(TEXT("LogHttp"), 6)) AddInfo(What + TEXT(": ") + OWJL::Shown(L));
		if (P.bAssert)
		{
			TestEqual(What + TEXT(": the key in no line"), WithKey, 0);
			TestEqual(What + TEXT(": the join code in no line"), WithCode, 0);
		}
		S->Record.reset();
		S->Server.reset();
		S->Probe.Reset();
		S->bRunning = false;
		++S->Phase;
		return false;
	}));
	return true;
}

#endif
