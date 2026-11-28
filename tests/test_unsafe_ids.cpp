// A new element whose id cannot be one file name inside its type folder is refused before anything is written (folder
// spec section 3.2): the id is one non-empty segment (no "/", no "\", not "." or ".."), and the file name made from it,
// short or with the full id, passes the archive's own name checks (OWWorldArchive::PathFault and NameFault, section
// 4.7's OW-16 and OW-22). Then every resolved path lies inside the folder written, whatever made the name. Each case
// writes into box/world two levels inside its own temporary folder, so a write that left the world folder would still
// land where the case looks.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldExport.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const std::string U = "0191f3a2-7c4e-7b1a-9d2e-3f4a5b6c7d8e";
const std::string WorldJson = "{\"id\": \"0191f3a2-7c4e-7b1a-9d2e-3f4a5b6c7d8e\", \"name\": \"W\"}";

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

// Ids no writer names a file from: the probe's, a drive or a root, a colon, a character Windows refuses, a control
// character (C0 and C1).
const std::vector<std::string> RefusedIds = {
    "../x", "../../../escaped", "a/b", "a\\b", "..", ".", std::string("ctl\x01") + "id", "urn:uuid:" + U, "C:evil",
    "C:\\evil", "C:\\x", "\\x", "/x", "a<b", "a|b", "a?b", "a*b", "a\"b", "x\xC2\x85"};

void Under(const std::string& root, const std::string& rel, std::string& out) {
    std::string here = rel.empty() ? root : root + "/" + rel;
    for (const std::string& f : OWFiles::List(here, false)) out += (out.empty() ? "" : " ") + (rel.empty() ? f : rel + "/" + f);
    for (const std::string& d : OWFiles::List(here, true)) Under(root, rel.empty() ? d : rel + "/" + d, out);
}

// Every file under a folder, relative to it, joined by spaces.
std::string Files(const std::string& root) {
    std::string out;
    Under(root, "", out);
    return out;
}

std::string Fresh() {
    std::string root = P().tmp + "/unsafe-ids";
    OWFiles::RemoveTree(root);
    OWFiles::MakeDirs(root + "/box");
    return root;
}

OWJsonValue Body(const std::string& id, const std::string& name) {
    OWJsonValue b = OWJsonValue::Object();
    b.Set("id", OWJsonValue::String(id));
    b.Set("type", OWJsonValue::String("character"));
    b.Set("name", OWJsonValue::String(name));
    return b;
}

std::unique_ptr<OWWorld> WorldWith(const std::string& id, const std::string& name) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles({{"world.json", Bytes(WorldJson)}});
    w->Add("character", Body(id, name));
    return w;
}

void AssertRefused(owtest::T& t, OWWorld& w, const std::string& id, const std::string& label) {
    std::string why = w.WriteRefusal();
    t.Ok(why.rfind("character element id " + OWJson::Quote(id) + " ", 0) == 0, label + ": the refusal names the id: " + why);
    std::string root = Fresh();
    std::string error;
    t.Ok(!w.WriteFolder(root + "/box/world", false, &error), label + ": WriteFolder refuses");
    t.Eq(error, why, label + ": with WriteRefusal's line");
    t.Eq(Files(root), std::string(), label + ": nothing written, inside the folder or beside it");
    OWPacked packed = w.Pack("world");
    t.Ok(!packed.ok && packed.message == why, label + ": Pack refuses with the same words: " + packed.message);
    w.ToFiles();
    t.Eq(w.WriteRefusal(), why, label + ": still refused after ToFiles");
    OWFiles::RemoveTree(root);
}

}  // namespace

OW_TEST(unsafe_ids_an_id_that_is_no_file_name_is_refused_and_nothing_is_written) {
    for (const std::string& id : RefusedIds)
        for (const char* name : {"", "Probe Name"}) {
            std::unique_ptr<OWWorld> w = WorldWith(id, name);
            AssertRefused(t, *w, id, OWJson::Quote(id) + ", name '" + name + "'");
        }
}

