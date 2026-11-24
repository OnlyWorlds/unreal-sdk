// Three names a reader takes as written, at both doors (a world folder, an .owworld's files): an element file's
// extension is .json in lower case, so X.JSON is skipped with a reason; a type folder is one of the 22 type words in
// lower case, so elements/Character/ is no type folder and is not read; an id is compared as written, never as a UUID,
// so one UUID in capitals and in lower case is two elements. A file not read is kept as its bytes, so a copy carries it.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\"id\": \"019a9200-0000-7000-8000-000000000001\", \"name\": \"W\"}";

std::string Element(const std::string& id, const std::string& name) {
    return "{\"id\": \"" + id + "\", \"name\": \"" + name + "\"}";
}

void WriteTree(const std::string& root, const OWFileMap& files) {
    OWFiles::RemoveTree(root);
    for (const auto& kv : files) OWFiles::Write(root + "/" + kv.first, kv.second, true);
}

// The error line that starts with `rel` followed by ": ", or "" when there is none.
std::string LineFor(const OWWorld& w, const std::string& rel) {
    for (const std::string& e : w.errors)
        if (e.compare(0, rel.size() + 2, rel + ": ") == 0) return e;
    return "";
}

// The world through each door: the folder reader (with and without other parts) and an .owworld read back.
struct Door {
    std::string name;
    bool keeps_other_files;
    std::unique_ptr<OWWorld> world;
};

std::vector<Door> Doors(owtest::T& t, const std::string& label, const OWFileMap& files) {
    std::vector<Door> out;
    std::string dir = P().tmp + "/names-" + label;
    WriteTree(dir, files);
    out.push_back({"folder", true, OWWorld::ReadFolder(dir)});
    out.push_back({"folder, no other parts", false, OWWorld::ReadFolder(dir, false)});
    OWPacked packed = OWWorldArchive::PackFiles(files, "w");
    t.Ok(packed.ok, label + ": packed (" + packed.message + ")");
    OWArchiveRead read = OWWorldArchive::Read(packed.bytes);
    t.Ok(read.ok, label + ": the archive reads (" + read.message + ")");
    out.push_back({"archive", true, OWWorld::FromFiles(read.files)});
    OWFiles::RemoveTree(dir);
    for (const Door& d : out) t.Ok(d.world != nullptr, label + ", " + d.name + ": a world");
    return out;
}

}  // namespace

// Ruling 1: X.JSON and y.Json in a type folder are no element files. Not served, named with why, kept as their bytes.
OW_TEST(names_an_extension_in_another_case_is_skipped_with_a_reason) {
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    files["elements/character/ann.json"] = Bytes(Element("c-1", "Ann"));
    files["elements/character/X.JSON"] = Bytes(Element("c-2", "Upper"));
    files["elements/character/y.Json"] = Bytes(Element("c-3", "Mixed"));
    for (Door& d : Doors(t, "extension", files)) {
        if (!d.world) continue;
        const std::string at = "extension, " + d.name + ": ";
        t.Eq(d.world->elements.size(), size_t(1), at + "one element read");
        t.Ok(d.world->Get("c-1") != nullptr, at + "ann.json is served");
        t.Ok(d.world->Get("c-2") == nullptr, at + "X.JSON is not served");
        t.Ok(d.world->Get("c-3") == nullptr, at + "y.Json is not served");
        for (const char* rel : {"elements/character/X.JSON", "elements/character/y.Json"}) {
            std::string line = LineFor(*d.world, rel);
            t.Ok(line.find(".json in lower case") != std::string::npos, at + std::string(rel) + " is named with why: '" + line + "'");
            if (d.keeps_other_files) {
                auto kept = d.world->other_files.find(rel);
                t.Ok(kept != d.world->other_files.end() && kept->second == files[rel], at + std::string(rel) + " kept as its bytes");
            }
        }
        t.Eq(LineFor(*d.world, "elements/character/ann.json"), std::string(), at + "nothing said of ann.json");
    }
}

