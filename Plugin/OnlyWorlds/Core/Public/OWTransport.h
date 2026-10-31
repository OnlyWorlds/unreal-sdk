#pragma once
// The seam between OWClient's wire logic and however bytes move: one request in, one answer out. OWClient builds every
// request (the URL, its query escaped, the credential headers) and reads every answer; a transport only sends.
//
// It exists so the client is tested without a network (a fake records each request and answers from a script: the
// tests assert on what the client would have sent), and so the plugin moves bytes with Unreal's own HTTP. The answer
// comes back through a callback, never as a return value: Unreal's HTTP completes later, on the game thread, and a game
// thread must never wait on the network. A test's fake calls the callback before Send returns; Unreal's transport
// calls it when the request completes. The client is written for both.
//
// The contract a transport keeps:
//   - It calls `done` exactly once for every Send, with what came back, or with status 0 and `failure` saying why no
//     answer came (no connection, a timeout, a cancel, a URL it cannot read). It never throws, and never drops one.
//   - It calls `done` on the thread the client is used from (the game thread), never on a worker: the client is not
//     thread-safe.
//   - It sends the request as given: the method, the URL, every header, the body when there is one, nothing added
//     that changes what keel reads (a credential, a query), and it follows no redirect (a credential would follow).
//
//   struct MyTransport : OnlyWorlds::OWTransport {
//       void Send(OnlyWorlds::OWRequest request, OnlyWorlds::OWResponseHandler done) override {
//           // ... start the request; when it completes:
//           OnlyWorlds::OWResponse r;
//           r.status = 200;
//           r.body = "{\"status\":\"ok\"}";
//           done(std::move(r));
//       }
//   };

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// Header name and value pairs, in the order set or received.
using OWHeaders = std::vector<std::pair<std::string, std::string>>;

// A header's value by name, ignoring ASCII case (HTTP names are case-insensitive, and keel sends them lowercase), or
// nullptr when it is absent. The first of two with one name.
const std::string* OWHeader(const OWHeaders& headers, std::string_view name);

// A request the client wants sent.
struct OWRequest {
    std::string method = "GET";  // GET, POST, PUT, PATCH or DELETE
    std::string url;             // the whole URL, its query escaped
    OWHeaders headers;           // as the client names them (API-Key, If-None-Match), in the order it sets them
    // The serialized JSON body; absent on every read.
    std::optional<std::string> body;

    const std::string* Header(std::string_view name) const { return OWHeader(headers, name); }
};

// What came back.
struct OWResponse {
    int status = 0;        // the HTTP status, or 0 when no answer came (`failure` says why)
    OWHeaders headers;     // as received
    std::string body;      // the body's bytes (UTF-8 from keel), "" for none
    std::string failure;   // why no answer came, "" when one did

    bool IsSuccess() const { return status >= 200 && status < 300; }
    const std::string* Header(std::string_view name) const { return OWHeader(headers, name); }
};

using OWResponseHandler = std::function<void(OWResponse)>;

class OWTransport {
public:
    virtual ~OWTransport() = default;
    // Send `request`, and call `done` once with the answer (see the contract above). Mirrors the TypeScript SDK's
    // `fetch` option.
    virtual void Send(OWRequest request, OWResponseHandler done) = 0;
};

}  // namespace OnlyWorlds
