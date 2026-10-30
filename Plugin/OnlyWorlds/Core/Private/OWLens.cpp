#include "OWLens.h"

#include "OWMessage.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>

// keel's lens reader (elements/lens.py): the pair, the recipe's keys, where a seat's choice lives and the date table.
// Where a line mirrors one of the profile's rules or one of keel's readings, the comment says which. The Godot SDK's
// ow_lens.gd is the closest port in shape.

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

// Each type's own fields, schema.json's order (dist.91), held equal to it by tests/test_lens.cpp.
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
    {"parent_location", K::Single, "location"}, {"populations", K::Multi, "collective"},
    {"political_climate", K::Text, ""}, {"primary_power", K::Single, "institution"},
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
const std::vector<std::string> Rooms = {"narrative/message", "narrative/board", "relation/work item",
                                        "construct/spec",    "relation/heart",  "relation/take",
                                        "relation/vote",     "event/sitting",   "character/member"};
const std::vector<std::string> Aids = {"law/charter", "construct/guide", "construct/domain"};
const std::vector<std::string> Reasons = {"budget", "types",  "window", "stale", "undated", "after_as_of",
                                          "anchor_not_visible", "not_visible", "hops_not_served"};
const std::vector<std::string> OwnerOnly = {"anchor_not_visible", "not_visible"};
// presentation.json's types.<type>.public (dist.91): only the Character has public fields.
const std::vector<std::string> CharacterPublic = {"physicality", "height", "weight", "species", "reputation"};
const std::vector<std::string> NoFields;
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

// A whole number as keel reads one in x_lens (`isinstance(v, int)`): an integer's text, -?(0|[1-9][0-9]*), inside 64
// bits. An integral float (7.0, 1e1) is a float to Python's json and refused; past 64 bits is a platform limit.
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

// A whole number as a stored date reads: an integer's text, or a number with no fraction inside 64 bits (7.0 as 7).
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
        std::optional<int64_t> n = IntegerText(v);
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
        r.window = IntegerText(v);
        if (!r.window || *r.window < 0) return std::string("window");
        if (at("from") || at("to")) return std::string("window");
    }
    if (const OWJsonValue* v = at("from")) {
        r.from = IntegerText(v);
        if (!r.from) return std::string("from");
    }
    if (const OWJsonValue* v = at("to")) {
        r.to = IntegerText(v);
        if (!r.to || !r.from || !(*r.from < *r.to)) return std::string("to");
    }
    if (const OWJsonValue* v = at("undated")) {
        if (!word(v, UndatedWords)) return std::string("undated");
        r.undated = v->Text();
    }
    if (const OWJsonValue* v = at("changed_within")) {
        r.changed_within = IntegerText(v);
        if (!r.changed_within || *r.changed_within < 1) return std::string("changed_within");
    }
    if (const OWJsonValue* v = at("show")) {
        if (!word(v, ShowWords)) return std::string("show");
        r.show = v->Text();
    }
    if (const OWJsonValue* v = at("budget")) {
        std::optional<int64_t> n = IntegerText(v);
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

const std::vector<std::string>& PublicFieldsOf(std::string_view type) {
    return type == "character" ? CharacterPublic : NoFields;
}

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
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& id : list->Items())
            if (id.IsString()) out.push_back(id.Text());
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

bool IsProposed(const OWJsonValue& element) {
    if (!HasMark(element, ProposedMark)) return false;
    const OWJsonValue* v = element.Find(ProposedMark);
    return !((v->IsArray() || v->IsObject()) && v->Size() == 0);
}

}  // namespace OWLens
}  // namespace OnlyWorlds
