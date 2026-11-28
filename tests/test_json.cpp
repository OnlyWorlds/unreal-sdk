// OWJson against JavaScript's JSON.stringify(value, null, 2): numbers, escapes, key order, empty containers; the
// parser's strictness; and the dist's example world, written by the reference implementation, rewritten byte for byte.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"

#include <limits>

using namespace OnlyWorlds;
using owtest::P;

namespace {
OWJsonValue ParseOk(owtest::T& t, const std::string& text) {
    OWJsonParseResult r = OWJson::Parse(text);
    t.Ok(r.ok, "parses: " + text + " (" + r.error + ")");
    return r.value;
}
}  // namespace

OW_TEST(json_numbers_as_javascript_writes_them) {
    // Expected strings are what JavaScript's Number.prototype.toString gives (the Godot SDK's cases).
    struct C { double d; const char* js; } cases[] = {
        {1.0, "1"}, {-5.0, "-5"}, {0.0, "0"}, {-0.0, "0"}, {4255.0, "4255"}, {0.1, "0.1"}, {1.5, "1.5"},
        {123.456, "123.456"}, {0.30000000000000004, "0.30000000000000004"}, {2.5e-5, "0.000025"}, {1e-7, "1e-7"},
        {1.5e-7, "1.5e-7"}, {1e21, "1e+21"}, {1e20, "100000000000000000000"}, {123456789012.25, "123456789012.25"},
        {12345678.9, "12345678.9"}, {-0.001, "-0.001"}, {1.7976931348623157e308, "1.7976931348623157e+308"},
        {9007199254740992.0, "9007199254740992"}, {2966.215418, "2966.215418"}, {5e-324, "5e-324"}, {0.000001, "0.000001"},
        {123e-20, "1.23e-18"}};
    for (const C& c : cases) t.Eq(OWJson::NumberToString(c.d), std::string(c.js), std::string("number ") + c.js);
    t.Eq(OWJson::NumberToString(std::numeric_limits<double>::quiet_NaN()), std::string("null"), "NaN");
    t.Eq(OWJson::NumberToString(std::numeric_limits<double>::infinity()), std::string("null"), "Infinity");
}

OW_TEST(json_number_text_in_section_5_spelling) {
    struct C { const char* in; const char* out; } cases[] = {
        {"1", "1"}, {"-0", "0"}, {"1.0", "1"}, {"4255.0", "4255"}, {"-3.50", "-3.5"}, {"1e3", "1000"}, {"1E3", "1000"},
        {"0.10", "0.1"}, {"-0.0", "0"}, {"1.5e300", "1.5e+300"}, {"2966.215418", "2966.215418"},
        // An integer literal keeps its value past 2^53 (JavaScript would round it): the Unity writer's choice.
        {"9007199254740993", "9007199254740993"}, {"12345678901234567890", "12345678901234567890"},
        // Past a double: too large keeps its text (JSON.stringify would write null), too small is 0.
        {"1E400", "1E400"}, {"-1e400", "-1e400"}, {"1e-400", "0"}, {"-1e-400", "0"}, {"0.0e999", "0"},
        // An exponent past a 32-bit long is still too large, not 0 (it overflowed the sum once).
        {"7e2147483647", "7e2147483647"}, {"7e99999999999999999999", "7e99999999999999999999"},
        {"-77.7e2147483646", "-77.7e2147483646"}, {"7e+99999999999999999999", "7e+99999999999999999999"},
        {"7e-99999999999999999999", "0"}};
    for (const C& c : cases) t.Eq(OWJson::CanonicalNumber(c.in), std::string(c.out), std::string("number text ") + c.in);
    OWJsonValue v = ParseOk(t, "{\"a\": 1.0, \"b\": 9007199254740993}");
    t.Eq(OWJson::Stringify(v), std::string("{\n  \"a\": 1,\n  \"b\": 9007199254740993\n}\n"), "section 5 spelling");
    t.Eq(OWJson::Stringify(v, true), std::string("{\n  \"a\": 1.0,\n  \"b\": 9007199254740993\n}\n"), "kept as spelled");
}

OW_TEST(json_escapes_as_javascript) {
    t.Eq(OWJson::Quote("a\"b\\c"), std::string("\"a\\\"b\\\\c\""), "quote and backslash");
    t.Eq(OWJson::Quote("\b\f\n\r\t"), std::string("\"\\b\\f\\n\\r\\t\""), "the short escapes");
    t.Eq(OWJson::Quote(std::string("\x01\x1f", 2)), std::string("\"\\u0001\\u001f\""), "other controls, lowercase hex");
    t.Eq(OWJson::Quote(std::string("\0", 1)), std::string("\"\\u0000\""), "NUL");
    t.Eq(OWJson::Quote(u8"\u00e9\U0001F600/ \x7f"), std::string(u8"\"\u00e9\U0001F600/ \x7f\""), "everything else as itself");
    // A lone surrogate from an escape is kept and written back escaped, as JSON.stringify writes it.
    OWJsonValue lone = ParseOk(t, "[\"a\\ud83db\", \"\\udc00\"]");
    t.Eq(OWJson::Stringify(lone), std::string("[\n  \"a\\ud83db\",\n  \"\\udc00\"\n]\n"), "lone surrogates");
    OWJsonValue pair = ParseOk(t, "\"\\ud83d\\ude00\\u00E9\"");
    t.Eq(pair.Text(), std::string(u8"\U0001F600\u00e9"), "a surrogate pair is one code point");
    OWJsonValue back = ParseOk(t, OWJson::Stringify(OWJsonValue::String(std::string("\x01", 1))));
    t.Eq(back.Text(), std::string("\x01"), "an escaped control character reads back");
}

