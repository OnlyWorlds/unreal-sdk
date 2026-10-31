// OWClient's read calls through a fake transport (tests/fake_transport.h): no test touches the network. Each call's
// request (method, URL, query, headers, body) on each credential, each refusal before sending, keel's error envelope,
// the cursor walk and its guard, the change feed's tip poll, and the seam itself: an answer that comes later, a client
// gone before it comes, a transport that answers twice. The credential rules are the Unity SDK's OWClient
// (RequestWithResponseAsync), held call by call as its OWClientTests hold them.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWCharter.h"
#include "OWClient.h"
#include "OWJson.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";
const char* const Page0 = "{\"data\":[],\"has_more\":false,\"next_cursor\":null}";
// One body every read call can read: an object with a page's, a feed's and an element's keys.
const char* const Answer = "{\"data\":[],\"has_more\":false,\"changes\":[],\"head\":0,\"cursor\":\"0\",\"id\":\"l1\"}";

std::shared_ptr<FakeTransport> Fake(const std::string& body = "{}") {
    auto f = std::make_shared<FakeTransport>();
    f->body = body;
    return f;
}

OWClient Make(const std::shared_ptr<FakeTransport>& fake, const std::string& key = "ow_w_test",
              const std::string& pin = "0000", std::optional<std::string> world = std::nullopt) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = pin;
    config.world = std::move(world);
    config.base_url = Base;
    config.transport = fake;
    return OWClient(std::move(config));
}

// A handler that keeps the reply and counts its calls.
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
    bool Refused() const { return reply.error && reply.error->kind == OWApiError::Kind::Refused; }
};

OWJsonValue Parse(std::string_view text) { return OWJson::Parse(text).value; }

// A member's text, or "" when the value has no such member.
std::string TextOf(const OWJsonValue& v, std::string_view key) {
    const OWJsonValue* m = v.Find(key);
    return m ? m->Text() : "";
}

