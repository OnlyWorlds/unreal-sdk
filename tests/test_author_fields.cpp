// A pack leaves out the five author fields (created_by, changed_by, changed_via, changed_client, doors) of every file
// under elements/ and spatial/ that is one JSON object, unless asked (OWWorldArchive::StripAuthorFields). Held to the
// npm SDK's and the Python package's shared fixture (tests/data/author-fields.fixture.json, the same bytes, held by its
// sha256): every case's entries byte for byte and its count, through every door (the file map, PackWorld, PackFolder
// from disk, Pack, a world read and packed). Then the edges the fixture does not hold, each with the npm SDK 4.203.1's
// verdict (measured on its build) unless the label says the core keeps the file's bytes: an integer JavaScript would
// round, a file nested past 200. Ported from @onlyworlds/sdk 4.203.1 (6794d27), test/world-file.test.mjs.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/author-fields.fixture.json at 4.203.1 (6794d27); the Python package holds the same bytes.
const char* const FixtureSha256 = "6943ce5a8c2cd7bf69f82facb6f1d545d87d68f5ea0104cc4d711d94e035898a";
const char* const WorldJson = "{\n  \"id\": \"0192a000-aaaa-7bbb-8ccc-000000000001\",\n  \"name\": \"W\"\n}\n";
const char* const Edge = "elements/object/edge.json";

const OWBytes& FixtureBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/author-fields.fixture.json", bytes);
        read = true;
    }
    return bytes;
}

std::shared_ptr<OWJsonValue> Doc() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWJsonParseResult r = OWJson::Parse(FixtureBytes());
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

const OWJsonValue& Get(const OWJsonValue& v, const char* key) {
    static const OWJsonValue none;
    const OWJsonValue* x = v.Find(key);
    return x ? *x : none;
}

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }
std::string Text(const OWBytes& b) { return std::string(b.begin(), b.end()); }

// A case's files or its default: path -> the string's UTF-8.
OWFileMap FilesOf(const OWJsonValue& texts) {
    OWFileMap out;
    for (size_t i = 0; i < texts.Size(); ++i) out[texts.Keys()[i]] = Bytes(texts.Items()[i].Text());
    return out;
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& x : v) out += (out.empty() ? "" : " ") + x;
    return "[" + out + "]";
}

// Each entry as text (a readable failure), then the maps whole.
void Same(owtest::T& t, const OWFileMap& got, const OWFileMap& want, const std::string& label) {
    std::vector<std::string> a, b;
    for (const auto& kv : got) a.push_back(kv.first);
    for (const auto& kv : want) b.push_back(kv.first);
    t.Eq(Join(a), Join(b), label + ": the entries");
    for (const auto& kv : want) {
        auto it = got.find(kv.first);
        if (it != got.end()) t.Eq(Text(it->second), Text(kv.second), label + ": " + kv.first);
    }
    t.Ok(got == want, label + ": byte for byte");
}

OWFileMap Entries(const OWPacked& packed) {
    OWArchiveRead r = OWWorldArchive::Read(packed.bytes);
    return r.ok ? r.files : OWFileMap{{"refused", Bytes(r.reason + " " + r.message)}};
}

