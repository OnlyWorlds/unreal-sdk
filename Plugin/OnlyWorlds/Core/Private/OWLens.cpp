#include "OWLens.h"

#include "OWLensView.h"
#include "OWMessage.h"
#include "OWPresentation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

// keel's lens reader (elements/lens.py) over a world snapshot, with the bodies keel serves each reader
// (elements/serialize.py, elements/visibility.py redact and known_of). Where a line mirrors one of the profile's rules
// or one of keel's readings, the comment says which. The Godot SDK's ow_lens.gd is the closest port in shape.

namespace OnlyWorlds {
namespace OWLens {

const char* const Type = "construct";
const char* const Supertype = "Lens";
const char* const TemplateSubtype = "Template";
const char* const Field = "x_lens";
const char* const UsesField = "x_lens_uses";
const char* const RoomMark = "x_room_of";
const char* const ReadingAidMark = "x_charter_of";
const char* const ProposedMark = "x_review_proposed";
const char* const DefaultUndated = "include";
const char* const DefaultShow = "full";
const char* const DefaultMail = "none";

namespace {

using K = OWFieldKind;

// Each type's own fields, schema.json's order (dist.103: Location's vehicle), held equal to it by tests/test_lens.cpp.
// Written by hand, not generated: a schema field the vendored schema.json gains lands here in the repin's commit.
struct TypeRow {
    const char* type;
    const OWLensField* fields;
    size_t count;
};
const OWLensField F_ability[] = {
    {"activation", K::Text, ""}, {"duration", K::Int, ""}, {"potency", K::Int, ""}, {"range", K::Int, ""},
    {"effects", K::Multi, "phenomenon"}, {"challenges", K::Text, ""}, {"talents", K::Multi, "trait"},
    {"requisites", K::Multi, "construct"}, {"prevalence", K::Text, ""}, {"tradition", K::Single, "construct"},
    {"source", K::Single, "phenomenon"}, {"locus", K::Single, "location"}, {"instruments", K::Multi, "object"},
    {"systems", K::Multi, "construct"},
};
const OWLensField F_collective[] = {
    {"composition", K::Text, ""}, {"count", K::Int, ""}, {"formation_date", K::Int, ""},
    {"operator", K::Single, "institution"}, {"equipment", K::Multi, "object"}, {"activity", K::Text, ""},
    {"disposition", K::Text, ""}, {"state", K::Text, ""}, {"abilities", K::Multi, "ability"},
    {"symbolism", K::Multi, "construct"}, {"species", K::Multi, "species"}, {"characters", K::Multi, "character"},
    {"creatures", K::Multi, "creature"}, {"phenomena", K::Multi, "phenomenon"},
};
const OWLensField F_character[] = {
    {"physicality", K::Text, ""}, {"mentality", K::Text, ""}, {"height", K::Int, ""}, {"weight", K::Int, ""},
    {"species", K::Multi, "species"}, {"traits", K::Multi, "trait"}, {"abilities", K::Multi, "ability"},
    {"background", K::Text, ""}, {"motivations", K::Text, ""}, {"birth_date", K::Int, ""},
    {"birthplace", K::Single, "location"}, {"languages", K::Multi, "language"}, {"reputation", K::Text, ""},
    {"location", K::Single, "location"}, {"objects", K::Multi, "object"}, {"institutions", K::Multi, "institution"},
    {"charisma", K::Int, ""}, {"coercion", K::Int, ""}, {"competence", K::Int, ""}, {"compassion", K::Int, ""},
    {"creativity", K::Int, ""}, {"courage", K::Int, ""}, {"family", K::Multi, "family"},
    {"friends", K::Multi, "character"}, {"rivals", K::Multi, "character"}, {"level", K::Int, ""},
    {"hit_points", K::Int, ""}, {"STR", K::Int, ""}, {"DEX", K::Int, ""}, {"CON", K::Int, ""}, {"INT", K::Int, ""},
    {"WIS", K::Int, ""}, {"CHA", K::Int, ""},
};
const OWLensField F_construct[] = {
    {"rationale", K::Text, ""}, {"history", K::Text, ""}, {"status", K::Text, ""}, {"reach", K::Text, ""},
    {"start_date", K::Int, ""}, {"end_date", K::Int, ""}, {"founder", K::Single, "character"},
    {"custodian", K::Single, "institution"}, {"characters", K::Multi, "character"}, {"objects", K::Multi, "object"},
    {"locations", K::Multi, "location"}, {"species", K::Multi, "species"}, {"creatures", K::Multi, "creature"},
    {"institutions", K::Multi, "institution"}, {"traits", K::Multi, "trait"}, {"collectives", K::Multi, "collective"},
    {"zones", K::Multi, "zone"}, {"abilities", K::Multi, "ability"}, {"phenomena", K::Multi, "phenomenon"},
    {"languages", K::Multi, "language"}, {"families", K::Multi, "family"}, {"relations", K::Multi, "relation"},
    {"titles", K::Multi, "title"}, {"constructs", K::Multi, "construct"}, {"events", K::Multi, "event"},
    {"narratives", K::Multi, "narrative"},
};
const OWLensField F_creature[] = {
    {"appearance", K::Text, ""}, {"weight", K::Int, ""}, {"height", K::Int, ""}, {"species", K::Multi, "species"},
    {"habits", K::Text, ""}, {"demeanor", K::Text, ""}, {"traits", K::Multi, "trait"},
    {"abilities", K::Multi, "ability"}, {"languages", K::Multi, "language"}, {"status", K::Text, ""},
    {"birth_date", K::Int, ""}, {"location", K::Single, "location"}, {"zone", K::Single, "zone"},
    {"challenge_rating", K::Int, ""}, {"hit_points", K::Int, ""}, {"armor_class", K::Int, ""}, {"speed", K::Int, ""},
    {"actions", K::Multi, "ability"},
};
const OWLensField F_event[] = {
    {"history", K::Text, ""}, {"challenges", K::Text, ""}, {"consequences", K::Text, ""}, {"start_date", K::Int, ""},
    {"end_date", K::Int, ""}, {"triggers", K::Multi, "event"}, {"characters", K::Multi, "character"},
    {"objects", K::Multi, "object"}, {"locations", K::Multi, "location"}, {"species", K::Multi, "species"},
    {"creatures", K::Multi, "creature"}, {"institutions", K::Multi, "institution"}, {"traits", K::Multi, "trait"},
    {"collectives", K::Multi, "collective"}, {"zones", K::Multi, "zone"}, {"abilities", K::Multi, "ability"},
    {"phenomena", K::Multi, "phenomenon"}, {"languages", K::Multi, "language"}, {"families", K::Multi, "family"},
    {"relations", K::Multi, "relation"}, {"titles", K::Multi, "title"}, {"constructs", K::Multi, "construct"},
};
const OWLensField F_family[] = {
    {"spirit", K::Text, ""}, {"history", K::Text, ""}, {"traditions", K::Multi, "construct"},
    {"traits", K::Multi, "trait"}, {"abilities", K::Multi, "ability"}, {"languages", K::Multi, "language"},
    {"ancestors", K::Multi, "character"}, {"reputation", K::Text, ""}, {"estates", K::Multi, "location"},
    {"governs", K::Multi, "institution"}, {"heirlooms", K::Multi, "object"}, {"creatures", K::Multi, "creature"},
};
const OWLensField F_institution[] = {
    {"doctrine", K::Text, ""}, {"founding_date", K::Int, ""}, {"parent_institution", K::Single, "institution"},
    {"zones", K::Multi, "zone"}, {"objects", K::Multi, "object"}, {"creatures", K::Multi, "creature"},
    {"status", K::Text, ""}, {"allies", K::Multi, "institution"}, {"adversaries", K::Multi, "institution"},
    {"constructs", K::Multi, "construct"},
};
const OWLensField F_language[] = {
    {"phonology", K::Text, ""}, {"grammar", K::Text, ""}, {"lexicon", K::Text, ""}, {"writing", K::Text, ""},
    {"classification", K::Single, "construct"}, {"status", K::Text, ""}, {"spread", K::Multi, "location"},
    {"dialects", K::Multi, "language"},
};
const OWLensField F_law[] = {
    {"declaration", K::Text, ""}, {"purpose", K::Text, ""}, {"date", K::Int, ""}, {"parent_law", K::Single, "law"},
    {"penalties", K::Multi, "construct"}, {"author", K::Single, "institution"}, {"locations", K::Multi, "location"},
    {"zones", K::Multi, "zone"}, {"prohibitions", K::Multi, "construct"}, {"adjudicators", K::Multi, "title"},
    {"enforcers", K::Multi, "title"},
};
const OWLensField F_location[] = {
    {"form", K::Text, ""}, {"function", K::Text, ""}, {"founding_date", K::Int, ""},
    {"parent_location", K::Single, "location"}, {"vehicle", K::Single, "object"},
    {"populations", K::Multi, "collective"}, {"political_climate", K::Text, ""}, {"primary_power", K::Single, "institution"},
    {"governing_title", K::Single, "title"}, {"secondary_powers", K::Multi, "institution"},
    {"zone", K::Single, "zone"}, {"rival", K::Single, "location"}, {"partner", K::Single, "location"},
    {"customs", K::Text, ""}, {"founders", K::Multi, "character"}, {"cults", K::Multi, "construct"},
    {"delicacies", K::Multi, "species"}, {"extraction_methods", K::Multi, "construct"},
    {"extraction_goods", K::Multi, "construct"}, {"industry_methods", K::Multi, "construct"},
    {"industry_goods", K::Multi, "construct"}, {"infrastructure", K::Text, ""},
    {"extraction_markets", K::Multi, "location"}, {"industry_markets", K::Multi, "location"},
    {"currencies", K::Multi, "construct"}, {"architecture", K::Text, ""}, {"buildings", K::Multi, "object"},
    {"building_methods", K::Multi, "construct"}, {"defensibility", K::Text, ""}, {"elevation", K::Int, ""},
    {"fighters", K::Multi, "construct"}, {"defensive_objects", K::Multi, "object"},
};
const OWLensField F_map[] = {
    {"background_color", K::Text, ""}, {"hierarchy", K::Int, ""}, {"width", K::Int, ""}, {"height", K::Int, ""},
    {"depth", K::Int, ""}, {"parent_map", K::Single, "map"}, {"location", K::Single, "location"},
};
const OWLensField F_marker[] = {
    {"map", K::Single, "map"}, {"zone", K::Single, "zone"}, {"x", K::Int, ""}, {"y", K::Int, ""}, {"z", K::Int, ""},
    {"order", K::Int, ""},
};
const OWLensField F_narrative[] = {
    {"story", K::Text, ""}, {"consequences", K::Text, ""}, {"start_date", K::Int, ""}, {"end_date", K::Int, ""},
    {"order", K::Int, ""}, {"parent_narrative", K::Single, "narrative"}, {"protagonist", K::Single, "character"},
    {"antagonist", K::Single, "character"}, {"narrator", K::Single, "character"},
    {"conservator", K::Single, "institution"}, {"events", K::Multi, "event"}, {"characters", K::Multi, "character"},
    {"objects", K::Multi, "object"}, {"locations", K::Multi, "location"}, {"species", K::Multi, "species"},
    {"creatures", K::Multi, "creature"}, {"institutions", K::Multi, "institution"}, {"traits", K::Multi, "trait"},
    {"collectives", K::Multi, "collective"}, {"zones", K::Multi, "zone"}, {"abilities", K::Multi, "ability"},
    {"phenomena", K::Multi, "phenomenon"}, {"languages", K::Multi, "language"}, {"families", K::Multi, "family"},
    {"relations", K::Multi, "relation"}, {"titles", K::Multi, "title"}, {"constructs", K::Multi, "construct"},
    {"laws", K::Multi, "law"},
};
const OWLensField F_object[] = {
    {"aesthetics", K::Text, ""}, {"weight", K::Int, ""}, {"amount", K::Int, ""},
    {"parent_object", K::Single, "object"}, {"materials", K::Multi, "construct"},
    {"technology", K::Multi, "construct"}, {"utility", K::Text, ""}, {"effects", K::Multi, "phenomenon"},
    {"abilities", K::Multi, "ability"}, {"consumes", K::Multi, "construct"}, {"origins", K::Text, ""},
    {"location", K::Single, "location"}, {"language", K::Single, "language"}, {"affinities", K::Multi, "trait"},
};
const OWLensField F_phenomenon[] = {
    {"expression", K::Text, ""}, {"effects", K::Text, ""}, {"duration", K::Int, ""},
    {"catalysts", K::Multi, "object"}, {"empowerments", K::Multi, "ability"}, {"mythology", K::Text, ""},
    {"system", K::Single, "phenomenon"}, {"triggers", K::Multi, "construct"}, {"wielders", K::Multi, "character"},
    {"environments", K::Multi, "location"},
};
const OWLensField F_pin[] = {
    {"map", K::Single, "map"}, {"element_type", K::GenericType, ""}, {"element_id", K::GenericId, "element_type"},
    {"x", K::Int, ""}, {"y", K::Int, ""}, {"z", K::Int, ""},
};
const OWLensField F_relation[] = {
    {"background", K::Text, ""}, {"start_date", K::Int, ""}, {"end_date", K::Int, ""}, {"intensity", K::Int, ""},
    {"actor", K::Single, "character"}, {"events", K::Multi, "event"}, {"characters", K::Multi, "character"},
    {"objects", K::Multi, "object"}, {"locations", K::Multi, "location"}, {"species", K::Multi, "species"},
    {"creatures", K::Multi, "creature"}, {"institutions", K::Multi, "institution"}, {"traits", K::Multi, "trait"},
    {"collectives", K::Multi, "collective"}, {"zones", K::Multi, "zone"}, {"abilities", K::Multi, "ability"},
    {"phenomena", K::Multi, "phenomenon"}, {"languages", K::Multi, "language"}, {"families", K::Multi, "family"},
    {"titles", K::Multi, "title"}, {"constructs", K::Multi, "construct"}, {"narratives", K::Multi, "narrative"},
};
const OWLensField F_species[] = {
    {"appearance", K::Text, ""}, {"life_span", K::Int, ""}, {"weight", K::Int, ""},
    {"nourishment", K::Multi, "species"}, {"reproduction", K::Multi, "construct"},
    {"adaptations", K::Multi, "ability"}, {"instincts", K::Text, ""}, {"sociality", K::Text, ""},
    {"temperament", K::Text, ""}, {"communication", K::Text, ""}, {"aggression", K::Int, ""},
    {"traits", K::Multi, "trait"}, {"role", K::Text, ""}, {"parent_species", K::Single, "species"},
    {"locations", K::Multi, "location"}, {"zones", K::Multi, "zone"}, {"affinities", K::Multi, "phenomenon"},
};
const OWLensField F_title[] = {
    {"authority", K::Text, ""}, {"eligibility", K::Text, ""}, {"grant_date", K::Int, ""}, {"revoke_date", K::Int, ""},
    {"issuer", K::Single, "institution"}, {"body", K::Single, "institution"}, {"superior_title", K::Single, "title"},
    {"holders", K::Multi, "character"}, {"symbols", K::Multi, "object"}, {"status", K::Text, ""},
    {"history", K::Text, ""}, {"characters", K::Multi, "character"}, {"institutions", K::Multi, "institution"},
    {"families", K::Multi, "family"}, {"zones", K::Multi, "zone"}, {"locations", K::Multi, "location"},
    {"objects", K::Multi, "object"}, {"constructs", K::Multi, "construct"}, {"laws", K::Multi, "law"},
    {"collectives", K::Multi, "collective"}, {"creatures", K::Multi, "creature"},
    {"phenomena", K::Multi, "phenomenon"}, {"species", K::Multi, "species"}, {"languages", K::Multi, "language"},
};
const OWLensField F_trait[] = {
    {"social_effects", K::Text, ""}, {"physical_effects", K::Text, ""}, {"functional_effects", K::Text, ""},
    {"personality_effects", K::Text, ""}, {"behaviour_effects", K::Text, ""}, {"charisma", K::Int, ""},
    {"coercion", K::Int, ""}, {"competence", K::Int, ""}, {"compassion", K::Int, ""}, {"creativity", K::Int, ""},
    {"courage", K::Int, ""}, {"significance", K::Text, ""}, {"anti_trait", K::Single, "trait"},
    {"empowered_abilities", K::Multi, "ability"},
};
const OWLensField F_zone[] = {
    {"role", K::Text, ""}, {"shape", K::Text, ""}, {"start_date", K::Int, ""}, {"end_date", K::Int, ""},
    {"phenomena", K::Multi, "phenomenon"}, {"linked_zones", K::Multi, "zone"}, {"context", K::Text, ""},
    {"populations", K::Multi, "collective"}, {"titles", K::Multi, "title"}, {"principles", K::Multi, "construct"},
};
const TypeRow Types[] = {
    {"ability", F_ability, std::size(F_ability)},
    {"collective", F_collective, std::size(F_collective)},
    {"character", F_character, std::size(F_character)},
    {"construct", F_construct, std::size(F_construct)},
    {"creature", F_creature, std::size(F_creature)},
    {"event", F_event, std::size(F_event)},
    {"family", F_family, std::size(F_family)},
    {"institution", F_institution, std::size(F_institution)},
    {"language", F_language, std::size(F_language)},
    {"law", F_law, std::size(F_law)},
    {"location", F_location, std::size(F_location)},
    {"map", F_map, std::size(F_map)},
    {"marker", F_marker, std::size(F_marker)},
    {"narrative", F_narrative, std::size(F_narrative)},
    {"object", F_object, std::size(F_object)},
    {"phenomenon", F_phenomenon, std::size(F_phenomenon)},
    {"pin", F_pin, std::size(F_pin)},
    {"relation", F_relation, std::size(F_relation)},
    {"species", F_species, std::size(F_species)},
    {"title", F_title, std::size(F_title)},
    {"trait", F_trait, std::size(F_trait)},
    {"zone", F_zone, std::size(F_zone)}
};

const std::vector<std::string> Anchors = {"characters", "objects",   "locations", "species",   "creatures", "institutions",
                                          "traits",     "collectives", "zones",   "abilities", "phenomena", "languages",
                                          "families",   "relations", "constructs", "events",   "narratives"};
const std::vector<std::string> Order = {"ability",  "collective", "character", "construct", "creature", "event",
                                        "family",   "institution", "language", "law",       "location", "map",
                                        "marker",   "narrative",  "object",    "phenomenon", "pin",     "relation",
                                        "species",  "title",      "trait",     "zone"};
const std::vector<std::pair<std::string, std::string>> Dates = {
    {"character", "birth_date"},      {"creature", "birth_date"}, {"institution", "founding_date"},
    {"location", "founding_date"},    {"collective", "formation_date"}, {"law", "date"},
    {"title", "grant_date"},          {"construct", "start_date"}, {"event", "start_date"},
    {"narrative", "start_date"},      {"relation", "start_date"},  {"zone", "start_date"}};
const std::vector<std::string> Rooms = {"narrative/message",  "narrative/board", "narrative/report",
                                        "relation/work item", "construct/spec",  "relation/heart",
                                        "relation/take",      "relation/vote",   "event/sitting",
                                        "character/member"};
const std::vector<std::string> Aids = {"law/charter", "construct/guide", "construct/domain"};
const std::vector<std::string> Reasons = {"budget", "types",  "window", "stale", "undated", "after_as_of",
                                          "anchor_not_visible", "not_visible", "hops_not_served"};
const std::vector<std::string> OwnerOnly = {"anchor_not_visible", "not_visible"};
// What a known-of stub keeps beside its type's public fields (keel's KNOWN_OF_BASE).
const std::vector<std::string> StubBase = {"type", "id", "name", "description", "supertype", "subtype",
                                           "image_url", "created_at", "updated_at", "change_seq"};
// The base text fields of every type (keel's field kinds, less name, which a stub keeps). `world` is none of keel's
// field kinds, so a stub drops it as it drops an extension key.
const std::vector<std::string> BaseText = {"description", "supertype", "subtype", "image_url"};
// The server's hands: a stub carries none (keel's _known_of_value).
const std::vector<std::string> Hands = {"created_by", "changed_by", "changed_via", "changed_client"};
const char* const ServedTrimmed = "x_served_trimmed";

const char* const Rungs[] = {"owner", "contributor", "guest", "public"};
const std::vector<std::string> UndatedWords = {"include", "exclude"};
const std::vector<std::string> ShowWords = {"full", "first_line", "name"};
const std::vector<std::string> MailWords = {"unread_full_read_titles", "all", "none"};

bool Has(const std::vector<std::string>& list, std::string_view s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

const TypeRow* RowOf(std::string_view type) {
    for (const TypeRow& r : Types)
        if (type == r.type) return &r;
    return nullptr;
}

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }

// Python's truthiness on a JSON value, as keel reads a body's fields.
bool Truthy(const OWJsonValue* v) {
    if (!v) return false;
    switch (v->kind()) {
        case OWJsonValue::Kind::Null: return false;
        case OWJsonValue::Kind::Bool: return v->AsBool();
        case OWJsonValue::Kind::Number: return v->AsDouble() != 0;
        case OWJsonValue::Kind::String: return !v->Text().empty();
        default: return v->Size() > 0;
    }
}

// An integer's text, -?(0|[1-9][0-9]*), inside 64 bits.
std::optional<int64_t> IntegerText(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    const std::string& s = v->Text();
    size_t i = s[0] == '-' ? 1 : 0;
    if (i >= s.size() || (s[i] == '0' && i + 1 < s.size())) return std::nullopt;
    uint64_t n = 0;
    for (size_t j = i; j < s.size(); ++j) {
        if (s[j] < '0' || s[j] > '9') return std::nullopt;
        uint64_t d = uint64_t(s[j] - '0');
        if (n > (UINT64_MAX - d) / 10) return std::nullopt;
        n = n * 10 + d;
    }
    bool negative = i == 1;
    if (n > (negative ? uint64_t(INT64_MAX) + 1 : uint64_t(INT64_MAX))) return std::nullopt;
    return negative ? int64_t(0 - n) : int64_t(n);
}

// A whole number, as a stored date and an x_lens key read one: an integer's text, or a number with no fraction inside
// 64 bits (7.0 as 7). A whole float reads whole in x_lens too (the profile's owner, 2026-10-30, retiring serial 83's
// refusal where a reader can see the text; keel refused it); past 64 bits is a platform limit.
std::optional<int64_t> Integral(const OWJsonValue* v) {
    if (std::optional<int64_t> n = IntegerText(v)) return n;
    if (!v || !v->IsNumber()) return std::nullopt;
    double d = v->AsDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d >= 9223372036854775808.0 || d < -9223372036854775808.0)
        return std::nullopt;
    return int64_t(d);
}

// A mark counts by presence: any value but null, a string only when something is left after trimming White_Space.
bool HasMark(const OWJsonValue& el, const char* field) {
    const OWJsonValue* v = Get(el, field);
    if (!v || v->IsNull()) return false;
    return !v->IsString() || !OWMessage::Trim(v->Text()).empty();
}

// The element's pair as a `types` word: type and supertype folded, a slash between; "" when either is not text.
std::string PairWord(const OWJsonValue& el) {
    const OWJsonValue* t = Get(el, "type");
    const OWJsonValue* s = Get(el, "supertype");
    if (!t || !t->IsString() || !s || !s->IsString()) return std::string();
    return OWMessage::Fold(t->Text()) + "/" + OWMessage::Fold(s->Text());
}

OWJsonValue Strings(const std::vector<std::string>& list) {
    OWJsonValue out = OWJsonValue::Array();
    for (const std::string& s : list) out.Push(OWJsonValue::String(s));
    return out;
}

OWJsonValue Counts(const std::vector<std::pair<std::string, int64_t>>& cut) {
    OWJsonValue out = OWJsonValue::Object();
    for (const auto& [reason, n] : cut) out.Set(reason, OWJsonValue::Number(n));
    return out;
}

// -- the recipe ------------------------------------------------------------------------------------------------------

struct Recipe {
    int64_t depth = DefaultDepth;
    std::set<std::string> types;  // each word folded
    std::optional<int64_t> window, from, to, changed_within;
    std::string undated = DefaultUndated;
    std::string show = DefaultShow;
    int64_t budget = DefaultBudget;
    std::string mail = DefaultMail;
    const OWJsonValue* notes = nullptr;  // an object of strings, or nullptr