std::string Decode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back(static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// The URL's path under Base, without its query.
std::string PathOf(const std::string& url) {
    std::string p = url.substr(0, url.find('?'));
    return p.rfind(Base, 0) == 0 ? p.substr(Base.size()) : p;
}
// The URL's raw query, without its '?'; "" for none.
std::string QueryOf(const std::string& url) {
    size_t q = url.find('?');
    return q == std::string::npos ? "" : url.substr(q + 1);
}
// The URL's query, decoded, as "name=value" joined by '&' in order.
std::string Decoded(const std::string& url) {
    std::string q = QueryOf(url), out;
    size_t start = 0;
    while (!q.empty() && start <= q.size()) {
        size_t amp = q.find('&', start);
        std::string pair = q.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        size_t eq = pair.find('=');
        if (!out.empty()) out += "&";
        out += Decode(pair.substr(0, eq)) + "=" + (eq == std::string::npos ? "" : Decode(pair.substr(eq + 1)));
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}
// The query's last parameter, decoded ("world=w1").
std::string LastParam(const std::string& url) {
    std::string d = Decoded(url);
    size_t amp = d.rfind('&');
    return amp == std::string::npos ? d : d.substr(amp + 1);
}

std::string HeaderOr(const OWRequest& r, const char* name) {
    const std::string* v = r.Header(name);
    return v ? *v : "<absent>";
}
std::string HeaderNames(const OWRequest& r) {
    std::string out;
    for (auto& [k, v] : r.headers) out += (out.empty() ? "" : ",") + k;
    return out;
}

// -- The read calls, swept over every credential ---------------------------------------------------------------------

struct Outcome {
    int calls = 0;
    std::optional<OWApiError> error;
};

// One read call: its name, whether a prefixed key sends its PIN on it, whether an account token with a world sends it,
// whether a keyless client with a world sends it, and the call.
struct ReadCall {
    const char* name;
    bool pin;
    bool account;
    bool keyless;
    std::function<Outcome(OWClient&)> run;
};

template <typename T>
Outcome OutOf(const Got<T>& g) {
    return Outcome{g.calls, g.reply.error};
}

const std::vector<ReadCall>& Calls() {
    static const std::vector<ReadCall> calls = {
        {"health", false, true, true, [](OWClient& c) { Got<OWJsonValue> g; c.Health(g.H()); return OutOf(g); }},
        {"world", false, true, true, [](OWClient& c) { Got<OWJsonValue> g; c.GetWorld(g.H()); return OutOf(g); }},
        {"me", true, false, false, [](OWClient& c) { Got<OWJsonValue> g; c.Me(g.H()); return OutOf(g); }},
        {"members", false, true, false, [](OWClient& c) { Got<OWJsonValue> g; c.Members(g.H()); return OutOf(g); }},
        {"list", false, true, true, [](OWClient& c) { Got<OWPage> g; c.List("location", {}, g.H()); return OutOf(g); }},
        {"list (typed options)", false, true, true,
         [](OWClient& c) {
             OWListOptions o;
             o.ids = {"l1"};
             o.created_by = "owner";
             o.expand = {"zone"};
             o.view_as = "public";
             o.name_contains = "harb";
             Got<OWPage> g;
             c.List("location", o, g.H());
             return OutOf(g);
         }},
        {"list all", false, true, true,
         [](OWClient& c) { Got<std::vector<OWJsonValue>> g; c.ListAll("location", {}, g.H()); return OutOf(g); }},
        {"get", false, true, true,
         [](OWClient& c) { Got<OWJsonValue> g; c.Get("location", "l1", {}, g.H()); return OutOf(g); }},
        {"changes", false, false, false,
         [](OWClient& c) { Got<OWChangesPage> g; c.Changes({}, g.H()); return OutOf(g); }},
        {"changes head", false, false, false,
         [](OWClient& c) { Got<OWChangesPage> g; c.ChangesHead(g.H()); return OutOf(g); }},
    };
    return calls;
}

struct Sent {
    std::string name;
    bool pin;
    OWRequest request;
};

// Every read call on its own client; the request each sent. A call such a client does not send is checked refused
// with nothing sent, and left out.
std::vector<Sent> Sweep(owtest::T& t, const std::string& key, const std::string& pin,
                        std::optional<std::string> world = std::nullopt) {
    std::vector<Sent> sent;
    bool account = key.rfind("ow_a_", 0) == 0;
    bool keyless = key.empty();
    for (const ReadCall& call : Calls()) {
        auto fake = Fake(Answer);
        OWClient c = Make(fake, key, pin, world);
        Outcome o = call.run(c);
        std::string n = call.name;
        t.Eq(o.calls, 1, n + ": the handler called once");
        bool sends = account ? call.account : keyless ? call.keyless : true;
        if (!sends) {
            t.Ok(o.error && o.error->kind == OWApiError::Kind::Refused, n + ": refused on this client");
            t.Eq(fake->calls.size(), size_t(0), n + ": nothing sent");
            continue;
        }
        t.Ok(!o.error, n + ": answered (" + (o.error ? o.error->ToString() : "") + ")");
        t.Eq(fake->calls.size(), size_t(1), n + ": one request");
        if (fake->calls.size() == 1) {
            t.Eq(fake->Last().method, "GET", n + ": a GET");
            t.Ok(!fake->Last().body.has_value(), n + ": no body");
            sent.push_back({n, call.pin, fake->Last()});
        }
    }
    return sent;
}

// True for health, after checking it carried no credential at all.
bool SentWithNone(owtest::T& t, const Sent& s) {
    if (s.name != "health") return false;
    t.Eq(HeaderNames(s.request), "", "health: no header at all");
    t.Eq(s.request.url, Base + "/health/", "health: no world=");
    return true;
}

}  // namespace

OW_TEST(client_pin_world_key_with_a_pin_only_on_me) {
    std::vector<Sent> all = Sweep(t, "ow_w_test", "1234");
    t.Eq(all.size(), size_t(10), "every read call sent");
    for (const Sent& s : all) {
        if (SentWithNone(t, s)) continue;
        t.Eq(HeaderOr(s.request, "API-Key"), "ow_w_test", s.name);
        t.Eq(HeaderOr(s.request, "API-Pin"), s.pin ? "1234" : "<absent>", s.name + ": the PIN only where keel checks it");
        t.Eq(HeaderOr(s.request, "Authorization"), "<absent>", s.name + ": no Bearer");
        t.Ok(s.request.url.find("world=") == std::string::npos, s.name + ": a world key names its own world");
    }
}

OW_TEST(client_pin_legacy_key_on_every_request) {
    // A 10-digit legacy key needs its PIN on reads of a world with one (v1 parity).
    for (const Sent& s : Sweep(t, "0123456789", "1234")) {
        if (SentWithNone(t, s)) continue;
        t.Eq(HeaderOr(s.request, "API-Key"), "0123456789", s.name);
        t.Eq(HeaderOr(s.request, "API-Pin"), "1234", s.name);
    }
}

OW_TEST(client_pin_read_key_never_without_one) {
    for (const Sent& s : Sweep(t, "ow_r_share", "")) {
        if (SentWithNone(t, s)) continue;
        t.Eq(HeaderNames(s.request), "API-Key", s.name + ": the key alone");
        t.Eq(HeaderOr(s.request, "API-Key"), "ow_r_share", s.name);
    }
}

OW_TEST(client_pin_unknown_key_reads_as_a_prefixed_key) {
    // A key of no known shape is sent as API-Key, its PIN on /me alone, as Unity's OWKeyKind.Unknown is.
    for (const Sent& s : Sweep(t, "abc", "1234")) {
        if (SentWithNone(t, s)) continue;
        t.Eq(HeaderOr(s.request, "API-Key"), "abc", s.name);
        t.Eq(HeaderOr(s.request, "API-Pin"), s.pin ? "1234" : "<absent>", s.name);
    }
}

OW_TEST(client_account_token_bearer_never_a_pin_world_on_its_reads) {
    std::vector<Sent> all = Sweep(t, "ow_a_acct", "1234", "w1");
    std::string names;
    for (const Sent& s : all) names += (names.empty() ? "" : ",") + s.name;
    t.Eq(names, "health,world,members,list,list (typed options),list all,get",
         "the health pulse, the world reads and the roster sent; /me and the feed refused");
    for (const Sent& s : all) {
        if (SentWithNone(t, s)) continue;
        t.Eq(HeaderNames(s.request), "Authorization", s.name + ": the Bearer alone, never the PIN");
        t.Eq(HeaderOr(s.request, "Authorization"), "Bearer ow_a_acct", s.name);
        t.Eq(LastParam(s.request.url), "world=w1", s.name + ": world= last");
    }
}

OW_TEST(client_keyless_reader_sends_no_credential_and_world) {
    std::vector<Sent> all = Sweep(t, "", "1234", "w1");
    std::string names;
    for (const Sent& s : all) names += (names.empty() ? "" : ",") + s.name;
    t.Eq(names, "health,world,list,list (typed options),list all,get",
         "the health pulse and the world reads; the roster has no public reader");
    for (const Sent& s : all) {
        t.Eq(HeaderNames(s.request), "", s.name + ": no credential");
        if (s.name != "health") t.Eq(LastParam(s.request.url), "world=w1", s.name);
    }
}

OW_TEST(client_bare_client_sends_the_health_pulse_alone) {
    // No key and no world: the Unity SDK's bare client (the TypeScript SDK's new OwV2Client()), built, health only.
    auto fake = Fake(Answer);
    OWClient c = Make(fake, "", "");
    t.Eq(c.Error(), "", "built");
    t.Ok(c.KeyKind() == OWKeyKind::None, "no key");
    for (const ReadCall& call : Calls()) {
        Outcome o = call.run(c);
        std::string n = call.name;
        if (n == "health") {
            t.Ok(!o.error, "health answered");
            continue;
        }
        t.Ok(o.error && o.error->kind == OWApiError::Kind::Refused, n + ": refused");
        if (o.error)
            t.Ok(o.error->message.find("needs a key") != std::string::npos, n + ": " + o.error->message);
    }
    t.Eq(fake->calls.size(), size_t(1), "the health pulse alone was sent");
}

OW_TEST(client_account_token_without_world_refuses_the_world_reads) {
    for (const ReadCall& call : Calls()) {
        auto fake = Fake(Answer);
        OWClient c = Make(fake, "ow_a_acct", "");
        Outcome o = call.run(c);
        std::string n = call.name;
        if (n == "health") {
            t.Ok(!o.error, "health needs no world");
            continue;
        }
        t.Ok(o.error && o.error->kind == OWApiError::Kind::Refused, n + ": refused");
        t.Eq(fake->calls.size(), size_t(0), n + ": nothing sent");
        if (o.error) {
            const std::string& m = o.error->message;
            t.Ok(m.size() >= 17 && m.compare(m.size() - 17, 17, "Nothing was sent.") == 0, n + ": says so (" + m + ")");
        }
    }
}

OW_TEST(client_world_reads_paths_and_world_placement) {
    auto fake = Fake(Answer);
    OWClient c = Make(fake, "ow_a_acct", "", "w 1");
    Got<OWJsonValue> g;
    c.GetWorld(g.H());
    t.Eq(fake->Last().url, Base + "/world/?world=w%201", "the world GET, the world escaped");
    OWGetOptions o;
    o.fields = {"name"};
    c.Get("character", "c1", o, g.H());
    t.Eq(fake->Last().url, Base + "/character/c1/?fields=name&world=w%201", "world= beside the get's own query");
    c.Members(g.H());
    t.Eq(fake->Last().url, Base + "/members/?world=w%201", "the roster names its world");
    c.Health(g.H());
    t.Eq(fake->Last().url, Base + "/health/", "the health pulse names none");
}

// -- Construction ------------------------------------------------------------------------------------------------------

OW_TEST(client_config_refusals_answer_every_call_with_nothing_sent) {
    struct Case {
        std::string key;
        std::optional<std::string> world;
        bool transport;
        const char* says;
    };
    const std::vector<Case> cases = {
        {"ow_w_k", "w1", true, "A world key reads its own world"},
        {"0123456789", "w1", true, "A world key reads its own world"},
        {"ow_r_k", "w1", true, "A world key reads its own world"},
        {"abc", "w1", true, "A world key reads its own world"},
        {"ow_a_k", "", true, "world is a world's id"},
        {"ow_a_k", "  ", true, "world is a world's id"},
        {"", " \t", true, "world is a world's id"},
        {"ow_w_k", std::nullopt, false, "A transport is required"},
    };
    for (const Case& k : cases) {
        auto fake = Fake();
        OWClientConfig config;
        config.api_key = k.key;
        config.world = k.world;
        if (k.transport) config.transport = fake;
        OWClient c(std::move(config));
        std::string n = "'" + k.key + "' world '" + k.world.value_or("<absent>") + "'";
        t.Ok(c.Error().find(k.says) != std::string::npos, n + ": " + k.says + " (got '" + c.Error() + "')");
        Got<OWJsonValue> g;
        c.Health(g.H());
        t.Ok(g.Refused(), n + ": even health is refused");
        t.Eq(g.calls, 1, n + ": the handler called once");
        if (g.reply.error)
            t.Ok(g.reply.error->message.find("Nothing was sent.") != std::string::npos, n + ": " + g.reply.error->message);
        t.Eq(fake->calls.size(), size_t(0), n + ": nothing sent");
    }
}

OW_TEST(client_config_defaults_and_the_built_client) {
    OWClientConfig config;
    config.api_key = "ow_w_k";
    config.transport = Fake();
    OWClient c(config);
    t.Eq(c.Error(), "", "built");
    t.Eq(c.BaseUrl(), "https://www.onlyworlds.com/api/v2", "production by default");
    t.Eq(std::string(OWClient::DefaultBaseUrl), "https://www.onlyworlds.com/api/v2", "");
    t.Eq(c.PageSize(), int64_t(100), "100 a page");
    t.Ok(c.KeyKind() == OWKeyKind::Write, "ow_w_");
    t.Ok(!c.World().has_value(), "no world");
    config.base_url = "http://x/api/v2//";
    config.page_size = 0;
    t.Eq(OWClient(config).BaseUrl(), "http://x/api/v2", "trailing slashes trimmed");
    t.Eq(OWClient(config).PageSize(), int64_t(100), "a page size of 0 is the default");
    config.page_size = -3;
    t.Eq(OWClient(config).PageSize(), int64_t(100), "and a negative one");
    config.page_size = 7;
    t.Eq(OWClient(config).PageSize(), int64_t(7), "");
    OWClientConfig keyless;
    keyless.world = "w1";
    keyless.transport = Fake();
    OWClient k(keyless);
    t.Eq(k.Error(), "", "a keyless reader with a world");
    t.Ok(k.KeyKind() == OWKeyKind::None, "");
    t.Eq(k.World().value_or(""), "w1", "");
    OWClientConfig moved;
    moved.api_key = "ow_r_k";
    moved.transport = Fake();
    OWClient a(moved);
    OWClient b(std::move(a));
    t.Ok(b.KeyKind() == OWKeyKind::Read, "a client moves");
}

OW_TEST(client_key_kinds) {
    const std::vector<std::pair<std::string, OWKeyKind>> kinds = {
        {"ow_w_x", OWKeyKind::Write},       {"ow_r_x", OWKeyKind::Read},       {"ow_a_x", OWKeyKind::Account},
        {"0123456789", OWKeyKind::Legacy},  {"012345678", OWKeyKind::Unknown}, {"01234567890", OWKeyKind::Unknown},
        {"+123456789", OWKeyKind::Unknown}, {"ow_s_x", OWKeyKind::Unknown},    {"OW_W_x", OWKeyKind::Unknown},
        {"", OWKeyKind::None},
    };
    for (auto& [key, kind] : kinds) t.Ok(OWClient::KindOf(key) == kind, "'" + key + "'");
}

// -- List ---------------------------------------------------------------------------------------------------------------

OW_TEST(client_list_sends_its_options_in_order_escaped) {
    auto fake = Fake(Page0);
    OWClient c = Make(fake);
    OWListOptions o;
    o.view_as = "public";
    o.subtype = "u";
    o.supertype = "s w";
    o.name_contains = "m&n";
    o.name = "n";
    o.created_by = "owner";
    o.ids = {"c1", "c2"};
    o.fields = {"name", "supertype"};
    o.expand = {"friends"};
    o.cursor = "c=1";
    o.limit = 5;
    o.filter = {{"zone", ""}, {"birthplace", "p1"}};
    Got<OWPage> g;
    c.List("character", o, g.H());
    t.Eq(PathOf(fake->Last().url), "/character/", "");
    t.Eq(QueryOf(fake->Last().url),
         "limit=5&cursor=c%3D1&expand=friends&fields=name%2Csupertype&ids=c1%2Cc2&created_by=owner&name=n"
         "&name__icontains=m%26n&supertype=s%20w&subtype=u&birthplace=p1&zone=&as=public",
         "limit, cursor, expand, fields, ids, created_by, the named filters, the filter by field name, as last");
    c.List("character", {}, g.H());
    t.Eq(fake->Last().url, Base + "/character/?limit=100", "the page size alone");
    OWListOptions zero;
    zero.limit = 0;
    c.List("character", zero, g.H());
    t.Eq(fake->Last().url, Base + "/character/?limit=0", "a limit given is sent as given");
    OWListOptions only;
    only.filter = {{"ids", "c9"}, {"as", "public"}};
    c.List("character", only, g.H());
    t.Eq(fake->Last().url, Base + "/character/?limit=100&as=public&ids=c9",
         "a filter key no typed option sends this time goes out once, in field order");
}

OW_TEST(client_query_escaping_is_rfc_3986) {
    // Unity's Uri.EscapeDataString: the unreserved four (- _ . ~) and ASCII letters and digits kept, every other byte
    // of the UTF-8 escaped, in capitals; a space %20, never '+'.
    auto fake = Fake(Page0);
    OWClient c = Make(fake);
    OWListOptions o;
    o.name = "a b~-_.!*'(),/?&=+:@ \xC3\xA9";
    Got<OWPage> g;
    c.List("character", o, g.H());
    t.Eq(QueryOf(fake->Last().url), "limit=100&name=a%20b~-_.%21%2A%27%28%29%2C%2F%3F%26%3D%2B%3A%40%20%C3%A9", "");
}

OW_TEST(client_list_reads_the_bodies_as_served) {
    auto fake = Fake(
        "{\"data\":[{\"id\":\"c1\",\"name\":\"Ana\",\"level\":3,\"x_tool\":{\"a\":1}},{\"id\":\"c2\",\"name\":\"Bo\"}],"
        "\"has_more\":true,\"next_cursor\":\"n2\"}");
    Got<OWPage> g;
    Make(fake).List("character", {}, g.H());
    t.Ok(g.reply.Ok(), "answered");
    const OWPage& page = g.reply.value;
    t.Eq(page.data.size(), size_t(2), "two rows");
    t.Eq(page.data.empty() ? std::string() : OWJson::Stringify(page.data[0]), OWJson::Stringify(Parse("{\"id\":\"c1\",\"name\":\"Ana\",\"level\":3,\"x_tool\":{\"a\":1}}")),
         "a row is the body as served, key order and extension keys kept");
    t.Eq(page.data.size() == 2 ? TextOf(page.data[1], "name") : "", "Bo", "");
    t.Ok(page.has_more, "");
    t.Eq(page.next_cursor, "n2", "");
    t.Ok(g.reply.response && g.reply.response->status == 200, "the response beside it");
    fake->body = "{\"data\":[{\"id\":\"z\"}],\"has_more\":false,\"next_cursor\":null}";
    Make(fake).List("unknown_type", {}, g.H());
    t.Eq(g.reply.value.data.size(), size_t(1), "a type not among the 22 read whole");
    t.Eq(g.reply.value.next_cursor, "", "a null cursor is none");
    fake->body = "{\"data\":[],\"has_more\":true,\"next_cursor\":17}";
    Make(fake).List("character", {}, g.H());
    t.Eq(g.reply.value.next_cursor, "17", "a cursor served as a number is its text");
    for (const char* bad : {"{\"data\":{}}", "{\"data\":[1]}", "[]", "null"}) {
        fake->body = bad;
        Make(fake).List("character", {}, g.H());
        t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer && g.reply.error->status == 200,
             std::string(bad) + ": a 2xx that is not a page is an error");
        t.Eq(g.reply.value.data.size(), size_t(0), std::string(bad) + ": no rows");
    }
}

