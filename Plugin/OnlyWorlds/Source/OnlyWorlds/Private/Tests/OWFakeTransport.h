#pragma once
// The client nodes' fake transport, for their tests (OWClientNodesTests.cpp, OWClientWriteNodesTests.cpp): no test
// that uses it touches the network.
//
// Records every request and answers each from the script, in order (Fallback when it runs out). Inside Send by
// default; with bLater, held until Pump, as the HTTP module answers on a later tick. Times 2 answers each request
// twice, a transport that breaks the seam's contract.

#include "CoreMinimal.h"

#include "OWTransport.h"

#include <deque>
#include <utility>
#include <vector>

namespace OWClientNodesTest
{
	struct FFakeTransport : OnlyWorlds::OWTransport
	{
		std::vector<OnlyWorlds::OWRequest> Sent;
		std::deque<OnlyWorlds::OWResponse> Script;
		OnlyWorlds::OWResponse Fallback;
		bool bLater = false;
		int32 Times = 1;
		std::vector<std::pair<OnlyWorlds::OWResponseHandler, OnlyWorlds::OWResponse>> Held;

		void Send(OnlyWorlds::OWRequest Request, OnlyWorlds::OWResponseHandler Done) override
		{
			Sent.push_back(std::move(Request));
			OnlyWorlds::OWResponse Reply = Fallback;
			if (!Script.empty())
			{
				Reply = std::move(Script.front());
				Script.pop_front();
			}
			if (bLater)
			{
				Held.emplace_back(std::move(Done), std::move(Reply));
				return;
			}
			for (int32 I = 0; I < Times; ++I) Done(Reply);
		}

		// Answers every request held now (one sent while answering waits for the next Pump); how many it answered.
		int32 Pump()
		{
			std::vector<std::pair<OnlyWorlds::OWResponseHandler, OnlyWorlds::OWResponse>> Now = std::move(Held);
			Held.clear();
			for (std::pair<OnlyWorlds::OWResponseHandler, OnlyWorlds::OWResponse>& H : Now)
			{
				for (int32 I = 0; I < Times; ++I) H.first(H.second);
			}
			return static_cast<int32>(Now.size());
		}
	};
}
