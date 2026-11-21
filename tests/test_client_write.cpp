// OWClient's element writes through a fake transport (tests/fake_transport.h): no test touches the network. Each
// write's request (method, URL, query, headers, body) on each credential, each refusal before sending, the answers
// (an element, a proposal, a bulk's slots, nothing on a delete), keel's error envelope and the write helpers, and
// Sanitize's cases. The rules are the Unity SDK's OWClient (CreateAsync, UpsertAsync, PatchAsync, DeleteAsync,
// EditLinksAsync, BulkAsync, Sanitize, RefuseKnownOf, RequestWithResponseAsync), held call by call as its tests hold
// them; the helpers' names are the TypeScript SDK's.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"
#include "OWFiles.h"
#include "OWId.h"
#include "OWJson.h"
#include "OWLens.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;
using owtest::P;

namespace {

// A bulk slot's id: a string, or null when the slot answers none. The overloads read either spelling of the field, so
// the null test could be seen red against a reader that turned null into "".
template <typename Id>
bool IdIsNull(const Id& id) {
    if constexpr (std::is_same_v<Id, std::string>) return false;
    else return !id;
}
template <typename Id>
std::string IdText(const Id& id) {
    if constexpr (std::is_same_v<Id, std::string>) return id;
    else return id.value_or("<null>");
}

const std::string Base = "https://example.test/api/v2";
const char* const Element = "{\"id\":\"c1\",\"name\":\"Ana\",\"type\":\"character\",\"change_seq\":4}";

std::shared_ptr<FakeTransport> Fake(const std::string& body = Element) {
    auto f = std::make_shared<FakeTransport>();
    f->body = body;
    return f;
}

OWClientConfig Config(const std::shared_ptr<FakeTransport>& fake, const std::string& key = "ow_w_test",
                      const std::string& pin = "1234", std::optional<std::string> world = std::nullopt) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = pin;
    config.world = std::move(world);
    config.base_url = Base;
    config.transport = fake;
    return config;
}

OWClient Make(const std::shared_ptr<FakeTransport>& fake, const std::string& key = "ow_w_test",
              const std::string& pin = "1234", std::optional<std::string> world = std::nullopt) {
    return OWClient(Config(fake, key, pin, std::move(world)));
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
    bool Refused() const { return reply.error && reply.error->kind == OWApiError::Kind::Refused; }
    std::string Why() const { return reply.error ? reply.error->ToString() : "no error"; }
};

OWJsonValue Parse(std::string_view text) { return OWJson::Parse(text).value; }

// A body sent, parsed (null when there was none or it is not JSON).
OWJsonValue BodyOf(const OWRequest& r) { return r.body ? OWJson::Parse(*r.body).value : OWJsonValue(); }

std::string TextOf(const OWJsonValue& v, std::string_view key) {
    const OWJsonValue* m = v.Find(key);
    return m ? m->Text() : "<absent>";
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
std::string KeysOf(const OWJsonValue& v) {
    std::string out;
    for (const std::string& k : v.Keys()) out += (out.empty() ? "" : ",") + k;
    return out;
}
bool EndsNothingSent(const std::string& m) {
    return m.size() >= 17 && m.compare(m.size() - 17, 17, "Nothing was sent.") == 0;
}
bool IsV7(const std::string& id) { return id.size() == 36 && id[14] == '7' && id[8] == '-'; }

// -- The six writes, swept over the credentials ------------------------------------------------------------------------

struct Outcome {
    int calls = 0;
    std::optional<OWApiError> error;
};
template <typename T>
Outcome OutOf(const Got<T>& g) {
    return Outcome{g.calls, g.reply.error};
}

struct WriteCall {
    const char* name;
    const char* method;
    const char* path;  // under Base
    std::function<Outcome(OWClient&)> run;
};

const std::vector<WriteCall>& Writes() {
    static const std::vector<WriteCall> writes = {
        {"create", "POST", "/character/",
         [](OWClient& c) { Got<OWJsonValue> g; c.Create("character", Parse("{\"name\":\"Ana\"}"), {}, g.H()); return OutOf(g); }},
        {"upsert", "PUT", "/character/c1/",
         [](OWClient& c) { Got<OWJsonValue> g; c.Upsert("character", "c1", Parse("{\"name\":\"Ana\"}"), g.H()); return OutOf(g); }},
        {"patch", "PATCH", "/character/c1/",
         [](OWClient& c) { Got<OWJsonValue> g; c.Patch("character", "c1", Parse("{\"name\":\"Bo\"}"), {}, g.H()); return OutOf(g); }},
        {"delete", "DELETE", "/character/c1/",
         [](OWClient& c) { Got<OWJsonValue> g; c.Delete("character", "c1", {}, g.H()); return OutOf(g); }},
        {"edit links", "POST", "/character/c1/links/friends",
         [](OWClient& c) {
             OWLinkEdit e;
             e.add = std::vector<std::string>{"c2"};
             Got<OWJsonValue> g;
             c.EditLinks("character", "c1", "friends", e, {}, g.H());
             return OutOf(g);
         }},
        {"bulk", "POST", "/bulk/",
         [](OWClient& c) {
             Got<OWBulkResult> g;
             c.Bulk({{"character", Parse("{\"name\":\"A\"}")}}, {}, g.H());
             return OutOf(g);
         }},
    };
    return writes;
}

// Every write on its own client of this credential: the request each sent, in order.
std::vector<OWRequest> SweepWrites(owtest::T& t, const OWClientConfig& base, const std::string& body = Element) {
    std::vector<OWRequest> sent;
    for (const WriteCall& w : Writes()) {
        auto fake = Fake(w.name == std::string("bulk") ? "{\"errors\":false,\"items\":[]}" : body);
        OWClientConfig config = base;
        config.transport = fake;
        OWClient c(config);
        Outcome o = w.run(c);
        t.Eq(o.calls, 1, std::string(w.name) + ": the handler called once");
        t.Ok(!o.error, std::string(w.name) + ": answered (" + (o.error ? o.error->ToString() : "") + ")");
        t.Eq(fake->calls.size(), size_t(1), std::string(w.name) + ": one request");
        if (fake->calls.size() != 1) continue;
        t.Eq(fake->Last().method, w.method, std::string(w.name) + ": its method");
        t.Eq(fake->Last().url, Base + w.path, std::string(w.name) + ": its URL, no query");
        sent.push_back(fake->Last());
    }
    return sent;
}

}  // namespace

// -- The config: the client's name and the agent ---------------------------------------------------------------------------