OW_TEST(client_list_refusals_send_nothing) {
    struct Case {
        OWListOptions o;
        const char* says;
    };
    auto with = [](std::map<std::string, std::string> f) {
        OWListOptions o;
        o.filter = std::move(f);
        return o;
    };
    std::vector<Case> cases = {
        {with({{"world", "w2"}}), "world is not a filter"},
        {with({{"Parent", "p"}}), "'Parent' is not a field name"},
        {with({{"", "p"}}), "'' is not a field name"},
        {with({{"2nd", "p"}}), "'2nd' is not a field name"},
        {with({{"zone-x", "p"}}), "'zone-x' is not a field name"},
        {with({{"limit", "9"}}), "'limit' is sent by its own option"},
    };
    OWListOptions named = with({{"name", "m"}});
    named.name = "n";
    cases.push_back({named, "'name' is sent by its own option"});
    OWListOptions as = with({{"as", "public"}});
    as.view_as = "public";
    cases.push_back({as, "'as' is sent by its own option"});
    OWListOptions cursor = with({{"cursor", "x"}});
    cursor.cursor = "y";
    cases.push_back({cursor, "'cursor' is sent by its own option"});
    for (const Case& k : cases) {
        auto fake = Fake(Page0);
        Got<OWPage> g;
        Make(fake).List("character", k.o, g.H());
        t.Ok(g.Refused() && g.reply.error->message.find(k.says) != std::string::npos,
             std::string(k.says) + " (got " + (g.reply.error ? g.reply.error->message : "no error") + ")");
        t.Eq(g.calls, 1, std::string(k.says) + ": the handler called once");
        t.Eq(fake->calls.size(), size_t(0), std::string(k.says) + ": nothing sent");
    }
}

