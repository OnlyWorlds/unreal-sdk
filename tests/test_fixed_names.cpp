// The three fixed names, world.json, elements/ and spatial/, are read as written at every door (the folder format's
// section 4.8, ruled 2026-11-25): a folder or a world file that holds one of them in another case, and not the name
// itself, is refused with the cause name_case, never read as an empty world. ReadFolder and FromFiles answer nullptr,
// as for no world.json; the archive reader refuses name_case; PackFolder is not ok and WriteFolder writes nothing, each
// with its message. On Windows and macOS the file system folds a name's case, so a reader that asks it for "elements"
// is served "Elements/"; on ext4 it is served nothing. These tests build the folder on disk and pass on both.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\"id\": \"019a9200-0000-7000-8000-000000000001\", \"name\": \"W\"}";
const std::string IdA = "019a9200-0000-7000-8000-00000000000a";
const std::string IdB = "019a9200-0000-7000-8000-00000000000b";

std::string Element(const std::string& id, const std::string& name = "A") {
    return "{\"id\": \"" + id + "\", \"name\": \"" + name + "\"}";
}

std::string Root(const std::string& tag) { return P().tmp + "/fixed-" + tag; }

void WriteTree(const std::string& root, const std::vector<std::pair<std::string, std::string>>& files) {
    OWFiles::RemoveTree(root);
    OWFiles::MakeDirs(root);
    for (const auto& kv : files) OWFiles::Write(root + "/" + kv.first, Bytes(kv.second), true);
}

OWFileMap Map(const std::vector<std::pair<std::string, std::string>>& files) {
    OWFileMap out;
    for (const auto& kv : files) out[kv.first] = Bytes(kv.second);
    return out;
}

// Every file under a folder, relative to it, by a walk of its own: a level's files, then each folder's, as listed.
void FilesUnder(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    std::string here = rel.empty() ? root : root + "/" + rel;
    for (const std::string& f : OWFiles::List(here, false)) out.push_back(rel.empty() ? f : rel + "/" + f);
    for (const std::string& d : OWFiles::List(here, true)) FilesUnder(root, rel.empty() ? d : rel + "/" + d, out);
}

std::string Joined(const std::string& root) {
    std::vector<std::string> files;
    FilesUnder(root, "", files);
    std::string out;
    for (const std::string& f : files) out += (out.empty() ? "" : " ") + f;
    return out;
}

void SayTheDisk(const std::string& root) {
    std::printf(OWFiles::IsDir(root + "/elements")
                    ? "       this disk folds case: the lookup of elements finds Elements/, so the refusal is the listing's\n"
                    : "       this disk keeps case: the lookup of elements finds nothing, so the refusal is the listing's\n");
}

}  // namespace

OW_TEST(fixed_names_a_folder_holding_elements_in_another_case_is_refused) {
    std::string root = Root("elements");
    WriteTree(root, {{"world.json", WorldJson}, {"Elements/character/a.json", Element(IdA)}});
    SayTheDisk(root);
    t.Ok(OWWorld::ReadFolder(root) == nullptr, "refused, never read as the world it would be on another disk");
    t.Ok(OWWorld::ReadFolder(root, false) == nullptr, "refused without the other parts too");
    OWFiles::RemoveTree(root);
}

OW_TEST(fixed_names_a_folder_holding_world_json_or_spatial_in_another_case_is_refused) {
    const std::vector<std::vector<std::pair<std::string, std::string>>> cases = {
        {{"World.JSON", WorldJson}, {"elements/character/a.json", Element(IdA)}},
        {{"world.json", WorldJson}, {"Spatial/pin/p.json", Element(IdB)}},
        {{"world.json", WorldJson}, {"\xC5\xBFpatial/pin/p.json", Element(IdB)}},  // U+017F: OW-18's fold makes it an s
    };
    for (const auto& files : cases) {
        std::string root = Root("other");
        WriteTree(root, files);
        t.Ok(OWWorld::ReadFolder(root) == nullptr, files.front().first + " / " + files.back().first + ": refused");
        OWFiles::RemoveTree(root);
    }
}