    const OWJsonValue* NoteOf(const std::string& id) const { return notes ? notes->Find(id) : nullptr; }
};

bool IsTypeWord(const OWJsonValue& word) {
    if (!word.IsString()) return false;
    std::string w = OWMessage::Fold(word.Text());
    return Has(Order, w) || Has(Rooms, w) || Has(Aids, w);
}

// keel's check_keys and recipe: the first key refused, in the profile's order, else the recipe with its defaults.
std::optional<std::string> ReadRecipe(const OWJsonValue* raw, Recipe* out) {
    if (!raw || raw->IsNull()) return std::nullopt;
    if (!raw->IsObject()) return std::string(Field);
    // null reads as absent.
    auto at = [&](const char* key) -> const OWJsonValue* {
        const OWJsonValue* v = raw->Find(key);
        return v && !v->IsNull() ? v : nullptr;
    };
    auto word = [](const OWJsonValue* v, const std::vector<std::string>& words) {
        return v->IsString() && Has(words, v->Text());
    };
    Recipe r;
    if (const OWJsonValue* v = at("depth")) {
        std::optional<int64_t> n = Integral(v);
        if (!n || *n < 0 || *n > 2) return std::string("depth");
        r.depth = *n;
    }
    if (const OWJsonValue* v = at("types")) {
        if (!v->IsArray()) return std::string("types");
        for (const OWJsonValue& w : v->Items()) {
            if (!IsTypeWord(w)) return std::string("types");
            r.types.insert(OWMessage::Fold(w.Text()));
        }
    }
    if (const OWJsonValue* v = at("window")) {
        r.window = Integral(v);
        if (!r.window || *r.window < 0) return std::string("window");
        if (at("from") || at("to")) return std::string("window");
    }
    if (const OWJsonValue* v = at("from")) {
        r.from = Integral(v);
        if (!r.from) return std::string("from");
    }
    if (const OWJsonValue* v = at("to")) {
        r.to = Integral(v);
        if (!r.to || !r.from || !(*r.from < *r.to)) return std::string("to");
    }
    if (const OWJsonValue* v = at("undated")) {
        if (!word(v, UndatedWords)) return std::string("undated");
        r.undated = v->Text();
    }
    if (const OWJsonValue* v = at("changed_within")) {
        r.changed_within = Integral(v);
        if (!r.changed_within || *r.changed_within < 1) return std::string("changed_within");
    }
    if (const OWJsonValue* v = at("show")) {
        if (!word(v, ShowWords)) return std::string("show");
        r.show = v->Text();
    }
    if (const OWJsonValue* v = at("budget")) {
        std::optional<int64_t> n = Integral(v);
        if (!n || *n < 1 || *n > MaxBudget) return std::string("budget");
        r.budget = *n;
    }
    if (const OWJsonValue* v = at("mail")) {
        if (!word(v, MailWords)) return std::string("mail");
        r.mail = v->Text();
    }
    if (const OWJsonValue* v = at("notes")) {
        if (!v->IsObject()) return std::string("notes");
        for (const OWJsonValue& s : v->Items())
            if (!s.IsString()) return std::string("notes");
        r.notes = v;
    }
    if (out) *out = std::move(r);
    return std::nullopt;
}

// -- time -----------------------------------------------------------------------------------------------------------

using Time = OWMessage::OWTimeKey;

Time Before(Time t, int64_t days) {
    t.seconds -= days * 86400;
    return t;
}

Time MachineNow() {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    Time t;
    t.seconds = us / 1000000;
    t.micros = us % 1000000;
    if (t.micros < 0) {
        t.micros += 1000000;
        --t.seconds;
    }
    return t;
}

// An element's clock, the one rule 5's order and changed_within read: the newest of its updated_at, server_updated_at
// and local_updated_at that read as a time (a folder element carries the last two), decided element by element; a
// value that cannot be read is ignored; absent when none can be read.
std::optional<Time> ChangedAt(const OWJsonValue& el) {
    std::optional<Time> at;
    for (const char* key : {"updated_at", "server_updated_at", "local_updated_at"}) {
        std::optional<Time> k = OWMessage::TimeKey(Get(el, key));
        if (k && (!at || *at < *k)) at = k;
    }
    return at;
}

// -- one reader's view of the world ------------------------------------------------------------------------------------

// An element as keel keys it: its type and its id. An anchor the reader cannot see keeps its field's target type.
using Key = std::pair<std::string, std::string>;

struct View {
    std::vector<const OWJsonValue*> els;
    std::vector<std::string> ids, types;
    std::unordered_map<std::string, size_t> by_id;
    std::vector<char> room, proposed, hidden, view, stub, scoped;
    std::vector<std::string> aid;  // the reading aid's pair word, or ""
    std::vector<std::vector<Key>> links;               // the stored body's links, each typed by its field
    std::vector<std::vector<size_t>> in_view, in_full;  // who links to the element: in the reader's view, in the world
    bool owner_counts = false;
    std::string seen_rung;
    std::optional<std::string> character;
    const OWJsonValue* members = nullptr;
    std::optional<int64_t> as_of;
    Time wall;
    std::map<size_t, OWJsonValue> served;