// -- ListAll: the cursor walk ---------------------------------------------------------------------------------------------

OW_TEST(client_list_all_walks_every_page_by_has_more) {
    auto fake = Fake();
    // The middle page is short: a short page is no end (keel clamps the limit at 1000 silently).
    fake->Then(200, "{\"data\":[{\"id\":\"a\"},{\"id\":\"b\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"c\"}],\"has_more\":true,\"next_cursor\":\"p3\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"d\"}],\"has_more\":false,\"next_cursor\":null}");
    std::vector<std::pair<int, size_t>> pages;
    OWListOptions o;
    o.supertype = "City";
    o.filter = {{"zone", "z1"}};
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", o, g.H(), [&](int n, size_t count) { pages.emplace_back(n, count); });
    t.Ok(g.reply.Ok(), "answered");
    t.Eq(g.calls, 1, "the handler called once");
    std::string ids;
    for (const OWJsonValue& e : g.reply.value) ids += TextOf(e, "id");
    t.Eq(ids, "abcd", "every element, in order");
    t.Eq(fake->calls.size(), size_t(3), "three pages");
    if (fake->calls.size() == 3) {
        t.Eq(Decoded(fake->calls[0].url), "limit=100&supertype=City&zone=z1", "the first page");
        t.Eq(Decoded(fake->calls[1].url), "limit=100&cursor=p2&supertype=City&zone=z1", "each page's cursor passed back");
        t.Eq(Decoded(fake->calls[2].url), "limit=100&cursor=p3&supertype=City&zone=z1", "every filter on every page");
    }
    t.Eq(pages.size(), size_t(3), "on_page after each page");
    if (pages.size() == 3) {
        t.Ok(pages[0] == std::make_pair(1, size_t(2)) && pages[1] == std::make_pair(2, size_t(3)) &&
                 pages[2] == std::make_pair(3, size_t(4)),
             "on_page: the page's number and the count so far");
    }
}

OW_TEST(client_list_all_starts_at_a_given_cursor_and_stops_without_one) {
    auto fake = Fake();
    fake->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":null}");
    OWListOptions o;
    o.cursor = "k7";
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", o, g.H());
    t.Eq(Decoded(fake->Last().url), "limit=100&cursor=k7", "the walk starts where it was told");
    t.Eq(fake->calls.size(), size_t(1), "has_more with no cursor ends the walk rather than looping");
    t.Ok(g.reply.Ok(), "");
    t.Eq(g.reply.value.size(), size_t(1), "");
}

OW_TEST(client_list_all_refuses_a_page_that_repeats_its_cursor) {
    auto fake = Fake();
    fake->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"b\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", {}, g.H());
    t.Eq(fake->calls.size(), size_t(2), "the second page is not read again");
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Transport &&
             g.reply.error->message.find("answered the cursor it was sent") != std::string::npos,
         "an error, not a loop (" + (g.reply.error ? g.reply.error->ToString() : "") + ")");
    t.Eq(g.reply.value.size(), size_t(0), "no half list");
    // The caller's starting cursor counts: a first page that hands it back stops at once.
    auto again = Fake();
    again->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":\"k7\"}");
    OWListOptions o;
    o.cursor = "k7";
    Make(again).ListAll("location", o, g.H());
    t.Eq(again->calls.size(), size_t(1), "the starting cursor handed back");
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Transport, "");
}

