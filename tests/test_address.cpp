// OWAddress on the address profile's conformance fixture (schema-dist profiles/address.fixture.json, the pinned copy,
// dist.146's, its seventy-seven prose cases added at dist.123, dist.126, dist.127 and dist.146, its two prose-all cases at dist.128): every format case through WriteJson, every parse case through Parse,
// every prose case through ReadAt (an address found inside a line, serial 123), each refusal by the reason the fixture
// names. Then the readings the fixture leaves open, as the TypeScript and Python packages and the Unity SDK answer them: the writer's refusal
// order, a moment with a zero fraction, an empty string, an unknown key.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWUnicode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.146's MANIFEST.json records it.
const char* const DistSha256 = "922182d418225234184c5a7e3a3a152e2fde8059c0fe2aa98596a7faa0aab487";

const std::string World = "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f";
const std::string Element = "019a1f3c-2b4e-7d10-8c3a-6f2e9b0d4a71";
const std::string Lens = "019a2b7e-4c1d-7f20-9b3e-1a2c3d4e5f60";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/address.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

OWJsonValue Opt(const std::optional<std::string>& s) { return s ? OWJsonValue::String(*s) : OWJsonValue::Null(); }

// A prose case's index counts code points (the fixture's note; index_utf16 is for UTF-16 readers, serial 127); the
// core's ReadAt takes a byte offset into the UTF-8 text. Walks that many characters from the text's start, each as
// long as its lead byte says; npos when the walk leaves the text.
size_t ByteOffsetOfCodePoint(std::string_view text, size_t code_points) {
    size_t at = 0;
    for (size_t n = 0; n < code_points; ++n) {
        if (at >= text.size()) return std::string::npos;
        const uint8_t lead = static_cast<uint8_t>(text[at]);
        at += lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    }
    return at <= text.size() ? at : std::string::npos;
}

// A reading as the fixture writes one: every key, absent as JSON null.
OWJsonValue AsJson(const OWAddressReading& r) {
    OWJsonValue o = OWJsonValue::Object();
    o.Set("form", OWJsonValue::String(r.form));
    o.Set("world", Opt(r.world));
    o.Set("world_is_slug", OWJsonValue::Bool(r.world_is_slug));
    o.Set("type", Opt(r.type));
    o.Set("id", Opt(r.id));
    o.Set("lens", Opt(r.lens));
    o.Set("moment", r.moment ? OWJsonValue::Number(int64_t(*r.moment)) : OWJsonValue::Null());
    o.Set("canonical", OWJsonValue::Bool(r.canonical));
    return o;
}

std::string Compact(const OWJsonValue& v) {
    // Section 5's layout on one line: each newline dropped with the indent after it.
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    return out;
}

// The same keys and values, in any order.
bool SameReading(const OWJsonValue& want, const OWJsonValue& got) {
    if (!want.IsObject() || want.Size() != got.Size()) return false;
    for (size_t i = 0; i < want.Size(); ++i) {
        const OWJsonValue* g = got.Find(want.Keys()[i]);
        if (!g || !(*g == want.Items()[i])) return false;
    }
    return true;
}

