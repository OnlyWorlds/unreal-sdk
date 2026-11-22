// OWApiError::FieldsRefusal through a fake transport (tests/fake_transport.h): a read whose `fields` list names fields
// its type does not have, answered with keel's one 422 and every unknown name beside it. The TypeScript SDK's three
// tests (test/v2-client.test.mjs, 4.179.0), case for case, on a List with fields; then a Get with fields, which keel
// refuses the same way.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWApiError.h"
#include "OWClient.h"
#include "OWJson.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

// The body is the server's answer to fields=name,story,hidden,narrator,nonsuch on a Character list, its allowed list
// cut to four names.
OWJsonValue FieldsRefused() {
    return OWJson::Parse(
               "{\"error\":{\"type\":\"invalid_request\",\"code\":\"invalid_request\","
               "\"message\":\"Unknown fields in ?fields: story, hidden, narrator, nonsuch\",\"param\":\"fields\","
               "\"doc_url\":\"https://onlyworlds.github.io/api/errors/#invalid_request\","
               "\"unknown_fields\":[\"story\",\"hidden\",\"narrator\",\"nonsuch\"],"
               "\"types_with_field\":{\"story\":[\"narrative\"],\"narrator\":[\"narrative\"]},"
               "\"allowed_fields\":[\"id\",\"name\",\"description\",\"supertype\"]}}")
        .value;
}

OWJsonValue& Inner(OWJsonValue& body) { return *body.Find("error"); }

// The error a List with fields answers when keel answers `status` with `body`.
OWApiError ListWithFields(int status, const OWJsonValue& body) {
    auto fake = std::make_shared<FakeTransport>();
    fake->Then(status, OWJson::Stringify(body));
    OWClientConfig config;
    config.api_key = "ow_w_test";
    config.api_pin = "0000";
    config.base_url = "https://example.test/api/v2";
    config.transport = fake;
    OWClient c(std::move(config));
    OWListOptions o;
    o.fields = {"name", "story", "hidden", "narrator", "nonsuch"};
    std::optional<OWApiError> error;
    c.List("character", o, [&](OWReply<OWPage> r) { error = r.error; });
    return error.value_or(OWApiError());
}

using Strings = std::vector<std::string>;
using ByName = std::vector<std::pair<std::string, Strings>>;

}  // namespace

OW_TEST(fields_refusal_reads_the_three_keys_of_a_422_on_fields_as_served) {
    OWApiError e = ListWithFields(422, FieldsRefused());
    t.Eq(e.param, std::string("fields"), "param");
    std::optional<OWFieldsRefusal> r = e.FieldsRefusal();
    if (!r) return t.Fail("no refusal read");
    t.Ok(r->unknown_fields == Strings{"story", "hidden", "narrator", "nonsuch"}, "unknown_fields, in the order sent");
    t.Ok(r->types_with_field == ByName{{"story", {"narrative"}}, {"narrator", {"narrative"}}},
         "types_with_field, in the order served");
    t.Ok(r->allowed_fields == Strings{"id", "name", "description", "supertype"}, "allowed_fields");
    const Strings* story = r->TypesWith("story");
    t.Ok(story && *story == Strings{"narrative"}, "TypesWith(story)");
    t.Ok(r->TypesWith("nonsuch") == nullptr, "a name no type has is not a key");
    t.Eq(OWJson::Stringify(r->ToJson()),
         OWJson::Stringify(OWJson::Parse("{\"unknown_fields\":[\"story\",\"hidden\",\"narrator\",\"nonsuch\"],"
                                         "\"types_with_field\":{\"story\":[\"narrative\"],\"narrator\":[\"narrative\"]},"
                                         "\"allowed_fields\":[\"id\",\"name\",\"description\",\"supertype\"]}")
                               .value),
         "ToJson, npm's shape");
}

