// The writer refuses a new element whose type is not one of the 22 type slugs as the standard spells them: its file goes
// to elements/<type>/, and a reader reads a type folder only when it is one of the 22 as written (folder spec section
// 4.8), so WriteRefusal names the cause, WriteFolder writes nothing and Pack is not ok, as the TypeScript SDK's and the
// Python package's folder writers refuse an unknown type. That covers "" (a path elements//x.json) and a type that is a
// path ("../x", "../../x"), which wrote the file outside elements/ and outside the world folder.

#include "ow_test.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const std::string WorldJson = "{\"id\": \"w\", \"name\": \"W\"}";

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

// Every file and folder under a folder, relative to it, by a walk of its own, joined by spaces.
void Under(const std::string& root, const std::string& rel, std::string& out) {
    std::string here = rel.empty() ? root : root + "/" + rel;
    for (const std::string& f : OWFiles::List(here, false)) out += (out.empty() ? "" : " ") + (rel.empty() ? f : rel + "/" + f);
    for (const std::string& d : OWFiles::List(here, true)) {
        std::string sub = rel.empty() ? d : rel + "/" + d;
        out += (out.empty() ? "" : " ") + sub + "/";
        Under(root, sub, out);
    }
}

std::string Tree(const std::string& root) {
    std::string out;
    Under(root, "", out);
    return out;
}

std::unique_ptr<OWWorld> WorldWith(const std::string& type) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", Bytes(WorldJson)}});
    w->Add(type, OWJson::Parse(Bytes("{\"id\":\"n1\",\"name\":\"N\"}")).value);
    return w;
}

// WriteRefusal names the cause, WriteFolder writes nothing inside the world or beside it, Pack refuses with the words.
void Refused(owtest::T& t, const std::string& type, const std::string& cause) {
    std::unique_ptr<OWWorld> w = WorldWith(type);
    std::string why = w->WriteRefusal();
    t.Ok(why.find(cause) != std::string::npos, "'" + type + "': the refusal names the cause: " + why);
    std::string root = P().tmp + "/declared-write";
    OWFiles::RemoveTree(root);
    OWFiles::MakeDirs(root);
    std::string error;
    t.Ok(!w->WriteFolder(root + "/world", false, &error), "'" + type + "': WriteFolder refuses");
    t.Eq(error, why, "'" + type + "': with WriteRefusal's line");
    t.Eq(Tree(root), std::string(), "'" + type + "': nothing written, inside the folder or beside it");
    OWPacked packed = w->Pack("w");
    t.Ok(!packed.ok && packed.message == why, "'" + type + "': Pack refuses with the same words");
    OWFiles::RemoveTree(root);
}

}  // namespace

OW_TEST(declared_type_writer_a_type_outside_the_22_is_refused_naming_the_cause) {
    for (const char* type : {"Character", "CHARACTER", " character", "character ", "\xC5\xBFpecies", "widget", "characters"})
        Refused(t, type, "not one of the 22 type slugs");
}

OW_TEST(declared_type_writer_a_type_that_is_a_path_is_refused_and_nothing_is_written_outside_elements) {
    for (const char* type : {"../escape", "../../escape", "character/../../escape", "..", "."})
        Refused(t, type, "not one of the 22 type slugs");
}

OW_TEST(declared_type_writer_an_element_with_no_type_is_refused) { Refused(t, "", "has no type"); }

OW_TEST(declared_type_writer_each_of_the_22_is_written_into_its_own_folder) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", Bytes(WorldJson)}});
    for (const char* slug : OWAddress::TypeWords)
        w->Add(slug, OWJson::Parse(Bytes(std::string("{\"id\":\"") + slug + "-1\",\"name\":\"N\"}")).value);
    t.Eq(w->WriteRefusal(), std::string(), "no refusal");
    std::string root = P().tmp + "/declared-write22";
    OWFiles::RemoveTree(root);
    std::string error;
    t.Ok(w->WriteFolder(root, false, &error), "written: " + error);
    std::unique_ptr<OWWorld> back = OWWorld::ReadFolder(root);
    t.Ok(back != nullptr, "read back");
    if (!back) return;
    t.Eq(back->elements.size(), size_t(22), "22 read back");
    for (const char* slug : OWAddress::TypeWords) t.Eq(back->OfType(slug).size(), size_t(1), std::string(slug) + ": one");
    OWFiles::RemoveTree(root);
}

// ToFiles gives a new element its path; a WriteFolder after it still refuses the type, so the order of the calls
// does not open the door.
OW_TEST(declared_type_writer_a_refused_type_stays_refused_after_to_files) {
    std::unique_ptr<OWWorld> w = WorldWith("../escape");
    w->ToFiles();
    t.Ok(w->WriteRefusal().find("not one of the 22 type slugs") != std::string::npos, "still refused");
    t.Ok(!w->Pack("w").ok, "Pack too");
}

// ToFiles, which PackFiles and a caller's own writer take, leaves a refused element out, as it leaves out one whose two
// file names are both held: no path outside elements/ ever reaches a map. A typed sibling keeps its short name.
OW_TEST(declared_type_writer_to_files_leaves_out_an_element_the_writer_refuses_by_its_type) {
    for (const char* type : {"Character", "CHARACTER", " character", "character ", "widget", "characters", "../escape",
                             "../../escape", "character/../../escape", "..", ".", ""}) {
        std::unique_ptr<OWWorld> w = WorldWith(type);
        w->Add("character", OWJson::Parse(Bytes("{\"id\":\"0695db83-0000-7000-8000-00000000ab01\",\"name\":\"N\"}")).value);
        std::string keys;
        for (const auto& kv : w->ToFiles()) keys += kv.first + "\n";
        t.Eq(keys, std::string("elements/character/n--0000ab01.json\nworld.json\n"),
             std::string("'") + type + "': only the world and the typed sibling");
    }
}