void Case(owtest::T& t, const OWJsonValue& c) {
    std::string name = Str(c, "name"), kind = Str(c, "kind"), refuse = Str(c, "refuse");
    if (kind == "format") {
        const OWJsonValue* address = c.Find("address");
        if (!address) return t.Fail("format/" + name + ": no address");
        OWAddressWritten w = OWAddress::WriteJson(*address);
        if (!refuse.empty()) {
            t.Ok(!w.ok, "format/" + name + ": wrote " + w.address + ", the fixture refuses it as " + refuse);
            t.Eq(w.reason, refuse, "format/" + name + " (" + w.message + ")");
        } else {
            t.Ok(w.ok, "format/" + name + ": refused as " + w.reason + " " + w.message);
            t.Eq(w.address, Str(c, "expect"), "format/" + name);
        }
    } else if (kind == "parse") {
        OWAddressReading r = OWAddress::Parse(Str(c, "text"));
        if (!refuse.empty()) {
            t.Ok(!r.ok, "parse/" + name + ": read " + Compact(AsJson(r)) + ", the fixture refuses it as " + refuse);
            t.Eq(r.reason, refuse, "parse/" + name);
        } else {
            t.Ok(r.ok, "parse/" + name + ": refused as " + r.reason);
            const OWJsonValue* want = c.Find("expect");
            OWJsonValue got = AsJson(r);
            t.Ok(want && SameReading(*want, got),
                 "parse/" + name + ": gave " + Compact(got) + ", the fixture says " + (want ? Compact(*want) : "nothing"));
        }
    } else if (kind == "prose") {
        // ReadAt finds the address that starts at the case's index: the slice it covers, and Parse's reading of that
        // slice; or nothing, where the fixture finds nothing. The fixture's index counts code points; since serial
        // 127 four cases hold a letter or a mark before it, so it is walked into a byte offset here.
        const OWJsonValue* expect = c.Find("expect");
        const OWJsonValue* address = expect ? expect->Find("address") : nullptr;
        const std::string text = Str(c, "text");
        const OWJsonValue* at = c.Find("index");
        const size_t index =
            at && at->IsNumber() ? ByteOffsetOfCodePoint(text, size_t(std::stoll(at->Text()))) : std::string::npos;
        t.Ok(index != std::string::npos, "prose/" + name + ": its code point index is inside the text");
        std::optional<OWAddressFound> found = OWAddress::ReadAt(text, index);
        if (!address || address->IsNull()) {
            t.Ok(!found, "prose/" + name + ": found " + (found ? text.substr(index, found->end - index) : std::string()) +
                             ", the fixture finds no address");
            const OWJsonValue* reading = expect ? expect->Find("reading") : nullptr;
            t.Ok(!reading || reading->IsNull(), "prose/" + name + ": no address, no reading");
        } else {
            const std::string want_address = address->Text();
            t.Ok(found.has_value(), "prose/" + name + ": found no address at " + std::to_string(index) +
                                        ", the fixture finds " + want_address);
            if (found) {
                t.Eq(found->end, index + want_address.size(), "prose/" + name + ": the end");
                t.Eq(text.substr(index, found->end - index), want_address, "prose/" + name + ": the slice at the index");
                const OWJsonValue* want = expect->Find("reading");
                OWJsonValue got = AsJson(found->reading);
                t.Ok(want && SameReading(*want, got), "prose/" + name + ": gave " + Compact(got) +
                                                          ", the fixture says " + (want ? Compact(*want) : "nothing"));
            }
        }
    } else if (kind == "prose-all") {
        // FindAll finds every address in the text, in order; found is each one's slice of the text, never an index, so
        // it reads the same in every language (serial 128). Each reading is Parse's of its slice.
        const std::string text = Str(c, "text");
        OWJsonValue got = OWJsonValue::Array();
        const std::vector<OWAddressFound> all = OWAddress::FindAll(text);
        for (const OWAddressFound& f : all) {
            got.Push(OWJsonValue::String(text.substr(f.start, f.end - f.start)));
            OWJsonValue reading = AsJson(f.reading);
            t.Ok(SameReading(AsJson(OWAddress::Parse(text.substr(f.start, f.end - f.start))), reading),
                 "prose-all/" + name + ": the reading at " + std::to_string(f.start));
        }
        const OWJsonValue* want = c.Find("found");
        t.Ok(want && got == *want, "prose-all/" + name + ": found " + Compact(got) + ", the fixture finds " +
                                       (want ? Compact(*want) : "nothing"));
    } else {
        t.Fail("case " + name + ": a case of unknown kind '" + kind + "'");
    }
}

OWJsonValue J(const std::string& text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? r.value : OWJsonValue();
}

// The reason WriteJson refuses this JSON by ("" when it writes it).
std::string Refusal(const std::string& json) { return OWAddress::WriteJson(J(json)).reason; }
std::string Written(const std::string& json) { return OWAddress::WriteJson(J(json)).address; }

}  // namespace

