// OWApiError::Limit and its three predicates through a fake transport: keel's three size refusals keep the code
// invalid_request and name the limit passed in the envelope's `limit`, a key of the API document's
// info.x-ow-limits. The TypeScript SDK's test (test/v2-client.test.mjs, 4.219.0) ported, with a bulk slot's case and
// a limit that is not a string beside it.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"
#include "OWJson.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string LimitBase = "https://example.test/api/v2";

OWClient LimitClient(const std::shared_ptr<FakeTransport>& fake) {
    OWClientConfig config;
    config.api_key = "ow_w_test";
    config.api_pin = "1234";
    config.base_url = LimitBase;
    config.transport = fake;
    return OWClient(config);
}

// A 422 invalid_request envelope (or `code`'s), with `limit` as raw JSON when given.
std::string SizeRefusal(const char* limit_json, const char* code = "invalid_request") {
    std::string inner = std::string("\"type\":\"invalid_request\",\"code\":\"") + code +
                        "\",\"message\":\"m\",\"param\":null,\"doc_url\":\"https://example.test/e\"";
    if (limit_json) inner += std::string(",\"limit\":") + limit_json;
    return "{\"error\":{" + inner + "}}";
}

// The error a create of a Character answers when keel says `status` with `body`.
std::optional<OWApiError> CreateRefused(int status, const std::string& body) {
    auto fake = std::make_shared<FakeTransport>();
    fake->Then(status, body);
    std::optional<OWApiError> out;
    LimitClient(fake).Create("character", OWJson::Parse("{\"name\":\"K\"}").value, {},
                             [&out](OWReply<OWJsonValue> r) { out = r.error; });
    return out;
}

}  // namespace

OW_TEST(limit_names_the_size_limit_a_422_passed_and_the_three_predicates_read_it) {
    for (const char* limit : {"element_bytes", "element_link_ids", "extensions_bytes"}) {
        std::string name = limit;
        std::optional<OWApiError> e = CreateRefused(422, SizeRefusal(("\"" + name + "\"").c_str()));
        t.Ok(e.has_value(), name + ": an error");
        if (!e) continue;
        t.Eq(e->code, "invalid_request", name + ": the code stays");
        t.Eq(e->Limit().value_or("<none>"), name, name + ": Limit");
        t.Eq(e->IsElementTooLarge(), name == "element_bytes", name + ": IsElementTooLarge");
        t.Eq(e->IsElementTooManyLinks(), name == "element_link_ids", name + ": IsElementTooManyLinks");
        t.Eq(e->IsExtensionsTooLarge(), name == "extensions_bytes", name + ": IsExtensionsTooLarge");
    }
}

OW_TEST(limit_is_an_open_string_none_without_one_and_never_off_a_422) {
    // a value the package does not know is read, and no predicate claims it
    std::optional<OWApiError> later = CreateRefused(422, SizeRefusal("\"element_fields\""));
    t.Ok(later.has_value(), "an unknown value: an error");
    if (later) {
        t.Eq(later->Limit().value_or("<none>"), "element_fields", "an unknown value is read");
        t.Ok(!later->IsElementTooLarge() && !later->IsElementTooManyLinks() && !later->IsExtensionsTooLarge(),
             "and no predicate claims it");
    }
    std::optional<OWApiError> plain = CreateRefused(422, SizeRefusal(nullptr));
    t.Ok(plain && !plain->Limit() && !plain->IsElementTooLarge(), "no limit sent: none, false");
    // a limit that is not a string is not one
    std::optional<OWApiError> number = CreateRefused(422, SizeRefusal("5"));
    t.Ok(number && !number->Limit() && !number->IsElementTooLarge() && !number->IsElementTooManyLinks() &&
             !number->IsExtensionsTooLarge(),
         "a number: none, no predicate");
    // never off a 422: a 409 resync_required carries `reason`, and a `limit` there is not read
    auto fake = std::make_shared<FakeTransport>();
    fake->Then(409, SizeRefusal("\"element_bytes\"", "resync_required"));
    std::optional<OWApiError> resync;
    OWChangesOptions o;
    o.since = "12:abc";
    LimitClient(fake).Changes(o, [&resync](OWReply<OWChangesPage> r) { resync = r.error; });
    t.Ok(resync && resync->IsResyncRequired(), "the 409 is a resync");
    t.Ok(resync && !resync->Limit() && !resync->IsElementTooLarge(), "a 409: none, false");
    // the client's own refusal, which sends nothing
    t.Ok(!OWApiError::Of(OWApiError::Kind::Refused, "no").Limit(), "the client's own refusal: none");
}

OW_TEST(limit_is_on_a_bulk_slots_error) {
    auto fake = std::make_shared<FakeTransport>();
    fake->Then(200, "{\"errors\":true,\"items\":[{\"status\":201,\"id\":\"a\"},{\"status\":422,\"error\":"
                    "{\"type\":\"invalid_request\",\"code\":\"invalid_request\",\"message\":\"m\",\"param\":null,"
                    "\"doc_url\":\"https://example.test/e\",\"limit\":\"element_link_ids\"}}]}");
    std::optional<OWReply<OWBulkResult>> got;
    LimitClient(fake).Bulk({{"character", OWJson::Parse("{\"name\":\"A\"}").value},
                            {"character", OWJson::Parse("{\"name\":\"B\"}").value}},
                           {}, [&got](OWReply<OWBulkResult> r) { got = std::move(r); });
    t.Ok(got && got->Ok(), "the bulk answered");
    if (!got || !got->Ok() || got->value.items.size() != 2) return t.Fail("two slots");
    t.Ok(!got->value.items[0].error, "the created slot has no error");
    const std::optional<OWApiError>& e = got->value.items[1].error;
    t.Ok(e.has_value(), "the failed slot's error");
    if (!e) return;
    t.Eq(e->Limit().value_or("<none>"), "element_link_ids", "the failed slot's limit");
    t.Ok(e->IsElementTooManyLinks(), "and its predicate");
    t.Eq(e->code, "invalid_request", "its code stays");
}