OW_TEST(client_list_all_stops_at_an_error) {
    auto fake = Fake();
    fake->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    fake->Then(429, "{\"error\":{\"type\":\"rate_limit\",\"code\":\"rate_limited\",\"message\":\"slow\"}}",
               {{"retry-after", "3"}});
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", {}, g.H());
    t.Eq(fake->calls.size(), size_t(2), "");
    t.Ok(g.reply.error && g.reply.error->status == 429 && g.reply.error->kind == OWApiError::Kind::Api, "the page's error");
    t.Eq(g.reply.error ? g.reply.error->retry_after.value_or(-1) : -1, 3, "Retry-After read");
    t.Eq(g.reply.value.size(), size_t(0), "no half list");
    t.Ok(g.reply.response && g.reply.response->status == 429, "the page's response beside it");
    // A refusal on the first page (a filter it refuses) answers it, nothing sent.
    auto refused = Fake();
    OWListOptions o;
    o.filter = {{"world", "w"}};
    Make(refused).ListAll("location", o, g.H());
    t.Ok(g.Refused(), "a refused page refuses the walk");
    t.Eq(refused->calls.size(), size_t(0), "");
}

OW_TEST(client_list_all_walks_a_long_world_without_deepening_the_stack) {
    // A transport that answers before Send returns must not turn each page into a deeper call: 20,000 pages.
    auto fake = Fake();
    const int n = 20000;
    for (int i = 1; i < n; ++i)
        fake->Then(200, "{\"data\":[{\"id\":\"e" + std::to_string(i) + "\"}],\"has_more\":true,\"next_cursor\":\"p" +
                            std::to_string(i) + "\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"last\"}],\"has_more\":false}");
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", {}, g.H());
    t.Ok(g.reply.Ok(), "answered");
    t.Eq(g.reply.value.size(), size_t(n), "every page");
    t.Eq(g.calls, 1, "once");
}

// -- Answers that come later, a client gone, a transport that answers twice ------------------------------------------

OW_TEST(client_answers_that_come_later) {
    auto fake = Fake();
    fake->deferred = true;
    fake->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"b\"}],\"has_more\":false}");
    fake->Then(200, "{\"head\":4,\"cursor\":4}", {{"ETag", "\"4\""}});
    fake->Then(304, "");
    Got<std::vector<OWJsonValue>> walk;
    Got<OWChangesPage> head;
    {
        OWClient c = Make(fake);
        c.ListAll("location", {}, walk.H());
        t.Eq(walk.calls, 0, "nothing answered before the transport answers");
        t.Eq(fake->Pending(), size_t(1), "one page in flight");
        fake->Flush();
        t.Eq(walk.calls, 1, "the walk answered after its second page");
        t.Eq(fake->calls.size(), size_t(2), "");
        t.Eq(walk.reply.value.size(), size_t(2), "");
        c.ChangesHead(head.H());
        fake->Flush();
        t.Eq(head.reply.value.head, int64_t(4), "");
        c.ChangesHead(head.H());
        t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "\"4\"", "the tag held across answers that come later");
        // The client is destroyed here, its tip read still in flight.
    }
    head = {};
    fake->Flush();
    t.Eq(head.calls, 1, "a client gone before its answer: the handler still called");
    t.Ok(head.reply.Ok() && head.reply.value.unchanged && head.reply.value.head == 4,
         "and still reads with the state it held");
}

OW_TEST(client_a_second_answer_is_ignored) {
    auto fake = Fake();
    fake->answer_twice = true;
    fake->Then(200, "{\"data\":[{\"id\":\"a\"}],\"has_more\":true,\"next_cursor\":\"p2\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"b\"}],\"has_more\":true,\"next_cursor\":\"p3\"}");
    fake->Then(200, "{\"data\":[{\"id\":\"c\"}],\"has_more\":false}");
    Got<std::vector<OWJsonValue>> g;
    Make(fake).ListAll("location", {}, g.H());
    t.Eq(g.calls, 1, "a transport that calls back twice: the handler once");
    t.Eq(fake->calls.size(), size_t(3), "each page read once");
    t.Eq(g.reply.value.size(), size_t(3), "");
}

// -- Get -----------------------------------------------------------------------------------------------------------------

OW_TEST(client_get_sends_its_options_and_reads_the_body) {
    auto fake = Fake("{\"id\":\"p1\",\"name\":\"Gate\",\"element_type\":\"location\",\"element_id\":\"l1\",\"x\":4}");
    OWClient c = Make(fake);
    OWGetOptions o;
    o.view_as = "m1";
    o.fields = {"name"};
    o.expand = {"map", "zone"};
    Got<OWJsonValue> g;
    c.Get("pin", "p1", o, g.H());
    t.Eq(fake->Last().url, Base + "/pin/p1/?expand=map%2Czone&fields=name&as=m1", "expand, fields, as");
    t.Eq(TextOf(g.reply.value, "element_type"), "location", "the body as served");
    t.Eq(TextOf(g.reply.value, "x"), "4", "a number keeps its text");
    c.Get("pin", "p1", {}, g.H());
    t.Eq(fake->Last().url, Base + "/pin/p1/", "no options, no query");
    c.Get("pin", "a/b?c", {}, g.H());
    t.Eq(fake->Last().url, Base + "/pin/a%2Fb%3Fc/", "the id escaped: a path segment, never a route or a query");
}

OW_TEST(client_an_answer_that_is_not_the_body) {
    auto fake = Fake("[1]");
    Got<OWJsonValue> g;
    Make(fake).Get("pin", "p1", {}, g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, "an answer that is not the element is an error");
    fake->body = "<html>";
    Make(fake).GetWorld(g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer && g.reply.error->status == 200 &&
             g.reply.error->raw_body == "<html>",
         "a 200 that is not JSON");
    fake->body = "";
    Make(fake).Health(g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, "a 200 with no body");
    fake->status = 204;
    Make(fake).Me(g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer && g.reply.error->status == 204, "a 204");
}

// -- Me, members, health, the world ---------------------------------------------------------------------------------------

OW_TEST(client_me_and_its_reading_aids) {
    auto fake = Fake(
        "{\"world\":{\"id\":\"w1\",\"name\":\"W\"},\"scope\":\"write\",\"kind\":\"owner\",\"role\":\"owner\","
        "\"membership\":null,\"character\":null,\"lenses\":[],\"reading_aids\":{\"charter\":{\"id\":\"l1\",\"name\":"
        "\"House\",\"rules\":[],\"skill\":[]},\"guide\":null,\"domains\":[{\"id\":\"d1\",\"name\":\"Port\",\"sentence\":"
        "\"S\",\"hubs\":[]}],\"strand\":{\"narrative\":{\"id\":\"n1\",\"name\":\"Tale\"},\"event\":null,\"location\":"
        "null,\"character\":null,\"now\":null,\"same\":null}}}");
    Got<OWJsonValue> g;
    Make(fake, "ow_w_k", "4821").Me(g.H());
    t.Eq(fake->Last().url, Base + "/me/", "GET /me/");
    t.Eq(HeaderOr(fake->Last(), "API-Pin"), "4821", "the connection test carries the PIN");
    t.Eq(TextOf(g.reply.value, "role"), "owner", "the body as served");
    std::optional<OWReadingAids> aids = OWCharter::Served(g.reply.value);
    t.Ok(aids.has_value(), "OWCharter::Served reads its reading_aids");
    if (aids) {
        t.Eq(aids->charter ? aids->charter->name : "", "House", "");
        t.Eq(aids->domains.size(), size_t(1), "");
        t.Ok(aids->strand && aids->strand->narrative && aids->strand->narrative->name == "Tale", "the strand fourth");
    }
}