OW_TEST(client_write_config_names_the_client_by_keels_rule) {
    auto fake = Fake();
    OWClient plain = Make(fake);
    t.Eq(plain.ClientName(), "unreal-sdk", "the default name");
    t.Eq(std::string(OWClient::DefaultClientName), "unreal-sdk", "");
    t.Ok(!plain.Agent(), "not an agent unless declared");
    for (const char* ok : {"my-game", "a", "0123456789abcdefghijklmnopqrstuv", "atlas"}) {
        OWClientConfig config = Config(fake);
        config.client = ok;
        OWClient c(config);
        t.Eq(c.Error(), "", std::string(ok) + ": built");
        t.Eq(c.ClientName(), ok, std::string(ok) + ": kept as given");
    }
    for (const char* bad : {"", "Atlas", "atlas-1.76", "my game", "atlas\n", " atlas", "0123456789abcdefghijklmnopqrstuvw",
                            "atl\xC3\xA1s", "my_game"}) {
        OWClientConfig config = Config(fake);
        config.client = bad;
        OWClient c(config);
        t.Ok(c.Error().find("[a-z0-9-]{1,32}") != std::string::npos, OWJson::Quote(bad) + ": refused (" + c.Error() + ")");
        Got<OWJsonValue> g;
        c.Me(g.H());
        t.Ok(g.Refused(), OWJson::Quote(bad) + ": every call refused");
    }
    t.Eq(fake->calls.size(), size_t(0), "nothing sent");
    OWClientConfig agent = Config(fake);
    agent.agent = true;
    t.Ok(OWClient(agent).Agent(), "declared an agent");
}

// -- Each write's request ------------------------------------------------------------------------------------------------

OW_TEST(client_write_create_sends_the_body_cleaned_with_a_minted_id) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    OWJsonValue ana = Parse("{\"name\":\"Ana\",\"world\":\"w1\",\"type\":\"character\",\"created_at\":\"t\","
                            "\"updated_at\":\"t\",\"change_seq\":4,\"known_of\":false,\"background\":null,"
                            "\"birthplace\":null,\"level\":null,\"x_tool\":1}");
    OWJsonValue before = ana;
    c.Create("character", ana, {}, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(g.calls, 1, "answered once");
    const OWRequest& r = fake->Last();
    t.Eq(r.method, "POST", "");
    t.Eq(r.url, Base + "/character/", "POST /{type}/, no query");
    t.Eq(HeaderNames(r), "API-Key,API-Pin,Content-Type,X-OW-Client", "the headers, in the Unity SDK's order");
    t.Eq(HeaderOr(r, "API-Pin"), "1234", "the PIN on a write");
    t.Eq(HeaderOr(r, "Content-Type"), "application/json", "");
    t.Eq(HeaderOr(r, "X-OW-Client"), "unreal-sdk", "the client's name");
    OWJsonValue body = BodyOf(r);
    t.Eq(KeysOf(body), "name,background,birthplace,level,x_tool,id", "the read-only six left out, the order kept, the id last");
    t.Eq(TextOf(body, "background"), "", "a null string field sent as \"\"");
    t.Ok(body.Find("background") && body.Find("background")->IsString(), "as a string");
    t.Ok(body.Find("birthplace") && body.Find("birthplace")->IsNull(), "a single link's null kept");
    t.Ok(body.Find("level") && body.Find("level")->IsNull(), "a number's null kept");
    t.Eq(TextOf(body, "x_tool"), "1", "an extension key untouched");
    t.Ok(IsV7(TextOf(body, "id")), "a UUIDv7 minted: " + TextOf(body, "id"));
    t.Ok(ana == before, "the caller's element is not changed");
    t.Eq(TextOf(g.reply.value, "id"), "c1", "the element as keel wrote it");
}

OW_TEST(client_write_create_keeps_an_id_given_and_mints_for_none) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Create("character", Parse("{\"id\":\"c9\",\"name\":\"A\"}"), {}, g.H());
    t.Eq(TextOf(BodyOf(fake->Last()), "id"), "c9", "an id given is kept");
    c.Create("character", Parse("{\"id\":null,\"name\":\"A\"}"), {}, g.H());
    t.Ok(IsV7(TextOf(BodyOf(fake->Last()), "id")), "a null id: minted");
    t.Eq(KeysOf(BodyOf(fake->Last())), "id,name", "in its place");
    c.Create("character", Parse("{\"id\":\"\",\"name\":\"A\"}"), {}, g.H());
    t.Ok(IsV7(TextOf(BodyOf(fake->Last()), "id")), "an empty id: minted");
    std::string first = TextOf(BodyOf(fake->Last()), "id");
    c.Create("character", Parse("{\"name\":\"A\"}"), {}, g.H());
    t.Ok(TextOf(BodyOf(fake->Last()), "id") != first, "without a key, a fresh id each call");
}

OW_TEST(client_write_create_under_a_key_sends_it_and_the_keyed_id) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    OWCreateOptions keyed;
    keyed.idempotency_key = "retry-1";
    c.Create("character", Parse("{\"name\":\"A\"}"), keyed, g.H());
    const OWRequest first = fake->Last();
    t.Eq(HeaderNames(first), "API-Key,API-Pin,Content-Type,Idempotency-Key,X-OW-Client", "");
    t.Eq(HeaderOr(first, "Idempotency-Key"), "retry-1", "");
    t.Eq(TextOf(BodyOf(first), "id"), "1b88c4e4-831f-503c-8149-cece1519ca32", "the id derived from the key, index 0");
    c.Create("character", Parse("{\"name\":\"A\"}"), keyed, g.H());
    t.Ok(first.body == fake->Last().body, "a retry under the key sends the same bytes");
    c.Create("character", Parse("{\"name\":\"A\"}"), {}, g.H());
    t.Eq(HeaderOr(fake->Last(), "Idempotency-Key"), "<absent>", "no key, no header");
}

OW_TEST(client_write_upsert_puts_the_element_whole) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Upsert("character", "c1",
             Parse("{\"id\":\"c1\",\"name\":\"Ana\",\"type\":\"character\",\"change_seq\":4,\"friends\":[\"c2\"]}"), g.H());
    t.Ok(g.reply.Ok(), g.Why());
    const OWRequest& r = fake->Last();
    t.Eq(r.method, "PUT", "");
    t.Eq(r.url, Base + "/character/c1/", "");
    t.Eq(HeaderNames(r), "API-Key,API-Pin,Content-Type,X-OW-Client", "no Idempotency-Key or If-Match: keel declares neither here");
    t.Eq(r.body.value_or(""), "{\"id\":\"c1\",\"name\":\"Ana\",\"friends\":[\"c2\"]}", "compact, cleaned, its id as given");
}

