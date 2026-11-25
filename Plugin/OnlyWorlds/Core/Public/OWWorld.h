#pragma once
// One world: world.json and its elements, read from a world folder or an .owworld's files.
//
// Writing follows the Godot SDK's rule: an element nobody edited is written back as the bytes it was read with, so a
// world read and written unedited is byte-equal; an edited one (and world.json, once edited) in the folder spec's section 5 form (OWJson).
//
//   auto w = OnlyWorlds::OWWorld::ReadFolder("worlds/moppetopia");
//   for (OWElement* pin : w->OfType("pin")) pin->Set("x", OWJsonValue::Number(int64_t(12)));
//   w->WriteFolder("worlds/moppetopia", /*only_edited=*/true);

#include "OWApi.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorldArchive.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OnlyWorlds {

struct OW_API OWElement {
    // The type folder the element was read from ("character"), or the type it was added as.
    std::string folder_type;
    // Its path inside the world ("elements/character/ana--0000ab01.json"); "" for an element not yet written.
    std::string source_path;
    OWBytes source_bytes;
    OWJsonValue body;
    bool edited = false;

    // The body's id ("" when it has none).
    std::string Id() const;
    std::string Name() const;
    // The declared type when the body has one, else the folder's (folder spec 4.1: never override a declared one).
    std::string Type() const;
    // Set a field; the element is then written in section 5's form.
    void Set(const std::string& key, OWJsonValue value);
};

class OW_API OWWorld {
public:
    // The element folders: elements/, and spatial/, the format's dated layout, map, pin, zone and marker only (section 4.8).
    static const char* const ElementFolders[2];

    // Read a world folder: world.json, every elements/<type>/*.json (and the legacy spatial/{map,pin,zone,marker}/), and, with
    // `other_parts`, media/ and records/ as bytes (so a write to a new folder gives the world whole; the same
    // selection Pack makes). nullptr when there is no world.json, and when the folder holds world.json, elements/ or
    // spatial/ in another case and not as written (FolderFault says why). Names are read as written, at both doors: <type> is
    // one of the 22 type words in lower case (elements/Character/ and elements/vessel/ are not read) and the extension
    // is .json in lower case (X.JSON is not read); each is named in `errors` and kept in `other_files`. Ids are
    // compared as written: one UUID in capitals in one file and in lower case in another is two elements.
    static std::unique_ptr<OWWorld> ReadFolder(const std::string& path, bool other_parts = true);
    // A world from files keyed by their path inside it (an OWArchiveRead's files). nullptr, as ReadFolder answers, when
    // a first-level name is world.json, elements or spatial in another case and the name itself is absent (FixedNameFault
    // over the first segments says why).
    static std::unique_ptr<OWWorld> FromFiles(const OWFileMap& files);

    // The three fixed names of a world, read as written (folder spec section 4.8, ruled 2026-11-25).
    static const char* const FixedNames[3];
    // Why a level of a world holding `names` (as written) is refused, or "": a name of `wanted` (the three fixed names
    // when not given) absent as written while one or more of `names` is it in another case, compared by OW-18's fold
    // (OWUnicode::Fold: NFC, then simple case folding), so a long s makes "spatial". The reason begins with the cause,
    // "name_case: ", and names both spellings. A level holding both (only a disk that keeps case can) has no fault: the
    // name as written is read, the other left unread. One listing answers it for ReadFolder, FromFiles, the archive
    // reader, PackFolder and WriteFolder, never the file system's lookup, which folds case on Windows and macOS.
    static std::string FixedNameFault(const std::vector<std::string>& names);
    static std::string FixedNameFault(const std::vector<std::string>& names, const std::vector<std::string>& wanted);
    // FixedNameFault for a folder on disk, from its root's listing (files and folders); "" for a folder not there.
    static std::string FolderFault(const std::string& path);
    // Why writing `rels` (paths inside the world) into the folder `path` would put a file into a type folder (the second
    // level under elements/ and spatial/) that is held in another spelling, by the folder's own listing or by another
    // of `rels`, or "": on a disk that ignores case the two are one folder, and a type folder is read as written
    // (ruled 2026-11-23), so the file would be read by no one. Compared by OW-18's fold; the reason names both
    // folders. A folder holding both spellings already (only a disk that keeps case can) is written as each is spelled.
    static std::string TypeFolderFault(const std::string& path, const std::vector<std::string>& rels);