OW_TEST(address_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the address fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.128's");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("address"), "the address profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    size_t format = 0, parse = 0, prose = 0, prose_all = 0, refusals = 0;
    for (const OWJsonValue& c : cases->Items()) {
        format += Str(c, "kind") == "format";
        parse += Str(c, "kind") == "parse";
        prose += Str(c, "kind") == "prose";
        prose_all += Str(c, "kind") == "prose-all";
        refusals += c.Has("refuse");
    }
    t.Eq(cases->Size(), size_t(219), "219 cases");
    t.Eq(format, size_t(36), "36 format cases");
    t.Eq(parse, size_t(104), "104 parse cases");
    t.Eq(prose, size_t(77), "77 prose cases (57 at dist.123, four more at dist.126, fifteen at dist.127, one at dist.146), each through ReadAt");
    t.Eq(prose_all, size_t(2), "2 prose-all cases (dist.128), each through FindAll");
    t.Eq(refusals, size_t(75), "20 format refusals and 55 parse refusals");
}

// The prose loop walks each case's code point index into a byte offset. Where the text before the index is ASCII the
// two agree; serial 127's four cases with a letter or a mark before it are where they differ, each byte offset counted
// from its text by hand (é is two bytes, U+0301 two, U+1D49C four).
OW_TEST(address_fixture_prose_index_walks_to_its_byte_offset) {
    const OWJsonValue* cases = Fixture()->Find("cases");
    t.Ok(cases && cases->IsArray(), "the fixture's cases");
    if (!cases) return;
    const std::vector<std::pair<std::string, size_t>> want = {
        {"prose-start-glued-non-ascii-letter", 5},  // "café": index 4
        {"prose-start-glued-combining-mark", 6},    // "cafe" + U+0301: index 5
        {"prose-start-glued-astral-letter", 4},     // U+1D49C: index 1
        {"prose-start-astral-letter-then-space", 5},  // U+1D49C and a space: index 2
    };
    std::vector<std::pair<std::string, size_t>> differ;
    for (const OWJsonValue& c : cases->Items()) {
        if (Str(c, "kind") != "prose") continue;
        const std::string text = Str(c, "text");
        const size_t index = size_t(std::stoll(c.Find("index")->Text()));
        const size_t bytes = ByteOffsetOfCodePoint(text, index);
        t.Ok(bytes != std::string::npos, Str(c, "name") + ": inside the text");
        if (bytes != index) differ.emplace_back(Str(c, "name"), bytes);
        else t.Ok(OWUnicode::IsAscii(std::string_view(text).substr(0, index)), Str(c, "name") + ": ASCII up to its index");
    }
    t.Ok(differ == want, "the four cases whose byte offset is not their code point index, at their bytes");
}

OW_TEST(address_type_words_and_refusals_are_the_fixtures) {
    auto doc = Fixture();
    std::vector<std::string> mine(std::begin(OWAddress::TypeWords), std::end(OWAddress::TypeWords)), theirs;
    if (const OWJsonValue* types = doc->Find("types"))
        for (const OWJsonValue& v : types->Items()) theirs.push_back(v.Text());
    t.Ok(mine == theirs, "the 22 type words, the fixture's list in its order");
    // And the schema's own 22.
    OWBytes bytes;
    OWFiles::Read(P().root + "/codegen/schema-dist/schema.json", bytes);
    OWJsonParseResult schema = OWJson::Parse(bytes);
    const OWJsonValue* st = schema.ok ? schema.value.Find("types") : nullptr;
    std::vector<std::string> schema_types = st ? st->Keys() : std::vector<std::string>();
    std::sort(mine.begin(), mine.end());
    std::sort(schema_types.begin(), schema_types.end());
    t.Ok(mine == schema_types, "the type words are schema.json's types");

    std::vector<std::string> reasons(std::begin(OWAddress::Refusals), std::end(OWAddress::Refusals)), listed;
    if (const OWJsonValue* r = doc->Find("refusals"))
        for (const OWJsonValue& v : r->Items()) listed.push_back(v.Text());
    t.Ok(reasons == listed, "the ten refusals, in the order a parser checks them");
}

OW_CASES(address_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back("address_fixture:not-found", [](owtest::T& t) { t.Fail("the address fixture did not load"); });
        return out;
    }
    // Every case by name; a prose case skips by name, with its reason (ProseSkip).
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("address_fixture:" + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c); });
    return out;
}