OW_TEST(client_write_patch_sends_what_changed_and_if_match) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Patch("character", "c1", Parse("{\"name\":\"Bo\"}"), {}, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(fake->Last().method, "PATCH", "a PATCH, never a PUT");
    t.Eq(fake->Last().url, Base + "/character/c1/", "");
    t.Eq(fake->Last().body.value_or(""), "{\"name\":\"Bo\"}", "only what changed");
    t.Eq(HeaderOr(fake->Last(), "If-Match"), "<absent>", "unconditional without if_match");
    OWWriteOptions when;
    when.if_match = 4;
    c.Patch("character", "c1", Parse("{\"name\":\"Bo\"}"), when, g.H());
    t.Eq(HeaderNames(fake->Last()), "API-Key,API-Pin,Content-Type,X-OW-Client,If-Match", "");
    t.Eq(HeaderOr(fake->Last(), "If-Match"), "\"4\"", "the change_seq, quoted");
    when.if_match = 0;
    c.Patch("character", "c1", Parse("{}"), when, g.H());
    t.Eq(HeaderOr(fake->Last(), "If-Match"), "\"0\"", "zero is a change_seq");
    t.Eq(fake->Last().body.value_or(""), "{}", "an empty patch is an empty object");
}

OW_TEST(client_write_delete_sends_no_body_and_answers_null) {
    auto fake = Fake();
    fake->Then(204, "");
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    OWWriteOptions when;
    when.if_match = 7;
    c.Delete("character", "c1", when, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Ok(g.reply.value.IsNull(), "no value");
    const OWRequest& r = fake->Last();
    t.Eq(r.method, "DELETE", "");
    t.Eq(r.url, Base + "/character/c1/", "");
    t.Ok(!r.body.has_value(), "no body");
    t.Eq(HeaderNames(r), "API-Key,API-Pin,X-OW-Client,If-Match", "no Content-Type without a body");
    t.Eq(HeaderOr(r, "If-Match"), "\"7\"", "");
    fake->Then(200, "  ");
    c.Delete("character", "c1", {}, g.H());
    t.Ok(g.reply.Ok() && g.reply.value.IsNull(), "a 200 with white space: null (" + g.Why() + ")");
}

OW_TEST(client_write_edit_links_sends_add_and_remove_as_given) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    OWLinkEdit both;
    both.add = std::vector<std::string>{"c2"};
    both.remove = std::vector<std::string>{"c3", "c4"};
    OWWriteOptions when;
    when.if_match = 4;
    c.EditLinks("character", "c1", "friends", both, when, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    t.Eq(fake->Last().method, "POST", "");
    t.Eq(fake->Last().url, Base + "/character/c1/links/friends", "no trailing slash, as keel's route");
    t.Eq(fake->Last().body.value_or(""), "{\"add\":[\"c2\"],\"remove\":[\"c3\",\"c4\"]}", "");
    t.Eq(HeaderOr(fake->Last(), "If-Match"), "\"4\"", "");
    OWLinkEdit add;
    add.add = std::vector<std::string>{"c2"};
    c.EditLinks("character", "c1", "friends", add, {}, g.H());
    t.Eq(fake->Last().body.value_or(""), "{\"add\":[\"c2\"]}", "remove absent: not sent");
    OWLinkEdit empty;
    empty.remove = std::vector<std::string>{};
    c.EditLinks("character", "c1", "friends", empty, {}, g.H());
    t.Eq(fake->Last().body.value_or(""), "{\"remove\":[]}", "an empty list given is sent");
    c.EditLinks("character", "c1", "friends", {}, {}, g.H());
    t.Eq(fake->Last().body.value_or(""), "{}", "neither: an empty object");
}

OW_TEST(client_write_bulk_sends_each_item_cleaned_with_ids_by_position) {
    auto fake = Fake("{\"errors\":false,\"items\":[]}");
    OWClient c = Make(fake);
    Got<OWBulkResult> g;
    OWBulkOptions o;
    o.atomic = true;
    o.idempotency_key = "retry-1";
    c.Bulk({{"character", Parse("{\"name\":\"A\",\"updated_at\":\"t\",\"background\":null}")},
            {"location", Parse("{\"name\":\"Port\",\"parent_location\":null}")},
            {"character", Parse("{\"id\":\"c9\",\"name\":\"C\"}")},
            {"character", OWJsonValue()}},
           o, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    const OWRequest& r = fake->Last();
    t.Eq(r.method, "POST", "");
    t.Eq(r.url, Base + "/bulk/", "");
    t.Eq(HeaderNames(r), "API-Key,API-Pin,Content-Type,Idempotency-Key,X-OW-Client", "");
    t.Eq(HeaderOr(r, "Idempotency-Key"), "retry-1", "");
    t.Eq(r.body.value_or(""),
         "{\"items\":[{\"type\":\"character\",\"element\":{\"name\":\"A\",\"background\":\"\",\"id\":"
         "\"1b88c4e4-831f-503c-8149-cece1519ca32\"}},{\"type\":\"location\",\"element\":{\"name\":\"Port\","
         "\"parent_location\":null,\"id\":\"2f7b4a01-0c9b-5b1e-8602-270abbee8184\"}},{\"type\":\"character\","
         "\"element\":{\"id\":\"c9\",\"name\":\"C\"}},{\"type\":\"character\",\"element\":{\"id\":"
         "\"44bb129d-48b9-5fc2-b3c9-cc5f549e3e6e\"}}],\"atomic\":true}",
         "each item cleaned, keyed by its position, an id given kept, a null element an empty one");
    c.Bulk({{"character", Parse("{\"name\":\"A\"}")}}, {}, g.H());
    OWJsonValue body = BodyOf(fake->Last());
    t.Ok(body.Find("atomic") && body.Find("atomic")->IsBool() && !body.Find("atomic")->AsBool(), "atomic false by default, sent");
    t.Eq(HeaderOr(fake->Last(), "Idempotency-Key"), "<absent>", "no key, no header");
    const OWJsonValue* items = body.Find("items");
    t.Ok(items && items->Size() == 1 && IsV7(TextOf(*items->Items()[0].Find("element"), "id")), "a fresh v7 without a key");
}

// The TypeScript SDK 4.185.0's vector: create_only true is sent as create_only; left out or false, the body is the one
// earlier versions sent. Its refusal of a value that is not a boolean has no counterpart: OWBulkOptions::create_only is
// a bool. Kills: create_only never sent, sent always, or sent as false.
OW_TEST(client_write_bulk_create_only_is_sent_when_true_and_left_out_otherwise) {
    auto fake = Fake("{\"errors\":false,\"items\":[]}");
    OWClient c = Make(fake);
    Got<OWBulkResult> g;
    std::vector<OWBulkItem> items = {{"character", Parse("{\"id\":\"mine\",\"name\":\"A\"}")}};
    OWBulkOptions on;
    on.create_only = true;
    c.Bulk(items, on, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    OWJsonValue body = BodyOf(fake->Last());
    t.Ok(body.Keys() == std::vector<std::string>{"items", "atomic", "create_only"}, "items, atomic, create_only: " +
                                                                                     fake->Last().body.value_or(""));
    t.Ok(body.Find("create_only") && body.Find("create_only")->IsBool() && body.Find("create_only")->AsBool(),
         "create_only a JSON true");
    for (bool given : {false, true}) {
        OWBulkOptions o;
        if (given) o.create_only = false;
        c.Bulk(items, o, g.H());
        t.Ok(BodyOf(fake->Last()).Keys() == std::vector<std::string>{"items", "atomic"},
             std::string(given ? "false" : "left out") + ": " + fake->Last().body.value_or(""));
    }
}

// Under create_only an id the world already holds answers its slot 409 id_conflict, and the rest of the batch goes on:
// that slot reads as any failed slot does.
OW_TEST(client_write_bulk_a_409_id_conflict_slot_reads_as_a_failed_slot) {
    auto fake = Fake();
    fake->Then(200,
               "{\"errors\":true,\"items\":[{\"status\":409,\"id\":\"mine\",\"error\":{\"type\":\"invalid_request\","
               "\"code\":\"id_conflict\",\"message\":\"A create supplied an id that already exists.\",\"param\":\"id\"}},"
               "{\"status\":201,\"id\":\"b\"}]}");
    OWClient c = Make(fake);
    Got<OWBulkResult> g;
    OWBulkOptions o;
    o.create_only = true;
    c.Bulk({{"character", Parse("{\"id\":\"mine\"}")}, {"character", Parse("{}")}}, o, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    const OWBulkResult& b = g.reply.value;
    t.Ok(b.Failed() == std::vector<size_t>{0}, "Failed names the held id's slot");
    t.Eq(b.SucceededCount(), size_t(1), "the rest of the batch goes on");
    if (b.items.size() != 2 || !b.items[0].error) return;
    t.Eq(b.items[0].status, 409, "");
    t.Eq(IdText(b.items[0].id), "mine", "");
    t.Eq(b.items[0].error->status, 409, "the slot's status");
    t.Eq(b.items[0].error->code, "id_conflict", "");
    t.Ok(b.items[0].error->IsIdConflict(), "");
}

OW_TEST(client_write_bulk_answers_its_slots) {
    auto fake = Fake();
    fake->Then(200,
               "{\"errors\":true,\"items\":[{\"status\":201,\"id\":\"a1\",\"created_at\":\"t1\",\"updated_at\":\"t2\"},"
               "{\"status\":202,\"id\":\"a2\",\"proposal\":{\"id\":\"p1\"}},"
               "{\"status\":422,\"id\":null,\"error\":{\"type\":\"invalid_request\",\"code\":\"invalid_field\",\"message\":"
               "\"No such field.\",\"param\":\"colour\"}}]}",
               {{"idempotent-replay", "TRUE"}});
    OWClient c = Make(fake);
    Got<OWBulkResult> g;
    c.Bulk({{"character", Parse("{}")}, {"character", Parse("{}")}, {"character", Parse("{}")}}, {}, g.H());
    t.Ok(g.reply.Ok(), g.Why());
    const OWBulkResult& b = g.reply.value;
    t.Ok(b.errors, "keel's flag");
    t.Ok(b.was_replay, "Idempotent-Replay read by any case");
    t.Eq(b.items.size(), size_t(3), "a slot per item, in order");
    if (b.items.size() != 3) return;
    t.Eq(b.items[0].status, 201, "");
    t.Eq(IdText(b.items[0].id), "a1", "");
    t.Eq(b.items[0].created_at.value_or(""), "t1", "");
    t.Eq(b.items[0].updated_at.value_or(""), "t2", "");
    t.Ok(b.items[0].Succeeded() && !b.items[0].IsProposed() && !b.items[0].error, "a create");
    t.Ok(b.items[1].IsProposed() && b.items[1].Succeeded(), "a proposal counts as succeeded");
    t.Eq(b.items[1].proposal ? TextOf(*b.items[1].proposal, "id") : "<absent>", "p1", "the proposal row");
    t.Ok(!b.items[1].created_at, "a stamp absent is absent");
    t.Ok(!b.items[2].Succeeded(), "a failed slot");
    t.Ok(IdIsNull(b.items[2].id), "a null id stays null, never \"\" (the API document's string or null)");
    t.Ok(b.items[2].error.has_value(), "its error");
    if (b.items[2].error) {
        const OWApiError& e = *b.items[2].error;
        t.Ok(e.kind == OWApiError::Kind::Api, "kind Api");
        t.Eq(e.status, 422, "the slot's status");
        t.Eq(e.code, "invalid_field", "");
        t.Eq(e.param, "colour", "");
        t.Eq(e.message, "No such field.", "");
    }
    t.Ok(b.Failed() == std::vector<size_t>{2}, "Failed names the third");
    t.Eq(b.SucceededCount(), size_t(2), "");
    t.Ok(!b.AllSucceeded(), "");
    fake->Then(200, "{\"errors\":false,\"items\":[{\"status\":200,\"id\":\"a1\"}]}");
    c.Bulk({{"character", Parse("{}")}}, {}, g.H());
    t.Ok(g.reply.value.AllSucceeded() && !g.reply.value.was_replay, "all succeeded, no replay header");
}

OW_TEST(client_write_bulk_answer_that_is_not_its_shape_is_an_answer_error) {
    for (const char* body : {"{\"errors\":false,\"items\":{}}", "{\"errors\":false,\"items\":[1]}", "[]", "nope"}) {
        auto fake = Fake();
        fake->Then(200, body);
        OWClient c = Make(fake);
        Got<OWBulkResult> g;
        c.Bulk({{"character", Parse("{}")}}, {}, g.H());
        t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, std::string(body) + ": " + g.Why());
    }
}

// -- The credentials on a write ------------------------------------------------------------------------------------------

OW_TEST(client_write_pin_on_every_write_of_every_world_key) {
    // keel checks the PIN on every write (any method but GET): a prefixed key, a legacy key, a key of no known shape.
    for (const char* key : {"ow_w_test", "0123456789", "abc", "ow_r_test"}) {
        auto fake = Fake();
        for (const OWRequest& r : SweepWrites(t, Config(fake, key, "1234"))) {
            std::string where = std::string(key) + " " + r.method + " " + r.url;
            t.Eq(HeaderOr(r, "API-Key"), key, where);
            t.Eq(HeaderOr(r, "API-Pin"), "1234", where + ": the PIN");
            t.Eq(HeaderOr(r, "Authorization"), "<absent>", where + ": no Bearer");
            t.Eq(HeaderOr(r, "X-OW-Client"), "unreal-sdk", where);
            t.Eq(HeaderOr(r, "X-OW-Agent"), "<absent>", where + ": not an agent");
        }
    }
    auto fake = Fake();
    std::vector<OWRequest> none = SweepWrites(t, Config(fake, "ow_w_test", ""));
    t.Eq(none.size(), size_t(6), "a world without a PIN: every write sent");
    for (const OWRequest& r : none) t.Eq(HeaderOr(r, "API-Pin"), "<absent>", r.method + " " + r.url + ": no PIN to send");
}

OW_TEST(client_write_account_token_and_keyless_client_write_nothing) {
    struct Who {
        const char* key;
        std::optional<std::string> world;
        const char* says;
    };
    const Who who[] = {{"ow_a_acct", std::string("w1"), "needs a world key"},
                       {"ow_a_acct", std::nullopt, "needs a world key"},
                       {"", std::string("w1"), "needs a key"},
                       {"", std::nullopt, "needs a key"}};
    for (const Who& w : who) {
        for (const WriteCall& call : Writes()) {
            auto fake = Fake();
            OWClient c = Make(fake, w.key, "1234", w.world);
            Outcome o = call.run(c);
            std::string n = std::string(w.key[0] ? w.key : "no key") + (w.world ? " with a world " : " ") + call.name;
            t.Eq(o.calls, 1, n + ": answered once");
            t.Ok(o.error && o.error->kind == OWApiError::Kind::Refused, n + ": refused");
            t.Eq(fake->calls.size(), size_t(0), n + ": nothing sent");
            if (o.error) {
                t.Ok(o.error->message.find(w.says) != std::string::npos, n + ": " + o.error->message);
                t.Ok(EndsNothingSent(o.error->message), n + ": says so");
            }
        }
    }
}

OW_TEST(client_write_agent_and_client_name_go_on_writes_alone) {
    auto fake = Fake();
    OWClientConfig config = Config(fake, "ow_w_seat", "ow_s_secret");
    config.agent = true;
    config.client = "my-game";
    for (const OWRequest& r : SweepWrites(t, config)) {
        t.Eq(HeaderOr(r, "X-OW-Agent"), "1", r.method + " " + r.url + ": the agent declared");
        t.Eq(HeaderOr(r, "X-OW-Client"), "my-game", r.method + " " + r.url + ": the client's own name");
        t.Eq(HeaderOr(r, "API-Pin"), "ow_s_secret", r.method + " " + r.url + ": the seat's secret as the PIN");
    }
    config.transport = fake;
    OWClient c(config);
    Got<OWJsonValue> g;
    c.Me(g.H());
    c.Get("character", "c1", {}, g.H());
    for (const OWRequest& r : fake->calls) {
        t.Eq(HeaderOr(r, "X-OW-Agent"), "<absent>", r.url + ": never on a read");
        t.Eq(HeaderOr(r, "X-OW-Client"), "<absent>", r.url + ": never on a read");
    }
}

OW_TEST(client_write_a_client_not_built_writes_nothing) {
    auto fake = Fake();
    OWClientConfig config = Config(fake);
    config.client = "Not A Name";
    for (const WriteCall& call : Writes()) {
        OWClient c(config);
        Outcome o = call.run(c);
        t.Ok(o.error && o.error->kind == OWApiError::Kind::Refused, std::string(call.name) + ": refused");
        if (o.error) t.Ok(o.error->message.find("not built") != std::string::npos, o.error->message);
    }
    t.Eq(fake->calls.size(), size_t(0), "nothing sent");
}

// -- Refused before sending ----------------------------------------------------------------------------------------------

OW_TEST(client_write_a_known_of_stub_is_refused_on_every_body) {
    OWJsonValue stub = Parse("{\"id\":\"c1\",\"name\":\"Ana\",\"description\":\"\",\"known_of\":true}");
    auto fake = Fake("{\"errors\":false,\"items\":[]}");
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    std::vector<std::pair<std::string, std::function<void()>>> calls = {
        {"create", [&] { c.Create("character", stub, {}, g.H()); }},
        {"upsert", [&] { c.Upsert("character", "c1", stub, g.H()); }},
        {"patch", [&] { c.Patch("character", "c1", stub, {}, g.H()); }},
    };
    for (auto& [name, run] : calls) {
        g = {};
        run();
        t.Ok(g.Refused(), name + ": refused");
        if (g.reply.error) {
            t.Ok(g.reply.error->message.find("known-of stub") != std::string::npos, name + ": " + g.reply.error->message);
            t.Ok(g.reply.error->message.rfind(name + ":", 0) == 0, name + ": named");
            t.Ok(EndsNothingSent(g.reply.error->message), name + ": says so");
        }
    }
    Got<OWBulkResult> b;
    c.Bulk({{"character", Parse("{\"name\":\"A\"}")}, {"character", stub}}, {}, b.H());
    t.Ok(b.Refused(), "bulk: the whole call refused");
    if (b.reply.error) t.Ok(b.reply.error->message.rfind("bulk: items[1]:", 0) == 0, b.reply.error->message);
    t.Eq(fake->calls.size(), size_t(0), "nothing sent");
    // Only the JSON true marks a stub: false is a whole element, and the key is left out.
    c.Create("character", Parse("{\"name\":\"Ana\",\"known_of\":false}"), {}, g.H());
    t.Eq(fake->calls.size(), size_t(1), "known_of false: sent");
    t.Ok(!BodyOf(fake->Last()).Has("known_of"), "without the key");
    c.Create("character", Parse("{\"name\":\"Ana\",\"known_of\":\"true\"}"), {}, g.H());
    t.Eq(fake->calls.size(), size_t(2), "known_of \"true\": a whole element, sent without the key");
}

OW_TEST(client_write_refusals_before_sending) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    Got<OWBulkResult> b;
    OWWriteOptions negative;
    negative.if_match = -1;
    OWLinkEdit add;
    add.add = std::vector<std::string>{"c2"};
    std::vector<std::tuple<std::string, std::string, std::function<void()>>> cases = {
        {"create: an array", "JSON object", [&] { c.Create("character", Parse("[]"), {}, g.H()); }},
        {"create: null", "JSON object", [&] { c.Create("character", OWJsonValue(), {}, g.H()); }},
        {"upsert: a string", "JSON object", [&] { c.Upsert("character", "c1", Parse("\"Ana\""), g.H()); }},
        {"patch: a number", "JSON object", [&] { c.Patch("character", "c1", Parse("4"), {}, g.H()); }},
        {"patch: a negative if_match", "whole number", [&] { c.Patch("character", "c1", Parse("{}"), negative, g.H()); }},
        {"delete: a negative if_match", "whole number", [&] { c.Delete("character", "c1", negative, g.H()); }},
        {"edit links: a negative if_match", "whole number",
         [&] { c.EditLinks("character", "c1", "friends", add, negative, g.H()); }},
        {"create: a type not one segment", "path segment", [&] { c.Create("Character", Parse("{}"), {}, g.H()); }},
        {"create: a type with a slash", "path segment", [&] { c.Create("character/x", Parse("{}"), {}, g.H()); }},
        {"upsert: an id with a slash", "path segment", [&] { c.Upsert("character", "c1/hide", Parse("{}"), g.H()); }},
        {"upsert: no id", "path segment", [&] { c.Upsert("character", "", Parse("{}"), g.H()); }},
        {"patch: an id with a dot first", "path segment", [&] { c.Patch("character", ".c1", Parse("{}"), {}, g.H()); }},
        {"delete: no id", "path segment", [&] { c.Delete("character", "", {}, g.H()); }},
        {"edit links: a field with a slash", "path segment",
         [&] { c.EditLinks("character", "c1", "friends/x", add, {}, g.H()); }},
        {"edit links: no field", "path segment", [&] { c.EditLinks("character", "c1", "", add, {}, g.H()); }},
    };
    for (auto& [name, says, run] : cases) {
        g = {};
        run();
        t.Ok(g.Refused(), name + ": refused");
        t.Eq(g.calls, 1, name + ": answered once");
        if (g.reply.error) {
            t.Ok(g.reply.error->message.find(says) != std::string::npos, name + ": " + g.reply.error->message);
            t.Ok(EndsNothingSent(g.reply.error->message), name + ": says so");
        }
    }
    std::vector<std::tuple<std::string, std::string, std::vector<OWBulkItem>>> bulks = {
        {"bulk: no items", "at least one item", {}},
        {"bulk: an item without a type", "items[1] needs a type", {{"character", Parse("{}")}, {"", Parse("{}")}}},
        {"bulk: an element that is not an object", "items[0]", {{"character", Parse("[1]")}}},
    };
    for (auto& [name, says, items] : bulks) {
        b = {};
        c.Bulk(items, {}, b.H());
        t.Ok(b.Refused(), name + ": refused");
        if (b.reply.error) {
            t.Ok(b.reply.error->message.find(says) != std::string::npos, name + ": " + b.reply.error->message);
            t.Ok(EndsNothingSent(b.reply.error->message), name + ": says so");
        }
    }
    t.Eq(fake->calls.size(), size_t(0), "nothing sent");
}

// -- The answers ---------------------------------------------------------------------------------------------------------

OW_TEST(client_write_a_202_is_a_proposal_never_the_element) {
    OWLinkEdit add;
    add.add = std::vector<std::string>{"c2"};
    std::vector<std::pair<std::string, std::function<void(OWClient&, Got<OWJsonValue>&)>>> calls = {
        {"upsert", [](OWClient& c, Got<OWJsonValue>& g) { c.Upsert("character", "c1", Parse("{}"), g.H()); }},
        {"patch", [](OWClient& c, Got<OWJsonValue>& g) { c.Patch("character", "c1", Parse("{}"), {}, g.H()); }},
        {"edit links", [add](OWClient& c, Got<OWJsonValue>& g) { c.EditLinks("character", "c1", "friends", add, {}, g.H()); }},
    };
    for (auto& [name, run] : calls) {
        auto fake = Fake();
        fake->Then(202, "{\"proposal\":{\"id\":\"p1\",\"status\":\"open\"}}");
        fake->Then(202, "{\"id\":\"c1\",\"name\":\"Ana\"}");
        fake->Then(202, "not json");
        OWClient c = Make(fake);
        Got<OWJsonValue> g;
        run(c, g);
        t.Ok(g.reply.error.has_value(), name + ": an error, never the element");
        if (g.reply.error) {
            const OWApiError& e = *g.reply.error;
            t.Ok(e.kind == OWApiError::Kind::Api, name + ": kind Api");
            t.Eq(e.status, 202, name);
            t.Ok(e.IsProposed(), name + ": IsProposed");
            t.Eq(e.proposal ? TextOf(*e.proposal, "id") : "<absent>", "p1", name + ": the row");
            t.Ok(e.message.find("proposal") != std::string::npos, name + ": " + e.message);
        }
        for (const char* what : {"a body that is not a proposal", "a body that is not JSON"}) {
            g = {};
            run(c, g);
            t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, name + ", " + what + ": " + g.Why());
            if (g.reply.error) {
                t.Eq(g.reply.error->status, 202, name);
                t.Ok(!g.reply.error->IsProposed(), name + ": no proposal made up");
            }
        }
    }
}

OW_TEST(client_write_errors_read_keels_envelope_and_the_write_helpers) {
    struct Case {
        int status;
        const char* body;
        const char* code;
        std::function<bool(const OWApiError&)> holds;
    };
    const Case cases[] = {
        {412, "{\"error\":{\"code\":\"precondition_failed\"},\"current\":{\"id\":\"c1\",\"change_seq\":9}}",
         "precondition_failed",
         [](const OWApiError& e) {
             return e.IsPreconditionFailed() && e.Current() && TextOf(*e.Current(), "change_seq") == "9" &&
                    !e.IsIdConflict() && !e.IsRetryable();
         }},
        {409, "{\"error\":{\"code\":\"id_conflict\"}}", "id_conflict",
         [](const OWApiError& e) { return e.IsIdConflict() && !e.IsIdempotencyConflict() && !e.Current(); }},
        {409, "{\"error\":{\"code\":\"idempotency_error\"}}", "idempotency_error",
         [](const OWApiError& e) { return e.IsIdempotencyConflict() && !e.IsIdConflict() && !e.IsRetryable(); }},
        {422, "{\"error\":{\"code\":\"known_of_not_writable\",\"param\":\"known_of\"}}", "known_of_not_writable",
         [](const OWApiError& e) { return e.IsKnownOfNotWritable() && e.IsValidationError(); }},
        {409, "{\"error\":{\"code\":\"stale_proposal\"},\"current\":{\"id\":\"c1\"}}", "stale_proposal",
         [](const OWApiError& e) { return e.Current() && TextOf(*e.Current(), "id") == "c1" && !e.IsPreconditionFailed(); }},
        {404, "{\"error\":{\"code\":\"not_found\"},\"current\":{\"id\":\"c1\"}}", "not_found",
         [](const OWApiError& e) { return !e.Current() && e.IsNotFound(); }},
        {412, "<html>precondition</html>", "", [](const OWApiError& e) { return !e.Current() && !e.IsPreconditionFailed(); }},
        {412, "{\"error\":{\"code\":\"precondition_failed\"},\"current\":[1]}", "precondition_failed",
         [](const OWApiError& e) { return e.IsPreconditionFailed() && !e.Current(); }},
        {409, "{\"error\":{\"code\":\"precondition_failed\"}}", "precondition_failed",
         [](const OWApiError& e) { return !e.IsPreconditionFailed(); }},
        {200, "", "", [](const OWApiError& e) { return !e.IsProposed() && !e.Current(); }},
    };
    for (const Case& k : cases) {
        auto fake = Fake();
        OWClient c = Make(fake);
        Got<OWJsonValue> g;
        if (k.status == 200) {
            // A 2xx that is not an element: kind Answer, no helper true.
            fake->Then(200, "[]");
            c.Patch("character", "c1", Parse("{}"), {}, g.H());
            t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, "a list for an element: " + g.Why());
            if (g.reply.error) t.Ok(k.holds(*g.reply.error), "no helper reads it");
            continue;
        }
        fake->Then(k.status, k.body);
        OWWriteOptions when;
        when.if_match = 4;
        c.Patch("character", "c1", Parse("{\"name\":\"Bo\"}"), when, g.H());
        std::string n = std::to_string(k.status) + " " + k.body;
        t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Api, n + ": kind Api");
        if (!g.reply.error) continue;
        t.Eq(g.reply.error->status, k.status, n);
        t.Eq(g.reply.error->code, k.code, n);
        t.Ok(k.holds(*g.reply.error), n + ": its helpers");
        t.Ok(g.reply.response.has_value(), n + ": the answer beside it");
    }
}

