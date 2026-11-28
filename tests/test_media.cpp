// OWClient's images through a fake transport (tests/fake_transport.h): no test touches the network. CreateMediaTicket,
// UploadImage, CreateRemovalTicket and RemoveImage: the keel ticket (the key and its PIN, no X-OW-*), then the upload
// host's step (the ticket's own URL, the ticket as a Bearer and nothing else of the client's), keel's and the host's
// refusals as typed errors, a ticket's max_bytes held before any byte is sent, an account token refused with nothing
// sent, and a ticket never in an error's words. The TypeScript SDK's image tests (test/v2-client.test.mjs, "Images:
// the keel ticket, then the bytes to the edge", and the agent and account-token rows), ported test by test; the
// ticket-in-words case is the Unity SDK's OWMediaRouteTests.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"
#include "OWJson.h"
#include "OWMedia.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";

const std::string Ticket =
    R"({"ticket":"tkt.sig","upload_url":"https://upload.example/v1/upload","prefix":"u/w1/","max_bytes":8,)"
    R"("exp":1900000000,"uses":1,"issued_to":{"account":"a1","membership":null}})";
const std::string Uploaded =
    R"({"url":"https://media.example/u/w1/x.png","key":"u/w1/x.png","bytes":4,"type":"image/png","etag":"e"})";
const std::string RTicket =
    R"({"ticket":"d1.sig","remove_url":"https://upload.example/v1/remove","exp":1900000000,"key":"u/w1/x.png",)"
    R"("referenced":2})";
const std::string Removed = R"({"removed":"u/w1/x.png","bytes":4,"cache":"may_linger"})";

std::shared_ptr<FakeTransport> Fake() { return std::make_shared<FakeTransport>(); }

OWClient Make(const std::shared_ptr<FakeTransport>& fake, bool agent = false, const std::string& key = "ow_w_test",
              std::optional<std::string> world = std::nullopt) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = key == "ow_w_test" ? "1234" : "";
    config.base_url = Base;
    config.transport = fake;
    config.agent = agent;
    config.world = std::move(world);
    return OWClient(std::move(config));
}

template <typename T>
struct Got {
    OWReply<T> reply;
    int calls = 0;
    OWHandler<T> H() {
        return [this](OWReply<T> r) {
            reply = std::move(r);
            ++calls;
        };
    }
    bool Kind(OWApiError::Kind k) const { return reply.error && reply.error->kind == k; }
    std::string Why() const { return reply.error ? reply.error->ToString() : "no error"; }
};

// Every header as name=value, in the order set, joined by |.
std::string Headers(const OWRequest& r) {
    std::string out;
    for (auto& [k, v] : r.headers) out += (out.empty() ? "" : "|") + k + "=" + v;
    return out;
}
std::string HeaderOr(const OWRequest& r, const char* name) {
    const std::string* v = r.Header(name);
    return v ? *v : "<absent>";
}
std::string BodyOr(const OWRequest& r) { return r.body ? *r.body : "<none>"; }

const OWBytes Png = {0x89, 0x50, 0x4e, 0x47};
std::string Raw(const OWBytes& b) { return std::string(b.begin(), b.end()); }

OWMediaTicket TicketOf() { return *OWMediaTicket::Of(OWJson::Parse(Ticket).value); }
OWRemovalTicket RTicketOf() { return *OWRemovalTicket::Of(OWJson::Parse(RTicket).value); }

}  // namespace

OW_TEST(media_ticket_posts_with_the_key_and_pin_and_no_body) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    Got<OWMediaTicket> g;
    Make(fake).CreateMediaTicket(g.H());
    t.Eq(g.calls, 1, "answered once");
    t.Eq(fake->calls.size(), size_t{1}, "one request");
    if (fake->calls.empty()) return;
    const OWRequest& r = fake->calls[0];
    t.Eq(r.url, Base + "/media/ticket", "the route");
    t.Eq(r.method, std::string("POST"), "the method");
    t.Eq(BodyOr(r), std::string("<none>"), "no body");
    t.Eq(Headers(r), std::string("API-Key=ow_w_test|API-Pin=1234"), "the key and the PIN, nothing else");
    t.Ok(g.reply.Ok(), "the ticket: " + g.Why());
    t.Eq(g.reply.value.ticket, std::string("tkt.sig"), "ticket");
    t.Eq(g.reply.value.upload_url, std::string("https://upload.example/v1/upload"), "upload_url");
    t.Eq(g.reply.value.prefix, std::string("u/w1/"), "prefix");
    t.Eq(g.reply.value.max_bytes, int64_t{8}, "max_bytes");
    t.Eq(g.reply.value.exp, int64_t{1900000000}, "exp");
    t.Eq(g.reply.value.uses, int64_t{1}, "uses");
    t.Eq(g.reply.value.issued_to.account.value_or("<absent>"), std::string("a1"), "issued_to.account");
    t.Ok(!g.reply.value.issued_to.membership, "issued_to.membership null is absent");
}