OW_TEST(address_every_reading_writes_back_canonical) {
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) return t.Fail("the address fixture did not load");
    size_t read = 0;
    for (const OWJsonValue& c : cases->Items()) {
        if (Str(c, "kind") != "parse" || !c.Has("expect")) continue;
        std::string name = Str(c, "name"), text = Str(c, "text");
        OWAddressReading r = OWAddress::Parse(text);
        t.Ok(r.ok, name);
        if (r.world_is_slug) {
            // A writer never writes a slug.
            t.Eq(OWAddress::Write(r).reason, std::string("bad_world"), name + ": a slug is not written back");
            continue;
        }
        OWAddressWritten w = OWAddress::Write(r);
        t.Ok(w.ok, name + ": written back");
        if (r.canonical) t.Eq(w.address, text, name + ": a canonical string is what the writer writes");
        OWAddressReading again = OWAddress::Parse(w.address);
        t.Ok(again.ok && again.canonical, name + ": what the writer wrote reads canonical: " + w.address);
        t.Eq(OWAddress::Write(again).address, w.address, name + ": and writes back the same");
        ++read;
    }
    t.Ok(read > 40, "the fixture's readings (" + std::to_string(read) + ")");
}

OW_TEST(address_typed_writer_three_spellings_and_a_world) {
    auto write = [](OWAddressParts p) { return OWAddress::Write(p); };
    t.Eq(write({"human", World, "character", Element, {}, {}}).address,
         "https://www.onlyworlds.com/w/" + World + "/character/" + Element, "human");
    t.Eq(write({"text", {}, "object", Element, Lens, -7}).address, "ow://object/" + Element + "@-7?lens=" + Lens,
         "text inside a world, moment and lens");
    t.Eq(write({"text", World, "location", Element, {}, INT32_MAX}).address,
         "ow://" + World + "/location/" + Element + "@2147483647", "text across worlds, the largest step");
    t.Eq(write({"text", World, {}, {}, {}, {}}).address, "ow://" + World, "a world's text address");
    t.Eq(write({"human", World, {}, {}, {}, 0}).address, "https://www.onlyworlds.com/w/" + World + "@0",
         "a world's human address at step 0");
    std::string upper = World;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) { return c >= 'a' && c <= 'f' ? char(c - 32) : c; });
    t.Eq(write({"text", upper, {}, {}, {}, {}}).address, "ow://" + World, "an id read in either case, written lowercase");
    t.Eq(write({"human", {}, "character", Element, {}, {}}).reason, std::string("no_world"), "the human form needs a world");
    t.Eq(write({"Human", World, {}, {}, {}, {}}).reason, std::string("bad_form"), "a form in another case");
    t.Eq(write({"text", {}, {}, Element, {}, {}}).reason, std::string("segments"), "an id without a type");
}

OW_TEST(address_parse_refusals_and_readings_beside_the_fixture) {
    t.Eq(OWAddress::Parse("").reason, std::string("not_an_address"), "an empty string");
    t.Eq(OWAddress::Parse("ow://dragon/" + Element).reason, std::string("unknown_type"), "not a type word");
    OWAddressReading world = OWAddress::Parse("ow://" + World);
    t.Ok(world.ok && world.IsWorld() && world.form == "text", "a world's own text address");
    OWAddressReading element = OWAddress::Parse("ow://object/" + Element);
    t.Ok(element.ok && !element.IsWorld() && !element.world, "an in-world element has no world");
    // The trim is Unicode's White_Space: U+3000 and a tab go, U+001C (a Python str.strip() space, not White_Space) stays.
    OWAddressReading spaced = OWAddress::Parse("\xE3\x80\x80ow://" + World + "\t");
    t.Ok(spaced.ok && !spaced.canonical, "White_Space trimmed, and the string as received is not canonical");
    t.Eq(OWAddress::Parse("\x1Cow://" + World).reason, std::string("not_an_address"), "U+001C is not trimmed");
}

// -- the readings the fixture leaves open, as the reference packages answer them ------------------------------------