OW_TEST(client_write_answers_that_are_not_an_element) {
    for (const char* body : {"[]", "not json", "", "\"Ana\""}) {
        auto fake = Fake();
        fake->Then(201, body);
        OWClient c = Make(fake);
        Got<OWJsonValue> g;
        c.Create("character", Parse("{\"name\":\"Ana\"}"), {}, g.H());
        t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Answer, OWJson::Quote(body) + ": " + g.Why());
        if (g.reply.error) t.Eq(g.reply.error->status, 201, OWJson::Quote(body));
    }
}

OW_TEST(client_write_no_answer_is_a_transport_error) {
    auto fake = Fake();
    fake->Fail("connection reset");
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Create("character", Parse("{\"name\":\"Ana\"}"), {}, g.H());
    t.Ok(g.reply.error && g.reply.error->kind == OWApiError::Kind::Transport, g.Why());
    if (g.reply.error) t.Ok(g.reply.error->message.find("POST /character/") != std::string::npos, g.reply.error->message);
}

OW_TEST(client_write_answers_after_the_client_is_gone) {
    // Unreal's HTTP answers later: a write's state lives as long as its request, and the handler is called once.
    auto fake = Fake("{\"errors\":false,\"items\":[{\"status\":201,\"id\":\"a1\"}]}");
    fake->deferred = true;
    Got<OWJsonValue> one;
    Got<OWBulkResult> many;
    {
        OWClient c = Make(fake);
        c.Delete("character", "c1", {}, one.H());
        c.Bulk({{"character", Parse("{}")}}, {}, many.H());
    }
    t.Eq(one.calls + many.calls, 0, "nothing answered yet");
    t.Eq(fake->Flush(), size_t(2), "two answered");
    t.Eq(one.calls, 1, "the delete's handler once");
    t.Eq(many.calls, 1, "the bulk's handler once");
    t.Ok(many.reply.Ok() && many.reply.value.items.size() == 1, many.Why());
}

