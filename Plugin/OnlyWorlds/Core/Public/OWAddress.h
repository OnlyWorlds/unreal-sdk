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
// identity key and not a promise that its target exists. Parse reads one string whole; ReadAt reads an address found
// inside other text (a sentence, a line of a report) from the index where its prefix starts, and says where it ends.
//
// Strings are UTF-8, and an index into one is a byte offset. Nothing is percent-decoded; every part the grammar reads
// is ASCII.

#include "OWApi.h"
#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// What ReadAt finds: the address read, and the index just past it in the text (a UTF-8 byte offset).
struct OWAddressFound {
    OWAddressReading reading;  // what Parse reads from text.substr(index, end - index)
    size_t end = 0;            // the text's first byte that is not part of the address, or its size
    size_t start = 0;          // the address's first byte: the index ReadAt was given, where FindAll found it
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

extern OW_API const char* const Human;        // "human"
extern OW_API const char* const Text;         // "text"
extern OW_API const char* const HumanPrefix;  // "https://www.onlyworlds.com/w/", the only scheme and host a reader reads
extern OW_API const char* const TextPrefix;   // "ow://"
extern OW_API const char* const LensKey;      // "lens", the one query key the profile reads

// Every refusal, in the order a parser checks them; the last two (no_world, bad_form) are a writer's only.
extern OW_API const char* const Refusals[10];

// The 22 type words, lowercase and singular, as the site and the API spell them.
extern OW_API const char* const TypeWords[22];

// Read one string: trim Unicode White_Space at both ends, then the profile's nine checks in order.
OW_API OWAddressReading Parse(std::string_view text);

// The address that starts exactly at `index` in a text, found in prose, and where it ends; nothing when the text there
// is not an address (the address page's "An address in prose", serial 123; the TypeScript SDK's readOwLinkAt and the
// Python package's read_ow_link_at).
//
// Both forms, the prefix as written (lowercase). In the site's form the run after /w/ is read whole, once: it ends at
// the first character outside the slug's alphabet (lowercase letters, digits, hyphens) or the id's (hex in either
// case); exactly an id's form is the id, any other run Parse reads as a slug is the slug, else there is no address. A
// run followed by a letter of any script, a combining mark or an underscore is cut inside a word and is no address
// (/w/mopPetopia., /w/hyper_ion.); the character after it is decoded whole from its UTF-8.
//
// After the world, or in the text form from the prefix, the path is the longest one Parse reads. After it the address
// continues only with @ and a moment (its whole run of an optional minus and digits, taken only when it reads as a
// step) and then ?lens= and a lens (an id, which ends where its 36 characters end). Any other query key or a fragment
// is not part of an address found in prose: the address ends before its ?, & or #. What follows is prose, a full stop
// included. Parse itself still reads a string whole: "ow://object/<id>." handed to it is refused.
//
// An address begins at the start of the text or after a character that does not glue it to a word: after a letter of
// any script, a combining mark or an underscore the start is inside a word and this answers nothing (elbow://...); a
// hyphen, a digit, white space or punctuation does not glue (x-ow://, 3ow://, (ow://). The same set read from both
// ends; the character before is decoded whole (schema-dist serial 127).
//
// `index` and `end` are byte offsets into the UTF-8 text (not UTF-16 units as in JavaScript, nor code points as in
// Python); an index past the end, or inside a character's bytes, reads nothing.
OW_API std::optional<OWAddressFound> ReadAt(std::string_view text, size_t index = 0);

// Every address in a text, in order: what ReadAt reads at each place either prefix (ow://,
// https://www.onlyworlds.com/w/) starts, so a start glued to a word is prose and what follows an address is prose as
// ReadAt says (the address page's find-all, schema-dist serial 128; the TypeScript SDK's findOwLinks and the Python
// package's find_ow_links). The search goes on from the end of each address found, so no two overlap and a prefix
// inside an address found is no start, and from just past a start that reads nothing. Linear in the text: each
// prefix's next place is searched once, and a start reads a bounded stretch. Each one's start and end are byte offsets
// into the UTF-8 text, as ReadAt's are.
OW_API std::vector<OWAddressFound> FindAll(std::string_view text);

// The address a writer writes: lowercase throughout, the world's id (never its slug), the moment before the lens, and
// nothing else. A world's own address has no type and no id.
OW_API OWAddressWritten Write(const OWAddressParts& parts);

// A reading written back canonically. A reading by slug refuses as bad_world, a refused reading as bad_form.
OW_API OWAddressWritten Write(const OWAddressReading& reading);

// The parts as JSON, the shape the packages take and the fixture writes: form, world, type, id, lens, moment (and a
// reading's world_is_slug and canonical, which are ignored). Only a JSON null or a missing key is absent. A moment that
// is not a whole JSON number within the 32-bit range (a fraction, a string, a boolean) refuses as bad_moment; 3.0 is
// the step 3. A part that is not a string is present and refuses as that part would. Any other key is not an address's
// part: the call fails with reason empty, naming the key.
OW_API OWAddressWritten WriteJson(const OWJsonValue& address);

// A UUID in the hyphenated 8-4-4-4-12 form, any version, its hex in either case.
OW_API bool IsUuid(std::string_view s);

}  // namespace OWAddress
}  // namespace OnlyWorlds