OW_TEST(address_writer_refuses_form_then_segments_then_no_world) {
    // A type without an id (or an id without a type) and no world: segments before no_world.
    t.Eq(Refusal("{\"form\": \"human\", \"type\": \"pin\"}"), std::string("segments"), "a type alone, no world");
    t.Eq(Refusal("{\"form\": \"human\", \"id\": \"" + Element + "\"}"), std::string("segments"), "an id alone, no world");
    t.Eq(OWAddress::Write(OWAddressParts{"human", {}, "character", {}, {}, {}}).reason, std::string("segments"),
         "the typed writer alike");
    // A form it does not know before either.
    t.Eq(Refusal("{\"form\": \"url\", \"type\": \"pin\"}"), std::string("bad_form"), "an unknown form first");
    t.Eq(Refusal("{\"type\": \"pin\"}"), std::string("bad_form"), "a missing form first");
    // With a whole triple, or nothing, the missing world is what refuses.
    t.Eq(Refusal("{\"form\": \"human\", \"type\": \"pin\", \"id\": \"" + Element + "\"}"), std::string("no_world"),
         "a human triple with no world");
    t.Eq(Refusal("{\"form\": \"text\"}"), std::string("no_world"), "nothing at all");
    // Then the reader's order: the world before the type, the type before the id.
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"hyperion\", \"type\": \"dragon\", \"id\": 7}"),
         std::string("bad_world"), "the world before the type");
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"" + World + "\", \"type\": \"dragon\", \"id\": 7}"),
         std::string("unknown_type"), "the type before the id");
}

OW_TEST(address_json_moment_with_a_zero_fraction_is_a_whole_step) {
    t.Eq(Written("{\"form\": \"text\", \"type\": \"character\", \"id\": \"" + Element + "\", \"moment\": 3.0}"),
         "ow://character/" + Element + "@3", "3.0 is the step 3, written @3");
    t.Eq(Written("{\"form\": \"text\", \"world\": \"" + World + "\", \"moment\": -0.0}"), "ow://" + World + "@0",
         "-0.0 is the step 0");
    t.Eq(Written("{\"form\": \"text\", \"world\": \"" + World + "\", \"moment\": -2147483648.0}"),
         "ow://" + World + "@-2147483648", "the smallest step, as a float");
    t.Eq(Written("{\"form\": \"text\", \"world\": \"" + World + "\", \"moment\": 3e2}"), "ow://" + World + "@300",
         "an exponent that makes a whole number");
    // A fraction, a whole number past the 32-bit range (as a float, or an integer past 64 bits), refuses.
    for (const char* m : {"1.5", "2147483648.0", "2147483648", "-2147483649", "1e20", "1e400",
                          "1000000000000000000000000000000", "\"3\"", "true", "[3]"})
        t.Eq(Refusal("{\"form\": \"text\", \"world\": \"" + World + "\", \"moment\": " + m + "}"),
             std::string("bad_moment"), std::string("moment ") + m);
    // A refusal before the moment still names itself, whatever the moment holds.
    t.Eq(Refusal("{\"form\": \"human\", \"moment\": 1000000000000000000000000000000}"), std::string("no_world"),
         "no_world before the moment");
}

OW_TEST(address_empty_string_is_present_only_null_or_missing_is_absent) {
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"\", \"type\": \"character\", \"id\": \"" + Element + "\"}"),
         std::string("bad_world"), "world \"\"");
    t.Eq(Refusal("{\"form\": \"human\", \"world\": \"\"}"), std::string("bad_world"), "a human world \"\"");
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"" + World + "\", \"lens\": \"\"}"), std::string("bad_lens"),
         "lens \"\"");
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"" + World + "\", \"type\": \"\", \"id\": \"\"}"),
         std::string("unknown_type"), "type and id \"\"");
    t.Eq(Refusal("{\"form\": \"text\", \"type\": \"\", \"id\": \"" + Element + "\"}"), std::string("unknown_type"),
         "type \"\"");
    t.Eq(Refusal("{\"form\": \"text\", \"type\": \"character\", \"id\": \"\"}"), std::string("bad_id"), "id \"\"");
    t.Eq(Refusal("{\"form\": \"text\", \"world\": \"" + World + "\", \"type\": \"\"}"), std::string("segments"),
         "type \"\" with no id");
    // null and a missing key are the same absence.
    t.Eq(Written("{\"form\": \"text\", \"world\": \"" + World +
                 "\", \"type\": null, \"id\": null, \"lens\": null, \"moment\": null}"),
         "ow://" + World, "every null part absent");
    t.Eq(OWAddress::Write(OWAddressParts{"text", World, {}, {}, std::string(), {}}).reason, std::string("bad_lens"),
         "the typed writer alike");
}

