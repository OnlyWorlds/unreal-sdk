// The one-file world export onlyworlds.com hands an owner (keel's API spec, section 8c), read into an OWWorld. The
// fixture is keel's own: the emitter's output for one fixed world, pinned by a keel test and published in schema-dist as
// example/world-export.json, read here from the vendored tree (codegen/PIN). When keel's emitter moves on purpose, the
// dist moves with it, and the facts below with the pin. The edge cases are inline.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldExport.h"

#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const WorldId = "01999999-0000-7000-8000-000000000420";
const char* const Keep = "01999999-0000-7000-8000-000000000001";
const char* const Mill = "01999999-0000-7000-8000-000000000002";
const char* const Pip = "01999999-0000-7000-8000-000000000003";
const char* const Mara = "01999999-0000-7000-8000-000000000004";
const char* const Brave = "01999999-0000-7000-8000-000000000005";

std::string FixturePath() { return P().root + "/codegen/schema-dist/example/world-export.json"; }

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

std::string Envelope(const std::string& elements, const std::string& world = R"({"id":"w1","name":"W"})") {
    return R"({"format":"onlyworlds-world-export","world":)" + world + R"(,"elements":)" + elements + "}";
}

OWExportRead ReadText(const std::string& json, const std::string& source = "") {
    return OWWorldExport::Read(Bytes(json), source);
}

const OWJsonValue* At(const OWJsonValue& v, std::initializer_list<const char*> keys) {
    const OWJsonValue* cur = &v;
    for (const char* k : keys) {
        cur = cur && cur->IsObject() ? cur->Find(k) : nullptr;
        if (!cur) return nullptr;
    }
    return cur;
}

std::string Text(const OWJsonValue& v, std::initializer_list<const char*> keys) {
    const OWJsonValue* f = At(v, keys);
    return f && (f->IsString() || f->IsNumber()) ? f->Text() : std::string("<absent>");
}

bool StartsWith(const std::string& s, const std::string& head) { return s.compare(0, head.size(), head) == 0; }

std::vector<std::string> Strings(const OWJsonValue* v) {
    std::vector<std::string> out;
    if (v && v->IsArray())
        for (const OWJsonValue& i : v->Items()) out.push_back(i.IsString() ? i.Text() : "<not a string>");
    return out;
}

}  // namespace

// -- The file ---------------------------------------------------------------------------------------------------------

OW_TEST(export_the_fixture_is_the_dists_bytes) {
    OWBytes raw, manifest;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the vendored export is there");
    t.Ok(OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest), "the vendored manifest is there");
    OWJsonParseResult m = OWJson::Parse(manifest);
    t.Eq(owtest::Sha256Hex(raw), Text(m.value, {"files", "example/world-export.json"}),
         "the export's bytes are the ones the dist's manifest records (a checkout's line endings change them)");
}

OW_TEST(export_the_fixture_reads_whole) {
    OWExportRead r = OWWorldExport::ReadFile(FixturePath());
    t.Ok(r.ok, "the fixture is read: " + r.message);
    if (!r.world) return t.Fail("no world");
    OWWorld& w = *r.world;

    t.Eq(r.reason, std::string(), "no refusal");
    t.Eq(r.world_id, std::string(WorldId), "the world's id");
    t.Eq(w.Id(), std::string(WorldId), "the world's id, as a folder's");
    t.Eq(Text(w.info, {"name"}), std::string("Fixture World"), "the world's name");
    t.Eq(r.format, std::string("onlyworlds-world-export"), "the format");
    t.Eq(r.schema_version, std::string("00.34.01"), "the standard's version, as dist.149 sends it");
    t.Eq(r.exported_at, std::string("2026-10-19T12:30:00+00:00"), "when it was downloaded");
    t.Eq(w.root, std::string(), "no folder");
    t.Eq(w.elements.size(), size_t(5), "five elements");
    t.Eq(w.errors.size(), size_t(0), "nothing skipped");
    t.Eq(w.OfType("character").size(), size_t(2), "two characters");
    t.Eq(w.OfType("location").size(), size_t(2), "two locations");
    t.Eq(w.OfType("trait").size(), size_t(1), "one trait");
    for (const OWElement& e : w.elements) {
        t.Eq(e.folder_type, e.Type(), e.Id() + ": held as the type it declares");
        t.Ok(!e.edited, e.Id() + ": opening is not editing");
        t.Eq(e.source_path, std::string(), e.Id() + ": no file yet");
    }

    // Keel's download says where it came from and as whom (folder spec section 4.2), and the world keeps both.
    t.Eq(Text(w.info, {"source", "server"}), std::string("https://www.onlyworlds.com"), "source.server");
    t.Eq(Text(w.info, {"source", "world"}), std::string(WorldId), "source.world");
    t.Eq(Text(w.info, {"exported_as", "kind"}), std::string("owner"), "exported_as.kind");
    const OWJsonValue* character = At(w.info, {"exported_as", "character"});
    t.Ok(character && character->IsNull(), "exported_as.character is null");
    // The world block is the World whole, its first import and its extension store at the top level: kept as given.
    t.Eq(Text(w.info, {"origin", "producer", "tool"}), std::string("atlas"), "origin.producer.tool");
    t.Eq(Text(w.info, {"origin", "source", "world"}), std::string("01999999-0000-7000-8000-000000000419"),
         "origin.source.world");
    t.Eq(Text(w.info, {"x_names", "types", "character", "one"}), std::string("Villager"), "x_names");
    const OWJsonValue* pub = At(w.info, {"public_read"});
    t.Ok(pub && pub->IsBool(), "public_read is a boolean");
    t.Ok(!w.info.Has("api"), "no api block: the world is local, and no sync attaches to it");
}