OW_TEST(fixed_names_a_folder_with_the_names_as_written_reads_as_before) {
    std::string root = Root("plain");
    WriteTree(root, {{"world.json", WorldJson}, {"elements/character/a.json", Element(IdA)}, {"Media/a.png", "x"}});
    auto w = OWWorld::ReadFolder(root);
    t.Ok(w && w->Get(IdA) != nullptr, "read, the element served");
    OWFiles::RemoveTree(root);
}

OW_TEST(fixed_names_files_holding_a_fixed_name_in_another_case_are_refused) {
    t.Ok(OWWorld::FromFiles(Map({{"world.json", WorldJson}, {"Elements/character/a.json", Element(IdA)}})) == nullptr,
         "Elements/: refused, never a world with no elements");
    t.Ok(OWWorld::FromFiles(Map({{"World.JSON", WorldJson}, {"elements/character/a.json", Element(IdA)}})) == nullptr,
         "World.JSON: refused");
    auto both = OWWorld::FromFiles(Map({{"world.json", WorldJson}, {"elements/character/a.json", Element(IdA)},
                                        {"Elements/character/b.json", Element(IdB)}}));
    t.Ok(both && both->Get(IdA) != nullptr && both->Get(IdB) == nullptr,
         "both spellings: elements/ read as written, Elements/ left unread");
}

OW_TEST(fixed_names_an_archive_holding_a_fixed_name_in_another_case_is_refused_name_case) {
    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> cases = {
        {"elements", {{"abbey/world.json", WorldJson}, {"abbey/Elements/character/a.json", Element(IdA)}}},
        {"world in its folder", {{"abbey/World.JSON", WorldJson}, {"abbey/elements/character/a.json", Element(IdA)}}},
        {"world at the root", {{"WORLD.json", WorldJson}, {"elements/character/a.json", Element(IdA)}}},
        {"spatial", {{"abbey/world.json", WorldJson}, {"abbey/Spatial/pin/p.json", Element(IdB)}}},
    };
    for (const auto& c : cases) {
        OWPacked packed = OWWorldArchive::PackFiles(Map(c.second), "");
        OWArchiveRead r = OWWorldArchive::Read(packed.bytes);
        t.Eq(r.reason, std::string("name_case"), c.first + ": the cause (" + r.message + ")");
    }
}

OW_TEST(fixed_names_a_pack_of_a_folder_holding_a_fixed_name_in_another_case_is_refused) {
    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> cases = {
        {"Elements", {{"world.json", WorldJson}, {"Elements/character/a.json", Element(IdA)}}},
        {"World.json", {{"World.json", WorldJson}, {"elements/character/b.json", Element(IdB)}}},
    };
    for (const auto& c : cases) {
        std::string root = Root("pack");
        WriteTree(root, c.second);
        OWPacked p = OWWorldArchive::PackFolder(root);
        t.Ok(!p.ok, c.first + ": refused, never packed under the fixed name nor as an empty world");
        t.Ok(p.message.find("name_case") != std::string::npos && p.message.find("'" + c.first + "'") != std::string::npos,
             c.first + ": the message names the cause and the spelling (" + p.message + ")");
        OWFiles::RemoveTree(root);
    }
}

OW_TEST(fixed_names_a_write_into_a_folder_holding_elements_in_another_case_is_refused) {
    std::string root = Root("write");
    WriteTree(root, {{"world.json", WorldJson}, {"Elements/character/a.json", Element(IdA)}});
    auto w = OWWorld::FromFiles(Map({{"world.json", WorldJson}}));
    w->Add("character", OWJson::Parse(Bytes(Element(IdB, "Bo"))).value);
    std::string error;
    t.Ok(!w->WriteFolder(root, false, &error), "refused");
    t.Ok(error.find("name_case") != std::string::npos, "the error names the cause (" + error + ")");
    t.Eq(Joined(root), std::string("world.json Elements/character/a.json"),
         "nothing written (on a disk that folds case, elements/character/ would land in Elements/)");
    OWFiles::RemoveTree(root);
}