    View(const OWJsonValue& world, const OWJsonValue& reader, std::optional<int64_t> p_as_of, Time p_wall)
        : as_of(p_as_of), wall(p_wall) {
        std::set<std::string> listed_hidden;
        if (const OWJsonValue* h = world.Find("hidden"); h && h->IsArray())
            for (const OWJsonValue& id : h->Items())
                if (id.IsString()) listed_hidden.insert(id.Text());
        for (const OWJsonValue& el : world.Find("elements")->Items()) {
            const OWJsonValue* id = Get(el, "id");
            const OWJsonValue* type = Get(el, "type");
            if (!id || !id->IsString() || id->Text().empty() || by_id.count(id->Text())) continue;
            if (!type || !type->IsString()) continue;
            std::string slug = OWMessage::Fold(type->Text());
            if (!Has(Order, slug)) continue;
            by_id[id->Text()] = els.size();
            els.push_back(&el);
            ids.push_back(id->Text());
            types.push_back(slug);
            hidden.push_back(listed_hidden.count(id->Text()) || IsHidden(el));
            room.push_back(IsRoomElement(el));
            aid.push_back(IsReadingAid(el) ? PairWord(el) : std::string());
            proposed.push_back(IsProposed(el));
        }
        owner_counts = Str(reader.Find("rung")) == "owner";
        const OWJsonValue* as = reader.Find("as");
        const OWJsonValue& seen = owner_counts && as && as->IsObject() ? *as : reader;
        seen_rung = Str(seen.Find("rung"));
        if (const OWJsonValue* c = seen.Find("character"); c && c->IsString() && !c->Text().empty()) character = c->Text();
        std::set<std::string> whole, known_of;
        for (auto [key, into] : {std::pair<const char*, std::set<std::string>*>{"whole", &whole}, {"known_of", &known_of}})
            if (const OWJsonValue* list = seen.Find(key); list && list->IsArray())
                for (const OWJsonValue& id : list->Items())
                    if (id.IsString()) into->insert(id.Text());
        size_t n = els.size();
        view.assign(n, 0);
        stub.assign(n, 0);
        scoped.assign(n, 0);
        for (size_t i = 0; i < n; ++i) {
            bool sees = false;
            if (seen_rung == "owner") sees = true;
            else if (seen_rung == "contributor") sees = !hidden[i];
            else if (seen_rung == "public") sees = !hidden[i] && !room[i];
            else {
                // A guest: exactly the ids it holds whole and those it knows of (a stub). Hidden beats known_of: a hidden
                // element the guest did not make is not in its view (no stub, no row, no count, no mark); a hidden id
                // in whole is its own creation, served whole.
                bool told = known_of.count(ids[i]) && !hidden[i];
                sees = whole.count(ids[i]) || told;
                stub[i] = !whole.count(ids[i]) && told;
            }
            if (proposed[i]) {
                // A proposed element is in no reader's view; keel's scoped still serves the link to one below the
                // public rung (a body's links and a lens's notes keep it).
                scoped[i] = sees && seen_rung != "public";
            } else if (sees) {
                view[i] = scoped[i] = 1;
            }
        }
        if (const OWJsonValue* m = world.Find("members"); m && m->IsArray()) members = m;
        links.resize(n);
        in_view.resize(n);
        in_full.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const OWJsonValue& el = *els[i];
            for (const OWLensField& f : FieldsOf(types[i])) {
                const OWJsonValue* v = el.Find(f.name);
                if (f.kind == K::Single) {
                    if (v && v->IsString() && !v->Text().empty()) links[i].emplace_back(f.target, v->Text());
                } else if (f.kind == K::Multi) {
                    if (v && v->IsArray())
                        for (const OWJsonValue& t : v->Items())
                            if (t.IsString() && !t.Text().empty()) links[i].emplace_back(f.target, t.Text());
                } else if (f.kind == K::GenericId) {
                    // A generic pair links only when its type key names one of the 22 (keel's `et in _TYPES`).
                    std::string et = Str(el.Find(f.target));
                    if (v && v->IsString() && !v->Text().empty() && Has(Order, et)) links[i].emplace_back(et, v->Text());
                }
            }
            for (const Key& k : links[i]) {
                std::optional<size_t> j = Find(k);
                if (!j) continue;
                if (std::find(in_full[*j].begin(), in_full[*j].end(), i) == in_full[*j].end()) {
                    in_full[*j].push_back(i);
                    if (view[i] && !stub[i]) in_view[*j].push_back(i);
                }
            }
        }
    }

