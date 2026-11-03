#pragma once
// Element ids as the standard mints them: RFC 9562 UUIDv7 (48-bit Unix milliseconds, version 7, variant 10, 74 random
// bits), and under a caller's Idempotency-Key a UUIDv5 derived from it (Keyed), the ids OWClient's writes give an
// element that has none.
//
// The layout keel mints server-side (core/ids.py), the TypeScript SDK's mintUuid, the Unity SDK's OWId and the Python
// package's uuid7. v7 is a default, never a requirement: ids minted earlier as v4 stay valid. Ids minted a millisecond
// or more apart sort by creation, while the clock does not step backwards; a world also holds v4 ids and the old
// server's seconds-first ids, so never order elements by id (created_at is creation order). The random bits come from
// std::random_device.

#include <cstdint>
#include <string>
#include <string_view>

namespace OnlyWorlds {
namespace OWId {

// The namespace of the keyed ids: the Python package's _KEYED_IDS (onlyworlds/http.py), the Unity SDK's
// OWId.KeyedNamespace. "0199b6a0-6f0e-7c2a-9d1e-3b5c7a8e4f21".
extern const char* const KeyedNamespace;

// A new UUIDv7 in its lowercase 8-4-4-4-12 form, carrying the clock's Unix milliseconds. Mirrors the TypeScript SDK's
// `mintUuid`.
std::string NewId();
// A new UUIDv7 carrying `unix_ms` as its timestamp (its low 48 bits). Mirrors the TypeScript SDK's `mintUuid(now)`.
std::string NewId(int64_t unix_ms);

// The id for an element with none, written at `index` under the caller's `idempotency_key`: the RFC 9562 UUIDv5 of
// key + "\n" + index (UTF-8, the index in decimal) in KeyedNamespace. The same key with the same elements then sends
// the same body, so keel replays the first answer instead of refusing a second body (409 idempotency_error); the
// Python package, the Unity SDK and the TypeScript SDK derive the same id. A single create is index 0, a bulk item
// its position. "" for an empty key, which has no keyed id. Mirrors the TypeScript SDK's `keyedId`.
std::string Keyed(std::string_view idempotency_key, int64_t index);

// The id a write mints for an element with none: Keyed under a caller's key, else NewId. Mirrors the TypeScript SDK's
// create (`keyedId` under a key, else `mintUuid`).
std::string Mint(std::string_view idempotency_key, int64_t index);

}  // namespace OWId
}  // namespace OnlyWorlds
