// OWViews.h: the views reader over the generated table (OWViews.generated.cpp). Ported from the Godot SDK's
// ow_views.gd and the Unity SDK's OWViews.cs, which read the same rules; the profiles' own readers decide a pair's
// match (OWLens::IsRoomElement, OWCharter::IsCharterElement) and a row's own banner mark is read by the room profile's
// one presence rule (OWLensView::HasMark), never a copy of either.

#include "OWViews.h"
#include "OWViewsIn.h"

#include "OWCharter.h"
#include "OWLens.h"
#include "OWLensView.h"
#include "OWMessage.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

OWJsonValue OWView::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("row", row ? OWJsonValue::String(row->id) : OWJsonValue::Null());
    out.Set("sentence", sentence ? OWJsonValue::String(*sentence) : OWJsonValue::Null());
    OWJsonValue b = OWJsonValue::Array();
    for (const std::string& m : banners) b.Push(OWJsonValue::String(m));
    out.Set("banners", std::move(b));
    OWJsonValue o = OWJsonValue::Array();
    for (const std::string& f : order) o.Push(OWJsonValue::String(f));
    out.Set("order", std::move(o));
    return out;
}

namespace OWViews {

namespace {

const OWJsonValue* Get(const OWJsonValue& el, std::string_view key) { return el.IsObject() ? el.Find(key) : nullptr; }

const std::string* StringAt(const OWJsonValue& el, std::string_view key) {
    const OWJsonValue* v = Get(el, key);
    return v && v->IsString() ? &v->Text() : nullptr;
}

// A string with something left after trimming White_Space; anything else holds no text.
bool HoldsText(const OWJsonValue* v) { return v && v->IsString() && !OWMessage::Trim(v->Text()).empty(); }

bool Has(const std::vector<std::string>& xs, const std::string& x) { return std::find(xs.begin(), xs.end(), x) != xs.end(); }

// How specifically a row matches: 3 a profile's pair with its mark, 2 a type with a supertype, 1 a type alone, 0 not
// at all. Types and supertypes trimmed and folded as the room profile folds them.
int Score(const OWViewRow& row, const OWJsonValue& el) {
    const std::string* type = StringAt(el, "type");
    if (!type) return 0;
    const std::string* supertype = StringAt(el, "supertype");
    const OWViewKind& kind = row.kind;
    if (OWMessage::Fold(*type) != OWMessage::Fold(kind.type)) return 0;
    if (!kind.profile.empty()) {
        if (!supertype || OWMessage::Fold(*supertype) != OWMessage::Fold(kind.supertype)) return 0;
        if (kind.profile == "room") return OWLens::IsRoomElement(el) ? 3 : 0;
        if (kind.profile == "charter") return OWCharter::IsCharterElement(el) ? 3 : 0;
        return 0;
    }
    if (kind.supertype.empty()) return 1;
    return supertype && OWMessage::Fold(*supertype) == OWMessage::Fold(kind.supertype) ? 2 : 0;
}

// Whether the element carries the banner `mark`, read as the file reads that mark.
bool Carries(const OWJsonValue& el, const std::string& mark) {
    const OWJsonValue* v = Get(el, mark);
    if (mark == "hidden" || mark == "known_of") return v && v->IsBool() && v->AsBool();
    if (mark == "x_served_trimmed") {
        // it names a field: a string with something left after trimming (a list of blanks names none)
        if (!v || !v->IsArray()) return false;
        for (const OWJsonValue& x : v->Items())
            if (HoldsText(&x)) return true;
        return false;
    }
    if (mark == "x_review_proposed" || mark == "x_review_checked") return v && v->IsObject() && v->Size() > 0;
    return OWLensView::HasMark(el, mark.c_str());
}

// The one sentence: the first field the row names whose value is a string holding something after trimming, cut at its
// first line break (U+000A or U+000D) and trimmed again.
std::optional<std::string> SentenceOf(const OWViewRow& row, const OWJsonValue& el) {
    for (const std::string& field : row.sentence) {
        const std::string* v = StringAt(el, field);
        if (!v) continue;
        std::string_view text = OWMessage::Trim(*v);
        if (text.empty()) continue;
        const size_t cut = text.find_first_of("\n\r");
        return std::string(OWMessage::Trim(cut == std::string_view::npos ? text : text.substr(0, cut)));
    }
    return std::nullopt;
}

// A field holds nothing when absent, null, a string with nothing after trimming, an empty list, or an object with no
// keys.
bool Holds(const OWJsonValue* v) {
    if (!v || v->IsNull()) return false;
    if (v->IsString()) return !OWMessage::Trim(v->Text()).empty();
    if (v->IsArray() || v->IsObject()) return v->Size() > 0;
    return true;
}

// The fields drawn, in order: lead, then the type's section fields in neither lead nor fold, then fold; each once. A
// generic link is one field at its place, read by its two keys (views.json version 3).
std::vector<std::string> DrawOrder(const OWViewRow* row, const OWJsonValue& el) {
    const std::string* type = StringAt(el, "type");
    static const std::vector<std::string> no_fields;
    static const std::vector<OWViewGenericLink> no_links;
    const std::vector<std::string>& sections = type ? SectionFields(*type) : no_fields;
    const std::vector<OWViewGenericLink>& generic = type ? GenericLinks(*type) : no_links;
    static const std::vector<std::string> none;
    const std::vector<std::string>& lead = row ? row->lead : none;
    std::vector<std::string> folds;
    if (row)
        for (const OWViewFold& f : row->fold) folds.push_back(f.field);
    std::vector<std::string> drawn;
    auto holds = [&](const std::string& field) {
        for (const OWViewGenericLink& g : generic)
            if (g.field == field) return HoldsText(Get(el, g.type_key)) && HoldsText(Get(el, g.id_key));
        return Holds(Get(el, field));
    };
    auto draw = [&](const std::string& field) {
        if (!Has(drawn, field) && holds(field)) drawn.push_back(field);
    };
    for (const std::string& f : lead) draw(f);
    for (const std::string& f : sections)
        if (!Has(lead, f) && !Has(folds, f)) draw(f);
    for (const std::string& f : folds) draw(f);
    return drawn;
}

template <typename Row>
const Row* Lookup(const std::vector<std::pair<std::string, Row>>& table, std::string_view type) {
    const std::string slug = OWMessage::Fold(type);
    for (const auto& [t, row] : table)
        if (t == slug) return &row;
    return nullptr;
}

}  // namespace

const std::vector<std::string>& SectionFields(std::string_view type) {
    static const std::vector<std::string> none;
    const std::vector<std::string>* row = Lookup(SectionTable(), type);
    return row ? *row : none;
}

const std::vector<OWViewGenericLink>& GenericLinks(std::string_view type) {
    static const std::vector<OWViewGenericLink> none;
    const std::vector<OWViewGenericLink>* row = Lookup(GenericTable(), type);
    return row ? *row : none;
}

OWView ViewIn(const std::vector<OWViewBanner>& banners, const std::vector<OWViewRow>& rows, const OWJsonValue& element) {
    OWView out;
    if (!element.IsObject()) return out;
    int best = 0;
    for (const OWViewRow& r : rows) {
        const int s = Score(r, element);
        if (s > best) {  // strictly greater: on one rung the first row keeps it
            best = s;
            out.row = &r;
        }
    }
    for (const OWViewBanner& b : banners)
        if (Carries(element, b.mark)) out.banners.push_back(b.mark);
    if (!out.row) {
        out.order = DrawOrder(nullptr, element);
        return out;
    }
    for (const std::string& m : out.row->banner)
        if (Carries(element, m)) out.banners.push_back(m);
    out.sentence = SentenceOf(*out.row, element);
    out.order = DrawOrder(out.row, element);
    return out;
}

OWView ViewOf(const OWJsonValue& element) { return ViewIn(Banners(), Rows(), element); }

}  // namespace OWViews
}  // namespace OnlyWorlds