OW_TEST(address_unknown_key_is_an_error_not_ignored) {
    for (const char* key : {"wrold", "Type", "ID", "x"}) {
        OWAddressWritten w = OWAddress::WriteJson(J("{\"form\": \"text\", \"world\": \"" + World + "\", \"" + key + "\": null}"));
        t.Ok(!w.ok, std::string("refused: ") + key);
        t.Eq(w.reason, std::string(), std::string("an error, not a refusal: ") + key);
        t.Ok(w.message.find(std::string("'") + key + "'") != std::string::npos, "the message names " + std::string(key));
    }
    // A reading's two flags are keys of the shape, and ignored.
    t.Eq(Written("{\"form\": \"text\", \"world\": \"" + World + "\", \"world_is_slug\": true, \"canonical\": \"no\"}"),
         "ow://" + World, "world_is_slug and canonical ignored");
    OWAddressWritten array = OWAddress::WriteJson(J("[\"text\"]"));
    t.Ok(!array.ok && array.reason.empty() && !array.message.empty(), "not an object: an error");
}

// -- An address in prose, beside the fixture --------------------------------------------------------------------------

OW_TEST(address_read_at_the_index_defaults_to_0) {
    const OWJsonValue* cases = Fixture()->Find("cases");
    if (!cases) return t.Fail("no cases");
    int n = 0;
    for (const OWJsonValue& c : cases->Items()) {
        const OWJsonValue* at = c.Find("index");
        if (Str(c, "kind") != "prose" || !at || at->Text() != "0") continue;
        const std::string text = Str(c, "text");
        std::optional<OWAddressFound> given = OWAddress::ReadAt(text, 0), defaulted = OWAddress::ReadAt(text);
        t.Eq(defaulted.has_value(), given.has_value(), Str(c, "name"));
        if (given && defaulted) t.Eq(defaulted->end, given->end, Str(c, "name"));
        ++n;
    }
    t.Ok(n > 40, "cases at index 0: " + std::to_string(n));
}

OW_TEST(address_read_at_counts_utf8_bytes_so_an_emoji_before_the_address_still_slices_right) {
    // The TypeScript SDK's case, where the emoji is two UTF-16 units and the index 19: here it is four bytes.
    const std::string address = "ow://object/" + Element + "@4";
    const std::string text = "Built \xF0\x9F\x9A\xA2 the gate: " + address + ".";
    const size_t index = text.find("ow://");
    t.Eq(index, size_t(21), "the emoji is four UTF-8 bytes: 21 bytes come before the address");
    std::optional<OWAddressFound> found = OWAddress::ReadAt(text, index);
    t.Ok(found.has_value(), "found");
    if (!found) return;
    t.Eq(found->end, index + address.size(), "the end");
    t.Eq(text.substr(index, found->end - index), address, "the slice");
    t.Eq(text.substr(found->end), std::string("."), "the full stop after it");
    t.Eq(found->reading.id.value_or(""), Element, "the id");
    t.Eq(found->reading.moment.value_or(0), int32_t(4), "the moment");
}

OW_TEST(address_read_at_past_the_end_inside_a_character_or_empty_reads_nothing) {
    t.Ok(!OWAddress::ReadAt("ow://x", 99), "past the end");
    t.Ok(!OWAddress::ReadAt("", 0), "an empty text");
    t.Ok(!OWAddress::ReadAt("OW://object/" + Element), "the prefix as written, lowercase");
    t.Ok(!OWAddress::ReadAt("\xC3\xA9ow://object/" + Element, 1), "an index inside a character's bytes");
}

