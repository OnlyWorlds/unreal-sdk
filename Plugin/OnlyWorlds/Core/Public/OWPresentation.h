#pragma once
// The presentation sidecar (schema-dist presentation.json): each type's default family, its colour word, and its icon,
// and the fields a known-of stub shows beside its base. A visual aid for drawing the 22 types, not part of the standard,
// and defaults a tool may override; colour itself stays the tool's, so no colour value is here. The family is a key for
// drawing, never a classification and never a label.
//
// Generated (OWPresentation.generated.cpp, written by codegen/generate_presentation.py from the vendored sidecar). A
// family word not in colors.order fails loudly, naming the word: the generator refuses it and writes nothing, and the
// generated table holds the rule as a static_assert, so no reader draws a default colour.

#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {
namespace OWPresentation {

// The colour words in their order (colors.order: blue, green, pink, amber): every family is one of these.
const std::vector<std::string>& Order();
// The 22 type slugs, in the sidecar's order (schema.json's).
const std::vector<std::string>& Types();
// A type's colour word, and its icon (a Material Symbols name); "" for a word that is not one of the 22 slugs.
std::string FamilyOf(std::string_view type);
std::string IconOf(std::string_view type);
// The fields a known-of stub of the type shows beside its base fields (types.<type>.public); empty for a type with none
// and for a word that is not one of the 22.
const std::vector<std::string>& PublicOf(std::string_view type);

}  // namespace OWPresentation
}  // namespace OnlyWorlds