OW_TEST(json_layout_and_key_order) {
    OWJsonValue v = ParseOk(t, "{\"z\":1.0,\"a\":[],\"m\":{},\"list\":[\"x\",2.0,null,true],\"o\":{\"k\":\"v\"}}");
    std::string want =
        "{\n  \"z\": 1,\n  \"a\": [],\n  \"m\": {},\n  \"list\": [\n    \"x\",\n    2,\n    null,\n    true\n  ],\n"
        "  \"o\": {\n    \"k\": \"v\"\n  }\n}\n";
    t.Eq(OWJson::Stringify(v), want, "2-space indent, keys as given, integral numbers as integers");
    OWJsonValue dup = ParseOk(t, "{\"a\":1,\"b\":2,\"a\":3}");
    t.Eq(OWJson::Stringify(dup), std::string("{\n  \"a\": 3,\n  \"b\": 2\n}\n"), "a key twice: first place, last value");
    OWJsonValue big = OWJsonValue::Object();
    for (int i = 0; i < 40; ++i) big.Set("k" + std::to_string(i), OWJsonValue::Number(int64_t(i)));
    std::string text = OWJson::Stringify(big);
    text.insert(text.size() - 2, ",\n  \"k3\": 99");
    OWJsonValue again = ParseOk(t, text);
    t.Eq(again.Size(), size_t(40), "a duplicate past 32 keys is found by the index");
    t.Eq(again.Find("k3") ? again.Find("k3")->Text() : std::string(), std::string("99"), "and takes the last value");
}

OW_TEST(json_a_non_finite_number_is_a_null_that_remembers_it) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    auto spelled = [](const OWJsonValue& v) { return std::string(v.NonFinite() ? v.NonFinite() : "<none>"); };
    for (auto [d, want] : {std::make_pair(nan, "NaN"), std::make_pair(inf, "Infinity"), std::make_pair(-inf, "-Infinity")}) {
        OWJsonValue v = OWJsonValue::Number(d);
        t.Ok(v.IsNull(), std::string(want) + ": a null, as JSON.stringify writes it");
        t.Eq(spelled(v), want, "it remembers the number");
        t.Eq(OWJson::Stringify(v), "null\n", std::string(want) + ": written null");
        t.Ok(v == OWJsonValue::Null(), std::string(want) + ": equal as values to null");
        OWJsonValue copy = v;
        t.Eq(spelled(copy), want, "a copy remembers it too");
    }
    t.Eq(spelled(OWJsonValue::Null()), "<none>", "a null made as one");
    t.Eq(spelled(OWJson::Parse("null").value), "<none>", "a null parsed");
    t.Eq(spelled(OWJsonValue::Number(1.5)), "<none>", "a finite number");
}

OW_TEST(json_parser_is_strict) {
    const char* bad[] = {"{\"a\":}", "[1,]", "{\"a\":1,}", "\"a\x01\"", "01", "+1", "1.", ".5", "1e", "-", "NaN",
                         "Infinity", "{} x", "// c\n{}", "{'a':1}", "[\"\\x\"]", "\"\\u12\"", "{\"a\" 1}", "[1 2]",
                         "tru", "nul", "", "  ", "\"abc", "{\"a\":1", "\xef\xbb\xbf\xef\xbb\xbf{}"};
    for (const char* b : bad) t.Ok(!OWJson::Parse(std::string(b)).ok, std::string("refused: ") + b);
    // Bytes that are not UTF-8: a lone continuation byte, an overlong slash, a surrogate encoded.
    for (std::string b : {std::string("\"\x80\""), std::string("\"\xc0\xaf\""), std::string("\"\xed\xa0\x80\"")})
        t.Ok(!OWJson::Parse(b).ok, "refused: not UTF-8");
    t.Ok(OWJson::Parse("\xef\xbb\xbf{\"id\":\"w\"}").ok, "one byte order mark at the start is read past");
    t.Ok(OWJson::Parse(" \t\r\n[1e5, -0.5E-3, 0, \"\\/\"] \n").ok, "white space and the grammar's corners");
    std::string deep(200, '[');
    deep += std::string(200, ']');
    t.Ok(OWJson::Parse(deep).ok, "200 levels deep");
    t.Ok(!OWJson::Parse("[" + deep + "]").ok, "201 levels: refused (too_deep, OW-25), not a stack overflow");
}

OW_TEST(json_example_world_rewrites_byte_for_byte) {
    // Hyperion was written by the reference implementation, so parse-then-write must give its bytes back.
    std::vector<std::string> files = {"world.json"};
    for (const std::string& type : OWFiles::List(P().example + "/elements", true))
        for (const std::string& f : OWFiles::List(P().example + "/elements/" + type, false))
            files.push_back("elements/" + type + "/" + f);
    size_t same = 0;
    for (const std::string& rel : files) {
        OWBytes bytes;
        OWFiles::Read(P().example + "/" + rel, bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        if (r.ok && OWJson::ToBytes(r.value) == bytes) ++same;
        else t.Fail(rel + " differs after a rewrite");
    }
    t.Ok(files.size() > 10, "the example has its files (" + std::to_string(files.size()) + ")");
    t.Eq(same, files.size(), "byte-equal");
}