OW_TEST(address_find_all_finds_every_address_in_order_by_read_at_at_each_prefix) {
    // The TypeScript SDK's doc example: a start glued to a word is prose.
    const std::string id = "01a1d4ef-0000-7000-8000-000000000084";
    std::vector<OWAddressFound> one = OWAddress::FindAll("See ow://object/" + id + ", not elbow://object/" + id + ".");
    t.Eq(one.size(), size_t(1), "one address");
    if (one.size() == 1) {
        t.Eq(one[0].start, size_t(4), "where it starts");
        t.Eq(one[0].end, size_t(52), "where it ends");
        t.Eq(one[0].reading.type.value_or(""), std::string("object"), "its type");
        t.Eq(one[0].reading.id.value_or(""), id, "its id");
    }
    // Both forms, in the text's order, whichever prefix comes first; a start that reads nothing is passed by.
    const std::string human = "https://www.onlyworlds.com/w/" + World + "/object/" + Element;
    const std::string text = "Go to " + human + " then ow://nothing, then ow://object/" + Element + "@3.";
    std::vector<OWAddressFound> all = OWAddress::FindAll(text);
    t.Eq(all.size(), size_t(2), "two addresses");
    if (all.size() == 2) {
        t.Eq(text.substr(all[0].start, all[0].end - all[0].start), human, "the site's form first");
        t.Eq(all[0].reading.form, std::string(OWAddress::Human), "its form");
        t.Eq(text.substr(all[1].start, all[1].end - all[1].start), "ow://object/" + Element + "@3", "then the text form");
        t.Eq(all[1].reading.moment.value_or(0), int32_t(3), "its moment");
    }
    // Each is ReadAt's at its start, a byte offset: an emoji before it is four bytes.
    const std::string emoji = "Built \xF0\x9F\x9A\xA2 ow://object/" + Element + ".";
    std::vector<OWAddressFound> e = OWAddress::FindAll(emoji);
    t.Eq(e.size(), size_t(1), "one address after the emoji");
    if (e.size() == 1) {
        t.Eq(e[0].start, emoji.find("ow://"), "its start, in bytes");
        std::optional<OWAddressFound> at = OWAddress::ReadAt(emoji, e[0].start);
        t.Ok(at && at->end == e[0].end, "ReadAt's end");
    }
    t.Ok(OWAddress::FindAll("").empty(), "an empty text");
    t.Ok(OWAddress::FindAll("no address here, OW://object/" + Element).empty(), "the prefix as written, lowercase");
}

// A bare prefix is read once. What the TypeScript SDK's findOwLinks (4.203.0, its built dist/ on Node 22) took on each
// text, in ms, the slower of two medians of five runs: the name finder's timed recipe "address prefixes with no
// address" (60,000 "ow://character/", npm's test/fixtures/name-finder.cases.json) and 10,000 bare prefixes of both
// forms. The core is compiled (build.cmd's /O2) with npm's scan, so npm's time is the measure; three times it leaves
// room for a loaded machine and sits far under the shape this test exists to catch: a parse of a fresh substring at
// every length of each run, 17 to 20 times npm on the recipe (239 to 293 ms, measured before the finder read each
// candidate once; about 3 ms after, a fifth of npm's).
OW_TEST(address_find_all_reads_each_bare_prefix_once) {
    const double factor = 3.0;
    struct Bare {
        const char* name;
        std::string piece;
        int times;
        double npmMs;
    };
    const Bare texts[] = {
        {"recipe", "ow://character/", 60000, 14.3},
        {"both forms", "ow://the-world/character/someone https://www.onlyworlds.com/w/ab/character/ ", 5000, 4.0},
    };
    for (const Bare& b : texts) {
        std::string text;
        text.reserve(b.piece.size() * b.times);
        for (int i = 0; i < b.times; ++i) text += b.piece;
        const auto t0 = std::chrono::steady_clock::now();
        const size_t found = OWAddress::FindAll(text).size();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const double line = b.npmMs * factor;
        std::printf("      %.2f ms: FindAll on %s\n", ms, b.name);
        t.Eq(found, size_t(0), std::string(b.name) + ": no address");
        t.Ok(ms < line, std::string(b.name) + ": " + std::to_string(ms) + " ms, the line " + std::to_string(line) + " ms");
    }
}

OW_TEST(address_read_at_the_sites_form_a_run_cut_inside_a_word_is_no_address) {
    const std::string h = "https://www.onlyworlds.com/w/";
    t.Ok(!OWAddress::ReadAt(h + "hyperion\xF0\x9D\x92\x9C."), "a letter above U+FFFF (U+1D49C, four bytes) cuts");
    t.Ok(!OWAddress::ReadAt(h + "cafe\xCC\x81."), "a combining mark counts with the letters");
    t.Ok(!OWAddress::ReadAt(h + "hyper_ion."), "an underscore");
    std::optional<OWAddressFound> found = OWAddress::ReadAt(h + "hyperion\xF0\x9F\x9A\xA2");
    t.Eq(found ? found->end : size_t(0), (h + "hyperion").size(), "an emoji is no letter: the run ends before it");
}