OW_TEST(media_upload_image_ticket_then_the_bytes_with_the_ticket_alone) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    fake->Then(201, Uploaded);
    Got<OWUploadedImage> g;
    OWUploadImageOptions o;
    o.key = "u/w1/x.png";
    Make(fake).UploadImage(Png, o, g.H());
    t.Ok(g.reply.Ok(), "the upload: " + g.Why());
    t.Eq(fake->calls.size(), size_t{2}, "the ticket, then the host");
    if (fake->calls.size() != 2) return;
    const OWRequest& host = fake->calls[1];
    t.Eq(host.url, std::string("https://upload.example/v1/upload"), "the ticket's upload_url");
    t.Eq(host.method, std::string("POST"), "a POST");
    t.Eq(BodyOr(host), Raw(Png), "the bytes as given");
    t.Eq(Headers(host), std::string("Authorization=Bearer tkt.sig|X-Key=u/w1/x.png"),
         "the ticket and the key, never the API key or the PIN");
    t.Eq(g.reply.value.url, std::string("https://media.example/u/w1/x.png"), "url");
    t.Eq(g.reply.value.key, std::string("u/w1/x.png"), "key");
    t.Eq(g.reply.value.bytes, int64_t{4}, "bytes");
    t.Eq(g.reply.value.type, std::string("image/png"), "type");
    t.Eq(g.reply.value.etag, std::string("e"), "etag");
    t.Ok(g.reply.value.json.IsObject() && g.reply.value.json.Has("url"), "the answer as served");
}

OW_TEST(media_upload_image_a_ticket_passed_is_used_and_keel_is_not_called) {
    auto fake = Fake();
    fake->Then(201, Uploaded);
    Got<OWUploadedImage> g;
    OWUploadImageOptions o;
    o.ticket = TicketOf();
    Make(fake).UploadImage(OWBytes{1, 2, 3}, o, g.H());
    t.Ok(g.reply.Ok(), "the upload: " + g.Why());
    t.Eq(fake->calls.size(), size_t{1}, "the host alone");
    if (fake->calls.empty()) return;
    t.Eq(fake->calls[0].url, std::string("https://upload.example/v1/upload"), "to the ticket's upload_url");
    t.Eq(HeaderOr(fake->calls[0], "X-Key"), std::string("<absent>"), "no key: no X-Key");
}

OW_TEST(media_upload_image_larger_than_max_bytes_is_413_too_large_and_sends_nothing) {
    auto fake = Fake();
    Got<OWUploadedImage> g;
    OWUploadImageOptions o;
    o.ticket = TicketOf();
    Make(fake).UploadImage(OWBytes(9, 0), o, g.H());
    t.Eq(g.calls, 1, "answered once");
    t.Ok(g.Kind(OWApiError::Kind::Api), "kind Api: " + g.Why());
    if (!g.reply.error) return;
    t.Eq(g.reply.error->status, 413, "413");
    t.Eq(g.reply.error->code, std::string("too_large"), "too_large");
    t.Eq(g.reply.error->message, std::string("Image is 9 bytes; this ticket accepts at most 8. Nothing was sent."),
         "the TypeScript SDK's words");
    t.Eq(g.reply.error->raw_body, std::string(R"({"error":"too_large","max_bytes":8,"got":9})"), "the host's body");
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(media_upload_image_the_edges_refusal_arrives_with_its_code) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    fake->Then(401, R"({"error":"ticket_used","note":"one upload per ticket; request another"})");
    Got<OWUploadedImage> g;
    Make(fake).UploadImage(OWBytes(4, 0), {}, g.H());
    t.Ok(g.Kind(OWApiError::Kind::Api), "kind Api: " + g.Why());
    if (!g.reply.error) return;
    t.Eq(g.reply.error->status, 401, "401");
    t.Eq(g.reply.error->code, std::string("ticket_used"), "the host's code");
    t.Ok(g.reply.error->ToString().find("tkt.sig") == std::string::npos, "the ticket is not in the words");
}

