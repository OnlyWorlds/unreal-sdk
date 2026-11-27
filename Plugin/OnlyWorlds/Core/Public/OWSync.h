#pragma once
// Fills an OWWorldCache from the API and keeps it current from keel's change feed. Separate from the client and the
// cache on purpose: the client knows the wire, the cache holds the world, and the policy between them (when to walk
// again, what a rewound cursor means, what a reset keeps) belongs to neither. The feed's rules are keel's changes page
// (https://onlyworlds.github.io/docs/development/api/changes). Mirrors the Unity SDK's OWSync and the Godot SDK's.
//
//   OnlyWorlds::OWSync::Incremental(client, cache, [](OWReply<OWSyncResult> r) { ... });   // the first walks it all
//   OnlyWorlds::OWSync::HasChanges(client, cache, [&](OWReply<bool> r) {
//       if (r.Ok() && r.value) OnlyWorlds::OWSync::Incremental(client, cache, ...);
//   });
//
// Every call answers through its handler exactly once, never a throw: the value, or the failing request's own error
// (OWApiError). A read-only cache (OWWorldCache::read_only) and a cache with a sync in flight (Syncing) are refused,
// kind Refused, nothing sent. The client and the cache must outlive the answer: the walk calls the client again
// from each answer and writes into the cache. Each tip read goes through OWClient::ChangesHead, so a caller that keeps
// one client sends the last tip's ETag and gets a 304 while nothing moved. World-settings edits are not in the feed:
// poll OWClient::GetWorld for them.

#include "OWApi.h"
#include "OWClient.h"
#include "OWWorldCache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace OnlyWorlds {

// What a sync did.
struct OW_API OWSyncResult {
    // Elements written into the cache.
    int64_t fetched = 0;
    // Elements the feed deleted (or hid) that the cache held.
    int64_t removed = 0;
    // The change_seq the cache stands at after the sync.
    int64_t cursor = 0;
    // True when the cache was walked again from the start rather than updated.
    bool was_rebaselined = false;
    // Why it was walked again, in words; "" when it was not.
    std::string rebaseline_reason;
    // On a walk the server asked for (409 resync_required): its reason, "role" or "view" (OWApiError::ResyncReason);
    // absent when it sent none or asked for nothing.
    std::optional<std::string> resync_reason;
    // On such a walk for role or no reason, and on one for a cursor ahead of the server's head: how many elements the
    // cache held that the new walk did not name, and kept (out of view or rewound, not gone). 0 on view, which drops
    // them.
    int64_t kept = 0;

    // True when the sync changed the cache.
    bool ChangedAnything() const;
};

class OW_API OWSync {
public:
    // Walk every element type (OWAddress::TypeWords, in the table's order) into `cache`, replacing what it held, through
    // the list routes after a tip read: the tip is read first, and its head, cursor and epoch are what the cache
    // records, so a change landing during the walk is above the recorded cursor and the next sync applies it.
    // `on_type`, when set, is called after each type with the type and how many elements it held. A walk that fails
    // leaves the cache as it was, its elements and its cursor, and answers that failure: a baseline never empties a
    // copy on its own.
    static void Baseline(OWClient& client, OWWorldCache& cache, OWHandler<OWSyncResult> done,
                         std::function<void(const std::string&, size_t)> on_type = {});
    // Apply everything that changed since the cache's cursor. Resumes from the server's own cursor (CursorToken), passed
    // back as is, never one built from a number (a guest's feed answers a bare number above 0 with 409
    // resync_required); a cache with no token resumes from its bare Cursor. Walks again from the start instead of
    // updating (Baseline) when the cache has no cursor; when its cursor is ahead of the server's head (the server was
    // restored from a backup: walked again, and what the cache held that the restored server no longer names is kept,
    // OWSyncResult::kept); and when a feed page answers 409 resync_required, where what the new walk does not name
    // follows the 409's reason: view drops it, role or no reason keeps it. A reset whose walk fails puts the cache and
    // its cursor back as they were. Current, one tip read and nothing else, only when head and epoch both stand. An
    // upsert carries its element; one that does not is read by id (its error, a 409 too, answered as it is). Each
    // element is held under its body's own id, the row's written into a body that has none. Deletes remove the element
    // whatever their reason (hidden too). The seq is claimed only when a page said there is no more: a feed that says
    // more but gives no cursor leaves the seq where it was and the token at the last cursor given. A request that
    // fails mid-walk answers its error with the cursor unmoved, so the next sync replays from where this one began.
    static void Incremental(OWClient& client, OWWorldCache& cache, OWHandler<OWSyncResult> done);
    // Whether a sync would do anything, from one tip read and no element: true when the head is not the cache's cursor
    // (either way: a head below it means the server was restored, and Incremental walks again) or the epoch moved (a
    // guest's view changed). True means a sync is not a no-op, not that new content exists. Never refused for a sync
    // in flight or a read-only cache: it writes nothing.
    static void HasChanges(OWClient& client, const OWWorldCache& cache, OWHandler<bool> done);

private:
    // One sync's state and steps (OWSync.cpp).
    class Run;
};

}  // namespace OnlyWorlds