    // The element a typed link names: one of that id, of that type.
    std::optional<size_t> Find(const Key& k) const {
        auto it = by_id.find(k.second);
        if (it == by_id.end() || types[it->second] != k.first) return std::nullopt;
        return it->second;
    }
    Key KeyOf(size_t i) const { return {types[i], ids[i]}; }
    // The element an anchor's key names, keyed as the cut counts it: resolution reads the field's type and the id, but
    // the cut counts each element once under its first reason (the profile's line 51), so an id met under two types
    // in an umbrella and its child is one element, and one kept under its own type is never also cut. Its own key when
    // the id names an element of the world; a dangling id by the id alone.
    Key Element(const Key& k) const {
        auto it = by_id.find(k.second);
        return it == by_id.end() ? Key{std::string(), k.second} : KeyOf(it->second);
    }
    bool Whole(size_t i) const { return view[i] && !stub[i]; }

    // The fence (rule 8) on the element's own stored date, a stub's too, and undated.
    const char* Fence(size_t i, const Recipe& r) const {
        bool dated = false;
        for (const auto& [t, f] : Dates)
            if (t == types[i]) dated = true;
        if (!dated) return nullptr;
        std::optional<int64_t> d = DateOf(*els[i]);
        if (!d) return r.undated == "exclude" ? "undated" : nullptr;
        return as_of && *d > *as_of ? "after_as_of" : nullptr;
    }
    bool AfterAsOf(size_t i, const Recipe& r) const {
        const char* why = Fence(i, r);
        return why && std::string_view(why) == "after_as_of";
    }
    // window, or from and to (closed-open); a to after as_of is the fence's.
    const char* Window(size_t i, const Recipe& r) const {
        std::optional<int64_t> d = DateOf(*els[i]);
        bool dated = false;
        for (const auto& [t, f] : Dates)
            if (t == types[i]) dated = true;
        if (!dated || !d) return nullptr;
        if (r.window && as_of && *d < *as_of - *r.window) return "window";
        if (r.from && *d < *r.from) return "window";
        if (r.to && *d >= *r.to) return "window";
        return nullptr;
    }
    // changed_within, against the reading's wall clock; no cut when the element has no readable time.
    const char* Stale(size_t i, const Recipe& r) const {
        if (!r.changed_within) return nullptr;
        std::optional<Time> at = ChangedAt(*els[i]);
        if (!at) return nullptr;
        return *at < Before(wall, *r.changed_within) ? "stale" : nullptr;
    }
    std::string Pair(size_t i) const { return types[i] + "/" + OWMessage::Fold(Str(Get(*els[i], "supertype"))); }
    // keel's _admitted: a room element or a reading aid only when `types` names its pair; any other by its type.
    bool Admitted(size_t i, const Recipe& r) const {
        if (room[i] || !aid[i].empty()) return r.types.count(Pair(i)) > 0;
        return r.types.empty() || r.types.count(types[i]) > 0;
    }
    // keel's _stops (rule 4): nothing hops on out of a room element, nor out of a reading aid whose pair is not named.
    bool Stops(size_t i, const Recipe& r) const { return room[i] || (!aid[i].empty() && !r.types.count(Pair(i))); }

