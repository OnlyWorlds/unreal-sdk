#pragma once
// The one-file world export onlyworlds.com hands an owner from their world's page ("Export world"): one JSON document,
// {format: "onlyworlds-world-export", schema_version, exported_at, world: {...}, elements: {<type>: [<body>, ...]}}
// (keel's API spec, section 8c; schema-dist's example/world-export.json is the emitter's own output). Read into an
// OWWorld, the folder reader's own world, so a game opens the download as it opens a world folder.
//
//   OnlyWorlds::OWExportRead r = OnlyWorlds::OWWorldExport::ReadFile("Downloads/my-world.json");
//   if (r.ok) for (OnlyWorlds::OWElement* c : r.world->OfType("character")) Show(c->Name());
//
// The rules are the folder reader's (OWWorld::ReadFolder): an element with no id that is a non-empty string is skipped
// and named in the world's errors, a body's own type (one of the 22 type slugs, as written) beats the group it sits
// under, a group whose key is not one of the 22 is not read and is named, two bodies with one id serve the first and
// name the second, unknown keys are ignored, and each body is kept as given (key order, number texts). The world block
// is the world's world.json, whole. The file carries no media and no api block: a world read from it is local, and
// OWWorld::WriteFolder writes it as a world folder (world.json the world block, each element at section 3.2's path).
//
// The file is one JSON text, read whole: an object or array nested past 200 counted from its top (OWJson::MaxDepth)
// refuses the whole file, too_deep, so an element counts three levels deeper here (the document, elements, its list)
// than in a file of its own.

#include "OWApi.h"
#include "OWFiles.h"
#include "OWWorld.h"

#include <memory>
#include <string>

namespace OnlyWorlds {

struct OWExportRead {
    bool ok = false;
    std::string reason;          // a refusal's one cause (OWWorldExport::Reasons); "" when read
    std::string message;         // what was found, in words
    std::string format;          // the file's `format` as found; "" when it is missing or not a string
    std::string schema_version;  // the standard's version the elements follow, as sent ("" when absent): information,
                                 // never checked
    std::string exported_at;     // when the owner downloaded it (ISO 8601), as sent ("" when absent)
    std::string world_id;        // the world block's id, when read
    // The world, when ok: root "", info the world block, an element per body read, errors one line per thing skipped.
    std::unique_ptr<OWWorld> world;
};

namespace OWWorldExport {

extern OW_API const char* const Format;  // "onlyworlds-world-export"

// The one list of refusal causes: no file at the path; not JSON, or JSON but not an object; nested past 200; a
// `format` other than Format; no world block with an id that is a non-empty string. Everything else in the file
// degrades to a line in the world's errors.
extern OW_API const char* const Reasons[5];  // no_file, not_json, too_deep, not_an_export, no_world_id

// Read an export's bytes. `source` names where they came from, at the start of each error line's place
// ("x.json#elements/character/0"); "the export" when empty.
OW_API OWExportRead Read(const OWBytes& raw, const std::string& source = "");
// Read an export file: no_file when there is none, else Read of its bytes with the path as the source.
OW_API OWExportRead ReadFile(const std::string& path);

}  // namespace OWWorldExport
}  // namespace OnlyWorlds