OW_TEST(client_write_a_second_answer_is_dropped) {
    auto fake = Fake();
    fake->answer_twice = true;
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Create("character", Parse("{\"name\":\"Ana\"}"), {}, g.H());
    t.Eq(g.calls, 1, "the handler once");
}

// -- The body ------------------------------------------------------------------------------------------------------------

OW_TEST(client_write_body_is_compact_json_with_javascripts_numbers) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWJsonValue> g;
    c.Create("character",
             Parse("{\"id\":\"c1\",\"name\":\"Ana \\\"q\\\" \xC3\xA9\\n\",\"level\":3,\"x_f\":1.50,\"x_big\":12345678901234567890,"
                   "\"x_e\":1e3,\"x_neg\":-0.0,\"x_list\":[1, 2.0, {\"a\" : null}],\"x_t\":true,\"x_c\":\"\\u0001\"}"),
             {}, g.H());
    t.Eq(fake->Last().body.value_or(""),
         "{\"id\":\"c1\",\"name\":\"Ana \\\"q\\\" \xC3\xA9\\n\",\"level\":3,\"x_f\":1.5,\"x_big\":12345678901234567890,"
         "\"x_e\":1000,\"x_neg\":0,\"x_list\":[1,2,{\"a\":null}],\"x_t\":true,\"x_c\":\"\\u0001\"}",
         "no white space; a number as JSON.stringify writes it, an integer literal as written");
}