    // Rule 5 past the anchors: the schema's type order, newest first by the element's clock (ChangedAt: the newest of
    // its readable updated_at, server_updated_at and local_updated_at, the clock changed_within reads), then by id. One
    // clock per lens. An element with no readable clock comes last of its type (the profile's owner, 2026-10-30, as
    // the npm and Python packages read it; keel read it as now, the newest).
    std::vector<size_t> Ordered(std::vector<size_t> list) const {
        auto rank = [&](size_t i) { return size_t(std::find(Order.begin(), Order.end(), types[i]) - Order.begin()); };
        auto when = [&](size_t i) { return ChangedAt(*els[i]); };
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) {
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            std::optional<Time> ta = when(a), tb = when(b);
            if (ta.has_value() != tb.has_value()) return ta.has_value();
            if (ta && !(*ta == *tb)) return *tb < *ta;
            return ids[a] < ids[b];
        });
        return list;
    }

    const OWJsonValue& Served(size_t i);
};

// keel's to_known_of on one served body, in place: the stub's keeps stay, every other field reads unset, every other
// key (an extension) goes, and known_of: true.
void ToKnownOf(OWJsonValue& out, const std::string& type) {
    std::vector<OWLensField> fields = FieldsOf(type);
    const std::vector<std::string>& pub = PublicFieldsOf(type);
    OWJsonValue stub = OWJsonValue::Object();
    for (size_t k = 0; k < out.Size(); ++k) {
        const std::string& key = out.Keys()[k];
        if (Has(StubBase, key) || Has(pub, key)) {
            stub.Set(key, out.Items()[k]);
            continue;
        }
        if (Has(BaseText, key)) {
            stub.Set(key, OWJsonValue::String(""));
            continue;
        }
        if (Has(Hands, key)) {
            stub.Set(key, OWJsonValue::Null());
            continue;
        }
        for (const OWLensField& f : fields) {
            if (key != f.name) continue;
            switch (f.kind) {
                case K::Text: case K::GenericType: stub.Set(key, OWJsonValue::String("")); break;
                case K::Multi: stub.Set(key, OWJsonValue::Array()); break;
                default: stub.Set(key, OWJsonValue::Null()); break;
            }
        }
    }
    stub.Set("known_of", OWJsonValue::Bool(true));
    out = std::move(stub);
}

std::set<std::string> AnchorIds(const OWJsonValue& body) {
    std::set<std::string> out;
    for (const std::string& f : Anchors)
        if (const OWJsonValue* v = body.Find(f); v && v->IsArray())
            for (const OWJsonValue& id : v->Items())
                if (id.IsString()) out.insert(id.Text());
    return out;
}

// The body this reader is served (keel's serialize): the owner reading as itself, the body as stored; any other reader,
// each link it cannot see left out (redact), a lens's notes on what it cannot see dropped, the trimmed mark naming a
// Message's addressing or a lens's anchors that lost an id (never on the public rung), and a known-of element as its
// stub. A stored x_served_trimmed is never served. For every reader, `hidden: true` on what the hidden mark keeps (the
// world's list or the element's own key, one mark), and no `hidden` key on any other body, a stored false included
// (keel #537): below the owner only a guest's own creation carries it, a body it sees whole.
const OWJsonValue& View::Served(size_t i) {
    auto hit = served.find(i);
    if (hit != served.end()) return hit->second;
    OWJsonValue out = *els[i];
    out.Erase(ServedTrimmed);
    auto marked = [&](OWJsonValue&& body) -> const OWJsonValue& {
        if (hidden[i]) body.Set("hidden", OWJsonValue::Bool(true));
        else body.Erase("hidden");
        return served.emplace(i, std::move(body)).first->second;
    };
    if (seen_rung == "owner") return marked(std::move(out));
    const std::string& type = types[i];
    if (seen_rung == "public") out.Erase("doors");  // keel's members_only
    // The addressing before the trim: a Message's characters and narrator, a lens's anchor fields (keel's
    // _message_addressing; never for the public rung).
    std::vector<std::pair<std::string, OWJsonValue>> addressing;
    bool lens = IsLens(*els[i]);
    if (seen_rung != "public") {
        std::vector<std::string> fields;
        if (OWMessage::IsMessage(*els[i])) fields = {"characters", "narrator"};
        else if (lens) fields = Anchors;
        for (const std::string& f : fields)
            if (const OWJsonValue* v = out.Find(f)) addressing.emplace_back(f, *v);
    }
    const OWJsonValue* x = out.Find(Field);
    bool has_notes = lens && x && x->IsObject() && x->Find("notes") && x->Find("notes")->IsObject();
    std::set<std::string> before = has_notes ? AnchorIds(out) : std::set<std::string>();
    // redact: a link to an id the reader is not served as the field's type is left out; a generic pair whose target
    // it is not served reads as an empty pair (type "", id null).
    auto ok = [&](const std::string& target, const OWJsonValue& v) {
        if (!v.IsString()) return false;
        std::optional<size_t> j = Find({target, v.Text()});
        return j && scoped[*j];
    };
    for (const OWLensField& f : FieldsOf(type)) {
        if (f.kind == K::Single) {
            OWJsonValue* v = out.Find(f.name);
            if (v && Truthy(v) && !ok(f.target, *v)) *v = OWJsonValue::Null();
        } else if (f.kind == K::Multi) {
            OWJsonValue* v = out.Find(f.name);
            if (!v || !Truthy(v)) continue;
            OWJsonValue kept = OWJsonValue::Array();
            if (v->IsArray())
                for (const OWJsonValue& t : v->Items())
                    if (ok(f.target, t)) kept.Push(t);
            *v = std::move(kept);
        } else if (f.kind == K::GenericId) {
            OWJsonValue* id = out.Find(f.name);
            OWJsonValue* et = out.Find(f.target);
            if ((!id && !et) || !Truthy(id)) continue;
            std::string t = et && et->IsString() ? et->Text() : std::string();
            if (!(Has(Order, t) && ok(t, *id))) {
                if (et) *et = OWJsonValue::String("");
                if (id) *id = OWJsonValue::Null();
            }
        }
    }
    std::vector<std::string> trimmed;
    for (const auto& [f, was] : addressing)
        if (const OWJsonValue* now = out.Find(f); !now || *now != was) trimmed.push_back(f);
    // keel's _trim_lens_notes: the notes on a served anchor and on any other id this reader is served stay; an
    // anchor's note goes with the anchor, and names `notes` in the mark.
    if (has_notes) {
        std::set<std::string> now = AnchorIds(out);
        const OWJsonValue& notes = *out.Find(Field)->Find("notes");
        OWJsonValue kept = OWJsonValue::Object();
        bool gone = false;
        for (size_t k = 0; k < notes.Size(); ++k) {
            const std::string& id = notes.Keys()[k];
            bool lost = before.count(id) && !now.count(id);
            auto j = by_id.find(id);
            bool seen = !lost && !now.count(id) && j != by_id.end() && scoped[j->second];
            if (now.count(id) || seen) kept.Set(id, notes.Items()[k]);
            if (lost) gone = true;
        }
        if (kept.Size() != notes.Size()) {
            OWJsonValue xl = *out.Find(Field);
            xl.Set("notes", std::move(kept));
            out.Set(Field, std::move(xl));
            if (gone && !addressing.empty()) trimmed.push_back("notes");
        }
    }
    if (stub[i]) {
        ToKnownOf(out, type);
        trimmed.clear();
    }
    std::vector<std::string> mark;
    for (const std::string& f : trimmed)
        if (out.Has(f) || (f == "notes" && out.Has(Field))) mark.push_back(f);
    if (!mark.empty()) out.Set(ServedTrimmed, Strings(mark));
    return marked(std::move(out));
}