OW_TEST(media_upload_image_an_edge_that_cannot_be_reached_is_kind_transport) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    fake->Fail("no answer: ConnectionError");
    Got<OWUploadedImage> g;
    Make(fake).UploadImage(Png, {}, g.H());
    t.Ok(g.Kind(OWApiError::Kind::Transport), "kind Transport: " + g.Why());
    if (!g.reply.error) return;
    t.Eq(g.reply.error->message,
         std::string("OnlyWorlds image upload failed: POST https://upload.example/v1/upload: no answer: ConnectionError"),
         "the TypeScript SDK's words, the host's URL named");
    t.Ok(g.reply.error->ToString().find("tkt.sig") == std::string::npos, "the ticket is not in the words");
}

OW_TEST(media_removal_ticket_posts_the_key_and_a_whole_image_url_is_cut_to_it) {
    auto fake = Fake();
    fake->body = RTicket;
    OWClient c = Make(fake);
    Got<OWRemovalTicket> a, b;
    c.CreateRemovalTicket("u/w1/x.png", a.H());
    c.CreateRemovalTicket("https://media.onlyworlds.com/u/w1/x.png", b.H());
    t.Ok(a.reply.Ok(), "the ticket: " + a.Why());
    t.Eq(a.reply.value.ticket, std::string("d1.sig"), "ticket");
    t.Eq(a.reply.value.remove_url, std::string("https://upload.example/v1/remove"), "remove_url");
    t.Eq(a.reply.value.key, std::string("u/w1/x.png"), "key");
    t.Eq(a.reply.value.referenced, int64_t{2}, "referenced");
    t.Eq(a.reply.value.exp, int64_t{1900000000}, "exp");
    t.Eq(fake->calls.size(), size_t{2}, "two requests");
    if (fake->calls.size() != 2) return;
    for (const OWRequest& r : fake->calls) {
        t.Eq(r.url, Base + "/media/remove-ticket", "the route");
        t.Eq(r.method, std::string("POST"), "the method");
        t.Eq(BodyOr(r), std::string(R"({"key":"u/w1/x.png"})"), "the key");
        t.Eq(Headers(r), std::string("API-Key=ow_w_test|API-Pin=1234|Content-Type=application/json"),
             "the key, the PIN and the body's type; no X-OW-*");
    }
}

OW_TEST(media_remove_image_ticket_then_the_removal_with_the_ticket_alone) {
    auto fake = Fake();
    fake->Then(200, RTicket);
    fake->Then(200, Removed);
    Got<OWRemovedImage> g;
    Make(fake).RemoveImage("u/w1/x.png", std::nullopt, g.H());
    t.Ok(g.reply.Ok(), "the removal: " + g.Why());
    t.Eq(fake->calls.size(), size_t{2}, "the ticket, then the host");
    if (fake->calls.size() != 2) return;
    const OWRequest& host = fake->calls[1];
    t.Eq(host.url, std::string("https://upload.example/v1/remove"), "the ticket's remove_url");
    t.Eq(host.method, std::string("POST"), "a POST");
    t.Eq(BodyOr(host), std::string("<none>"), "no body");
    t.Eq(Headers(host), std::string("Authorization=Bearer d1.sig"), "the ticket alone");
    t.Eq(g.reply.value.removed, std::string("u/w1/x.png"), "removed");
    t.Eq(g.reply.value.bytes, int64_t{4}, "bytes");
    t.Eq(g.reply.value.cache, std::string("may_linger"), "cache");
}

OW_TEST(media_remove_image_a_ticket_passed_is_used_and_keel_is_not_called) {
    auto fake = Fake();
    fake->Then(200, Removed);
    Got<OWRemovedImage> g;
    Make(fake).RemoveImage("u/w1/x.png", RTicketOf(), g.H());
    t.Ok(g.reply.Ok(), "the removal: " + g.Why());
    t.Eq(fake->calls.size(), size_t{1}, "the host alone");
    if (fake->calls.empty()) return;
    t.Eq(fake->calls[0].url, std::string("https://upload.example/v1/remove"), "to the ticket's remove_url");
}