// -- Where an address in prose begins (schema-dist serial 127) ----------------------------------------------------------
// The mirror of the cut after a run: a letter of any script, a combining mark or an underscore before the prefix glues
// it into a word, and the text there is prose; anything else, or the start of the text, does not. The character
// before is decoded whole: in UTF-8 a letter past ASCII is two to four bytes, and one byte back is only its last.
// Written ahead of the fixture and red first on the finder before the rule; the fixture's prose cases now carry the
// rule, and these stay as this SDK's own vectors (each found at the prefix's byte by find, its end checked).

OW_CASES(address_start_cases) {
    const std::string id = "01a1d4ef-0000-7000-8000-000000000084";
    struct Start {
        const char* name;
        std::string text, prefix;
        bool found;
    };
    const std::vector<Start> starts = {
        {"glued-letter", "Built the gate: elbow://object/" + id + ".", "ow://", false},
        {"glued-underscore", "tag_ow://object/" + id + " here", "ow://", false},
        {"glued-non-ascii-letter", "caf\xC3\xA9ow://object/" + id, "ow://", false},
        {"glued-combining-mark", "e\xCC\x81ow://object/" + id, "ow://", false},
        {"glued-astral-letter", "\xF0\x9D\x92\x9Cow://object/" + id, "ow://", false},
        {"after-hyphen", "x-ow://object/" + id, "ow://", true},
        {"after-digit", "3ow://object/" + id, "ow://", true},
        {"after-space", "Built the gate: ow://object/" + id + ".", "ow://", true},
        {"after-opening-bracket", "Built the gate (ow://object/" + id + ").", "ow://", true},
        {"after-quote", "Built the gate \"ow://object/" + id + "\".", "ow://", true},
        {"start-of-text", "ow://object/" + id + " was built.", "ow://", true},
        {"glued-letter-site-form", "seehttps://www.onlyworlds.com/w/hyperion/object/" + id, "https://", false},
    };
    owtest::Cases out;
    for (const Start& s : starts) {
        out.emplace_back(std::string("address_start:") + s.name, [s, id](owtest::T& t) {
            const size_t index = s.text.find(s.prefix);
            std::optional<OWAddressFound> found = OWAddress::ReadAt(s.text, index);
            if (!s.found) {
                t.Ok(!found, "the start is glued to a word: found " +
                                 (found ? s.text.substr(index, found->end - index) : std::string()) + " at " +
                                 std::to_string(index));
                return;
            }
            t.Ok(found.has_value(), "found an address at " + std::to_string(index));
            if (!found) return;
            t.Eq(found->reading.id.value_or(""), id, "the id");
            t.Eq(found->end, index + ("ow://object/" + id).size(), "the address ends where its id ends");
        });
    }
    return out;
}

OW_TEST(address_start_after_bytes_that_are_not_utf8_does_not_glue) {
    // A stray continuation byte, or a lead whose sequence is cut short, is no letter: the address is found.
    const std::string address = "ow://object/" + Element;
    for (const std::string& before : {std::string("\x80"), std::string("\xE2\x80"), std::string("a\xBF\xBF\xBF")}) {
        std::optional<OWAddressFound> found = OWAddress::ReadAt(before + address, before.size());
        t.Ok(found && found->end == before.size() + address.size(), "found after " + std::to_string(before.size()) + " bytes");
    }
}

OW_TEST(address_letters_and_marks_table) {
    for (char32_t c : {0x41, 0x7A, 0xE9, 0x301, 0x5D0, 0x938, 0x93F, 0x4E00, 0xAC00, 0x1D49C, 0x20000, 0x2B0, 0x20DD})
        t.Ok(OWUnicode::IsLetterOrMark(c), "L or M: " + std::to_string(uint32_t(c)));
    for (char32_t c : {0x30, 0x5F, 0x2D, 0x20, 0x2E, 0xD7, 0x1F6A2, 0x10FFFF, 0x0, 0x3000, 0x660})
        t.Ok(!OWUnicode::IsLetterOrMark(c), "neither: " + std::to_string(uint32_t(c)));
}
