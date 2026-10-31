#pragma once
// Unreal's own HTTP (FHttpModule) as the core client's transport: what OnlyWorlds::OWClient sends, sent as given, and
// what comes back handed to the client on the game thread. The seam and its contract are the core's (OWTransport.h);
// this keeps it: one answer per request, a failure when none came (no connection, the timeout, a request that would
// not start), and no redirect followed, since the client's credential headers would go with it.
//
//   OnlyWorlds::OWClientConfig Config;
//   Config.api_key = "0000000001";                       // Moppetopia's public read key
//   Config.transport = FOWHttpTransport::Make();
//   OnlyWorlds::OWClient Client(std::move(Config));
//   Client.GetWorld([](OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> R) { ... });   // later, on the game thread

#include "CoreMinimal.h"

#include "OWTransport.h"

#include <memory>

class ONLYWORLDS_API FOWHttpTransport : public OnlyWorlds::OWTransport
{
public:
	/** A transport for an OWClientConfig. */
	static std::shared_ptr<OnlyWorlds::OWTransport> Make(float TimeoutSeconds = 30.f);

	explicit FOWHttpTransport(float InTimeoutSeconds = 30.f) : TimeoutSeconds(InTimeoutSeconds) {}

	/**
	 * Sends the request with FHttpModule and calls Done once, on the game thread: the status, the headers and the body's
	 * bytes as received, or status 0 with a failure. A 3xx is handed over as the answer it is, never followed.
	 */
	virtual void Send(OnlyWorlds::OWRequest Request, OnlyWorlds::OWResponseHandler Done) override;

private:
	float TimeoutSeconds;
};