// -- Sanitize --------------------------------------------------------------------------------------------------------------

OW_TEST(client_write_read_only_fields_and_known_of) {
    std::string names;
    for (const std::string& f : OWClient::ReadOnlyFields()) names += (names.empty() ? "" : ",") + f;
    t.Eq(names, "world,type,created_at,updated_at,change_seq,known_of", "the TypeScript SDK's READ_ONLY_FIELDS");
    t.Ok(OWClient::IsKnownOf(Parse("{\"known_of\":true}")), "the JSON true");
    for (const char* not_one : {"{\"known_of\":false}", "{\"known_of\":\"true\"}", "{\"known_of\":1}", "{\"known_of\":null}",
                                "{}", "[true]", "true"})
        t.Ok(!OWClient::IsKnownOf(Parse(not_one)), std::string(not_one) + ": a whole element");
}

OW_TEST(client_write_sanitize_leaves_out_the_read_only_and_sends_a_null_string_as_empty) {
    OWJsonValue in = Parse(
        "{\"world\":\"w1\",\"id\":null,\"type\":\"character\",\"name\":null,\"description\":null,\"supertype\":null,"
        "\"subtype\":null,\"image_url\":null,\"background\":null,\"birthplace\":null,\"friends\":null,\"level\":null,"
        "\"created_at\":\"t\",\"updated_at\":\"t\",\"change_seq\":4,\"known_of\":false,\"x_tool\":null,\"nonsense\":null,"
        "\"motivations\":\"keep\",\"hidden\":null}");
    OWJsonValue before = in;
    OWJsonValue out = OWClient::Sanitize(in, "character");
    t.Ok(in == before, "the payload is not changed");
    t.Eq(KeysOf(out),
         "id,name,description,supertype,subtype,image_url,background,birthplace,friends,level,x_tool,nonsense,motivations,"
         "hidden",
         "the six left out, every other key kept in its order");
    for (const char* s : {"name", "description", "supertype", "subtype", "image_url", "background"}) {
        const OWJsonValue* v = out.Find(s);
        t.Ok(v && v->IsString() && v->Text().empty(), std::string(s) + ": a null string sent as \"\"");
    }
    for (const char* n : {"id", "birthplace", "friends", "level", "x_tool", "nonsense", "hidden"}) {
        const OWJsonValue* v = out.Find(n);
        t.Ok(v && v->IsNull(), std::string(n) + ": not a scalar string, its null kept");
    }
    t.Eq(TextOf(out, "motivations"), "keep", "a value kept");
    // A key absent stays absent: on a PATCH it means "leave it alone".
    t.Eq(KeysOf(OWClient::Sanitize(Parse("{\"level\":2}"), "character")), "level", "nothing added");
    // An unknown type: the base strings alone.
    OWJsonValue odd = OWClient::Sanitize(Parse("{\"name\":null,\"background\":null}"), "nonesuch");
    t.Ok(odd.Find("name") && odd.Find("name")->IsString(), "a base string on any type");
    t.Ok(odd.Find("background") && odd.Find("background")->IsNull(), "a field of no type of this name: kept");
    // The generic link's type key is no scalar string (schema.json: kind generic).
    OWJsonValue pin = OWClient::Sanitize(Parse("{\"element_type\":null,\"element_id\":null,\"map\":null}"), "pin");
    t.Eq(OWJson::Stringify(pin, true), OWJson::Stringify(Parse("{\"element_type\":null,\"element_id\":null,\"map\":null}"), true),
         "a pin's links kept as null");
    // Anything but an object is answered as given.
    for (const char* v : {"null", "[1]", "\"x\"", "3"})
        t.Ok(OWClient::Sanitize(Parse(v), "character") == Parse(v), std::string(v) + ": as given");
}