// A type folder is read as written (ruled 2026-11-23), so a write into elements/Character/ is read by no one: refused,
// nothing written, the folders named in the error.
OW_TEST(fixed_names_a_write_into_a_type_folder_in_another_case_is_refused) {
    std::string root = Root("type-case");
    WriteTree(root, {{"world.json", WorldJson}, {"elements/Character/old.json", Element(IdA, "Old")}});
    SayTheDisk(root);
    auto w = OWWorld::ReadFolder(root);
    if (!w) {
        t.Fail("the world did not read");
        return;
    }
    w->Add("character", OWJson::Parse(Bytes(Element(IdB, "Bo"))).value);
    std::string error;
    t.Ok(!w->WriteFolder(root, true, &error), "an edited write: refused");
    t.Ok(error.find("elements/Character/") != std::string::npos && error.find("elements/character/") != std::string::npos,
         "the error names both folders (" + error + ")");
    t.Ok(!w->WriteFolder(root, false, &error), "a whole write: refused");
    t.Eq(Joined(root), std::string("world.json elements/Character/old.json"),
         "nothing written (on a disk that folds case, elements/character/ would land in Character/)");
    OWFiles::RemoveTree(root);
}

// The same when the world itself carries the folder in another case (read from an archive) and the target is new: on a
// disk that ignores case the two spellings would become one folder as they are written.
OW_TEST(fixed_names_a_write_whose_own_files_hold_a_type_folder_in_two_spellings_is_refused) {
    std::string root = Root("type-case-new");
    OWFiles::RemoveTree(root);
    auto w = OWWorld::FromFiles(Map({{"world.json", WorldJson}, {"elements/Character/old.json", Element(IdA, "Old")}}));
    w->Add("character", OWJson::Parse(Bytes(Element(IdB, "Bo"))).value);
    std::string error;
    t.Ok(!w->WriteFolder(root, false, &error), "refused");
    t.Ok(!OWFiles::IsDir(root + "/elements"), "nothing written");
    OWFiles::RemoveTree(root);
}

OW_TEST(fixed_names_another_types_folder_in_another_case_does_not_stop_a_write) {
    std::string root = Root("type-other");
    WriteTree(root, {{"world.json", WorldJson}, {"elements/Location/old.json", Element(IdA, "Old")}});
    auto w = OWWorld::ReadFolder(root);
    if (!w) {
        t.Fail("the world did not read");
        return;
    }
    w->Add("character", OWJson::Parse(Bytes(Element(IdB, "Bo"))).value);
    std::string error;
    t.Ok(w->WriteFolder(root, true, &error), "written (" + error + ")");
    auto back = OWWorld::ReadFolder(root);
    t.Ok(back && back->Get(IdB) != nullptr, "read back");
    t.Ok(w->WriteFolder(root, false, &error), "a whole write back carries Location/ as it is (" + error + ")");
    OWFiles::RemoveTree(root);
}

OW_TEST(fixed_names_a_write_into_a_new_folder_writes_as_before) {
    std::string root = Root("fresh");
    OWFiles::RemoveTree(root);
    auto w = OWWorld::FromFiles(Map({{"world.json", WorldJson}}));
    w->Add("character", OWJson::Parse(Bytes(Element(IdB, "Bo"))).value);
    std::string error;
    t.Ok(w->WriteFolder(root, false, &error), "written (" + error + ")");
    auto back = OWWorld::ReadFolder(root);
    t.Ok(back && back->Get(IdB) != nullptr, "read back");
    OWFiles::RemoveTree(root);
}