OW_TEST(export_a_body_is_kept_as_given) {
    OWExportRead r = OWWorldExport::ReadFile(FixturePath());
    if (!r.world) return t.Fail("not read: " + r.message);
    OWWorld& w = *r.world;
    const OWElement* mara = w.Get(Mara);
    if (!mara) return t.Fail("Mara is not held");
    t.Eq(Text(mara->body, {"atlas_color"}), std::string("#8b0000"), "an extension field rides along");
    t.Eq(Text(mara->body, {"created_at"}), std::string("2026-10-19T12:05:00+00:00"), "a timestamp is its text");
    t.Eq(Text(mara->body, {"change_seq"}), std::string("5"), "a number keeps its text");
    t.Eq(mara->body.Keys().back(), std::string("atlas_color"), "the body's keys in the file's order");
    std::vector<OWElement*> characters = w.OfType("character");
    t.Ok(characters.size() == 2 && characters[1]->Id() == Mara, "in the file's order: Mara is the second character");
}

OW_TEST(export_links_read_as_the_schema_names_them) {
    OWExportRead r = OWWorldExport::ReadFile(FixturePath());
    if (!r.world) return t.Fail("not read: " + r.message);
    OWWorld& w = *r.world;
    const OWElement* pip = w.Get(Pip);
    const OWElement* mill = w.Get(Mill);
    const OWElement* mara = w.Get(Mara);
    if (!pip || !mill || !mara) return t.Fail("an element is not held");
    // A single link, a multi-link, and a link between two of one type.
    t.Eq(Text(pip->body, {"location"}), std::string(Mill), "Pip is at the Mill");
    t.Ok(Strings(pip->body.Find("traits")) == std::vector<std::string>{Brave}, "Pip's traits are [Brave]");
    t.Eq(Text(mill->body, {"parent_location"}), std::string(Keep), "the Mill's parent is the Keep");
    t.Eq(Text(mara->body, {"location"}), std::string(Keep), "Mara is at the Keep");
}

OW_TEST(export_writes_as_a_world_folder_the_folder_reader_reads) {
    OWExportRead r = OWWorldExport::ReadFile(FixturePath());
    if (!r.world) return t.Fail("not read: " + r.message);
    const std::string root = P().tmp + "/world-export-folder";
    OWFiles::RemoveTree(root);
    std::string why;
    t.Ok(r.world->WriteFolder(root, false, &why), "written as a world folder: " + why);

    std::unique_ptr<OWWorld> back = OWWorld::ReadFolder(root);
    if (!back) return t.Fail("the folder reader reads no world there");
    t.Eq(back->Id(), std::string(WorldId), "the world's id");
    t.Eq(back->elements.size(), size_t(5), "five elements");
    t.Eq(back->errors.size(), size_t(0), "nothing the folder reader could not read");
    t.Eq(Text(back->info, {"source", "server"}), std::string("https://www.onlyworlds.com"),
         "world.json carries the download's source as it is");
    t.Eq(Text(back->info, {"exported_as", "kind"}), std::string("owner"), "and its exported_as");
    t.Ok(OWFiles::IsFile(root + "/elements/character/pip--00000003.json"), "each element at section 3.2's path");
    for (const OWElement& e : r.world->elements) {
        const OWElement* again = back->Get(e.Id());
        t.Ok(again && again->body == e.body && again->Type() == e.Type(), e.Id() + ": read back as it was");
    }
    OWFiles::RemoveTree(root);
}

// -- What degrades, and what refuses ----------------------------------------------------------------------------------