OW_TEST(client_write_sanitize_reads_the_schemas_scalar_strings) {
    // Every field set to null on each of the 22 types: those sent as "" are exactly schema.json's scalar_str fields of
    // the type and of the base (id and world aside: an id is kept, world left out), and no other.
    OWBytes bytes;
    t.Ok(OWFiles::Read(P().root + "/codegen/schema-dist/schema.json", bytes), "schema.json read");
    OWJsonValue schema = OWJson::Parse(bytes).value;
    const OWJsonValue* base = schema.Find("base");
    const OWJsonValue* types = schema.Find("types");
    t.Ok(base && base->IsArray() && types && types->IsObject() && types->Size() == 22, "its base and 22 types");
    if (!base || !types) return;
    std::set<std::string> base_strings;
    for (const OWJsonValue& f : base->Items())
        if (TextOf(f, "kind") == "scalar_str") base_strings.insert(TextOf(f, "name"));
    base_strings.erase("id");
    base_strings.erase("world");
    t.Eq(base_strings.size(), size_t(5), "name, description, supertype, subtype, image_url");
    size_t own = 0;
    for (size_t i = 0; i < types->Size(); ++i) {
        const std::string& type = types->Keys()[i];
        const OWJsonValue& fields = types->Items()[i].IsArray() ? types->Items()[i] : *types->Items()[i].Find("fields");
        std::set<std::string> want = base_strings;
        OWJsonValue all = OWJsonValue::Object();
        for (const std::string& b : base_strings) all.Set(b, OWJsonValue());
        for (const OWJsonValue& f : fields.Items()) {
            if (TextOf(f, "kind") == "scalar_str") {
                want.insert(TextOf(f, "name"));
                ++own;
            }
            all.Set(TextOf(f, "name"), OWJsonValue());
            if (const OWJsonValue* k = f.Find("type_key")) all.Set(k->Text(), OWJsonValue());
            if (const OWJsonValue* k = f.Find("id_key")) all.Set(k->Text(), OWJsonValue());
        }
        OWJsonValue out = OWClient::Sanitize(all, type);
        std::set<std::string> got;
        for (size_t k = 0; k < out.Size(); ++k)
            if (out.Items()[k].IsString()) got.insert(out.Keys()[k]);
        t.Ok(got == want, type + ": the scalar strings sent as \"\", and only those");
    }
    t.Eq(own, size_t(71), "the 71 scalar strings of the 22 types");
}

OW_TEST(client_write_the_lens_fields_are_the_schemas_kinds) {
    // Sanitize reads a type's scalar strings from OWLens::FieldsOf (kind Text): held to schema.json, field by field.
    OWBytes bytes;
    OWFiles::Read(P().root + "/codegen/schema-dist/schema.json", bytes);
    OWJsonValue schema = OWJson::Parse(bytes).value;
    const OWJsonValue* types = schema.Find("types");
    t.Ok(types && types->Size() == 22, "22 types");
    if (!types) return;
    for (size_t i = 0; i < types->Size(); ++i) {
        const std::string& type = types->Keys()[i];
        const OWJsonValue& fields = types->Items()[i].IsArray() ? types->Items()[i] : *types->Items()[i].Find("fields");
        std::set<std::string> want, got;
        for (const OWJsonValue& f : fields.Items())
            if (TextOf(f, "kind") == "scalar_str") want.insert(TextOf(f, "name"));
        for (const OWLens::OWLensField& f : OWLens::FieldsOf(type))
            if (f.kind == OWLens::OWFieldKind::Text) got.insert(f.name);
        t.Ok(got == want, type + ": OWLens's Text fields are schema.json's scalar_str fields");
    }
}