// Someone else's file: the onlyworlds.com download read (OWWorldExport), then written as a folder.
OW_TEST(unsafe_ids_a_crafted_download_read_then_written_is_refused_and_nothing_is_written) {
    for (const std::string& id : RefusedIds)
        for (const char* name : {"", "Probe Name"}) {
            OWJsonValue doc = OWJsonValue::Object();
            doc.Set("format", OWJsonValue::String("onlyworlds-world-export"));
            OWJsonValue world = OWJsonValue::Object();
            world.Set("id", OWJsonValue::String(U));
            world.Set("name", OWJsonValue::String("W"));
            doc.Set("world", world);
            OWJsonValue list = OWJsonValue::Array();
            list.Push(Body(id, name));
            OWJsonValue elements = OWJsonValue::Object();
            elements.Set("character", list);
            doc.Set("elements", elements);
            OWExportRead r = OWWorldExport::Read(OWJson::ToBytes(doc), "crafted.json");
            std::string label = "download " + OWJson::Quote(id) + ", name '" + name + "'";
            t.Ok(r.ok && r.world && r.world->elements.size() == 1, label + ": read, the element served: " + r.message);
            if (r.ok && r.world) AssertRefused(t, *r.world, id, label);
        }
}

OW_TEST(unsafe_ids_the_refusal_names_the_rule_in_the_words_the_other_packages_use) {
    t.Eq(WorldWith("../x", "")->WriteRefusal(),
         std::string("character element id \"../x\" cannot be a file name: it holds a path separator, / or \\."), "a separator");
    t.Eq(WorldWith("a\\b", "Named")->WriteRefusal(),
         std::string("character element id \"a\\\\b\" cannot be a file name: it holds a path separator, / or \\."),
         "a backslash, named");
    t.Eq(WorldWith("..", "")->WriteRefusal(), std::string("character element id \"..\" cannot be a file name: it is . or ..."),
         "dot dot");
    t.Ok(WorldWith("C:evil", "")->WriteRefusal().rfind(
             "character element id \"C:evil\" cannot be a file name: its file name \"C:evil.json\" ", 0) == 0,
         "a drive");
    t.Ok(WorldWith("urn:uuid:" + U, "Probe Name")->WriteRefusal().rfind(
             "character element id \"urn:uuid:" + U + "\" cannot be a file name: its file name \"probe-name--urn:uuid:" + U +
                 ".json\" ", 0) == 0,
         "a colon in the full-id form, though the short name is plain");
    t.Ok(WorldWith(std::string("ctl\x01") + "id", "")->WriteRefusal().find("\"ctl\\u0001id\"") != std::string::npos,
         "a control character, escaped");
    t.Ok(WorldWith("con", "")->WriteRefusal().find("con.json") != std::string::npos, "a device name");
}

OW_TEST(unsafe_ids_an_id_that_is_one_file_name_is_written_in_its_type_folder) {
    for (const auto& [id, name] : std::vector<std::pair<std::string, std::string>>{
             {U, ""}, {U, "Probe Name"}, {"c-1", ""}, {"c-1", "Probe Name"}, {"con", "Probe Name"}, {"x ", ""}}) {
        std::unique_ptr<OWWorld> w = WorldWith(id, name);
        t.Eq(w->WriteRefusal(), std::string(), id + ": no refusal");
        std::string root = Fresh();
        std::string error;
        t.Ok(w->WriteFolder(root + "/box/world", false, &error), id + ": written: " + error);
        std::string files = Files(root);
        const std::string world = "box/world/world.json ", folder = "box/world/elements/character/";
        std::string element = files.rfind(world, 0) == 0 ? files.substr(world.size()) : std::string();
        t.Ok(element.rfind(folder, 0) == 0 && element.find('/', folder.size()) == std::string::npos,
             id + ": world.json and one element file in its type folder: " + files);
        t.Ok(w->Pack("world").ok, id + ": packed");
        OWFiles::RemoveTree(root);
    }
}

// A refused element takes no path, so it takes no name from a sibling either.
OW_TEST(unsafe_ids_a_refused_id_is_left_out_of_to_files) {
    std::unique_ptr<OWWorld> w = WorldWith("../x", "");
    w->Add("character", Body("c-1", ""));
    std::string keys;
    for (const auto& kv : w->ToFiles()) keys += kv.first + "\n";
    t.Eq(keys, std::string("elements/character/c-1.json\nworld.json\n"), "only the plain id has a path");
}

// The second guard: a path that would leave the folder is refused whatever made it (here a file of the world's own).
OW_TEST(unsafe_ids_write_folder_refuses_a_path_that_would_leave_the_folder) {
    std::unique_ptr<OWWorld> w =
        OWWorld::FromFiles({{"world.json", Bytes(WorldJson)}, {"media/../../escaped.txt", Bytes("x")}});
    std::string root = Fresh();
    std::string error;
    t.Ok(!w->WriteFolder(root + "/box/world", false, &error), "refused");
    t.Ok(error.find("escaped.txt") != std::string::npos, "the error names the file: " + error);
    t.Eq(Files(root), std::string(), "nothing written");
    OWFiles::RemoveTree(root);
}
