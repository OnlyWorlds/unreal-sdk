#pragma once
// One world: world.json and its elements, read from a world folder or an .owworld's files.
//
// Writing follows the Godot SDK's rule (gaming/DECISIONS.md #2): an element nobody edited is written back as the bytes
// it was read with; an edited one (and world.json, once edited) in the folder spec's section 5 form (OWJson).
//
//   auto w = OnlyWorlds::OWWorld::ReadFolder("C:/worlds/moppetopia");
//   for (OWElement* pin : w->OfType("pin")) pin->Set("x", OWJsonValue::Number(int64_t(12)));
//   w->WriteFolder("C:/worlds/moppetopia", /*only_edited=*/true);

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorldArchive.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace OnlyWorlds {

struct OWElement {
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

class OWWorld {
public:
    // The element folders: elements/, and spatial/, the format's dated layout for the spatial types (section 4).
    static const char* const ElementFolders[2];

    // Read a world folder: world.json, every elements/<type>/*.json (and the legacy spatial/<type>/), and, with
    // `other_parts`, media/ and records/ as bytes (so a write to a new folder gives the world whole; the same
    // selection Pack makes). nullptr when there is no world.json.
    static std::unique_ptr<OWWorld> ReadFolder(const std::string& path, bool other_parts = true);
    // A world from files keyed by their path inside it (an OWArchiveRead's files).
    static std::unique_ptr<OWWorld> FromFiles(const OWFileMap& files);

    // The folder read from ("" for a world from files).
    std::string root;
    // world.json parsed (an empty object when it does not parse), and its bytes as read.
    OWJsonValue info;
    OWBytes world_bytes;
    bool info_edited = false;
    // Every element, in the order read (types and names in UTF-8 byte order).
    std::vector<OWElement> elements;
    // Files of the world that are not elements (media/, records/, a file that does not parse, a body with no id), by
    // path inside the world, kept as bytes so they write back whole.
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
    // just the files an edit changed (to write back into the folder read).
    OWFileMap ToFiles(bool only_edited = false);
    bool WriteFolder(const std::string& path, bool only_edited = false, std::string* error = nullptr);
    // The world as a portable .owworld (OWWorldArchive::PackWorld: api stripped, private records left out, unless asked).
    OWPacked Pack(const std::string& top, bool keep_api = false, bool include_private = false);

    // Section 3.2's filename for an element: <slug>--<id tail>.json, or <full id>.json when the name gives no slug;
    // with `full_id`, the collision fallback <slug>--<full id>.json.
    static std::string ElementFilename(const std::string& id, const std::string& name, bool full_id = false);
    // Lowercase ASCII kebab of a name, at most 40 characters (NFKD, nonspacing marks dropped: the Unity writer's rule).
    static std::string Slugify(const std::string& name);

private:
    std::unordered_map<std::string, size_t> by_id_;
    void SetWorldJson(OWBytes bytes);
    void AddParsed(const std::string& rel, const std::string& type, OWBytes bytes);
    void AssignPaths();
};

}  // namespace OnlyWorlds
