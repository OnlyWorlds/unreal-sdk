// OWPresentation, the generated sidecar, held to the vendored presentation.json it was generated from: the colour words
// in order, each type's colour word, icon and public fields, every family one of the words, and no row for a word that
// is not a type slug. A repin that moves the sidecar and is not regenerated goes red here (and in the generator's own
// --check).

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"
#include "OWPresentation.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWJsonValue Sidecar() {
    OWBytes bytes;
    OWFiles::Read(P().root + "/codegen/schema-dist/presentation.json", bytes);
    OWJsonParseResult r = OWJson::Parse(bytes);
    return r.ok ? std::move(r.value) : OWJsonValue();
}

std::vector<std::string> Words(const OWJsonValue* list) {
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& w : list->Items()) out.push_back(w.IsString() ? w.Text() : std::string("?"));
    return out;
}

}  // namespace

OW_TEST(presentation_is_the_vendored_sidecar) {
    OWJsonValue doc = Sidecar();
    const OWJsonValue* colors = doc.Find("colors");
    const OWJsonValue* types = doc.Find("types");
    t.Ok(colors && types, "presentation.json parses, with colors and types");
    if (!colors || !types) return;
    t.Ok(Words(colors->Find("order")) == OWPresentation::Order(), "the colour words are colors.order, in its order");
    t.Eq(OWPresentation::Order().size(), size_t(4), "four words");
    std::vector<std::string> slugs(types->Keys().begin(), types->Keys().end());
    t.Ok(slugs == OWPresentation::Types(), "a row per type, in the sidecar's order");
    t.Ok(OWPresentation::Types() == OWLens::TypeOrder(), "the sidecar's order is the schema's");
    for (size_t i = 0; i < types->Size(); ++i) {
        const std::string& type = types->Keys()[i];
        const OWJsonValue& row = types->Items()[i];
        OW_DEPRECATED_BEGIN
        t.Eq(OWPresentation::FamilyOf(type), row.Find("family")->Text(), type + "'s family");
        OW_DEPRECATED_END
        t.Eq(OWPresentation::IconOf(type), row.Find("icon")->Text(), type + "'s icon");
        t.Ok(OWPresentation::PublicOf(type) == Words(row.Find("public")), type + "'s public fields");
    }
}

OW_TEST(presentation_every_family_is_a_colour_word) {
    const std::vector<std::string>& order = OWPresentation::Order();
    for (const std::string& type : OWPresentation::Types()) {
        OW_DEPRECATED_BEGIN
        std::string family = OWPresentation::FamilyOf(type);
        OW_DEPRECATED_END
        t.Ok(std::find(order.begin(), order.end(), family) != order.end(),
             type + "'s family " + family + " is one of colors.order");
        t.Ok(!OWPresentation::IconOf(type).empty(), type + " has an icon");
    }
}

OW_TEST(presentation_a_word_not_a_slug_has_no_row) {
    for (const char* word : {"Character", " character", "people", ""}) {
        OW_DEPRECATED_BEGIN
        t.Eq(OWPresentation::FamilyOf(word), std::string(), std::string("no family for \"") + word + "\"");
        OW_DEPRECATED_END
        t.Eq(OWPresentation::IconOf(word), std::string(), std::string("no icon for \"") + word + "\"");
        t.Ok(OWPresentation::PublicOf(word).empty(), std::string("no public fields for \"") + word + "\"");
    }
    OW_DEPRECATED_BEGIN
    t.Eq(OWPresentation::FamilyOf("construct"), std::string("pink"), "a Construct is pink (serial 86)");
    t.Eq(OWPresentation::FamilyOf("ability"), std::string("amber"), "an Ability is amber (serial 86)");
    OW_DEPRECATED_END
}

// FamilyOf, deprecated since 0.7.0, answers as GroupOf does, for every slug and for a word that is not one. Unlike the
// TypeScript SDK's familyOf, which answers the words before its 4.102.0, it has always answered the colour words.
OW_TEST(presentation_family_of_answers_as_group_of) {
    std::vector<std::string> words = OWPresentation::Types();
    for (const char* word : {"Character", "people", ""}) words.push_back(word);
    for (const std::string& word : words) {
        OW_DEPRECATED_BEGIN
        t.Eq(OWPresentation::FamilyOf(word), OWPresentation::GroupOf(word), "\"" + word + "\"");
        OW_DEPRECATED_END
    }
}

// GroupOf, npm 5.0's groupOf: each type's colour group, held to the sidecar, "" for a word that is not a slug.
OW_TEST(presentation_group_of_is_each_types_colour_group) {
    OWJsonValue doc = Sidecar();
    const OWJsonValue* types = doc.Find("types");
    t.Ok(types != nullptr, "presentation.json has types");
    if (!types) return;
    const std::vector<std::string>& order = OWPresentation::Order();
    for (size_t i = 0; i < types->Size(); ++i) {
        const std::string& type = types->Keys()[i];
        std::string group = OWPresentation::GroupOf(type);
        t.Eq(group, types->Items()[i].Find("family")->Text(), type + "'s group");
        t.Ok(std::find(order.begin(), order.end(), group) != order.end(), type + "'s group " + group + " is one of colors.order");
    }
    for (const char* word : {"Character", " character", "people", ""})
        t.Eq(OWPresentation::GroupOf(word), std::string(), std::string("no group for \"") + word + "\"");
    t.Eq(OWPresentation::GroupOf("construct"), std::string("pink"), "a Construct is pink (serial 86)");
    t.Eq(OWPresentation::GroupOf("ability"), std::string("amber"), "an Ability is amber (serial 86)");
    t.Eq(OWPresentation::GroupOf("character"), std::string("blue"), "a Character is blue");
}
