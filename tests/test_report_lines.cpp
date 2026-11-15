// OWReport::ReadLine and ReadLines on the report profile's lines fixture (schema-dist profiles/report-lines.fixture.json,
// rule 10, dist.129's copy, its sha256 held to MANIFEST.json), read in the world its `world` names: every case one line
// as it stands, its address (ow://<type>/<id> or none) and its shown words; every text the lines it splits into, in
// order, each with its line, address and words. Each compared exactly, byte for byte; `kind`, `status` and `about` are
// notes, never compared. Then the npm SDK 4.169.0's own tests of the world: left out, and compared as given.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWMessage.h"
#include "OWReport.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.129's MANIFEST.json records it.
const char* const DistSha256 = "2face7cd693235d806e34d3884164b6934a78a299fb77f5ac283b89374e1b47a";
const char* const Id = "01a1d4ef-0000-7000-8000-000000000084";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/report-lines.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

std::vector<const OWJsonValue*> ListOf(const char* key) {
    const OWJsonValue* list = Fixture()->Find(key);
    return list ? OWMessage::List(*list) : std::vector<const OWJsonValue*>();
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }
std::string Str(const OWJsonValue& v, const char* key) { return Str(v.Find(key)); }

std::string World() { return Str(*Fixture(), "world"); }

// The address as the fixture writes it: the text, or "null" for none (no address is the four letters).
std::string AddressOf(const OWJsonValue& row) {
    const OWJsonValue* a = row.Find("address");
    return a && a->IsString() ? a->Text() : std::string("null");
}
std::string AddressOf(const OWReportLine& line) { return line.address ? *line.address : std::string("null"); }

// One row as one string, each part bracketed, so a mismatch shows where it is.
std::string Row(const std::string& line, const std::string& address, const std::string& words) {
    return "[" + line + "] [" + address + "] [" + words + "]";
}

void LineCase(owtest::T& t, const OWJsonValue& c) {
    const std::string line = Str(c, "line");
    const OWReportLine got = OWReport::ReadLine(line, World());
    t.Eq(got.line, line, "the line as read");
    t.Eq(AddressOf(got), AddressOf(c), "the address");
    t.Eq(got.words, Str(c, "words"), "the words");
    if (!got.address) {
        t.Ok(!got.reading && !got.start && !got.end, "no reading and no place without an address");
        return;
    }
    if (!got.reading || !got.start || !got.end || *got.end < *got.start || *got.end > line.size())
        return t.Fail("an address with no reading or no place");
    // The address as written stands at start..end (UTF-8 bytes) and reads as the element named.
    const OWAddressReading r = OWAddress::Parse(std::string_view(line).substr(*got.start, *got.end - *got.start));
    t.Ok(r.ok && r.type == got.reading->type && r.id == got.reading->id && r.world == got.reading->world &&
             r.canonical == got.reading->canonical,
         "the slice reads as the line's reading");
    t.Eq("ow://" + got.reading->type.value_or("") + "/" + got.reading->id.value_or(""), AddressOf(c),
         "the address is the reading's");
}

void TextCase(owtest::T& t, const OWJsonValue& x) {
    std::vector<std::string> want, got;
    const OWJsonValue* lines = x.Find("lines");
    if (lines)
        for (const OWJsonValue* l : OWMessage::List(*lines)) want.push_back(Row(Str(*l, "line"), AddressOf(*l), Str(*l, "words")));
    for (const OWReportLine& l : OWReport::ReadLines(Str(x, "text"), World()))
        got.push_back(Row(l.line, AddressOf(l), l.words));
    t.Eq(got.size(), want.size(), "the number of lines");
    for (size_t i = 0; i < want.size() && i < got.size(); ++i) t.Eq(got[i], want[i], "line " + std::to_string(i));
}

std::string Pad(size_t i, size_t width) {
    std::string s = std::to_string(i);
    return std::string(width > s.size() ? width - s.size() : 0, '0') + s;
}

}  // namespace