// -- resolution --------------------------------------------------------------------------------------------------------

// One lens read by its own recipe: its kept list and each cut element's first reason (keel's _kept and _cut), the mail
// part when it names a mail rule, and its anchors as keel's _anchors finds them.
struct Part {
    std::vector<Key> kept;
    std::map<Key, std::string> cut;
    std::optional<OWLensMail> mail;
    std::vector<Key> anchors, unseen;
};

struct Reader {
    View& v;

    // keel's _anchors: in the lens's link order, an anchor the reader sees; a child (a lens in `constructs` the
    // reader sees whole); an anchor or child the reader cannot see, keyed by its field's target type.
    void AnchorsOf(const OWJsonValue& lens, Part& p, std::vector<size_t>& anchors, std::vector<size_t>& children) {
        std::set<std::string> met;
        std::vector<OWLensField> fields = FieldsOf(Type);
        for (const std::string& f : Anchors) {
            const OWJsonValue* list = lens.Find(f);
            if (!list || !list->IsArray()) continue;
            std::string target;
            for (const OWLensField& lf : fields)
                if (f == lf.name) target = lf.target;
            for (const OWJsonValue& id : list->Items()) {
                if (!IsId(id) || !met.insert(id.Text()).second) continue;
                Key key{target, id.Text()};
                std::optional<size_t> i = v.Find(key);
                if (f == "constructs" && i && v.scoped[*i] && IsLens(*v.els[*i])) {
                    if (v.Whole(*i)) children.push_back(*i);
                    else p.unseen.push_back(key);
                } else if (i && v.view[*i]) {
                    anchors.push_back(*i);
                    p.anchors.push_back(key);
                } else {
                    p.unseen.push_back(key);
                }
            }
        }
    }

    // An anchor list's entry that is an id: a string with something in it besides White_Space (rule 7).
    static bool IsId(const OWJsonValue& id) { return id.IsString() && !OWMessage::Trim(id.Text()).empty(); }

    // A lens that names an anchor is never a filter: an anchor field with an id in it, or the trimmed mark naming one
    // (keel's _names_anchors). An empty string, or one of White_Space alone, is no id, so a lens whose anchor fields
    // hold only such strings names no anchor and is a filter, as rule 7 reads a lens with empty lists.
    static bool NamesAnchors(const OWJsonValue& lens) {
        for (const std::string& f : Anchors)
            if (const OWJsonValue* list = lens.Find(f); list && list->IsArray())
                for (const OWJsonValue& id : list->Items())
                    if (IsId(id)) return true;
        if (const OWJsonValue* mark = lens.Find(ServedTrimmed); mark && mark->IsArray())
            for (const OWJsonValue& f : mark->Items())
                if (f.IsString() && Has(Anchors, f.Text())) return true;
        return false;
    }

    // One hop from `frontier`, both ways, through what the reader is served; a Relation reached is an edge to its own
    // links in the same hop. Adds to `notvis` (the owner's count) what the reader's view stops.
    std::vector<size_t> Hop(const std::vector<size_t>& frontier, std::set<std::string>& known, std::set<Key>& notvis) {
        std::vector<size_t> ring;
        auto meet = [&](size_t j) {
            if (v.view[j] && known.insert(v.ids[j]).second) ring.push_back(j);
        };
        auto not_seen = [&](size_t j) {
            if (!v.view[j]) notvis.insert(v.KeyOf(j));
        };
        for (size_t f : frontier)
            for (const Key& k : v.links[f])
                if (std::optional<size_t> j = v.Find(k)) meet(*j);
        for (size_t f : frontier)
            for (size_t j : v.in_view[f]) meet(j);
        if (v.owner_counts)
            for (size_t f : frontier) {
                for (const Key& k : v.links[f])
                    if (std::optional<size_t> j = v.Find(k)) not_seen(*j);
                for (size_t j : v.in_full[f]) not_seen(j);
            }
        std::vector<size_t> landed = ring;
        for (size_t r : landed) {
            if (v.types[r] != "relation" || !v.Whole(r) || v.room[r]) continue;
            for (const Key& k : v.links[r])
                if (std::optional<size_t> j = v.Find(k)) {
                    meet(*j);
                    if (v.owner_counts) not_seen(*j);
                }
        }
        return ring;
    }

    // Rule 7: a lens with no anchors keeps, in rule 5's order, every element of its `types` the reader can see (a stub
    // too); the rest are not selected and not counted.
    std::vector<Key> Filter(const Recipe& r, const std::set<std::string>& visited, std::map<Key, std::string>& cut) {
        std::vector<size_t> pool;
        for (size_t i = 0; i < v.els.size(); ++i)
            if (v.view[i] && !visited.count(v.ids[i]) && v.Admitted(i, r)) pool.push_back(i);
        std::vector<Key> kept;
        for (size_t i : v.Ordered(pool)) {
            const char* why = v.Fence(i, r);
            if (!why) why = v.Window(i, r);
            if (!why) why = v.Stale(i, r);
            if (why) cut.emplace(v.KeyOf(i), why);
            else kept.push_back(v.KeyOf(i));
        }
        return kept;
    }