OW_TEST(export_an_element_with_no_id_is_skipped_and_said_so) {
    OWExportRead r = ReadText(Envelope(R"({"character":[{"name":"Nobody"},{"id":"c1","name":"A"},{"id":7},{"id":""}]})"),
                              "x.json");
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Eq(r.world->elements.size(), size_t(1), "one element");
    t.Eq(r.world->errors.size(), size_t(3), "three skipped: no id, a number id, an empty one");
    if (r.world->errors.size() < 3) return;
    t.Ok(StartsWith(r.world->errors[0], "x.json#elements/character/0: no id"), "named by its place: " + r.world->errors[0]);
    t.Ok(StartsWith(r.world->errors[1], "x.json#elements/character/2: no id"), "a number is no id: " + r.world->errors[1]);
    t.Ok(StartsWith(r.world->errors[2], "x.json#elements/character/3: no id"), "nor an empty string: " + r.world->errors[2]);
}

OW_TEST(export_the_bodys_type_beats_its_group) {
    OWExportRead r = ReadText(Envelope(R"({"character":[{"id":"a","type":"creature"},{"id":"b"},{"id":"c","type":"Creature"}]})"));
    if (!r.world || r.world->elements.size() != 3) return t.Fail("not three elements: " + r.message);
    const std::vector<OWElement>& e = r.world->elements;
    t.Eq(e[0].Type(), std::string("creature"), "a body's declared type wins");
    t.Eq(e[0].folder_type, std::string("creature"), "and it is held as that type");
    t.Eq(e[1].Type(), std::string("character"), "a missing one comes from the group");
    t.Eq(e[2].Type(), std::string("character"), "a type not spelled as the standard spells it declares nothing");
}

OW_TEST(export_a_group_that_is_no_type_is_not_read) {
    OWExportRead r = ReadText(
        Envelope(R"({"vessel":[{"id":"v1"}],"Character":[{"id":"c1","type":"character"}],"trait":[{"id":"t1"}]})"));
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Eq(r.world->elements.size(), size_t(1), "only the trait");
    t.Eq(r.world->errors.size(), size_t(2), "each group named once");
    if (r.world->errors.size() < 2) return;
    t.Ok(StartsWith(r.world->errors[0], "the export#elements/vessel: not a type"), r.world->errors[0]);
    t.Ok(StartsWith(r.world->errors[1], "the export#elements/Character: not a type"), r.world->errors[1]);
}

OW_TEST(export_two_bodies_with_one_id_serve_the_first) {
    OWExportRead r = ReadText(Envelope(R"({"character":[{"id":"a","name":"First"}],"creature":[{"id":"a","name":"Second"}]})"));
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Eq(r.world->elements.size(), size_t(1), "one element held");
    const OWElement* a = r.world->Get("a");
    t.Ok(a && a->Name() == "First", "the first body is the one served");
    t.Eq(r.world->errors.size(), size_t(1), "the second named");
    if (!r.world->errors.empty())
        t.Ok(StartsWith(r.world->errors[0], "the export#elements/creature/0: id a is also the export#elements/character/0"),
             r.world->errors[0]);
}

OW_TEST(export_not_a_list_or_not_an_object_is_skipped_not_fatal) {
    OWExportRead r = ReadText(Envelope(R"({"character":{"id":"a"},"event":[7,{"id":"e1"}]})"));
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Eq(r.world->elements.size(), size_t(1), "one element");
    t.Eq(r.world->errors.size(), size_t(2), "two skipped");
    if (r.world->errors.size() < 2) return;
    t.Ok(StartsWith(r.world->errors[0], "the export#elements/character: not a list"), r.world->errors[0]);
    t.Ok(StartsWith(r.world->errors[1], "the export#elements/event/0: not an object"), r.world->errors[1]);
}

OW_TEST(export_elements_that_are_no_object_are_named) {
    OWExportRead r = ReadText(Envelope("[]"));
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Eq(r.world->elements.size(), size_t(0), "no elements");
    t.Eq(r.world->errors.size(), size_t(1), "the elements named");
    if (!r.world->errors.empty()) t.Ok(StartsWith(r.world->errors[0], "the export#elements: not an object"), r.world->errors[0]);
}

OW_TEST(export_an_empty_world_has_no_elements_and_no_skips) {
    for (const std::string& json : {Envelope("{}"), std::string(R"({"format":"onlyworlds-world-export","world":{"id":"w1"}})")}) {
        OWExportRead r = ReadText(json);
        t.Ok(r.ok && r.world && r.world->elements.empty() && r.world->errors.empty(), "empty, nothing skipped: " + json);
    }
}

