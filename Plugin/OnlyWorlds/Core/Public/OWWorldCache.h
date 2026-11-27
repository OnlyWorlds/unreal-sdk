#pragma once
// One world held by a game, kept current from the API by OWSync (OWSync.h), and kept between launches in a file of its
// own (WriteFile, ReadFile). Each element is held as its type and its body whole, as served: extension fields, nulls
// and number texts included. Beside the elements, where the last sync stopped: Cursor, CursorToken and Epoch. ToWorld
// gives the held world as an OWWorld, so every reader that takes one (the lens, the graph, the messages, the plugin's
// structs) reads a synced world. Mirrors the Unity SDK's OWWorldCache and the Godot SDK's.
//
//   auto world = OnlyWorlds::OWWorldCache::ReadFile("Saved/world.owcache");   // empty the first time
//   OnlyWorlds::OWSync::Incremental(client, *world, [&](OWReply<OWSyncResult> r) {
//       if (r.Ok()) world->WriteFile("Saved/world.owcache");
//       for (const OWCachedElement* c : world->OfType("character")) Show(c->body.Find("name"));
//   });
//
// Not thread-safe, as the client is not: use a cache from the thread the client answers on.

#include "OWApi.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace OnlyWorlds {

// One held element: its id (the body's own), its type word ("character") and its body as served.
struct OWCachedElement {
    std::string id;
    std::string type;
    OWJsonValue body;
};

class OW_API OWWorldCache {
public:
    // The file's format key, which ReadFile checks.
    static const char* const Format;  // "onlyworlds-unreal-cache"
    // The file's version; ReadFile refuses a newer one.
    static constexpr int64_t Version = 1;

    explicit OWWorldCache(std::string id = "", std::string name = "", bool frozen = false);

    // The world's id and name, as the game names them (informational: a sync does not check them).
    std::string world_id;
    std::string world_name;
    // A frozen snapshot, never to be written into: OWSync refuses it, nothing sent. Consumers must honour it.
    bool read_only = false;

    // How many elements are held.
    size_t Count() const;
    // The element with this id, or nullptr. A pointer the next Upsert, Remove or Clear may invalidate.
    const OWCachedElement* Get(const std::string& id) const;
    bool Has(const std::string& id) const;
    // True when the id is held as a known-of stub (known_of is the JSON true, OWClient::IsKnownOf): false for a whole
    // element and for an id not held.
    bool IsKnownOf(const std::string& id) const;
    // Every element held, in the order first held.
    std::vector<const OWCachedElement*> Elements() const;
    // Every element of one type ("pin"), in the order first held.
    std::vector<const OWCachedElement*> OfType(const std::string& type) const;
    // The elements a list of ids names, in its order, skipping the ids not held: a dangling link is normal (a partial
    // sync, a sparse read), and Has says which.
    std::vector<const OWCachedElement*> Resolve(const std::vector<std::string>& ids) const;
    // The same for a link field's value: an array (its strings), or one id (a single link); anything else names none.
    std::vector<const OWCachedElement*> Resolve(const OWJsonValue& link) const;

    // Hold `body` under its own id (its "id", a non-empty string) as `type`, replacing the one held there, in its
    // place: an element whose type changed moves with it. False, and nothing held, for a body that is not an object or
    // has no such id, and for an empty type.
    bool Upsert(const std::string& type, OWJsonValue body);
    // Let go of the element with this id. False when none was held.
    bool Remove(const std::string& id);
    // Let go of every element, and of where the last sync stopped (Cursor, CursorToken, Epoch). The world's id, name and
    // read_only stay; SyncedAt stays too, as the Godot SDK keeps it.
    void Clear();

    // The change_seq the last sync reached; 0 before any.
    int64_t Cursor() const;
    // The feed's own cursor where the last sync stopped, opaque, as the server sent it; "" when there is none, and a
    // sync then resumes from Cursor.
    const std::string& CursorToken() const;
    // The view epoch the last sync ran under: a guest's or a contributor's key; absent for the others.
    const std::optional<int64_t>& Epoch() const;
    // When the last sync stopped (ISO 8601, UTC), "" before any.
    const std::string& SyncedAt() const;
    // Record where a sync stopped: the change_seq, the server's cursor, the view epoch, the time. A bare `seq` drops
    // any token and epoch held, since a token beside a seq set by hand would resume from the wrong place.
    void SetCursor(int64_t seq, std::string token = "", std::optional<int64_t> epoch = std::nullopt, std::string at = "");

    // True while a Baseline or an Incremental is in flight on this cache: a second one is refused, nothing sent.
    bool Syncing() const;

    // The cache as one JSON document: {format, version, world_id, world_name, read_only, cursor, cursor_token, epoch,
    // synced_at, elements: [{type, element}]}, the Godot SDK's keys under this format's word.
    OWJsonValue ToJson() const;
    // A cache from such a document, or nullptr and why (not an object, another format, a version newer than this
    // one's). A key the document lacks reads as unset: one with no cursor_token resumes from its cursor. A row that is
    // not {type, element} with an element this cache would hold is skipped.
    static std::unique_ptr<OWWorldCache> FromJson(const OWJsonValue& doc, std::string* why = nullptr);
    // Write ToJson to `path` (OWJson's section 5 form, each number as it was served). False, and why, when the file
    // could not be written.
    bool WriteFile(const std::string& path, std::string* error = nullptr) const;
    // A cache read from `path`, as WriteFile wrote it: an empty cache when there is no file (a first launch), nullptr
    // and why when the file is not a cache's (not JSON, another format, a newer version).
    static std::unique_ptr<OWWorldCache> ReadFile(const std::string& path, std::string* error = nullptr);

    // The held world as an OWWorld, for every reader that takes one: world.json is {"id": world_id, "name":
    // world_name}, each element added in the order held as its held type (OWWorld::Add). It holds no world settings:
    // they are not in the change feed (poll OWClient::GetWorld for them).
    std::unique_ptr<OWWorld> ToWorld() const;

private:
    std::vector<OWCachedElement> elements_;
    std::unordered_map<std::string, size_t> by_id_;
    int64_t cursor_ = 0;
    std::string cursor_token_;
    std::optional<int64_t> epoch_;
    std::string synced_at_;
    bool syncing_ = false;

    friend class OWSync;
};

}  // namespace OnlyWorlds
