#pragma once
// The names profile (schema-dist profiles/names.md, serial 92): a world's own words for the 22 types, x_names on the
// World. A display lookup only: the API, the folder, the schema and every id keep the standard's names, and a folder's
// elements/<type>/ directories are never translated.
//
// TypeWord is the npm SDK's typeWord and the Python package's type_word. The fixture beside the profile
// (profiles/names.fixture.json) is this file's test, as it is theirs: every reader refuses and ignores the same things,
// or one tool shows Employee where another shows Character. Trimming and folding are the room profile's
// (OWMessage::Trim, OWMessage::Fold).

#include "OWJson.h"

#include <optional>
#include <string>
#include <string_view>

namespace OnlyWorlds {
namespace OWNames {

// The World's field that holds the names map.
extern const char* const Field;  // "x_names"

// The world's own word for a type, or absent for the standard's name (print your usual label). `world` is a
// GET /world body or a folder's world.json; its x_names is read. `type` is a type slug, lowercase as the schema spells
// it; `supertype` the element's, when there is one; `many` true for a plural (a list's heading, a count).
//
// The order: the supertype's entry, its key matched trimmed and without regard to case; then the type's entry; then
// absent. An entry with no string `one` left after trimming is ignored, and the lookup goes on. For a plural, a `many`
// that is absent, not a string or blank prints `one`. Both are trimmed. A value that is not a names map is absent
// everywhere: not an object, `version` not the number 1 (true included; 1.0 and 1e0 are the number 1, as JavaScript's
// === 1 reads them), `types` or `supertypes` present and not objects (null included), or a `supertypes` value that is
// not an object. A bad entry or a key that is not one of the 22 slugs is ignored. The word is printed as written,
// never re-cased. Pure; never throws.
std::optional<std::string> TypeWord(const OWJsonValue& world, std::string_view type,
                                    std::optional<std::string_view> supertype = std::nullopt, bool many = false);

}  // namespace OWNames
}  // namespace OnlyWorlds
