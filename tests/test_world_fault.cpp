// A world file refused not_a_world for a world.json that is there but cannot be read says what is wrong with it, in the
// folder read report's words: the TypeScript SDK's OwWorldFileError.worldFault (4.212.0), OWArchiveRead::world_fault
// here.

#include "ow_test.h"

#include "OWWorldArchive.h"

#include <string>

using namespace OnlyWorlds;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string Wid = "019a9200-0000-7000-8000-000000000001";

OWArchiveRead ReadOne(const std::string& path, const std::string& text) {
    OWFileMap files;
    files[path] = Bytes(text);
    return OWWorldArchive::Read(OWWorldArchive::PackFiles(files, "w").bytes);
}

std::string Fault(owtest::T& t, const std::string& path, const std::string& text, const std::string& label) {
    OWArchiveRead r = ReadOne(path, text);
    t.Ok(!r.ok && r.reason == "not_a_world", label + ": not_a_world (" + r.reason + ": " + r.message + ")");
    return r.world_fault;
}

}  // namespace

OW_TEST(world_fault_a_world_json_that_cannot_be_read_says_why_any_other_not_a_world_has_none) {
    // npm's five vectors: too_deep (nested past 200), not_json, and none on a missing world.json, one with no id, and
    // one that is JSON but no object. Kills: the fault left unset, set on a refusal that has none, or the words swapped.
    const std::string deep = "{\"id\":\"" + Wid + "\",\"name\":\"Deep\",\"x\":" + std::string(200, '[') +
                             std::string(200, ']') + "}";
    t.Eq(Fault(t, "world.json", deep, "deep"), std::string("too_deep"), "nested past 200");
    t.Eq(Fault(t, "world.json", "{nope", "broken"), std::string("not_json"), "not JSON");
    t.Eq(Fault(t, "elements/a.json", "{}", "none"), std::string(), "no world.json");
    t.Eq(Fault(t, "world.json", "{\"name\":\"no id\"}", "no id"), std::string(), "a world.json with no id");
    t.Eq(Fault(t, "world.json", "[\"" + Wid + "\"]", "array"), std::string(), "JSON, but no object");
    t.Eq(Fault(t, "world.json", "null", "null"), std::string(), "JSON null, no object");
    // What this reader refuses that JSON.parse in the TypeScript reader does not: a value after the object and a raw
    // control character in a string are no JSON.
    t.Eq(Fault(t, "world.json", "{\"id\":\"" + Wid + "\"} x", "after"), std::string("not_json"), "a value after the object");
    t.Eq(Fault(t, "world.json", "{\"id\":\"" + Wid + "\",\"n\":\"a\tb\"}", "control"), std::string("not_json"),
         "a raw tab in a string");
    // A number past a double is kept as its text, so the world reads, as the TypeScript reader reads it.
    OWArchiveRead range = ReadOne("world.json", "{\"id\":\"" + Wid + "\",\"x\":1e400}");
    t.Ok(range.ok && range.world_fault.empty(), "a number past a double: the world reads (" + range.message + ")");
    // Any other refusal has none.
    OWFileMap two;
    two["a/world.json"] = Bytes("{\"id\":\"" + Wid + "\",\"name\":\"A\"}");
    two["b/world.json"] = Bytes("{\"id\":\"" + Wid + "\",\"name\":\"B\"}");
    OWArchiveRead r = OWWorldArchive::Read(OWWorldArchive::PackFiles(two, "").bytes);
    t.Ok(!r.ok && r.reason == "more_than_one" && r.world_fault.empty(), "more_than_one has none (" + r.reason + ")");
}

OW_TEST(world_fault_a_world_json_past_200_says_it_in_words_the_code_the_field) {
    // The TypeScript SDK 4.212.3: a world file's refusal says it in words alone; the code is world_fault. npm's vector,
    // '[' 201 deep inside the object.
    const std::string deep = "{\"id\": \"" + Wid + "\", \"x\": " + std::string(201, '[') + std::string(201, ']') + "}";
    OWArchiveRead r = ReadOne("world.json", deep);
    t.Ok(!r.ok && r.reason == "not_a_world", "not_a_world (" + r.reason + ")");
    t.Eq(r.world_fault, std::string("too_deep"), "the code is the field");
    t.Ok(r.message.find("too_deep") == std::string::npos, "no code mark in the words: " + r.message);
    t.Eq(r.message, std::string("world.json is nested more than 200 deep"), "the words");
    // The writer refuses such a world.json in the same words, as npm's writeWorldFile does (4.212.3); never "no id".
    OWFileMap files;
    files["world.json"] = Bytes(deep);
    for (bool copy : {true, false}) {
        OWPacked p = OWWorldArchive::PackWorld(files, "w", false, false, false, copy);
        t.Ok(!p.ok && p.bytes.empty(), std::string(copy ? "copy" : "share") + ": refused");
        t.Eq(p.message, std::string("world.json is nested more than 200 deep"), "the writer's words");
    }
}
