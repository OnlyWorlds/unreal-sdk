#include "OWNames.h"

#include "OWAddress.h"
#include "OWMessage.h"

// The npm SDK's typeWord (src/names.ts) line for line: the map judged whole first, then the supertype's entry, then the
// type's. The Python package's type_word and the Unity SDK's OWNames read the same; the Godot SDK's ow_names.gd is the
// closest port in shape.

namespace OnlyWorlds {
namespace OWNames {

const char* const Field = "x_names";

namespace {

const OWJsonValue* Get(const OWJsonValue& v, std::string_view key) { return v.IsObject() ? v.Find(key) : nullptr; }

bool IsSlug(std::string_view type) {
    for (const char* t : OWAddress::TypeWords)
        if (type == t) return true;
    return false;
}

// The word a pair prints, or absent when the entry is ignored (no string `one` left after trimming).
std::optional<std::string> Word(const OWJsonValue* pair, bool many) {
    const OWJsonValue* one = pair ? Get(*pair, "one") : nullptr;
    if (!one || !one->IsString()) return std::nullopt;
    std::string_view word = OWMessage::Trim(one->Text());
    if (word.empty()) return std::nullopt;
    if (!many) return std::string(word);
    const OWJsonValue* plural = Get(*pair, "many");
    std::string_view words = plural && plural->IsString() ? OWMessage::Trim(plural->Text()) : std::string_view();
    return std::string(words.empty() ? word : words);
}

}  // namespace

std::optional<std::string> TypeWord(const OWJsonValue& world, std::string_view type,
                                    std::optional<std::string_view> supertype, bool many) {
    const OWJsonValue* map = Get(world, Field);
    if (!map || !map->IsObject()) return std::nullopt;
    // The number 1, as === 1 reads it: a number whose value is 1, in any spelling (1.0, 1e0); never true or "1".
    const OWJsonValue* version = map->Find("version");
    if (!version || !version->IsNumber() || version->AsDouble() != 1.0) return std::nullopt;
    // Present and not an object refuses the map, a null too: only an absent part is no part.
    const OWJsonValue* types = map->Find("types");
    if (types && !types->IsObject()) return std::nullopt;
    const OWJsonValue* supertypes = map->Find("supertypes");
    if (supertypes) {
        if (!supertypes->IsObject()) return std::nullopt;
        for (const OWJsonValue& inner : supertypes->Items())
            if (!inner.IsObject()) return std::nullopt;
    }
    if (!IsSlug(type)) return std::nullopt;
    const std::string wanted = supertype ? OWMessage::Fold(*supertype) : std::string();
    const OWJsonValue* entries = supertypes ? supertypes->Find(type) : nullptr;
    if (!wanted.empty() && entries) {
        for (size_t i = 0; i < entries->Size(); ++i) {
            if (OWMessage::Fold(entries->Keys()[i]) != wanted) continue;
            if (std::optional<std::string> w = Word(&entries->Items()[i], many)) return w;
        }
    }
    return types ? Word(types->Find(type), many) : std::nullopt;
}

}  // namespace OWNames
}  // namespace OnlyWorlds