    // keel's resolve_lens below its top: the lens's anchors, its hop or filter, its children each once, then its mail
    // rule and its budget over the union.
    Part One(const OWJsonValue& lens, const Recipe& r, std::set<std::string>& visited) {
        Part p;
        std::vector<size_t> anchors, children;
        AnchorsOf(lens, p, anchors, children);
        if (v.owner_counts)
            for (const Key& k : p.unseen) p.cut.emplace(v.Element(k), "anchor_not_visible");
        if (!NamesAnchors(lens)) {
            p.kept = Filter(r, visited, p.cut);
        } else {
            for (size_t a : anchors) {
                if (const char* why = v.Fence(a, r)) p.cut.emplace(v.KeyOf(a), why);
                else p.kept.push_back(v.KeyOf(a));
            }
            std::set<std::string> known = visited;
            for (const Key& k : p.anchors) known.insert(k.second);
            for (const Key& k : p.unseen) known.insert(k.second);
            // Rule 4: a room anchor is never followed; nothing is hopped out of what the fence cuts after as_of.
            std::vector<size_t> frontier;
            for (size_t a : anchors)
                if (v.Whole(a) && !v.room[a] && !v.AfterAsOf(a, r)) frontier.push_back(a);
            std::vector<size_t> reached;
            std::set<Key> notvis, past;
            for (int64_t depth = 1; depth <= r.depth; ++depth) {
                std::vector<size_t> ring = Hop(frontier, known, depth <= 1 ? notvis : past);
                if (depth > 1) {
                    // What a hop past depth 1 reaches is counted, less what rule 4 would stop.
                    for (size_t k : ring)
                        if (!(v.room[k] || !v.aid[k].empty()) || v.Admitted(k, r))
                            p.cut.emplace(v.KeyOf(k), "hops_not_served");
                } else {
                    reached.insert(reached.end(), ring.begin(), ring.end());
                }
                frontier.clear();
                for (size_t k : ring)
                    if (v.Whole(k) && !v.Stops(k, r) && !v.AfterAsOf(k, r)) frontier.push_back(k);
            }
            for (size_t k : v.Ordered(reached)) {
                const char* why = v.Fence(k, r);
                if (!why && !v.Admitted(k, r)) why = "types";
                if (!why) why = v.Window(k, r);
                if (!why) why = v.Stale(k, r);
                if (why) p.cut.emplace(v.KeyOf(k), why);
                else p.kept.push_back(v.KeyOf(k));
            }
            std::set<std::string> anchor_ids;
            for (const Key& k : p.anchors) anchor_ids.insert(k.second);
            for (const Key& k : notvis)
                if (!anchor_ids.count(k.second) && !visited.count(k.second)) p.cut.emplace(k, "not_visible");
        }
        // The umbrella (rule 3): each child once, by its own recipe, the union in link order, an element dropped only
        // by its id.
        for (size_t child : children) {
            if (!visited.insert(v.ids[child]).second) continue;
            const OWJsonValue& body = *v.els[child];
            Recipe cr;
            if (ReadRecipe(body.Find(Field), &cr) || IsTemplate(body)) continue;
            Part sub = One(body, cr, visited);
            std::set<std::string> have;
            for (const Key& k : p.kept) have.insert(k.second);
            for (const Key& k : sub.kept)
                if (!have.count(k.second)) p.kept.push_back(k);
            for (const auto& [k, why] : sub.cut) p.cut.emplace(k, why);
        }
        if (r.mail != "none") {
            p.mail = Mail(r);
            std::set<std::string> in_mail(p.mail->unread.begin(), p.mail->unread.end());
            in_mail.insert(p.mail->read.begin(), p.mail->read.end());
            std::vector<Key> rest;
            for (const Key& k : p.kept)
                if (!in_mail.count(k.second)) rest.push_back(k);
            p.kept = std::move(rest);
        }
        if (int64_t(p.kept.size()) > r.budget) {
            for (size_t i = size_t(r.budget); i < p.kept.size(); ++i) p.cut[p.kept[i]] = "budget";
            p.kept.resize(size_t(r.budget));
        }
        for (const Key& k : p.kept) p.cut.erase(k);
        return p;
    }

    // Rule 9, through the message profile's readers over the reader's view: the messages addressed to its Character
    // and not its own, thirty days on created_at before the clock, the newest kept up to the budget.
    OWLensMail Mail(const Recipe& r) {
        OWLensMail part;
        if (!v.character) return part;
        // Whole bodies only, as served: a known-of stub serves no characters, so its recipients are unknown, never
        // everyone. In the order of the world's list, never by id: two messages with one created_at keep it, as the
        // message profile rules (the lens profile's owner, ahead of its next serial).
        std::vector<const OWJsonValue*> messages, relations;
        for (size_t i = 0; i < v.els.size(); ++i) {
            if (!v.Whole(i)) continue;
            if (OWMessage::IsMessage(*v.els[i])) messages.push_back(&v.Served(i));
            if (v.types[i] == "relation") relations.push_back(&v.Served(i));
        }
        const std::string& me = *v.character;
        const OWJsonValue* mark = OWMessage::ReadMarkOf(relations, me, v.members);
        std::vector<const OWJsonValue*> mine = OWMessage::UnreadFor(messages, me, nullptr, v.members);
        std::set<const OWJsonValue*> unread;
        for (const OWJsonValue* m : OWMessage::UnreadFor(messages, me, mark, v.members)) unread.insert(m);
        Time lower = Before(v.wall, MailDays);
        std::vector<const OWJsonValue*> recent;
        int64_t window = 0;
        for (const OWJsonValue* m : mine) {
            std::optional<Time> at = OWMessage::TimeKey(m->Find("created_at"));
            if (!at || *at < lower) ++window;
            else recent.push_back(m);
        }
        int64_t over = int64_t(recent.size()) - r.budget;
        if (over > 0) recent.erase(recent.begin(), recent.begin() + over);
        if (window > 0) part.cut.emplace_back("window", window);
        if (over > 0) part.cut.emplace_back("budget", over);
        for (const OWJsonValue* m : recent) {
            bool is_unread = unread.count(m) > 0;
            (is_unread ? part.unread : part.read).push_back(Str(m->Find("id")));
            if (r.mail == "all" || is_unread) {
                part.rows.push_back(*m);
            } else {
                OWJsonValue title = OWJsonValue::Object();
                for (const char* k : {"id", "name", "narrator", "created_at"}) {
                    const OWJsonValue* f = m->Find(k);
                    title.Set(k, f ? *f : OWJsonValue::Null());
                }
                part.rows.push_back(std::move(title));
            }
        }
        part.total_unread = int64_t(unread.size());
        part.total_read = int64_t(mine.size()) - part.total_unread;
        return part;
    }

    // One kept row as `show` makes it; a note captions an anchor of the lens read only.
    OWJsonValue Row(size_t i, const Recipe& r, const std::set<std::string>& anchor_ids) {
        const OWJsonValue& body = v.Served(i);
        OWJsonValue row;
        if (r.show == "full") {
            row = body;
        } else {
            row = OWJsonValue::Object();
            for (const char* k : {"id", "type", "name"}) {
                const OWJsonValue* f = body.Find(k);
                row.Set(k, f ? *f : OWJsonValue::Null());
            }
            // Serial 114: supertype and subtype where the served body holds one as a string with something left after
            // trimming White_Space, carried as stored (never trimmed); anything else gives no key.
            for (const char* k : {"supertype", "subtype"}) {
                const OWJsonValue* f = body.Find(k);
                if (f && f->IsString() && !OWMessage::Trim(f->Text()).empty()) row.Set(k, *f);
            }
            // Serial 115: the first line, the description split at every CR and every LF (no other break; a UTF-8 byte
            // 0x0A or 0x0D is only ever that character), that holds something after trimming White_Space, carried as
            // stored; no such line, or a description that is not a string, gives no key.
            if (const OWJsonValue* d = body.Find("description"); r.show == "first_line" && d && d->IsString()) {
                const std::string& text = d->Text();
                for (std::size_t start = 0; start <= text.size();) {
                    const std::size_t end = std::min(text.find_first_of("\r\n", start), text.size());
                    const std::string line = text.substr(start, end - start);
                    if (!OWMessage::Trim(line).empty()) {
                        row.Set("description", OWJsonValue::String(line));
                        break;
                    }
                    start = end + 1;
                }
            }
        }
        if (const OWJsonValue* note = r.NoteOf(v.ids[i]); note && anchor_ids.count(v.ids[i])) row.Set("note", *note);
        return row;
    }
};

bool IsRung(const OWJsonValue* rung) {
    for (const char* r : Rungs)
        if (rung && rung->IsString() && rung->Text() == r) return true;
    return false;
}

std::string CheckReader(const OWJsonValue& reader, const std::string& where) {
    if (!reader.IsObject() || !IsRung(reader.Find("rung")))
        return where + ": a reader is an object whose rung is owner, contributor, guest or public";
    if (const OWJsonValue* as = reader.Find("as"); as && !as->IsNull()) return CheckReader(*as, where + ".as");
    return std::string();
}

// The checks a reading makes before it reads (the world, the reader, the clock, the lens's kind; `lens` nullptr for
// none), then the reading's date and wall clock. "" when the call reads, else the mistake, prefixed by `where`.
std::string Prepare(const OWJsonValue& world, const OWJsonValue& reader, const OWJsonValue* lens,
                    const OWLensOptions& options, const std::string& where, std::optional<int64_t>& as_of, Time& wall) {
    const OWJsonValue* elements = world.IsObject() ? world.Find("elements") : nullptr;
    if (!elements || !elements->IsArray()) return where + ": world is an object with an elements list";
    if (std::string bad = CheckReader(reader, where + ": reader"); !bad.empty()) return bad;
    std::optional<Time> clock;
    if (options.clock) {
        OWJsonValue text = OWJsonValue::String(*options.clock);
        clock = OWMessage::TimeKey(&text);
        if (!clock) return where + ": clock is an RFC 3339 date-time; got " + *options.clock;
    }
    if (lens && !lens->IsString() && !lens->IsObject()) return where + ": lens is a lens body or its id";
    as_of = options.as_of;
    if (!as_of)
        for (const char* key : {"now", "time_current", "time_range_current"})
            if ((as_of = Integral(world.Find(key)))) break;
    wall = clock ? *clock : MachineNow();
    return std::string();
}