OW_TEST(client_members_reads_the_rows) {
    auto fake = Fake(
        "{\"data\":[{\"id\":null,\"kind\":\"owner\",\"role\":\"owner\"},{\"id\":\"m1\",\"kind\":\"agent\",\"role\":"
        "\"contributor\"}]}");
    Got<OWJsonValue> g;
    Make(fake).Members(g.H());
    t.Eq(fake->Last().url, Base + "/members/", "");
    t.Ok(g.reply.value.IsArray() && g.reply.value.Size() == 2, "the rows of {data}");
    if (g.reply.value.Size() == 2) t.Eq(TextOf(g.reply.value.Items()[1], "kind"), "agent", "");
    fake->body = "{}";
    Make(fake).Members(g.H());
    t.Ok(g.reply.Ok() && g.reply.value.IsArray() && g.reply.value.Size() == 0, "no rows: an empty array");
    fake->body = "{\"data\":null}";
    Make(fake).Members(g.H());
    t.Ok(g.reply.Ok() && g.reply.value.IsArray() && g.reply.value.Size() == 0, "rows that are not a list: an empty array");
}

OW_TEST(client_health_and_world_are_the_bodies_as_served) {
    auto fake = Fake("{\"status\":\"ok\",\"service\":\"keel\"}");
    Got<OWJsonValue> g;
    Make(fake).Health(g.H());
    t.Eq(fake->Last().url, Base + "/health/", "");
    t.Eq(TextOf(g.reply.value, "service"), "keel", "");
    fake->body = "{\"id\":\"w1\",\"name\":\"W\",\"time_range_current\":12}";
    Make(fake).GetWorld(g.H());
    t.Eq(fake->Last().url, Base + "/world/", "");
    t.Eq(TextOf(g.reply.value, "time_range_current"), "12", "as parsed");
}

// -- The change feed --------------------------------------------------------------------------------------------------------

OW_TEST(client_changes_page) {
    auto fake = Fake(
        "{\"changes\":[{\"op\":\"upsert\",\"type\":\"character\",\"id\":\"c1\",\"change_seq\":41,\"updated_at\":"
        "\"2026-10-30T10:00:00Z\",\"element\":{\"id\":\"c1\",\"name\":\"Ana\",\"level\":2}},{\"op\":\"delete\",\"type\":"
        "\"pin\",\"id\":\"p1\",\"change_seq\":42,\"deleted_at\":\"2026-10-30T11:00:00Z\",\"deleted_by\":\"m1\","
        "\"deleted_via\":\"api\",\"deleted_client\":\"atlas\",\"reason\":\"hidden\",\"element\":null}],\"cursor\":"
        "\"4:abc\",\"has_more\":true,\"head\":42,\"epoch\":3,\"epoch_reason\":\"view\"}");
    OWClientConfig config;
    config.api_key = "ow_r_k";
    config.base_url = Base;
    config.transport = fake;
    config.page_size = 5000;
    OWClient c(config);
    OWChangesOptions o;
    o.since = "4:a b";
    o.after = "2026-10-22T00:00:00Z";
    Got<OWChangesPage> g;
    c.Changes(o, g.H());
    t.Eq(fake->Last().url, Base + "/changes/?since=4%3Aa%20b&limit=1000&after=2026-10-22T00%3A00%3A00Z",
         "since, limit (the page size, at most 1000), after");
    t.Eq(HeaderNames(fake->Last()), "API-Key", "a feed page sends no tag");
    const OWChangesPage& page = g.reply.value;
    t.Eq(page.cursor, "4:abc", "");
    t.Ok(page.has_more, "");
    t.Eq(page.head, int64_t(42), "");
    t.Eq(page.epoch.value_or(-1), int64_t(3), "");
    t.Eq(page.epoch_reason.value_or(""), "view", "");
    t.Ok(!page.unchanged, "a page is never unchanged");
    t.Eq(page.changes.size(), size_t(2), "");
    if (page.changes.size() == 2) {
        const OWChange& up = page.changes[0];
        t.Eq(up.op + " " + up.type + " " + up.id, "upsert character c1", "");
        t.Eq(up.change_seq, int64_t(41), "");
        t.Eq(up.updated_at.value_or(""), "2026-10-30T10:00:00Z", "");
        t.Ok(up.element && up.element->Find("level") && up.element->Find("level")->Text() == "2", "an upsert's element");
        t.Ok(!up.deleted_at && !up.reason && !up.changed_at, "an upsert has no delete's keys");
        const OWChange& del = page.changes[1];
        t.Eq(del.deleted_at.value_or("") + " " + del.deleted_by.value_or("") + " " + del.deleted_via.value_or("") + " " +
                 del.deleted_client.value_or("") + " " + del.reason.value_or(""),
             "2026-10-30T11:00:00Z m1 api atlas hidden", "a delete's row");
        t.Ok(!del.element.has_value(), "a delete carries no element (null is none)");
    }
    c.Changes({}, g.H());
    t.Eq(fake->Last().url, Base + "/changes/?limit=1000", "from the start");
    Make(fake).Changes({}, g.H());
    t.Eq(fake->Last().url, Base + "/changes/?limit=100", "the page size");
    fake->body = "{\"changes\":{},\"head\":1}";
    Make(fake).Changes({}, g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, "changes that are not a list");
    fake->body = "{\"changes\":[7],\"head\":1}";
    Make(fake).Changes({}, g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, "a row that is not a change");
}

OW_TEST(client_changes_numbers_keep_their_text) {
    auto fake = Fake("{\"changes\":[],\"cursor\":17,\"has_more\":false,\"head\":9007199254740993,\"epoch\":null}");
    Got<OWChangesPage> g;
    Make(fake).Changes({}, g.H());
    t.Eq(g.reply.value.cursor, "17", "a cursor served as a number is its text, never 17.0");
    t.Eq(g.reply.value.head, int64_t(9007199254740993LL), "a head past 2^53 read whole");
    t.Ok(!g.reply.value.epoch && !g.reply.value.epoch_reason, "no epoch for this key");
}