namespace {

const std::string LongS = "\xC5\xBFpatial";  // spatial with U+017F first, which OW-18's fold makes an s

// npm 4.212.3's writer vectors: world.json, one element, and a pin under `spatial` (or Elements/ alone when empty).
std::vector<std::pair<std::string, std::string>> WriterCase(const std::string& spatial,
                                                            const std::string& elements = "elements") {
    std::vector<std::pair<std::string, std::string>> files = {{"world.json", WorldJson},
                                                              {elements + "/character/ada--c1.json", Element(IdA)}};
    if (!spatial.empty()) files.push_back({spatial + "/pin/p--p1.json", Element(IdB, "P")});
    return files;
}

const std::vector<std::string> ControlNames = {"mimetype", "w/elements/character/ada--c1.json",
                                               "w/spatial/pin/p--p1.json", "w/world.json"};

}  // namespace

OW_TEST(fixed_names_pack_world_refuses_spatial_in_another_case_beside_elements_as_a_copy_and_a_share) {
    // The TypeScript SDK 4.212.3: its writer dropped Spatial/ beside elements/ from a copy and emptied a share; it now
    // refuses name_case and writes nothing, as the readers refuse such a world. PackWorld, the files-in writer, the same.
    for (bool copy : {true, false}) {
        const std::string mode = copy ? "copy" : "share";
        for (const std::string& top : {std::string("Spatial"), LongS}) {
            OWPacked p = OWWorldArchive::PackWorld(Map(WriterCase(top)), "w", false, false, false, copy);
            t.Ok(!p.ok && p.bytes.empty() && p.names.empty(), mode + ", " + top + ": refused, nothing written");
            t.Ok(p.message.find("name_case") != std::string::npos && p.message.find("'" + top + "'") != std::string::npos,
                 mode + ", " + top + ": the cause and the spelling (" + p.message + ")");
        }
        OWPacked alone = OWWorldArchive::PackWorld(Map(WriterCase("", "Elements")), "w", false, false, false, copy);
        t.Ok(!alone.ok && alone.bytes.empty() && alone.message.find("name_case") != std::string::npos,
             mode + ", Elements/ alone: refused (" + alone.message + ")");
        // The control: the names as written, a share carrying the element and the legacy pin both.
        OWPacked control = OWWorldArchive::PackWorld(Map(WriterCase("spatial")), "w", false, false, false, copy);
        t.Ok(control.ok && control.names == ControlNames, mode + ": the names as written are written (" + control.message + ")");
        t.Ok(OWWorldArchive::Read(control.bytes).ok, mode + ": and read back");
    }
}

OW_TEST(fixed_names_pack_folder_refuses_spatial_in_another_case_beside_elements_as_a_copy_and_a_share) {
    // The same vectors on a folder on disk: held since the pack lists the folder (section 4.8, ruled 2026-11-25).
    for (bool copy : {true, false}) {
        const std::string mode = copy ? "copy" : "share";
        for (const std::string& top : {std::string("Spatial"), LongS}) {
            std::string root = Root("spatial-" + mode) + "/w";
            WriteTree(root, WriterCase(top));
            OWPacked p = OWWorldArchive::PackFolder(root, false, false, false, copy);
            t.Ok(!p.ok && p.bytes.empty(), mode + ", " + top + ": refused, nothing packed");
            t.Ok(p.message.find("name_case") != std::string::npos && p.message.find("'" + top + "'") != std::string::npos,
                 mode + ", " + top + ": the cause and the spelling (" + p.message + ")");
            OWFiles::RemoveTree(Root("spatial-" + mode));
        }
        std::string control = Root("control-" + mode) + "/w";
        WriteTree(control, WriterCase("spatial"));
        OWPacked c = OWWorldArchive::PackFolder(control, false, false, false, copy);
        t.Ok(c.ok && c.names == ControlNames, mode + ": the names as written pack (" + c.message + ")");
        OWFiles::RemoveTree(Root("control-" + mode));
    }
}