void Case(owtest::T& t, const OWJsonValue& c, int n) {
    OWFileMap files = FilesOf(Get(c, "files"));
    OWFileMap want = FilesOf(Get(c, "default"));
    size_t stripped = static_cast<size_t>(Get(c, "stripped").AsDouble());

    OWFileMap map = files;
    std::vector<std::string> left = OWWorldArchive::StripAuthorFields(map);
    Same(t, map, want, "StripAuthorFields");
    t.Eq(left.size(), stripped, "the files that lost a field, counted");
    t.Ok(std::is_sorted(left.begin(), left.end()), "the paths in order");

    OWPacked world = OWWorldArchive::PackWorld(files, "w");
    t.Ok(world.ok, "PackWorld (" + world.message + ")");
    Same(t, Entries(world), want, "PackWorld, the default");
    t.Eq(Join(world.author_fields_left_out), Join(left), "PackWorld counts the same files");
    OWPacked asked = OWWorldArchive::PackWorld(files, "w", false, false, true);
    Same(t, Entries(asked), files, "PackWorld, asked");
    t.Eq(asked.author_fields_left_out.size(), size_t(0), "asked: nothing left out");

    const std::string base = P().tmp + "/author-fields/case-" + std::to_string(n);
    OWFiles::RemoveTree(base);
    for (const auto& kv : files) OWFiles::Write(base + "/w/" + kv.first, kv.second);
    OWPacked folder = OWWorldArchive::PackFolder(base + "/w");
    t.Ok(folder.ok, "PackFolder (" + folder.message + ")");
    Same(t, Entries(folder), want, "PackFolder, the default");
    t.Eq(Join(folder.author_fields_left_out), Join(left), "PackFolder counts the same files");
    Same(t, Entries(OWWorldArchive::PackFolder(base + "/w", false, false, true)), files, "PackFolder, asked");
    OWPacked written = OWWorldArchive::Pack(base + "/w", base + "/w.owworld");
    OWBytes on_disk;
    t.Ok(written.ok && OWFiles::Read(base + "/w.owworld", on_disk) && on_disk == folder.bytes, "Pack writes PackFolder's bytes");
    OWFiles::RemoveTree(base);

    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
    if (w->WriteRefusal().empty()) {
        OWPacked wp = w->Pack("w");
        Same(t, Entries(wp), want, "OWWorld::Pack, the default");
        t.Eq(Join(wp.author_fields_left_out), Join(left), "OWWorld::Pack counts the same files");
        Same(t, Entries(w->Pack("w", false, false, true)), files, "OWWorld::Pack, asked");
    }
    // PackFiles writes what it is handed (OW-9).
    Same(t, Entries(OWWorldArchive::PackFiles(files, "w")), files, "PackFiles");
}

std::string Nested(int depth) { return std::string(depth - 1, '[') + std::string(depth - 1, ']'); }

struct EdgeCase {
    std::string label, text, want;  // want: "" keeps the bytes, "rewritten" the object without created_by in section 5's form
};