OW_TEST(client_changes_head_polls_with_its_tag) {
    auto fake = Fake();
    OWClient c = Make(fake);
    fake->Then(200, "{\"head\":9,\"cursor\":9,\"epoch\":null,\"epoch_reason\":null}", {{"etag", "W/\"9:-\""}});
    fake->Then(304, "");
    fake->Then(200, "{\"head\":10,\"cursor\":\"10\",\"epoch\":2,\"epoch_reason\":\"role\"}", {{"ETag", "\"10:2\""}});
    Got<OWChangesPage> g;
    c.ChangesHead(g.H());
    t.Eq(fake->Last().url, Base + "/changes/?head=true", "head=true alone");
    t.Eq(HeaderNames(fake->Last()), "API-Key", "nothing held: no tag, and no PIN on a read");
    const OWChangesPage& p = g.reply.value;
    t.Ok(p.head == 9 && p.cursor == "9" && !p.epoch && !p.unchanged, "the tip");
    c.ChangesHead(g.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "W/\"9:-\"", "the tag sent back exactly as received");
    t.Ok(g.reply.Ok() && p.head == 9 && p.cursor == "9" && !p.epoch && p.unchanged, "a 304: the last 200's tip");
    t.Ok(g.reply.response && g.reply.response->status == 304, "");
    c.ChangesHead(g.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "W/\"9:-\"", "");
    t.Ok(p.head == 10 && p.cursor == "10" && p.epoch.value_or(-1) == 2 && !p.unchanged, "moved");
    t.Eq(p.epoch_reason.value_or(""), "role", "a 200 reads why the epoch moved");
    fake->Then(304, "");
    c.ChangesHead(g.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "\"10:2\"", "the newest tag, read case-insensitively");
    t.Ok(p.unchanged && p.head == 10 && p.epoch.value_or(-1) == 2 && !p.epoch_reason, "a 304 hands back no reason");
    fake->Then(200, "{\"head\":11,\"cursor\":\"11\"}");
    c.ChangesHead(g.H());
    fake->Then(200, "{\"head\":11,\"cursor\":\"11\"}");
    c.ChangesHead(g.H());
    t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "<absent>", "a 200 with no tag: none held");
}

OW_TEST(client_changes_head_304_with_nothing_held_is_an_error) {
    auto fake = Fake();
    fake->Then(304, "");
    Got<OWChangesPage> g;
    Make(fake).ChangesHead(g.H());
    t.Ok(g.reply.error && g.reply.error->status == 304 && g.reply.error->kind == OWApiError::Kind::Api,
         "never a made-up head");
    fake->Then(304, "");
    Make(fake).Changes({}, g.H());
    t.Ok(g.reply.error && g.reply.error->status == 304, "a feed page sent no tag: a 304 is keel's error");
}

OW_TEST(client_resync_required_drops_the_tag) {
    const char* resync =
        "{\"error\":{\"type\":\"conflict\",\"code\":\"resync_required\",\"message\":\"walk again\",\"reason\":\"role\"}}";
    for (bool from_page : {false, true}) {
        auto fake = Fake();
        OWClient c = Make(fake);
        fake->Then(200, "{\"head\":9,\"cursor\":\"9\"}", {{"etag", "\"9\""}});
        fake->Then(409, resync);
        fake->Then(200, "{\"head\":9,\"cursor\":\"9\"}", {{"etag", "\"9\""}});
        Got<OWChangesPage> g;
        c.ChangesHead(g.H());
        if (from_page) {
            OWChangesOptions o;
            o.since = "9";
            c.Changes(o, g.H());
        } else {
            c.ChangesHead(g.H());
        }
        std::string n = from_page ? "from a page" : "from a tip read";
        t.Ok(g.reply.error && g.reply.error->IsResyncRequired(), n + ": the 409");
        t.Eq(g.reply.error ? g.reply.error->ResyncReason().value_or("") : "", "role", n + ": its reason");
        c.ChangesHead(g.H());
        t.Eq(HeaderOr(fake->Last(), "If-None-Match"), "<absent>", n + ": the next tip read is a full one");
    }
}

// -- keel's error envelope ----------------------------------------------------------------------------------------------------

OW_TEST(client_the_error_envelope) {
    auto fake = Fake();
    fake->Then(404,
               "{\"error\":{\"type\":\"invalid_request\",\"code\":\"not_found\",\"message\":\"No such character.\","
               "\"param\":\"id\",\"doc_url\":\"https://onlyworlds.com/docs/errors#not_found\"}}");
    Got<OWJsonValue> g;
    Make(fake).Get("character", "c9", {}, g.H());
    t.Ok(g.reply.error.has_value(), "an error");
    if (!g.reply.error) return;
    const OWApiError& e = *g.reply.error;
    t.Ok(e.kind == OWApiError::Kind::Api, "keel's no");
    t.Eq(e.status, 404, "");
    t.Eq(e.type + "|" + e.code + "|" + e.message + "|" + e.param + "|" + e.doc_url,
         "invalid_request|not_found|No such character.|id|https://onlyworlds.com/docs/errors#not_found", "");
    t.Ok(e.IsNotFound(), "");
    t.Eq(e.ToString(), "OnlyWorlds API 404 [not_found] (field: id): No such character.", "the line it prints");
    t.Ok(g.reply.response && g.reply.response->status == 404, "the response beside it");
}

