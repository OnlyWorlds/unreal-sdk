// OWId: the ids OWClient's writes mint for an element with none. A UUIDv7's layout (the timestamp in its first 48 bits,
// version 7, variant 10, the rest random) and the keyed UUIDv5 the Python package, the Unity SDK and the TypeScript SDK
// derive from an Idempotency-Key and a position: the vectors are the Unity SDK's OWIdTests (Python's uuid.uuid5 of the
// same name in the same namespace, checked again on 2026-11-03).

#include "ow_test.h"

#include "OWId.h"

#include <chrono>
#include <cstdint>
#include <set>
#include <string>

using namespace OnlyWorlds;

namespace {

// Lowercase 8-4-4-4-12 hex.
bool IsUuidForm(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        char c = s[i];
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// The first 48 bits of a UUID as a number (a v7's Unix milliseconds).
int64_t Millis(const std::string& id) {
    std::string hex = id.substr(0, 8) + id.substr(9, 4);
    return static_cast<int64_t>(std::stoull(hex, nullptr, 16));
}

}  // namespace

OW_TEST(id_keyed_is_pythons_uuid5_of_key_and_index) {
    t.Eq(std::string(OWId::KeyedNamespace), "0199b6a0-6f0e-7c2a-9d1e-3b5c7a8e4f21", "the Python package's _KEYED_IDS");
    struct Case {
        const char* key;
        int64_t index;
        const char* want;
    };
    const Case cases[] = {
        {"retry-1", 0, "1b88c4e4-831f-503c-8149-cece1519ca32"},
        {"retry-1", 1, "2f7b4a01-0c9b-5b1e-8602-270abbee8184"},
        {"retry-1", 3, "44bb129d-48b9-5fc2-b3c9-cc5f549e3e6e"},
        {"\xC3\x86r\xC3\xB8sk\xC3\xB8" "bing", 0, "337e8ce1-5eb9-5373-96e9-9f67300da4ed"},
        {"key \xF0\x9F\x97\xBA\xEF\xB8\x8F", 2, "cb7502a0-06e3-5d4e-8adf-8f9c22ff46e7"},
    };
    for (const Case& c : cases) t.Eq(OWId::Keyed(c.key, c.index), c.want, std::string(c.key) + ", " + std::to_string(c.index));
}

OW_TEST(id_keyed_has_none_for_an_empty_key) {
    t.Eq(OWId::Keyed("", 0), "", "an empty key has no keyed id");
}

OW_TEST(id_new_id_is_a_uuid_v7_carrying_its_milliseconds) {
    const int64_t ms = 0x0123456789AB;
    std::string id = OWId::NewId(ms);
    t.Ok(IsUuidForm(id), "the 8-4-4-4-12 form, lowercase: " + id);
    t.Eq(id.substr(0, 14), "01234567-89ab-", "the timestamp's 48 bits first, big-endian");
    t.Eq(id.substr(14, 1), "7", "version 7");
    t.Ok(id.size() > 19 && std::string("89ab").find(id[19]) != std::string::npos, "variant 10: " + id);
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) seen.insert(OWId::NewId(ms));
    t.Eq(seen.size(), size_t(1000), "74 random bits: a thousand ids in one millisecond, all different");
}

OW_TEST(id_new_id_reads_the_clock) {
    auto now = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    };
    int64_t before = now();
    std::string id = OWId::NewId();
    int64_t after = now();
    t.Ok(IsUuidForm(id) && id[14] == '7', "a v7: " + id);
    if (IsUuidForm(id)) t.Ok(Millis(id) >= before && Millis(id) <= after, "its milliseconds are now's: " + id);
}

OW_TEST(id_mint_is_keyed_under_a_key_else_new) {
    t.Eq(OWId::Mint("retry-1", 1), "2f7b4a01-0c9b-5b1e-8602-270abbee8184", "under a key, the keyed id");
    std::string fresh = OWId::Mint("", 0);
    t.Ok(IsUuidForm(fresh) && fresh[14] == '7', "without one, a fresh v7: " + fresh);
    t.Ok(OWId::Mint("", 0) != fresh, "and another each time");
}