    // The folder read from ("" for a world from files).
    std::string root;
    // world.json parsed (an empty object when it does not parse), and its bytes as read.
    OWJsonValue info;
    OWBytes world_bytes;
    bool info_edited = false;
    // Every element, in the order read (types and names in UTF-8 byte order).
    std::vector<OWElement> elements;
    // Files of the world that are not elements (media/, records/, a file that does not parse, a body with no id, a file
    // in a folder that is no type, one whose extension is .json in another case), by path inside the world, kept as
    // bytes so they write back whole.
    OWFileMap other_files;
    // What could not be read, one line each.
    std::vector<std::string> errors;

    std::string Id() const;
    OWElement* Get(const std::string& id);
    std::vector<OWElement*> OfType(const std::string& type);

    // Set a world.json key; world.json is then written in section 5's form.
    void SetInfo(const std::string& key, OWJsonValue value);

    // Add a new element of `type`; its path is given by section 3.2 when the world is written.
    OWElement& Add(const std::string& type, OWJsonValue body);

    // Every file of the world by its path inside it: world.json, each element, and other_files. `only_edited` gives
    // just the files an edit changed (to write back into the folder read). A held path never gets other bytes: an
    // edit to a world.json that could not be read is left out (its bytes as read), and so is a new element whose two
    // names are both held (WriteRefusal names each).
    OWFileMap ToFiles(bool only_edited = false);
    // Why WriteFolder and Pack would refuse to write this world, or "" when they would not: a world.json to be written
    // that could not be read (a writer never writes over such a file, the caller's id or not), or whose id is no
    // non-empty string; a new or edited element with no id; a new element whose short name and full-id name (section
    // 3.2) are both held, by an element or by a file the reader could not read. A writer mints no id (folder spec 4.1;
    // 3.4, format 0.3.22: a draft world conforms as any other, its id the tool's own until a host gives one), so the
    // caller sets it.
    std::string WriteRefusal(bool only_edited = false) const;
    // False, nothing written and `error` WriteRefusal's line, when it refuses; and when the folder holds world.json,
    // elements/ or spatial/ in another case and not as written (FolderFault's line): on a disk that folds case the
    // files would land inside Elements/, where no reader reads them; and when a file would go into a type folder held
    // in another spelling (TypeFolderFault's line: elements/character/ beside elements/Character/).
    bool WriteFolder(const std::string& path, bool only_edited = false, std::string* error = nullptr);
    // The world as a portable .owworld (OWWorldArchive::PackWorld: api stripped, private records and the author fields
    // left out, unless asked). Not ok, its message WriteRefusal's, when it refuses.
    OWPacked Pack(const std::string& top, bool keep_api = false, bool include_private = false,
                  bool include_author_fields = false);

    // Section 3.2's filename for an element: <slug>--<id tail>.json, or <full id>.json when the name gives no slug;
    // with `full_id`, the collision fallback <slug>--<full id>.json.
    static std::string ElementFilename(const std::string& id, const std::string& name, bool full_id = false);
    // Lowercase ASCII kebab of a name, at most 40 characters (NFKD, nonspacing marks dropped: the Unity writer's rule).
    static std::string Slugify(const std::string& name);

private:
    std::unordered_map<std::string, size_t> by_id_;
    // Why world.json could not be read ("" when it was, or when there was none).
    std::string world_unread_;
    void SetWorldJson(OWBytes bytes);
    void AddParsed(const std::string& rel, const std::string& type, OWBytes bytes);
    // Each new element with an id, by its index, and the path section 3.2 gives it ("" when both its names are held).
    std::vector<std::pair<size_t, std::string>> NewPaths() const;
    void AssignPaths();
};

}  // namespace OnlyWorlds