OW_TEST(media_remove_image_keels_refusal_and_the_edges_arrive_with_their_codes) {
    auto keel = Fake();
    keel->status = 409;
    keel->body = R"({"error":{"type":"invalid_request","code":"upload_pending","message":"retry"}})";
    Got<OWRemovedImage> a;
    Make(keel).RemoveImage("u/w1/x.png", std::nullopt, a.H());
    t.Ok(a.reply.error && a.reply.error->status == 409 && a.reply.error->code == "upload_pending",
         "keel's 409 upload_pending: " + a.Why());
    t.Eq(keel->calls.size(), size_t{1}, "the host never called");
    auto edge = Fake();
    edge->Then(200, RTicket);
    edge->Then(403, R"({"error":"not_removable"})");
    Got<OWRemovedImage> b;
    Make(edge).RemoveImage("u/w1/x.png", std::nullopt, b.H());
    t.Ok(b.reply.error && b.reply.error->status == 403 && b.reply.error->code == "not_removable",
         "the host's 403 not_removable: " + b.Why());
    t.Ok(b.Why().find("d1.sig") == std::string::npos, "the ticket is not in the words");
}

OW_TEST(media_an_agent_client_sends_no_x_ow_agent_on_the_image_ticket_or_to_the_edge) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    fake->Then(201, Uploaded);
    Got<OWUploadedImage> g;
    OWUploadImageOptions o;
    o.key = "u/w1/x.png";
    Make(fake, /*agent=*/true).UploadImage(OWBytes{1, 2, 3}, o, g.H());
    t.Eq(fake->calls.size(), size_t{2}, "two requests");
    if (fake->calls.size() != 2) return;
    t.Eq(HeaderOr(fake->calls[0], "X-OW-Agent"), std::string("<absent>"), "none on the ticket");
    t.Eq(HeaderOr(fake->calls[0], "X-OW-Client"), std::string("<absent>"), "no client name on the ticket");
    t.Eq(Headers(fake->calls[1]), std::string("Authorization=Bearer tkt.sig|X-Key=u/w1/x.png"), "the edge's two");
}

OW_TEST(media_an_account_token_with_a_world_is_refused_on_every_image_call_nothing_sent) {
    auto fake = Fake();
    OWClient c = Make(fake, false, "ow_a_acct", std::string("w1"));
    Got<OWMediaTicket> a;
    Got<OWUploadedImage> b;
    Got<OWRemovalTicket> d;
    Got<OWRemovedImage> e;
    c.CreateMediaTicket(a.H());
    c.UploadImage(OWBytes{1, 2, 3}, {}, b.H());
    c.CreateRemovalTicket("k", d.H());
    c.RemoveImage("k", std::nullopt, e.H());
    const std::vector<std::pair<const char*, std::string>> whys = {
        {"createMediaTicket", a.reply.error && a.Kind(OWApiError::Kind::Refused) ? a.reply.error->message : ""},
        {"uploadImage", b.reply.error && b.Kind(OWApiError::Kind::Refused) ? b.reply.error->message : ""},
        {"createRemovalTicket", d.reply.error && d.Kind(OWApiError::Kind::Refused) ? d.reply.error->message : ""},
        {"removeImage", e.reply.error && e.Kind(OWApiError::Kind::Refused) ? e.reply.error->message : ""},
    };
    for (const auto& [name, why] : whys)
        t.Ok(why.find("world key") != std::string::npos, std::string(name) + ": refused, naming a world key: " + why);
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(media_object_key_as_given_or_a_whole_urls_path) {
    t.Eq(OWMedia::ObjectKey("u/w1/x.png"), std::string("u/w1/x.png"), "a key as given");
    t.Eq(OWMedia::ObjectKey("https://media.onlyworlds.com/u/w1/x.png"), std::string("u/w1/x.png"), "a URL's path");
    t.Eq(OWMedia::ObjectKey("HTTP://media.example//u/w1/x.png"), std::string("u/w1/x.png"),
         "any case of the scheme, every leading slash");
    t.Eq(OWMedia::ObjectKey("https://media.example/u/w1/a%20b%C3%A9.png?v=2#top"), std::string("u/w1/a b\xC3\xA9.png"),
         "unescaped, without its query or fragment");
    t.Eq(OWMedia::ObjectKey("https://media.example"), std::string(""), "a URL with no path");
}