OW_TEST(report_lines_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the lines fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.129's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/report-lines.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    t.Eq(Str(*Fixture(), "profile"), std::string("report"), "the report profile's fixture");
    t.Eq(Str(*Fixture(), "table"), std::string("lines"), "its lines table");
    t.Eq(ListOf("cases").size(), size_t(101), "101 cases (dist.129)");
    t.Eq(ListOf("texts").size(), size_t(16), "16 texts (dist.129)");
    bool none = false, whole = false, cut = false;
    for (const OWJsonValue* c : ListOf("cases")) {
        const bool has = AddressOf(*c) != "null";
        none = none || !has;
        whole = whole || (has && Str(*c, "words") == Str(*c, "line"));
        cut = cut || (has && Str(*c, "words") != Str(*c, "line"));
    }
    t.Ok(none && whole && cut, "rows of each kind: no address, shown whole, shown without it");
}

OW_CASES(report_lines_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const std::vector<const OWJsonValue*> cases = ListOf("cases"), texts = ListOf("texts");
    if (cases.empty() || texts.empty()) {
        out.emplace_back("report_lines_fixture:not-found", [](owtest::T& t) { t.Fail("the rows did not load"); });
        return out;
    }
    for (size_t i = 0; i < cases.size(); ++i) {
        const OWJsonValue* c = cases[i];
        out.emplace_back("report_lines_fixture:case " + Pad(i, 3) + " " + Str(*c, "kind"),
                         [doc, c](owtest::T& t) { LineCase(t, *c); });
    }
    for (size_t i = 0; i < texts.size(); ++i) {
        const OWJsonValue* x = texts[i];
        out.emplace_back("report_lines_fixture:text " + Pad(i, 2) + " " + Str(*x, "kind"),
                         [doc, x](owtest::T& t) { TextCase(t, *x); });
    }
    return out;
}

// -- beside the fixture ------------------------------------------------------------------------------------------------

OW_TEST(report_lines_without_a_world_only_an_address_with_no_world_is_the_lines) {
    const std::string w = World(), id = Id;
    t.Ok(!OWReport::ReadLine("Built: ow://" + w + "/object/" + id).address, "a world named, none given");
    t.Eq(AddressOf(OWReport::ReadLine("Built: ow://object/" + id)), "ow://object/" + id, "no world named");
    t.Eq(OWReport::ReadLine("Built: ow://object/" + id, std::nullopt).words, std::string("Built"), "no world is none");
    t.Eq(AddressOf(OWReport::ReadLine("Built: ow://" + w + "/object/" + id, w)), "ow://object/" + id, "this world");
}

OW_TEST(report_lines_the_world_is_compared_as_given) {
    std::string upper = World();
    for (char& ch : upper) ch = (ch >= 'a' && ch <= 'f') ? static_cast<char>(ch - 'a' + 'A') : ch;
    t.Ok(!OWReport::ReadLine("Built: ow://" + World() + "/object/" + Id, upper).address,
         "an upper-case world id names no world here");
}

OW_TEST(report_lines_read_lines_splits_at_cr_and_lf_only) {
    // U+2028 stays inside a line; a line of a no-break space alone is no line.
    const std::string text = std::string("Built: ow://object/") + Id + "\r\n\r\n  nothing changed\xE2\x80\xA8still\n\xC2\xA0\n";
    const std::vector<OWReportLine> got = OWReport::ReadLines(text);
    t.Eq(got.size(), size_t(2), "two lines");
    if (got.size() == 2) {
        t.Eq(got[0].words, std::string("Built"), "the first, its address out");
        t.Eq(got[1].line, std::string("nothing changed\xE2\x80\xA8still"), "U+2028 stays inside a line");
        t.Ok(!got[1].address, "no address");
    }
    t.Eq(OWReport::ReadLines("").size(), size_t(0), "an empty text has no line");
}
