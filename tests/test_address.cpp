// OWAddress on the address profile's conformance fixture (schema-dist profiles/address.fixture.json, the pinned copy,
// dist.126's, its sixty-one prose cases added at dist.123 and dist.126): every format case through WriteJson, every parse case through Parse,
// each refusal by the reason the fixture names. The prose cases are a finder's (an address found inside a line), and
// this core has none: each checks the half Parse owns (the line holds the address at the index, and Parse reads it
// whole as the case's reading), then is a named, counted skip (the port is OW Gaming #1527). Then the readings the
// fixture leaves open, as the TypeScript and Python packages and the Unity SDK answer them: the writer's refusal
// order, a moment with a zero fraction, an empty string, an unknown key.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"

#include <algorithm>
#include <memory>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.126's MANIFEST.json records it.
const char* const DistSha256 = "383c02312829a71110ef770cbc1e539515a2193a80e70e4d824158f1f8846598";

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

// Why a prose case is skipped: it is a finder's (where an address found inside a line ends, the packages'
// readOwLinkAt), and this core has none.
const char* const ProseSkip =
    "skipped by kind 'prose': no finder of an address in prose in this SDK yet (OW Gaming #1527)";

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
        // The half this core owns: where the case names an address, the line holds it at the index (every prose case is
        // ASCII to its address's end, so a byte index is the fixture's), and the whole-string parser reads it as the
        // case's reading. A failed check fails the case; then the finding itself, which needs a finder, is skipped.
        const OWJsonValue* expect = c.Find("expect");
        const OWJsonValue* address = expect ? expect->Find("address") : nullptr;
        if (address && address->IsString()) {
            const std::string text = Str(c, "text"), found = address->Text();
            const OWJsonValue* at = c.Find("index");
            const size_t index = at && at->IsNumber() ? size_t(std::stoll(at->Text())) : std::string::npos;
            t.Eq(index < text.size() ? text.substr(index, found.size()) : std::string(), found,
                 "prose/" + name + ": the line holds the address at its index");
            OWAddressReading r = OWAddress::Parse(found);
            t.Ok(r.ok, "prose/" + name + ": the address, read whole, refused as " + r.reason);
            const OWJsonValue* want = expect->Find("reading");
            OWJsonValue got = AsJson(r);
            t.Ok(want && SameReading(*want, got), "prose/" + name + ": read whole, gave " + Compact(got) +
                                                      ", the fixture says " + (want ? Compact(*want) : "nothing"));
        }
        t.Skip(ProseSkip);
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
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.126's");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("address"), "the address profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    size_t format = 0, parse = 0, prose = 0, refusals = 0;
    for (const OWJsonValue& c : cases->Items()) {
        format += Str(c, "kind") == "format";
        parse += Str(c, "kind") == "parse";
        prose += Str(c, "kind") == "prose";
        refusals += c.Has("refuse");
    }
    t.Eq(cases->Size(), size_t(201), "201 cases");
    t.Eq(format, size_t(36), "36 format cases");
    t.Eq(parse, size_t(104), "104 parse cases");
    t.Eq(prose, size_t(61), "61 prose cases (57 at dist.123), a finder's, each skipped by name (OW Gaming #1527)");
    t.Eq(refusals, size_t(75), "20 format refusals and 55 parse refusals");
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