// Ruling 2: a type folder is one of the 22 type words in lower case. elements/Character/ is no type folder, as
// elements/vessel/ is not, and spatial/Pin/ neither (no world holds Pin/ and pin/ both: on NTFS and in an
// .owworld, OW-18, the two are one name): not read, named with why, every file in it kept as its bytes.
OW_TEST(names_a_type_folder_in_another_case_is_not_read) {
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    files["elements/location/harbour.json"] = Bytes(Element("l-1", "Harbour"));
    files["elements/Character/ann.json"] = Bytes(Element("c-1", "Ann"));
    files["elements/Character/notes.txt"] = Bytes("a note");
    files["elements/vessel/argo.json"] = Bytes(Element("v-1", "Argo"));
    files["spatial/Pin/p.json"] = Bytes(Element("p-1", "Pin"));
    files["spatial/map/m.json"] = Bytes(Element("m-1", "Map"));
    for (Door& d : Doors(t, "typefolder", files)) {
        if (!d.world) continue;
        const std::string at = "type folder, " + d.name + ": ";
        t.Eq(d.world->elements.size(), size_t(2), at + "two elements read");
        t.Ok(d.world->Get("l-1") != nullptr, at + "elements/location/ is read");
        t.Ok(d.world->Get("m-1") != nullptr, at + "spatial/map/ is read");
        for (const char* id : {"c-1", "v-1", "p-1"}) t.Ok(d.world->Get(id) == nullptr, at + id + " is not served");
        t.Eq(d.world->OfType("Character").size(), size_t(0), at + "no element of a type 'Character'");
        for (const char* folder : {"elements/Character/", "elements/vessel/", "spatial/Pin/"}) {
            std::string line = LineFor(*d.world, folder);
            t.Ok(line.find("not a type folder") != std::string::npos, at + folder + " is named with why: '" + line + "'");
        }
        if (d.keeps_other_files)
            for (const char* rel : {"elements/Character/ann.json", "elements/Character/notes.txt", "elements/vessel/argo.json", "spatial/Pin/p.json"}) {
                auto kept = d.world->other_files.find(rel);
                t.Ok(kept != d.world->other_files.end() && kept->second == files[rel], at + rel + " kept as its bytes");
            }
    }
    // A copy carries the folder that is no type byte for byte, and writes nothing of it into a type folder.
    std::string dir = P().tmp + "/names-folder-copy-src", out = P().tmp + "/names-folder-copy";
    WriteTree(dir, files);
    std::unique_ptr<OWWorld> w = OWWorld::ReadFolder(dir);
    OWFiles::RemoveTree(out);
    std::string err;
    t.Ok(w && w->WriteFolder(out, false, &err), "type folder: the copy is written (" + err + ")");
    OWBytes back;
    t.Ok(OWFiles::Read(out + "/elements/Character/ann.json", back) && back == files["elements/Character/ann.json"],
         "type folder: the copy carries elements/Character/ann.json as its bytes");
    OWFiles::RemoveTree(dir);
    OWFiles::RemoveTree(out);
}

// Ruling 3: an id is compared as written. One UUID in capitals, in lower case, without hyphens, in braces and with a
// space before it is five elements; none is a duplicate of another, and Get finds each by its own spelling only.
OW_TEST(names_ids_are_compared_as_written) {
    const std::vector<std::pair<std::string, std::string>> spellings = {
        {"elements/character/a.json", "019A9200-0000-7000-8000-0000000000C1"},
        {"elements/character/b.json", "019a9200-0000-7000-8000-0000000000c1"},
        {"elements/character/c.json", "019a92000000700080000000000000c1"},
        {"elements/character/d.json", "{019a9200-0000-7000-8000-0000000000c1}"},
        {"elements/character/e.json", " 019a9200-0000-7000-8000-0000000000c1"},
    };
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    for (const auto& [rel, id] : spellings) files[rel] = Bytes(Element(id, "Ann"));
    for (Door& d : Doors(t, "ids", files)) {
        if (!d.world) continue;
        const std::string at = "ids, " + d.name + ": ";
        t.Eq(d.world->elements.size(), spellings.size(), at + "five elements");
        t.Eq(d.world->errors.size(), size_t(0), at + "nothing skipped, no id held twice");
        for (const auto& [rel, id] : spellings) {
            OWElement* e = d.world->Get(id);
            t.Ok(e && e->source_path == rel, at + "'" + id + "' is " + rel);
        }
        t.Ok(d.world->Get("019a9200-0000-7000-8000-0000000000C1") == nullptr, at + "a spelling no file holds finds nothing");
        t.Ok(d.world->Get("019A9200-0000-7000-8000-0000000000c1") == nullptr, at + "nor another");
    }
    // An edit to the capitals is written to its own file only; the lower-case file is untouched.
    std::string dir = P().tmp + "/names-ids-edit";
    WriteTree(dir, files);
    std::unique_ptr<OWWorld> w = OWWorld::ReadFolder(dir);
    t.Ok(w != nullptr, "ids: read for the edit");
    if (w) {
        OWElement* upper = w->Get("019A9200-0000-7000-8000-0000000000C1");
        if (upper) upper->Set("name", OWJsonValue::String("Ann in capitals"));
        OWFileMap edited = w->ToFiles(true);
        t.Eq(edited.size(), size_t(1), "ids: one file to write");
        t.Ok(edited.count("elements/character/a.json") == 1, "ids: and it is a.json");
        std::string err;
        t.Ok(w->WriteFolder(dir, true, &err), "ids: the edit is written (" + err + ")");
        OWBytes b;
        OWFiles::Read(dir + "/elements/character/b.json", b);
        t.Ok(b == files["elements/character/b.json"], "ids: b.json is as it was");
    }
    OWFiles::RemoveTree(dir);
}