// One lens read over one reader's view: Resolve below its checks. With `cut_by`, each cut element's first reason, by
// its key, before the counts drop the owner's two for another reader.
OWLensReading ReadLens(View& v, const OWJsonValue& lens, std::map<Key, std::string>* cut_by) {
    OWLensReading out;
    // A lens named by id is read from the body this reader is served whole: a stub serves no links and no recipe.
    const OWJsonValue* body = &lens;
    if (lens.IsString()) {
        auto it = v.by_id.find(lens.Text());
        body = it != v.by_id.end() && v.Whole(it->second) ? v.els[it->second] : nullptr;
    }
    if (!body || !IsLens(*body)) return out;
    Recipe r;
    if ((out.unreadable = ReadRecipe(body->Find(Field), &r))) return out;
    if (IsTemplate(*body)) return out;
    std::set<std::string> visited = {Str(body->Find("id"))};
    Reader reading{v};
    Part p = reading.One(*body, r, visited);
    // The counts are the reader's: the owner's two only for the owner (reading as itself or as another).
    std::map<std::string, int64_t> counts;
    for (const auto& [k, why] : p.cut)
        if (v.owner_counts || !Has(OwnerOnly, why)) ++counts[why];
    for (const std::string& why : Reasons)
        if (counts[why] > 0) out.cut.emplace_back(why, counts[why]);
    if (cut_by) *cut_by = p.cut;
    std::set<std::string> anchor_ids, noted;
    for (const Key& k : p.anchors) anchor_ids.insert(k.second);
    noted = anchor_ids;
    for (const Key& k : p.unseen) noted.insert(k.second);
    // A note on an id that is not an anchor is counted only where the reader sees that id.
    if (r.notes)
        for (const std::string& id : r.notes->Keys()) {
            auto it = v.by_id.find(id);
            if (!noted.count(id) && it != v.by_id.end() && v.view[it->second]) ++out.notes_ignored;
        }
    for (const Key& k : p.kept) {
        size_t i = *v.Find(k);
        out.kept.push_back(k.second);
        out.rows.push_back(reading.Row(i, r, anchor_ids));
    }
    out.mail = std::move(p.mail);
    return out;
}

}  // namespace

const std::vector<std::string>& AnchorFields() { return Anchors; }
const std::vector<std::string>& TypeOrder() { return Order; }
const std::vector<std::pair<std::string, std::string>>& DateTable() { return Dates; }
const std::vector<std::string>& RoomPairs() { return Rooms; }
const std::vector<std::string>& ReadingAidPairs() { return Aids; }
const std::vector<std::string>& CutReasons() { return Reasons; }
const std::vector<std::string>& OwnerOnlyReasons() { return OwnerOnly; }

std::vector<OWLensField> FieldsOf(std::string_view type) {
    const TypeRow* row = RowOf(type);
    return row ? std::vector<OWLensField>(row->fields, row->fields + row->count) : std::vector<OWLensField>();
}

const std::vector<std::string>& PublicFieldsOf(std::string_view type) { return OWPresentation::PublicOf(type); }

bool IsLens(const OWJsonValue& element) {
    const OWJsonValue* t = Get(element, "type");
    const OWJsonValue* s = Get(element, "supertype");
    return t && t->IsString() && s && s->IsString() && OWMessage::Fold(t->Text()) == Type &&
           OWMessage::Fold(s->Text()) == OWMessage::Fold(Supertype);
}

bool IsTemplate(const OWJsonValue& element) {
    const OWJsonValue* s = Get(element, "subtype");
    return IsLens(element) && s && s->IsString() && OWMessage::Fold(s->Text()) == OWMessage::Fold(TemplateSubtype);
}

std::optional<std::string> CheckKeys(const OWJsonValue* x_lens) { return ReadRecipe(x_lens, nullptr); }

std::vector<std::string> LensesInUse(const OWJsonValue* membership, const OWJsonValue* x_lens_uses) {
    const OWJsonValue* list = membership && membership->IsArray() ? membership : x_lens_uses;
    // Each string once, in the order given, "" dropped (the profile's owner, 2026-10-30, as the npm and Python packages
    // read it; keel kept the list as written).
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& id : list->Items())
            if (id.IsString() && !id.Text().empty() && !Has(out, id.Text())) out.push_back(id.Text());
    return out;
}

std::optional<int64_t> DateOf(const OWJsonValue& element) {
    const OWJsonValue* t = Get(element, "type");
    if (!t || !t->IsString()) return std::nullopt;
    std::string type = OWMessage::Fold(t->Text());
    for (const auto& [slug, field] : Dates)
        if (slug == type) return Integral(element.Find(field));
    return std::nullopt;
}

bool IsRoomElement(const OWJsonValue& element) {
    return HasMark(element, RoomMark) && Has(Rooms, PairWord(element));
}

bool IsReadingAid(const OWJsonValue& element) {
    return HasMark(element, ReadingAidMark) && Has(Aids, PairWord(element));
}

bool IsHidden(const OWJsonValue& element) {
    const OWJsonValue* mark = element.IsObject() ? element.Find("hidden") : nullptr;
    return mark && !mark->IsNull() && !(mark->IsBool() && !mark->AsBool());
}

bool IsProposed(const OWJsonValue& element) {
    if (!HasMark(element, ProposedMark)) return false;
    const OWJsonValue* v = element.Find(ProposedMark);
    return !((v->IsArray() || v->IsObject()) && v->Size() == 0);
}

OWLensReading Resolve(const OWJsonValue& world, const OWJsonValue& lens, const OWJsonValue& reader,
                      const OWLensOptions& options) {
    std::optional<int64_t> as_of;
    Time wall;
    if (std::string bad = Prepare(world, reader, &lens, options, "resolve", as_of, wall); !bad.empty()) {
        OWLensReading out;
        out.error = bad;
        return out;
    }
    View v(world, reader, as_of, wall);
    return ReadLens(v, lens, nullptr);
}

}  // namespace OWLens

namespace OWLensView {

std::string CheckReader(const OWJsonValue& reader, const std::string& where) { return OWLens::CheckReader(reader, where); }

std::string See(const OWJsonValue& world, const OWJsonValue& reader, const OWJsonValue* lens,
                const OWLensOptions& options, const std::string& where, Sight& out) {
    std::optional<int64_t> as_of;
    OWLens::Time wall;
    if (std::string bad = OWLens::Prepare(world, reader, lens, options, where, as_of, wall); !bad.empty()) return bad;
    // The view outlives the call behind `served`, which makes each body once.
    auto held = std::make_shared<OWLens::View>(world, reader, as_of, wall);
    OWLens::View& v = *held;
    out.served = [held](size_t i) -> const OWJsonValue& { return held->Served(i); };
    out.els = v.els;
    out.ids = v.ids;
    out.types = v.types;
    out.by_id = v.by_id;
    out.room = v.room;
    out.hidden = v.hidden;
    out.proposed = v.proposed;
    out.view = v.view;
    out.scoped = v.scoped;
    out.stub = v.stub;
    out.aid.clear();
    for (const std::string& pair : v.aid) out.aid.push_back(!pair.empty());
    out.owner_counts = v.owner_counts;
    out.seen_rung = v.seen_rung;
    if (lens) {
        OWLensReading reading = OWLens::ReadLens(v, *lens, &out.cut_by);
        out.kept = std::move(reading.kept);
        out.cut = std::move(reading.cut);
        out.unreadable = std::move(reading.unreadable);
    }
    return std::string();
}

}  // namespace OWLensView

int64_t OWLensReading::Cut(std::string_view reason) const {
    for (const auto& [why, n] : cut)
        if (why == reason) return n;
    return 0;
}

OWJsonValue OWLensReading::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("kept", OWLens::Strings(kept));
    OWJsonValue r = OWJsonValue::Array();
    for (const OWJsonValue& row : rows) r.Push(row);
    out.Set("rows", std::move(r));
    out.Set("cut", OWLens::Counts(cut));
    out.Set("notes_ignored", OWJsonValue::Number(notes_ignored));
    if (mail) {
        OWJsonValue m = OWJsonValue::Object();
        m.Set("unread", OWLens::Strings(mail->unread));
        m.Set("read", OWLens::Strings(mail->read));
        OWJsonValue rows_ = OWJsonValue::Array();
        for (const OWJsonValue& row : mail->rows) rows_.Push(row);
        m.Set("rows", std::move(rows_));
        m.Set("cut", OWLens::Counts(mail->cut));
        OWJsonValue totals = OWJsonValue::Object();
        totals.Set("unread", OWJsonValue::Number(mail->total_unread));
        totals.Set("read", OWJsonValue::Number(mail->total_read));
        m.Set("totals", std::move(totals));
        out.Set("mail", std::move(m));
    } else {
        out.Set("mail", OWJsonValue::Null());
    }
    out.Set("unreadable", unreadable ? OWJsonValue::String(*unreadable) : OWJsonValue::Null());
    return out;
}

}  // namespace OnlyWorlds
