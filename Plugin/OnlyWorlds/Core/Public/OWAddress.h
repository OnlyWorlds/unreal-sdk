#pragma once
// The address profile (schema-dist profiles/address.md, serial 77): one element, or a world, named by one triple, the
// world's id, the type word and the element's id, in three spellings, each with an optional moment and lens.
//
//   https://www.onlyworlds.com/w/<world id>/<type>/<id>     a link to give a person (the site's page)
//   ow://<type>/<id>                                        an element of the same world, in a text field
//   ow://<world id>/<type>/<id>                             an element of another world, in a text field
//
// A world alone is https://www.onlyworlds.com/w/<world id> or ow://<world id>. Any of them may end in @<step> (the
// moment, a 32-bit step of the world's time line) and ?lens=<id>, the moment first.
//
// Parse reads a string as the profile's reader does, refusing it with the first check that fails (Refusals, in the
// reader's order); Write writes the parts as a writer does, refusing in the order bad_form, segments, no_world, then the
// reader's. The packages' parseOwLink and elementAddress, held to the same conformance fixture
// (profiles/address.fixture.json).
//
// An address in a field is public text: never resolve, fetch or unfurl it on a server. It is not a foreign key, not an
// identity key and not a promise that its target exists. One string is read whole: a link inside other text is the
// caller's to find first.
//
// Strings are UTF-8. Nothing is percent-decoded; every part the grammar reads is ASCII.

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace OnlyWorlds {

// What Parse reads from one string. When ok is false, reason names the refusal and nothing else is set.
struct OWAddressReading {
    bool ok = false;
    std::string reason;  // a refusal (OWAddress::Refusals), when not ok
    std::string form;    // "human" or "text"
    // The world's id in lowercase, or its slug as written when world_is_slug; absent in the in-world text form.
    std::optional<std::string> world;
    bool world_is_slug = false;  // the human form named its world by slug: read, never canonical, never written back
    std::optional<std::string> type;  // one of the 22 type words; absent for a world's own address
    std::optional<std::string> id;    // the element's id in lowercase; absent for a world's own address
    std::optional<std::string> lens;  // the lens query key's id in lowercase
    std::optional<int32_t> moment;    // the step after @
    bool canonical = false;  // the string as received is exactly what a writer writes for this reading

    bool IsWorld() const { return ok && !id; }
};

// The parts a writer takes. Absent is std::nullopt; an empty string is present and refuses as that part would.
struct OWAddressParts {
    std::string form;  // "human" or "text"
    std::optional<std::string> world;
    std::optional<std::string> type;
    std::optional<std::string> id;
    std::optional<std::string> lens;
    std::optional<int32_t> moment;
};

// What Write gives: the address, or a refusal (reason, one of OWAddress::Refusals). WriteJson also fails, with reason
// empty and message saying why, for input that is not an address's shape (not an object, or a key it does not hold).
struct OWAddressWritten {
    bool ok = false;
    std::string reason;
    std::string message;
    std::string address;
};

namespace OWAddress {

extern const char* const Human;        // "human"
extern const char* const Text;         // "text"
extern const char* const HumanPrefix;  // "https://www.onlyworlds.com/w/", the only scheme and host a reader reads
extern const char* const TextPrefix;   // "ow://"
extern const char* const LensKey;      // "lens", the one query key the profile reads

// Every refusal, in the order a parser checks them; the last two (no_world, bad_form) are a writer's only.
extern const char* const Refusals[10];

// The 22 type words, lowercase and singular, as the site and the API spell them.
extern const char* const TypeWords[22];

// Read one string: trim Unicode White_Space at both ends, then the profile's nine checks in order.
OWAddressReading Parse(std::string_view text);

// The address a writer writes: lowercase throughout, the world's id (never its slug), the moment before the lens, and
// nothing else. A world's own address has no type and no id.
OWAddressWritten Write(const OWAddressParts& parts);

// A reading written back canonically. A reading by slug refuses as bad_world, a refused reading as bad_form.
OWAddressWritten Write(const OWAddressReading& reading);

// The parts as JSON, the shape the packages take and the fixture writes: form, world, type, id, lens, moment (and a
// reading's world_is_slug and canonical, which are ignored). Only a JSON null or a missing key is absent. A moment that
// is not a whole JSON number within the 32-bit range (a fraction, a string, a boolean) refuses as bad_moment; 3.0 is
// the step 3. A part that is not a string is present and refuses as that part would. Any other key is not an address's
// part: the call fails with reason empty, naming the key.
OWAddressWritten WriteJson(const OWJsonValue& address);

// A UUID in the hyphenated 8-4-4-4-12 form, any version, its hex in either case.
bool IsUuid(std::string_view s);

}  // namespace OWAddress
}  // namespace OnlyWorlds