std::vector<EdgeCase> Edges() {
    return {
        // Numbers: a file is rewritten only when every number in it is written back as JSON.stringify writes it.
        {"safe integers and minus zero", R"({"id":"d","n":9007199254740991,"m":-0,"created_by":"u"})",
         "{\n  \"id\": \"d\",\n  \"n\": 9007199254740991,\n  \"m\": 0\n}\n"},
        {"an integer past 2^53 that JavaScript writes the same", R"({"id":"c","n":18014398509481984,"created_by":"u"})",
         "{\n  \"id\": \"c\",\n  \"n\": 18014398509481984\n}\n"},
        // The npm SDK rewrites these two as 12345678901234567000 and 9007199254740992; the core keeps an integer as
        // written, so a rewrite would write another number than npm's: the file keeps its bytes.
        {"an integer JavaScript would round keeps its bytes", R"({"id":"a","n":12345678901234567891,"created_by":"u"})", ""},
        {"an integer one past 2^53 keeps its bytes", R"({"id":"b","n":9007199254740993,"created_by":"u"})", ""},
        {"an integer no double holds keeps its bytes", "{\"id\":\"e\",\"n\":" + std::string(400, '9') + ",\"created_by\":\"u\"}", ""},
        {"a number past a double keeps its bytes", R"({"id":"e","n":-1.5e400,"created_by":"u"})", ""},
        {"numbers in another spelling", R"({"id":"t","a":2.0,"b":1E21,"c":-0.0,"d":0.10,"e":1e-7,"f":5e-324,"created_by":"u"})",
         "{\n  \"id\": \"t\",\n  \"a\": 2,\n  \"b\": 1e+21,\n  \"c\": 0,\n  \"d\": 0.1,\n  \"e\": 1e-7,\n  \"f\": 5e-324\n}\n"},
        {"a number below the smallest double is 0, as JSON.parse reads it", R"({"id":"t","g":1e-400,"created_by":"u"})",
         "{\n  \"id\": \"t\",\n  \"g\": 0\n}\n"},
        {"doubles, exact", R"({"id":"t","x":263.2953629956518,"y":0.1,"z":1.5e300,"v":123.80196114964559,"created_by":"u"})",
         "{\n  \"id\": \"t\",\n  \"x\": 263.2953629956518,\n  \"y\": 0.1,\n  \"z\": 1.5e+300,\n  \"v\": 123.80196114964559\n}\n"},
        // Grammar: JSON.parse's, which the core's parser holds too.
        {"a comment keeps its bytes", R"({"id":"f",/* note */"created_by":"u"})", ""},
        {"single quotes keep their bytes", R"({'id':'g','created_by':'u'})", ""},
        {"a bare name keeps its bytes", R"({id:"h","created_by":"u"})", ""},
        {"a trailing comma keeps its bytes", R"({"id":"i","created_by":"u",})", ""},
        {"NaN keeps its bytes", R"({"id":"j","n":NaN,"created_by":"u"})", ""},
        {"a raw tab in a string keeps its bytes", "{\"id\":\"k\",\"name\":\"a\tb\",\"created_by\":\"u\"}", ""},
        {"a leading zero keeps its bytes", R"({"id":"l","n":01,"created_by":"u"})", ""},
        {"a bare point keeps its bytes", R"({"id":"m","n":.5,"created_by":"u"})", ""},
        {"two values keep their bytes", R"({"id":"c-9","created_by":"u"} {"hidden": true})", ""},
        // Strings: the core keeps a lone surrogate's escape and writes it back escaped, as the npm SDK does.
        {"a lone high surrogate, escaped as npm writes it", R"({"id":"s","name":"\ud800x","created_by":"u"})",
         "{\n  \"id\": \"s\",\n  \"name\": \"\\ud800x\"\n}\n"},
        {"a lone low surrogate, escaped as npm writes it", R"({"id":"s","name":"x\udc00","created_by":"u"})",
         "{\n  \"id\": \"s\",\n  \"name\": \"x\\udc00\"\n}\n"},
        {"a high surrogate at the end, escaped as npm writes it", R"({"id":"s","name":"x\ud800","created_by":"u"})",
         "{\n  \"id\": \"s\",\n  \"name\": \"x\\ud800\"\n}\n"},
        {"an escaped pair is written raw", R"({"id":"s","name":"\"\ud83d\ude00\\","created_by":"u"})",
         "{\n  \"id\": \"s\",\n  \"name\": \"\\\"\xF0\x9F\x98\x80\\\\\"\n}\n"},
        {"raw above the controls", R"({"id":"v","name":"a\u007f\u0085\u2029b","created_by":"u"})",
         "{\n  \"id\": \"v\",\n  \"name\": \"a\x7F\xC2\x85\xE2\x80\xA9" "b\"\n}\n"},
        {"an escaped name is a name", R"({"id":"w","\u0063reated_by":"u"})", "{\n  \"id\": \"w\"\n}\n"},
        {"a duplicate key: first place, last value", R"({"id":"r","name":"A","x":1,"created_by":"u","name":"B"})",
         "{\n  \"id\": \"r\",\n  \"name\": \"B\",\n  \"x\": 1\n}\n"},
        // JSON.stringify puts integer-like keys first ("0", "12", "id", "b"); here every key keeps its place.
        {"integer-like keys keep their order", R"({"id":"q","b":1,"12":2,"0":3,"created_by":"u"})",
         "{\n  \"id\": \"q\",\n  \"b\": 1,\n  \"12\": 2,\n  \"0\": 3\n}\n"},
        // Depth: the npm SDK reads to 200 deep, an object at the top being 1; the core's parser reads to 512.
        {"nested 200 deep", "{\"id\":\"o\",\"x_d\":" + Nested(200) + ",\"created_by\":\"u\"}", "rewritten"},
        {"nested 201 deep keeps its bytes", "{\"id\":\"p\",\"x_d\":" + Nested(201) + ",\"created_by\":\"u\"}", ""},
    };
}

