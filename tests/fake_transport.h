#pragma once
// A transport that records what the client would have sent and answers from a script: no network. Each answer
// scripted with Then (or Fail) is given once, in order; after them, `status`, `body` and `headers` answer every
// request. It answers before Send returns, as a test needs; with `deferred` it holds each answer until Flush, as
// Unreal's HTTP answers later, so a test can see the client work both ways. Tests only.

#include "OWTransport.h"

#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace owtest {

struct FakeTransport : OnlyWorlds::OWTransport {
    // Every request sent, in order.
    std::vector<OnlyWorlds::OWRequest> calls;
    int status = 200;
    std::string body = "{}";
    OnlyWorlds::OWHeaders headers;
    // Hold each answer until Flush.
    bool deferred = false;
    // Call each handler twice: a transport that breaks the seam's contract.
    bool answer_twice = false;

    // Scripts one answer: its status, body and headers ({"etag", ...} for a tip read's tag).
    void Then(int answer_status, std::string answer_body, OnlyWorlds::OWHeaders answer_headers = {}) {
        OnlyWorlds::OWResponse r;
        r.status = answer_status;
        r.body = std::move(answer_body);
        r.headers = std::move(answer_headers);
        script_.push_back(std::move(r));
    }
    // Scripts one request that gets no answer (a dropped connection), with the transport's reason.
    void Fail(std::string reason) {
        OnlyWorlds::OWResponse r;
        r.failure = std::move(reason);
        script_.push_back(std::move(r));
    }

    void Send(OnlyWorlds::OWRequest request, OnlyWorlds::OWResponseHandler done) override {
        calls.push_back(std::move(request));
        OnlyWorlds::OWResponse r;
        if (!script_.empty()) {
            r = std::move(script_.front());
            script_.pop_front();
        } else {
            r.status = status;
            r.body = body;
            r.headers = headers;
        }
        if (deferred) {
            pending_.emplace_back(std::move(r), std::move(done));
            return;
        }
        if (answer_twice) done(r);
        done(std::move(r));
    }

    // Answers every held request, those sent while answering included; how many were answered.
    size_t Flush() {
        size_t n = 0;
        while (!pending_.empty()) {
            auto [r, done] = std::move(pending_.front());
            pending_.pop_front();
            done(std::move(r));
            ++n;
        }
        return n;
    }
    size_t Pending() const { return pending_.size(); }

    // The last request sent (an empty one when none was, so a red test fails rather than crashes).
    const OnlyWorlds::OWRequest& Last() const {
        static const OnlyWorlds::OWRequest none{"", "", {}, std::nullopt};
        return calls.empty() ? none : calls.back();
    }

private:
    std::deque<OnlyWorlds::OWResponse> script_;
    std::deque<std::pair<OnlyWorlds::OWResponse, OnlyWorlds::OWResponseHandler>> pending_;
};

}  // namespace owtest
