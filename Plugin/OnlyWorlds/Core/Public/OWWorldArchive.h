#pragma once
// A world folder as one file: the .owworld archive (folder spec section 4.7, checks OW-1 to OW-22).
//
// Read holds an archive to every reader rule, in the reference checker's order (run_py.py's read_archive), and
// refuses with one cause from the spec's list. Pack writes a folder as a portable .owworld: the marker stored first,
// only the folder's parts, entries in UTF-8 byte order, every one stamped 1980-01-01 00:00, methods 0 and 8.

#include "OWApi.h"
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
    // For a not_a_world whose world.json is there but cannot be read, what is wrong with it, in the folder read
    // report's words: "too_deep" (nested past 200) or "not_json". "" on every other refusal (no world.json, one that
    // is JSON but no object, one with no id) and on a world read. A tool reads this, never the message. The
    // TypeScript SDK's OwWorldFileError.worldFault.
    std::string world_fault;
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
    // The files written without their author fields (OWWorldArchive::StripAuthorFields), by path inside the world, in
    // UTF-8 byte order; empty when they were asked to travel, and for PackFiles.
    std::vector<std::string> author_fields_left_out;
    // On a share (include_hidden false, section 4.8), by path inside the world in UTF-8 byte order: the element files
    // left out for their mark, and apart from them every other file under elements/ and spatial/ left out because the
    // folder reader would not serve it as one element. Both empty on a copy, the default.
    std::vector<std::string> hidden_left_out;
    std::vector<std::string> unread_left_out;
};

namespace OWWorldArchive {

extern OW_API const char* const MediaType;  // application/vnd.onlyworlds.world+zip
extern OW_API const char* const Marker;     // mimetype
extern OW_API const char* const Extension;  // owworld

// The one list of refusal causes (section 4.7; name_case, a fixed name in another case, section 4.8).
extern OW_API const char* const Reasons[13];

// Read an archive's bytes as a conforming reader does. Nothing is extracted; an entry is never inflated more than one
// byte past what it declares.
OW_API OWArchiveRead Read(const OWBytes& raw, const OWArchiveBudget& budget = OWArchiveBudget());
OW_API OWArchiveRead ReadFile(const std::string& path, const OWArchiveBudget& budget = OWArchiveBudget());

// OW-16, strict: a plain relative path, '/'-separated, no empty, '.' or '..' segment, no drive, no backslash. "" when
// the name is one, else what is wrong with it.
OW_API std::string PathFault(const std::string& name);
// OW-22: every segment lands on any disk. "" when it does.
OW_API std::string NameFault(const std::string& name);

// What PackFolder selects from a folder, part by part. CollectContent: every file of a content folder (elements/,
// spatial/, media/) but dot-names and .meta files, recursively, keyed `rel/...`. CollectRecords: records/ whole but for
// Unity's own .meta files (private producers included: PackWorld decides what travels).
OW_API void CollectContent(const std::string& dir, const std::string& rel, OWFileMap& into);
OW_API void CollectRecords(const std::string& folder, OWFileMap& into);

// Pack exactly the files given under the top folder `top`: the marker first, then every file in UTF-8 byte order,
// deflated (an empty one stored). Nothing is filtered: what a caller hands in is what it asked for.
OW_API OWPacked PackFiles(const OWFileMap& files, const std::string& top);

// Which member and which tool wrote an element (a membership id, a door): nothing to someone the world is handed to,
// so a pack leaves them out unless asked. The TypeScript SDK's AUTHOR_FIELDS, in its order.
extern OW_API const char* const AuthorFields[5];

// A file without its author fields, in section 5's form (OWJson::ToBytes), into `out`; false, `out` untouched, when its
// bytes travel as they are: it holds none of the five at its top level, or it is not one JSON object as the TypeScript
// SDK reads a file it did not write (strict UTF-8, a byte order mark read past, one value and only white space after
// it, at most 200 deep, every number one a double holds), or a number in it would be written back other than as
// JSON.stringify writes it (an integer past 2^53 that JavaScript reads rounded, which this core keeps as written).
// Only top-level keys go: an x_ value that holds one of the names stays.
OW_API bool WithoutAuthorFields(const OWBytes& bytes, OWBytes& out);

// Leave the author fields out of every file under elements/ or spatial/ in `files` that WithoutAuthorFields rewrites,
// in place. Returns their paths, in UTF-8 byte order. What PackWorld does unless asked; PackFiles never does.
OW_API std::vector<std::string> StripAuthorFields(OWFileMap& files);

// Pack a world's files as a portable .owworld under `top`: world.json's api block stripped unless keep_api (then the
// file is a private copy, OW-10); unless include_private, every producer directory records/RECORDS.json marks
// private left out whole (section 4.5 rule 11; named in left_out); and unless include_author_fields, the author
// fields left out of every element-folder file (StripAuthorFields; named in author_fields_left_out): a .owworld is a
// file handed on (folder spec 4.2). Refused when world.json is not an object with an id, or when the manifest cannot
// say which producers are private.
//
// include_hidden true (the default) packs a copy: every file under elements/ and spatial/ travels byte for byte, as
// keel's own download keeps an owner's hidden elements, and a file is also how a person moves or backs up their own
// world. False packs a share, a file made to hand to someone else (folder spec section 4.8, ruled 2026-11-23): under
// those two folders nothing travels but a file the folder reader (OWWorld::FromFiles) serves as one element with no
// `hidden` mark (a mark is any value but absent, null and false). A served element with the mark is left out whole and
// named in hidden_left_out. Every other file there is left out whole and named apart, in unread_left_out: one that is
// not UTF-8, not JSON, not an object or has no id; one no reader serves (a notes.txt, a file in a deeper folder, a
// folder that is no type, a name spelled .JSON); and both files of an id two files hold, since the share cannot say
// which file's mark is the element's. Nothing else is rewritten for a share: a visible element's link to a hidden one
// keeps its id.
OW_API OWPacked PackWorld(const OWFileMap& files, const std::string& top, bool keep_api = false, bool include_private = false,
                          bool include_author_fields = false, bool include_hidden = true);

// Pack a world folder as a portable .owworld: world.json (its api block stripped unless keep_api: a private copy),
// elements/, spatial/ and media/ without dot-names and .meta files, and records/ whole but for Unity's own .meta
// files and, unless include_private, the producers records/RECORDS.json marks private; the author fields left out
// unless include_author_fields; a copy unless include_hidden is false, then a share (PackWorld).
OW_API OWPacked PackFolder(const std::string& folder, bool keep_api = false, bool include_private = false,
                           bool include_author_fields = false, bool include_hidden = true);

// PackFolder, written to `archive_path` (refused when that lies inside the folder).
OW_API OWPacked Pack(const std::string& folder, const std::string& archive_path, bool keep_api = false,
              bool include_private = false, bool include_author_fields = false, bool include_hidden = true);

}  // namespace OWWorldArchive
}  // namespace OnlyWorlds
