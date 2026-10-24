#pragma once
// A world folder as one file: the .owworld archive (folder spec section 4.7, checks OW-1 to OW-22).
//
// Read holds an archive to every reader rule, in the reference checker's order (run_py.py's read_archive), and
// refuses with one cause from the spec's list. Pack writes a folder as a portable .owworld: the marker stored first,
// only the folder's parts, entries in UTF-8 byte order, every one stamped 1980-01-01 00:00, methods 0 and 8.

#include "OWFiles.h"
#include "OWJson.h"
#include "OWZip.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace OnlyWorlds {

// Files by their path inside a world ("elements/character/ana--0000ab01.json"). std::map orders std::string keys by
// their unsigned bytes, which is OW-13's UTF-8 byte order.
using OWFileMap = std::map<std::string, OWBytes>;

struct OWArchiveBudget {
    uint64_t max_bytes = 2147483648ull;  // the floor every reader accepts (OW-15): 2 GiB inflated
    uint64_t max_entries = 500000;       // and 500,000 files
};

struct OWArchiveRead {
    bool ok = false;
    std::string reason;   // a refusal's one cause (OWWorldArchive::Reasons)
    std::string message;  // what was found, in words
    std::string world_id;
    // "" when world.json sits at the archive's root, else the top folder with its slash ("abbey/").
    std::string prefix;
    // Every file inside the world, by its path inside the world. world.json is the archive's own bytes, or, when it
    // carried an api block, re-written without it (section 5's form).
    OWFileMap files;
    // world.json parsed, without its api block.
    OWJsonValue world;
    // world.json as the archive holds it.
    OWBytes held_world_json;
    std::vector<OWZipEntry> entries;
    // The largest buffer one entry's inflate allocated (lean 3's measure).
    uint64_t largest_allocation = 0;
};

struct OWPacked {
    bool ok = false;
    std::string message;
    std::vector<std::string> names;  // every entry, in the archive's order
    OWBytes bytes;
    // Producer directories under records/ left out as private (section 4.5 rule 11), by name.
    std::vector<std::string> left_out;
};

namespace OWWorldArchive {

extern const char* const MediaType;  // application/vnd.onlyworlds.world+zip
extern const char* const Marker;     // mimetype
extern const char* const Extension;  // owworld

// The one list of refusal causes (section 4.7).
extern const char* const Reasons[12];

// Read an archive's bytes as a conforming reader does. Nothing is extracted; an entry is never inflated more than one
// byte past what it declares.
OWArchiveRead Read(const OWBytes& raw, const OWArchiveBudget& budget = OWArchiveBudget());
OWArchiveRead ReadFile(const std::string& path, const OWArchiveBudget& budget = OWArchiveBudget());

// OW-16, strict: a plain relative path, '/'-separated, no empty, '.' or '..' segment, no drive, no backslash. "" when
// the name is one, else what is wrong with it.
std::string PathFault(const std::string& name);
// OW-22: every segment lands on any disk. "" when it does.
std::string NameFault(const std::string& name);

// What PackFolder selects from a folder, part by part. CollectContent: every file of a content folder (elements/,
// spatial/, media/) but dot-names and .meta files, recursively, keyed `rel/...`. CollectRecords: records/ whole but for
// Unity's own .meta files and, unless include_private, the producers RECORDS.json marks private (named in left_out);
// "" or why the manifest cannot say which are private.
void CollectContent(const std::string& dir, const std::string& rel, OWFileMap& into);
std::string CollectRecords(const std::string& folder, bool include_private, OWFileMap& into,
                           std::vector<std::string>* left_out = nullptr);

// Pack exactly the files given under the top folder `top`: the marker first, then every file in UTF-8 byte order,
// deflated (an empty one stored). Nothing is filtered: what a caller hands in is what it asked for.
OWPacked PackFiles(const OWFileMap& files, const std::string& top);

// Pack a world folder as a portable .owworld: world.json (its api block stripped unless keep_api: a private copy),
// elements/, spatial/ and media/ without dot-names and .meta files, and records/ whole but for Unity's own .meta
// files and, unless include_private, the producers records/RECORDS.json marks private.
OWPacked PackFolder(const std::string& folder, bool keep_api = false, bool include_private = false);

// PackFolder, written to `archive_path` (refused when that lies inside the folder).
OWPacked Pack(const std::string& folder, const std::string& archive_path, bool keep_api = false,
              bool include_private = false);

}  // namespace OWWorldArchive
}  // namespace OnlyWorlds