void EdgeTest(owtest::T& t, const EdgeCase& e) {
    OWFileMap files{{"world.json", Bytes(WorldJson)}, {Edge, Bytes(e.text)}};
    std::vector<std::string> left = OWWorldArchive::StripAuthorFields(files);
    const std::string got = Text(files[Edge]);
    if (e.want.empty()) {
        t.Eq(got, e.text, "kept byte for byte, its author field included");
        t.Eq(left.size(), size_t(0), "nothing counted");
        return;
    }
    t.Eq(Join(left), Join({Edge}), "counted");
    if (e.want == "rewritten") {
        OWJsonParseResult r = OWJson::Parse(e.text);
        r.value.Erase("created_by");
        t.Eq(got, OWJson::Stringify(r.value), "section 5's form");
    } else {
        t.Eq(got, e.want, "written");
    }
}

}  // namespace

OW_TEST(author_fields_the_fixture_is_the_shared_file) {
    t.Eq(owtest::Sha256Hex(FixtureBytes()), std::string(FixtureSha256), "the fixture's bytes are npm's and Python's");
    const OWJsonValue& fields = Get(*Doc(), "fields");
    std::vector<std::string> names;
    for (const OWJsonValue& f : fields.Items()) names.push_back(f.Text());
    std::vector<std::string> core(std::begin(OWWorldArchive::AuthorFields), std::end(OWWorldArchive::AuthorFields));
    t.Eq(Join(names), Join({"created_by", "changed_by", "changed_via", "changed_client", "doors"}), "the five, in order");
    t.Eq(Join(core), Join(names), "the core's five");
    t.Eq(Get(*Doc(), "cases").Size(), size_t(7), "7 cases");
}

OW_CASES(author_fields_cases) {
    owtest::Cases out;
    auto doc = Doc();
    int n = 0;
    for (const OWJsonValue& c : Get(*doc, "cases").Items()) {
        const OWJsonValue* cp = &c;
        int i = ++n;
        out.push_back({"author fields " + std::to_string(i) + ": " + Get(c, "name").Text(),
                       [doc, cp, i](owtest::T& t) { Case(t, *cp, i); }});
    }
    for (const EdgeCase& e : Edges()) out.push_back({"author fields edge: " + e.label, [e](owtest::T& t) { EdgeTest(t, e); }});
    return out;
}

OW_TEST(author_fields_a_file_that_is_not_utf8_keeps_its_bytes) {
    const OWBytes latin = {0x7b, 0x22, 0x69, 0x64, 0x22, 0x3a, 0x22, 0xe9, 0x22, 0x2c, 0x22, 0x63, 0x72, 0x65,
                           0x61, 0x74, 0x65, 0x64, 0x5f, 0x62, 0x79, 0x22, 0x3a, 0x22, 0x75, 0x22, 0x7d};  // {"id":"\xe9","created_by":"u"}
    OWFileMap files{{"elements/character/latin.json", latin}};
    t.Eq(OWWorldArchive::StripAuthorFields(files).size(), size_t(0), "nothing counted");
    t.Ok(files["elements/character/latin.json"] == latin, "byte for byte");
    OWBytes out = {1, 2, 3};
    t.Ok(!OWWorldArchive::WithoutAuthorFields(latin, out) && out == OWBytes({1, 2, 3}), "WithoutAuthorFields: false, out untouched");
}