OW_TEST(export_unknown_keys_are_ignored) {
    OWExportRead r = ReadText(R"({"format":"onlyworlds-world-export","future":{"x":1},
        "world":{"id":"w1","name":"W","calendar":{}},"elements":{}})");
    t.Ok(r.ok, "read: " + r.message);
    t.Eq(r.world_id, std::string("w1"), "the world's id");
    t.Eq(r.schema_version, std::string(), "no schema_version: \"\"");
}

OW_TEST(export_the_world_block_is_kept_as_given) {
    const std::string world = R"({"id":"w1","name":"W",
        "origin":{"source":null,"producer":{"tool":"atlas","version":"1.19.0","input":"server"}},
        "x_names":{"types":{"character":{"one":"Villager"}},"version":1},"atlas_color":"#8b0000","shadow_seen":[]})";
    OWExportRead r = ReadText(Envelope("{}", world));
    if (!r.world) return t.Fail("not read: " + r.message);
    t.Ok(r.world->info == OWJson::Parse(world).value, "every key as the file gives it");
    t.Ok(r.world->info.Keys() == std::vector<std::string>{"id", "name", "origin", "x_names", "atlas_color", "shadow_seen"},
         "in the file's order");
    t.Ok(r.world->world_bytes == OWJson::ToBytes(r.world->info, true), "world.json's bytes are the block's");
}

OW_TEST(export_another_format_is_refused) {
    struct Case {
        const char* json;
        const char* named;
    };
    for (const Case& c : {Case{R"({"world":{"id":"w1"},"elements":{}})", "missing"},
                          Case{R"({"format":"onlyworlds-world","world":{"id":"w1"}})", "onlyworlds-world"},
                          Case{R"({"format":1,"world":{"id":"w1"}})", "missing"}}) {
        OWExportRead r = ReadText(c.json, "f.json");
        t.Ok(!r.ok && !r.world, std::string("refused: ") + c.json);
        t.Eq(r.reason, std::string("not_an_export"), c.json);
        t.Ok(r.message.find(std::string("'") + c.named + "'") != std::string::npos, "the message names it: " + r.message);
        t.Ok(StartsWith(r.message, "f.json "), "and the source: " + r.message);
    }
}

OW_TEST(export_a_world_with_no_id_is_refused) {
    for (const char* json : {R"({"format":"onlyworlds-world-export","elements":{}})",
                             R"({"format":"onlyworlds-world-export","world":{"name":"W"}})",
                             R"({"format":"onlyworlds-world-export","world":[]})",
                             R"({"format":"onlyworlds-world-export","world":{"id":5}})",
                             R"({"format":"onlyworlds-world-export","world":{"id":""}})"}) {
        OWExportRead r = ReadText(json);
        t.Ok(!r.ok && !r.world, std::string("refused: ") + json);
        t.Eq(r.reason, std::string("no_world_id"), json);
    }
}

OW_TEST(export_not_a_json_object_is_refused) {
    for (const char* text : {"", "not json", "[1,2]"}) {
        OWExportRead r = ReadText(text);
        t.Ok(!r.ok && !r.world, std::string("refused: '") + text + "'");
        t.Eq(r.reason, std::string("not_json"), text);
    }
}

OW_TEST(export_nested_past_200_is_refused_whole) {
    // An element 198 deep in its own terms is 201 deep in the document: the file is one JSON text, read whole.
    std::string deep;
    for (int i = 0; i < 197; ++i) deep += R"({"a":)";
    deep += "1";
    for (int i = 0; i < 197; ++i) deep += "}";
    OWExportRead r = ReadText(Envelope(R"({"character":[{"id":"c1","x_deep":)" + deep + "}]}"));
    t.Ok(!r.ok && !r.world, "refused");
    t.Eq(r.reason, std::string("too_deep"), "by name");

    std::string inside;
    for (int i = 0; i < 196; ++i) inside += R"({"a":)";
    inside += "1";
    for (int i = 0; i < 196; ++i) inside += "}";
    OWExportRead ok = ReadText(Envelope(R"({"character":[{"id":"c1","x_deep":)" + inside + "}]}"));
    t.Ok(ok.ok, "200 deep is read: " + ok.message);
}

OW_TEST(export_no_file_is_no_file) {
    OWExportRead r = OWWorldExport::ReadFile(P().tmp + "/no-such-export.json");
    t.Ok(!r.ok && !r.world, "refused");
    t.Eq(r.reason, std::string("no_file"), "by name");
}

OW_TEST(export_the_reasons_are_five) {
    std::vector<std::string> reasons(std::begin(OWWorldExport::Reasons), std::end(OWWorldExport::Reasons));
    t.Ok(reasons == std::vector<std::string>{"no_file", "not_json", "too_deep", "not_an_export", "no_world_id"},
         "the list, in its order");
    t.Eq(std::string(OWWorldExport::Format), std::string("onlyworlds-world-export"), "the format");
}
