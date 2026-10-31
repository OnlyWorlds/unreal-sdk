// FHttpModule as the core client's transport (OWHttpTransport.h). The contract is the core's (OWTransport.h): Done once,
// on the game thread, status 0 and a failure when no answer came.
//
// Redirects: the engine follows them before this code sees anything. On Windows the HTTP module is curl
// (WindowsPlatformHttp.cpp), and curl's request sets CURLOPT_FOLLOWLOCATION to 1 with no option to turn it off
// (CurlHttp.cpp), so a request whose server answers a 3xx has already gone to the new address, its headers with it.
// What this transport can do is refuse the answer: a request whose effective URL is not the one sent is handed to the
// client as a failure, so the client never reads another address's answer as keel's. Point the client at the server's
// own address (https://www.onlyworlds.com/api/v2, which redirects no API call), never one that redirects.

#include "OWHttpTransport.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "OWConvert.h"

#include <functional>
#include <utility>

namespace
{
	// "Name: Value" as GetAllHeaders gives it, split at the first ": ".
	void AddHeader(OnlyWorlds::OWHeaders& Headers, const FString& Line)
	{
		FString Name, Value;
		if (Line.Split(TEXT(": "), &Name, &Value)) Headers.emplace_back(OWConvert::Utf8(Name), OWConvert::Utf8(Value));
	}
}

std::shared_ptr<OnlyWorlds::OWTransport> FOWHttpTransport::Make(float TimeoutSeconds)
{
	return std::make_shared<FOWHttpTransport>(TimeoutSeconds);
}

void FOWHttpTransport::Send(OnlyWorlds::OWRequest Request, OnlyWorlds::OWResponseHandler Done)
{
	// Done once: the completion delegate and a request that would not start share one guard.
	struct FOnce
	{
		OnlyWorlds::OWResponseHandler Done;
		bool bAnswered = false;
		void Answer(OnlyWorlds::OWResponse R)
		{
			if (bAnswered) return;
			bAnswered = true;
			Done(std::move(R));
		}
	};
	auto Once = std::make_shared<FOnce>();
	Once->Done = std::move(Done);

	const FString Url = OWConvert::Str(Request.url);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetVerb(OWConvert::Str(Request.method));
	Http->SetURL(Url);
	for (const auto& [Name, Value] : Request.headers) Http->SetHeader(OWConvert::Str(Name), OWConvert::Str(Value));
	if (Request.body)
	{
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Request.body->data()), static_cast<int32>(Request.body->size()));
		Http->SetContent(MoveTemp(Bytes));
	}
	Http->SetTimeout(TimeoutSeconds);

	// The default thread policy completes on the game thread, where the client is used.
	Http->OnProcessRequestComplete().BindLambda(
		[Once, Url](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bProcessed)
		{
			OnlyWorlds::OWResponse R;
			if (!bProcessed || !Res.IsValid())
			{
				const TCHAR* Why = Req.IsValid() ? LexToString(Req->GetFailureReason()) : TEXT("no request");
				R.failure = "no answer: " + OWConvert::Utf8(FString(Why));
				Once->Answer(std::move(R));
				return;
			}

			const FString& Effective = Res->GetEffectiveURL();
			if (!Effective.IsEmpty() && Effective != Url)
			{
				R.failure = "redirected to " + OWConvert::Utf8(Effective) +
					": the answer is refused, since a redirect is not keel's (point the client at the server's own address)";
				Once->Answer(std::move(R));
				return;
			}

			R.status = Res->GetResponseCode();
			for (const FString& Line : Res->GetAllHeaders()) AddHeader(R.headers, Line);
			// The bytes as received: keel's UTF-8, never through GetContentAsString, which converts.
			const TArray<uint8>& Content = Res->GetContent();
			R.body.assign(reinterpret_cast<const char*>(Content.GetData()), static_cast<size_t>(Content.Num()));
			Once->Answer(std::move(R));
		});

	if (!Http->ProcessRequest())
	{
		OnlyWorlds::OWResponse R;
		R.failure = "the request would not start: " + Request.method + " " + Request.url;
		Once->Answer(std::move(R));
	}
}
