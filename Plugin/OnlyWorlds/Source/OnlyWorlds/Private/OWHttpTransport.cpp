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
#include <string>
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
	// A body with no Content-Type (an upload's raw bytes: the client sends none, and the upload host reads the type from
	// the bytes) goes as application/octet-stream. Unreal's curl request asserts that a POST or a DELETE either names its
	// type or holds only URL-safe bytes (CurlHttp.cpp, check(!GetHeader("Content-Type").IsEmpty() ||
	// RequestPayload->IsURLEncoded())), which an image or a pdf never does: without this, the upload would stop the
	// editor and any Development game. Every body keel is sent names application/json, and an empty one passes, so
	// nothing of keel's changes.
	if (Request.body && !Request.body->empty() && !OnlyWorlds::OWHeader(Request.headers, "Content-Type"))
		Http->SetHeader(TEXT("Content-Type"), TEXT("application/octet-stream"));
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
				// A status already read means an answer had begun: the connection was lost while its body came in (a
				// body short of its Content-Length, a chunked body with no last chunk; curl's error 18), for which the
				// engine's failure reason is only "Other".
				if (Res.IsValid() && Res->GetResponseCode() > 0)
				{
					R.failure = "the answer was lost while it was read (status " + std::to_string(Res->GetResponseCode()) +
						", " + OWConvert::Utf8(FString(Why)) + ")";
				}
				else
				{
					R.failure = "no answer: " + OWConvert::Utf8(FString(Why));
				}
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