OW_TEST(fields_refusal_is_absent_on_any_other_error_each_condition_alone) {
    // No unknown_fields: a server from before the keys, or another refusal that names fields.
    OWJsonValue bare = FieldsRefused();
    Inner(bare).Erase("unknown_fields");
    t.Ok(!ListWithFields(422, bare).FieldsRefusal(), "no unknown_fields");
    // The three keys under another param.
    OWJsonValue other_param = FieldsRefused();
    Inner(other_param).Set("param", OWJsonValue::String("expand"));
    t.Ok(!ListWithFields(422, other_param).FieldsRefusal(), "another param");
    // The three keys under another status.
    t.Ok(!ListWithFields(400, FieldsRefused()).FieldsRefusal(), "another status");
    // unknown_fields that is no list.
    OWJsonValue not_list = FieldsRefused();
    Inner(not_list).Set("unknown_fields", OWJsonValue::String("story"));
    t.Ok(!ListWithFields(422, not_list).FieldsRefusal(), "unknown_fields no list");
    // Beside npm's cases: the keys at the top level, with no envelope (npm reads them inside `error` only), and a body
    // that is not JSON.
    OWJsonValue full = FieldsRefused();
    OWJsonValue top = Inner(full);
    OWApiError unwrapped = ListWithFields(422, top);
    t.Eq(unwrapped.param, std::string("fields"), "the client reads an unwrapped envelope's param");
    t.Ok(!unwrapped.FieldsRefusal(), "no envelope, no refusal: npm reads the keys inside `error` only");
    OWApiError none = OWApiError::FromResponse(OWResponse{422, {}, "<html>", ""}, 0);
    none.param = "fields";
    t.Ok(!none.FieldsRefusal(), "a body that is not JSON");
}

OW_TEST(fields_refusal_leaves_out_what_is_not_in_shape_and_answers_empty_for_a_key_the_server_left_out) {
    OWJsonValue odd = FieldsRefused();
    Inner(odd).Set("unknown_fields", OWJson::Parse("[\"story\",7,null]").value);
    Inner(odd).Set("types_with_field", OWJson::Parse("{\"story\":[\"narrative\",3],\"narrator\":\"narrative\"}").value);
    Inner(odd).Set("allowed_fields", OWJson::Parse("[\"id\",{},\"name\"]").value);
    std::optional<OWFieldsRefusal> r = ListWithFields(422, odd).FieldsRefusal();
    if (!r) return t.Fail("no refusal read");
    t.Ok(r->unknown_fields == Strings{"story"}, "a non-string entry left out");
    t.Ok(r->types_with_field == ByName{{"story", {"narrative"}}}, "a non-list entry left out, a non-string type too");
    t.Ok(r->allowed_fields == Strings{"id", "name"}, "a non-string allowed field left out");

    OWJsonValue thin = FieldsRefused();
    Inner(thin).Erase("types_with_field");
    Inner(thin).Erase("allowed_fields");
    r = ListWithFields(422, thin).FieldsRefusal();
    if (!r) return t.Fail("no refusal read from a thin body");
    t.Ok(r->unknown_fields == Strings{"story", "hidden", "narrator", "nonsuch"}, "thin: unknown_fields");
    t.Ok(r->types_with_field.empty() && r->allowed_fields.empty(), "thin: the two keys left out read empty");

    OWJsonValue list = FieldsRefused();
    Inner(list).Set("types_with_field", OWJson::Parse("[[\"story\",[\"narrative\"]]]").value);
    r = ListWithFields(422, list).FieldsRefusal();
    t.Ok(r && r->types_with_field.empty(), "types_with_field as a list reads empty");
}

OW_TEST(fields_refusal_on_a_get_with_fields) {
    auto fake = std::make_shared<FakeTransport>();
    fake->Then(422, OWJson::Stringify(FieldsRefused()));
    OWClientConfig config;
    config.api_key = "ow_w_test";
    config.api_pin = "0000";
    config.base_url = "https://example.test/api/v2";
    config.transport = fake;
    OWClient c(std::move(config));
    OWGetOptions o;
    o.fields = {"name", "story"};
    std::optional<OWApiError> error;
    c.Get("character", "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e01", o, [&](OWReply<OWJsonValue> r) { error = r.error; });
    std::optional<OWFieldsRefusal> r = error ? error->FieldsRefusal() : std::nullopt;
    t.Ok(r && r->unknown_fields.size() == 4, "the get's 422 reads the same");
}
