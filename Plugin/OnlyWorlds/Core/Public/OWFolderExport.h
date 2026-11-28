#pragma once
// An onlyworlds.com world, read over the API, written as a world folder: an offline copy a game can ship or open later
// with OWWorld::ReadFolder. The folder is the one the onlyworlds Python package's `export_world` writes and the Unity
// SDK's `OWFolderExport` writes, element files byte for byte.
//
//   OnlyWorlds::OWFolderExport::Export(client, "C:/Games/MyGame/Worlds/harbour", {},
//       [](OnlyWorlds::OWFolderExportReport r) { if (!r.ok) Log(r.message); });
//
// Only GETs: the world is never written. GET /world/, then GET /me/ (which part of the world the key reads), then every
// element of the 22 types, page by page, all read before the first file is written. The answer comes through `done`,
// once, on the thread the client answers on; the client must outlive the export.
//
// world.json is the GET /world/ body, every key in the order served (WorldForDisk: the now renamed time_current,
// format_version added), with this export's provenance (folder spec section 4.2): `source` (which server, which world,
// when the export began), `exported_as` when GET /me/ can say which slice the key reads (ExportedAs), and `producer`
// (Producer). A world's `origin`, its first import as the server keeps it, rides along as served.
//
// Elements are written as served, extension keys and the served-trimmed mark (x_served_trimmed) included; a known-of
// stub without its unset fields (KnownOfForDisk). Within a type a repeated id keeps its last body: a page walk that met
// a write serves an element twice, never skips one, and the later copy is the newer. File names follow the folder
// format (section 3.2), the collision rule included.
//
// A folder is born whole or not at all. The folder must not exist, or be an empty directory. The tree is built in a
// sibling directory (.ow-<8 hex>) and moved into place as the last step, so a refusal leaves the folder as it was.
// Refused, with nothing written: an element with no id, one id under two types, an id that cannot be one file name in
// its type folder (OWWorld::WriteRefusal), a world with no id or name. Map images are not kept (the folder has their
// image_url).

#include "OWApi.h"
#include "OWApiError.h"
#include "OWJson.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace OnlyWorlds {

class OWClient;

// What Export did.
struct OW_API OWFolderExportReport {
    bool ok = false;
    // Why not, when ok is false; "" when it is true.
    std::string message;
    // The client's error when a read was refused or got no answer (IsAuthError for a wrong key or PIN); absent otherwise.
    std::optional<OWApiError> error;
    // The folder, absolute, '/' separated.
    std::string folder;
    // The world's id and name, from its world.json.
    std::string world_id;
    std::string world_name;
    // Elements written per type slug, types with none left out.
    std::map<std::string, size_t> counts;

    // Every element written.
    size_t Total() const;
};

// How Export runs.
struct OW_API OWFolderExportOptions {
    // The time as Unix milliseconds, read once for source.at before the first request. Empty: the system's clock.
    std::function<int64_t()> clock;
    // Told (type, done, total) after each of the 22 types is read.
    std::function<void(const std::string& type, int done, int total)> on_progress;
};

namespace OWFolderExport {

// The folder format version an export declares in world.json: the dist's folder check's FORMAT_VERSION.
extern OW_API const char* const FormatVersion;  // "0.3.25"
// The name this plugin's releases publish under (onlyworlds-unreal-X.Y.Z.zip): the producer line's tool.
extern OW_API const char* const ProducerTool;  // "onlyworlds-unreal"
// This plugin's version, as OnlyWorlds.uplugin's VersionName says (a test holds the two equal): the producer's version.
extern OW_API const char* const ProducerVersion;
// The keys every known-of stub keeps, written even when empty (folder spec section 4.4, keel's KNOWN_OF_BASE).
extern OW_API const char* const KnownOfKeeps[10];
// The member roles whose slice is the role's, with no Character (co_builder as keel serves it).
extern OW_API const char* const MemberRoles[2];
// The 22 element types in the standard's order, the order the export reads them in (the Python package's ELEMENT_TYPES).
extern OW_API const char* const Types[22];

// The producer line every folder Export makes carries (folder spec section 4.2): {"tool": "onlyworlds-unreal",
// "version": "<this plugin's>", "input": "server"}, the spec's key order, `server` its word for an export from
// onlyworlds.com.
OW_API OWJsonValue Producer();

// A known-of stub (OWClient::IsKnownOf) as the folder format writes it (section 4.4), its keys in received order, then
// known_of: true: every KnownOfKeeps key, and every other key the server sent with a value. The unset values keel
// serves on a stub ("", null, []) are left out: on disk they would read as a whole element with nothing filled in.
// 0 and false are values.
OW_API OWJsonValue KnownOfForDisk(const OWJsonValue& body);

// Why `world` cannot be a world.json, or "": it needs an id that is a non-empty string and a name that is a string,
// which may be empty (folder spec section 4.1).
OW_API std::string WorldRefusal(const OWJsonValue& world);

// The world.json for a GET /world/ body (or a world.json read from a folder): every key carried in received order,
// time_range_current renamed time_current in place (both kept when both are there: dropping either would be a drop),
// format_version added when absent (a carried one is kept, even null). snapshot_counts belongs to snapshots alone:
// recounted from `counts` when the body has snapshot_of and counts are given, else removed. origin and the World's
// extension keys ride along. Absent when WorldRefusal refuses the body.
OW_API std::optional<OWJsonValue> WorldForDisk(const OWJsonValue& world,
                                               const std::map<std::string, size_t>* counts = nullptr);

// The exported_as line (folder spec section 4.2) for a GET /me/ answer, or absent: write none. kind owner (an
// owner-minted or classic key) is the owner, role owner, no Character; a member with role co_builder or contributor is
// a member, its role, no Character; a guest of any kind (person, agent seat, player) is its Character, {id, name}, the
// name from `character_name` (called with the id; absent when it cannot be read). Anything else is absent: no answer
// (`me` null or not an object), a guest with no Character, a name that cannot be read, a kind or role this table does
// not name. An empty name is a name the server served, and is written.
OW_API std::optional<OWJsonValue> ExportedAs(
    const OWJsonValue* me, const std::function<std::optional<std::string>(const std::string&)>& character_name);

// Read the world `client`'s key reaches and write it as a world folder at `folder`, which must not exist or be an empty
// directory; its parents are made. `done` is called once, with what was written or why nothing was.
OW_API void Export(OWClient& client, const std::string& folder, OWFolderExportOptions options,
                   std::function<void(OWFolderExportReport)> done);

}  // namespace OWFolderExport
}  // namespace OnlyWorlds