OW_TEST(client_error_shapes_and_helpers) {
    struct Case {
        int status;
        const char* body;
        OWHeaders headers;
        const char* code;
        const char* message;
        std::function<bool(const OWApiError&)> holds;
    };
    std::vector<Case> cases = {
        {401, "{\"code\":\"invalid_credentials\",\"message\":\"bad key\"}", {}, "invalid_credentials", "bad key",
         [](const OWApiError& e) { return e.IsAuthError(); }},
        {401, "{\"error\":{\"code\":\"key_revoked\"}}", {}, "key_revoked", "", [](const OWApiError& e) { return e.IsAuthError(); }},
        {413, "{\"error\":\"too_large\"}", {}, "too_large", "", nullptr},
        {422, "{\"error\":{\"code\":\"invalid_request\",\"param\":\"as\"}}", {}, "invalid_request", "",
         [](const OWApiError& e) { return e.IsValidationError() && e.param == "as"; }},
        {400, "{\"error\":{\"code\":\"bad\"}}", {}, "bad", "", [](const OWApiError& e) { return e.IsValidationError(); }},
        {403, "{\"error\":{\"code\":\"owner_only\"}}", {}, "owner_only", "", [](const OWApiError& e) { return e.IsOwnerOnly(); }},
        {403, "{\"error\":{\"code\":\"view_as_owner_only\"}}", {}, "view_as_owner_only", "",
         [](const OWApiError& e) { return e.IsViewAsOwnerOnly() && !e.IsOwnerOnly(); }},
        {503, "{\"error\":{\"code\":\"server_busy\"}}", {{"Retry-After", " 12 "}}, "server_busy", "",
         [](const OWApiError& e) { return e.IsRetryable() && e.retry_after.value_or(-1) == 12; }},
        {409, "{\"error\":{\"code\":\"idempotency_in_flight\"}}", {}, "idempotency_in_flight", "",
         [](const OWApiError& e) { return e.IsRetryable(); }},
        {429, "{\"error\":{\"code\":\"quota_exceeded\"}}", {}, "quota_exceeded", "",
         [](const OWApiError& e) { return !e.IsRetryable(); }},
        {503, "{\"error\":{\"code\":\"ask_unavailable\"}}", {}, "ask_unavailable", "",
         [](const OWApiError& e) { return !e.IsRetryable(); }},
        {409, "{\"error\":{\"code\":\"stale_proposal\"}}", {}, "stale_proposal", "",
         [](const OWApiError& e) { return !e.IsRetryable(); }},
        {404, "{}", {}, "", "", [](const OWApiError& e) { return !e.IsNotFound(); }},
        {409, "{\"error\":{\"code\":\"resync_required\",\"reason\":\"other\"}}", {}, "resync_required", "",
         [](const OWApiError& e) { return e.IsResyncRequired() && !e.ResyncReason(); }},
        {409, "{\"error\":{\"code\":\"resync_required\",\"reason\":\"view\"}}", {}, "resync_required", "",
         [](const OWApiError& e) { return e.ResyncReason().value_or("") == "view"; }},
        {409, "{\"error\":{\"code\":\"resync_required\"}}", {}, "resync_required", "",
         [](const OWApiError& e) { return !e.ResyncReason(); }},
        {409, "{\"error\":{\"code\":\"other\",\"reason\":\"view\"}}", {}, "other", "",
         [](const OWApiError& e) { return !e.ResyncReason(); }},
        {500, "{\"error\":{\"code\":17,\"message\":null}}", {}, "17", "", nullptr},
    };
    for (const Case& k : cases) {
        auto fake = Fake();
        fake->Then(k.status, k.body, k.headers);
        Got<OWJsonValue> g;
        Make(fake).GetWorld(g.H());
        if (!g.reply.error) {
            t.Fail(std::string(k.body) + ": no error");
            continue;
        }
        const OWApiError& e = *g.reply.error;
        t.Eq(std::to_string(e.status) + "|" + e.code + "|" + e.message, std::to_string(k.status) + "|" + k.code + "|" + k.message,
             k.body);
        t.Eq(e.raw_body, k.body, std::string(k.body) + ": the raw body kept");
        if (k.holds) t.Ok(k.holds(e), std::string(k.body) + ": its helper");
    }
    std::string html = "<html>" + std::string(300, 'x') + "</html>";
    auto fake = Fake();
    fake->Then(502, html);
    Got<OWJsonValue> g;
    Make(fake).GetWorld(g.H());
    t.Ok(g.reply.error.has_value(), "");
    if (g.reply.error) {
        t.Eq(g.reply.error->message, html.substr(0, 200) + "...", "a proxy's page, not the envelope: its first 200");
        t.Eq(g.reply.error->code, "", "");
        t.Ok(g.reply.error->IsRetryable(), "a 5xx is retryable");
        t.Eq(g.reply.error->ToString(), "OnlyWorlds API 502: " + html.substr(0, 200) + "...", "");
    }
    // Characters, not bytes: a cut never splits a UTF-8 sequence.
    std::string wide;
    for (int i = 0; i < 250; ++i) wide += "\xC3\xA9";
    fake->Then(502, wide);
    Make(fake).GetWorld(g.H());
    std::string want;
    for (int i = 0; i < 200; ++i) want += "\xC3\xA9";
    t.Eq(g.reply.error ? g.reply.error->message : "", want + "...", "200 characters of a body that is not JSON");
    fake->Then(500, "[\"x\"]");
    Make(fake).GetWorld(g.H());
    t.Eq(g.reply.error ? g.reply.error->message : "", "[\"x\"]", "JSON that is not an object: the body as the message");
    fake->Then(500, "");
    Make(fake).GetWorld(g.H());
    t.Eq(g.reply.error ? g.reply.error->message : "?", "", "no body: no message");
    t.Eq(g.reply.error ? g.reply.error->ToString() : "", "OnlyWorlds API 500", "");
    t.Eq(OWApiError::Of(OWApiError::Kind::Refused, "list: no. Nothing was sent.").ToString(),
         "OnlyWorlds refused: list: no. Nothing was sent.", "the client's own error prints its kind");
}

OW_TEST(client_retry_after) {
    const int64_t now = 1792567670;  // 2026-10-21T07:27:50Z
    t.Eq(OWApiError::ParseRetryAfter("Wed, 21 Oct 2026 07:28:00 GMT", now).value_or(-1), 10, "an HTTP date, seconds from now");
    t.Eq(OWApiError::ParseRetryAfter("Wed, 21 Oct 2026 07:28:00 GMT", now + 20).value_or(-1), 0, "a date passed: now");
    t.Eq(OWApiError::ParseRetryAfter("Thu, 01 Jan 1970 00:00:00 GMT", 0).value_or(-1), 0, "the epoch");
    t.Eq(OWApiError::ParseRetryAfter("Sat, 29 Feb 2028 00:00:01 GMT", 1835395200).value_or(-1), 1, "a leap day");
    t.Eq(OWApiError::ParseRetryAfter("7", now).value_or(-1), 7, "seconds");
    t.Eq(OWApiError::ParseRetryAfter(" 7 ", now).value_or(-1), 7, "trimmed");
    t.Eq(OWApiError::ParseRetryAfter("0", now).value_or(-1), 0, "");
    t.Eq(OWApiError::ParseRetryAfter("99999999999999999999", now).value_or(-1), 2147483647, "past 32 bits: the largest");
    t.Eq(OWApiError::ParseRetryAfter("2147483648", now).value_or(-1), 2147483647, "");
    for (const char* bad : {"soon", "-1", "+5", "1.5", "", "  ", "Wed, 32 Oct 2026 07:28:00 GMT",
                            "Wed, 21 Okt 2026 07:28:00 GMT", "Wed, 21 Oct 2026 07:28:00 UTC", "Wed 21 Oct 2026 07:28:00 GMT",
                            "Wed, 21 Oct 2026 25:28:00 GMT"})
        t.Ok(!OWApiError::ParseRetryAfter(bad, now).has_value(), std::string("'") + bad + "': none");
}

OW_TEST(client_a_transport_failure_is_no_answer) {
    auto fake = Fake();
    fake->Fail("can't connect");
    Got<OWJsonValue> g;
    Make(fake).GetWorld(g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Transport && g.reply.error->status == 0,
         "not keel's no");
    if (g.reply.error) {
        t.Ok(g.reply.error->message.find("GET /world/") != std::string::npos &&
                 g.reply.error->message.find("can't connect") != std::string::npos,
             g.reply.error->message);
        t.Ok(!g.reply.error->IsRetryable(), "no answer is the caller's to retry, not a status's");
    }
    t.Ok(g.reply.response && g.reply.response->failure == "can't connect", "the transport's answer beside it");
}

OW_TEST(transport_header_lookup_ignores_case) {
    OWHeaders h = {{"etag", "\"1\""}, {"Retry-After", "3"}, {"ETAG", "\"2\""}};
    t.Eq(OWHeader(h, "ETag") ? *OWHeader(h, "ETag") : "", "\"1\"", "the first by any case");
    t.Eq(OWHeader(h, "retry-after") ? *OWHeader(h, "retry-after") : "", "3", "");
    t.Ok(OWHeader(h, "If-None-Match") == nullptr, "absent");
    t.Ok(OWHeader(h, "eta") == nullptr, "a prefix is not the name");
    OWResponse r;
    r.headers = h;
    t.Ok(r.Header("ETAG") != nullptr, "");
    t.Ok(!r.IsSuccess(), "status 0 is no success");
    r.status = 299;
    t.Ok(r.IsSuccess(), "");
}
