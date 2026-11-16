#include "OWCharter.h"

#include "OWLens.h"
#include "OWLensView.h"
#include "OWMessage.h"

#include <algorithm>
#include <set>

// keel's reading aids (elements/charter.py: read, packet, first_line, _hubs; accounts/front_door.py: charter_skill)
// over a world snapshot, read through the lens reader's own view (OWLensView::See): who sees what, and the body each
// reader is served. Where a line mirrors one of the profile's rules or one of keel's readings, the comment says which.
// The Godot SDK's ow_charter.gd is the closest port in shape.

namespace OnlyWorlds {

namespace OWCharter {
const char* const Mark = "x_charter_of";
const char* const RulesField = "x_checks_rules";
}  // namespace OWCharter

namespace {

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

std::string Text(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }

// The packet's type for an aid: its pair's own word as the page's table writes it (law, construct), whatever spelling
// the element's type is stored in (serial 134).
std::string PairType(std::string_view aid) {
    for (const OWCharter::OWCharterPair& p : OWCharter::Pairs())
        if (aid == p.aid) return p.type;
    return std::string();
}

// A served entry's type: as served when it is a string, else the aid's fixed type (a server from before serial 134).
std::string TypeOr(const OWJsonValue& entry, const char* fixed) {
    const OWJsonValue* t = Get(entry, "type");
    return t && t->IsString() ? t->Text() : std::string(fixed);
}

// The aid's word for element i ("" for none): the pair and the mark, read from the body as the owner is served it,
// since a stub serves no mark.
std::string AidOf(const OWLensView::Sight& s, size_t i) {
    if (!s.aid[i]) return std::string();
    std::optional<std::string> word = OWCharter::ReadingAidOf(*s.els[i]);
    return word ? *word : std::string();
}

// The elements the reader sees whose word is `word`, in the world's order. A proposed element is in no reader's view.
std::vector<size_t> Marked(const OWLensView::Sight& s, std::string_view word) {
    std::vector<size_t> out;
    for (size_t i = 0; i < s.els.size(); ++i)
        if (s.view[i] && AidOf(s, i) == word) out.push_back(i);
    return out;
}

// The newest of `list` by created_at as an instant (none, or one that does not read as a time, sorts as the oldest),
// then the higher id by code point (D152's order, so an edit never changes which one leads); absent for none.
std::optional<size_t> Newest(const OWLensView::Sight& s, const std::vector<size_t>& list) {
    std::optional<size_t> best;
    std::optional<OWMessage::OWTimeKey> best_at;
    for (size_t i : list) {
        std::optional<OWMessage::OWTimeKey> at = OWMessage::TimeKey(Get(*s.els[i], "created_at"));
        bool newer;
        if (!best) newer = true;
        else if (at.has_value() != best_at.has_value()) newer = at.has_value();
        else if (at && !(*at == *best_at)) newer = *best_at < *at;
        else newer = s.ids[i] > s.ids[*best];
        if (newer) {
            best = i;
            best_at = at;
        }
    }
    return best;
}

bool Hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// keel's skill_ids_in: every 8-4-4-4-12 hex run in the text (so every ow:// form, a site address and a bare id), left
// to right without overlap as a regex finds them, lowercase, each once in the order written.
std::vector<std::string> IdsIn(const std::string& text) {
    static const size_t Groups[] = {8, 4, 4, 4, 12};
    std::vector<std::string> out;
    size_t i = 0;
    while (i + 36 <= text.size()) {
        size_t k = i;
        bool ok = true;
        for (size_t g = 0; g < 5 && ok; ++g) {
            if (g > 0) ok = text[k++] == '-';
            for (size_t n = 0; n < Groups[g] && ok; ++n) ok = Hex(text[k++]);
        }
        if (!ok) {
            ++i;
            continue;
        }
        std::string id = Lower(text.substr(i, 36));
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        i += 36;
    }
    return out;
}

// keel's charter_skill: the ids the Charter's text names, kept when each is an Object of this world the reader sees,
// the first SkillMax of those kept. The text's id is taken in lowercase (IdsIn) and the Object's own id is read as
// written (the address profile's rule for the same id): an Object held in upper case is not the one a text names.
std::vector<std::string> Skill(const OWLensView::Sight& s, const std::string& text) {
    std::set<std::string> objects;
    for (size_t j = 0; j < s.els.size(); ++j)
        if (s.view[j] && s.types[j] == "object") objects.insert(s.ids[j]);
    std::vector<std::string> out;
    for (const std::string& id : IdsIn(text)) {
        if (out.size() >= OWCharter::SkillMax) break;
        if (objects.count(id)) out.push_back(id);
    }
    return out;
}

// keel's _hubs: the distinct targets of a served Domain's link fields, itself excluded, in the schema's field order
// (each list in its served order), each an element of the field's type the reader sees: the owner's body keeps a link
// to a proposed element as stored, and a proposed Object is in no reader's view, the owner's included, so it is no
// hub. A known-of stub serves no links.
std::vector<OWCharterHub> Hubs(const OWLensView::Sight& s, const std::string& id, const OWJsonValue& body) {
    std::vector<OWCharterHub> out;
    const OWJsonValue* stub = Get(body, "known_of");
    if (stub && stub->IsBool() && stub->AsBool()) return out;
    std::set<std::pair<std::string, std::string>> seen = {{"construct", id}};
    auto add = [&](const std::string& type, const OWJsonValue& v) {
        if (!v.IsString() || v.Text().empty()) return;
        std::optional<size_t> j = s.Find(type, v.Text());
        if (j && s.view[*j] && seen.insert({type, v.Text()}).second) out.push_back({type, v.Text()});
    };
    for (const auto& [field, type] : OWCharter::HubFields()) {
        const OWJsonValue* v = Get(body, field);
        if (!v) continue;
        if (v->IsArray())
            for (const OWJsonValue& t : v->Items()) add(type, t);
        else
            add(type, *v);
    }
    return out;
}

}  // namespace

OWJsonValue OWReadingAids::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    if (charter) {
        OWJsonValue c = OWJsonValue::Object();
        c.Set("id", OWJsonValue::String(charter->id));
        c.Set("type", OWJsonValue::String(charter->type));
        c.Set("name", OWJsonValue::String(charter->name));
        c.Set("rules", charter->rules);
        OWJsonValue skill = OWJsonValue::Array();
        for (const std::string& s : charter->skill) skill.Push(OWJsonValue::String(s));
        c.Set("skill", std::move(skill));
        out.Set("charter", std::move(c));
    } else {
        out.Set("charter", OWJsonValue::Null());
    }
    if (guide) {
        OWJsonValue g = OWJsonValue::Object();
        g.Set("id", OWJsonValue::String(guide->id));
        g.Set("type", OWJsonValue::String(guide->type));
        g.Set("name", OWJsonValue::String(guide->name));
        out.Set("guide", std::move(g));
    } else {
        out.Set("guide", OWJsonValue::Null());
    }
    OWJsonValue list = OWJsonValue::Array();
    for (const OWCharterDomain& d : domains) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("id", OWJsonValue::String(d.id));
        row.Set("type", OWJsonValue::String(d.type));
        row.Set("name", OWJsonValue::String(d.name));
        row.Set("sentence", OWJsonValue::String(d.sentence));
        OWJsonValue hubs = OWJsonValue::Array();
        for (const OWCharterHub& h : d.hubs) {
            OWJsonValue hub = OWJsonValue::Object();
            hub.Set("type", OWJsonValue::String(h.type));
            hub.Set("id", OWJsonValue::String(h.id));
            hubs.Push(std::move(hub));
        }
        row.Set("hubs", std::move(hubs));
        list.Push(std::move(row));
    }
    out.Set("domains", std::move(list));
    if (strand) out.Set("strand", strand->ToJson());
    return out;
}

namespace OWCharter {

const std::vector<OWCharterPair>& Pairs() {
    static const std::vector<OWCharterPair> pairs = {
        {"law", "Charter", "charter"}, {"construct", "Guide", "guide"}, {"construct", "Domain", "domain"}};
    return pairs;
}

const std::vector<std::pair<std::string, std::string>>& HubFields() {
    static const std::vector<std::pair<std::string, std::string>> fields = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const OWLens::OWLensField& f : OWLens::FieldsOf("construct"))
            if (f.kind == OWLens::OWFieldKind::Single || f.kind == OWLens::OWFieldKind::Multi)
                out.emplace_back(f.name, f.target);
        return out;
    }();
    return fields;
}

bool IsCharterElement(const OWJsonValue& element) { return OWLens::IsReadingAid(element); }

std::optional<std::string> ReadingAidOf(const OWJsonValue& element) {
    const OWJsonValue* t = Get(element, "type");
    const OWJsonValue* s = Get(element, "supertype");
    if (!t || !t->IsString() || !s || !s->IsString()) return std::nullopt;
    std::string type = OWMessage::Fold(t->Text()), supertype = OWMessage::Fold(s->Text());
    for (const OWCharterPair& p : Pairs())
        if (type == p.type && supertype == OWMessage::Fold(p.supertype)) return std::string(p.aid);
    return std::nullopt;
}

// keel's first_line: trimmed, up to the first line break (a carriage return included), trimmed again.
std::string FirstLine(std::string_view text) {
    std::string_view t = OWMessage::Trim(text);
    size_t cut = t.find_first_of("\r\n");
    return std::string(cut == std::string_view::npos ? t : OWMessage::Trim(t.substr(0, cut)));
}

OWReadingAids CharterOf(const OWJsonValue& world, const OWJsonValue& reader) {
    OWReadingAids out;
    OWLensView::Sight s;
    if (std::string bad = OWLensView::See(world, reader, nullptr, OWLensOptions(), "charter_of", s); !bad.empty()) {
        out.error = bad;
        return out;
    }
    // The Charter is the public's only on a world open to anyone (rule 5).
    const OWJsonValue* open = world.Find("open");
    if (s.seen_rung == "public" && !(open && open->IsBool() && open->AsBool())) return out;
    // The Charter, read whole as stored: every member rung's to read whole (D152), as keel reads its row.
    if (std::optional<size_t> law = Newest(s, Marked(s, "charter"))) {
        const OWJsonValue& el = *s.els[*law];
        OWCharterLaw c;
        c.id = s.ids[*law];
        c.type = PairType("charter");
        c.name = Text(Get(el, "name"));
        const OWJsonValue* rules = Get(el, RulesField);
        if (rules && !rules->IsNull()) c.rules = *rules;
        c.skill = Skill(s, Text(Get(el, "description")));
        out.charter = std::move(c);
    }
    std::vector<size_t> domains = Marked(s, "domain");
    std::vector<size_t> order;
    if (std::optional<size_t> g = Newest(s, Marked(s, "guide"))) {
        out.guide = OWCharterGuide{s.ids[*g], PairType("guide"), Text(Get(*s.els[*g], "name"))};
        // The Guide's constructs as this reader is served them: a stub's are none.
        if (const OWJsonValue* named = Get(s.served(*g), "constructs"); named && named->IsArray())
            for (const OWJsonValue& entry : named->Items()) {
                if (!entry.IsString()) continue;
                for (size_t d : domains)
                    if (s.ids[d] == entry.Text() && std::find(order.begin(), order.end(), d) == order.end())
                        order.push_back(d);
            }
    }
    std::vector<size_t> rest;
    for (size_t d : domains)
        if (std::find(order.begin(), order.end(), d) == order.end()) rest.push_back(d);
    std::vector<std::string> folded(s.els.size());
    for (size_t d : rest) folded[d] = OWMessage::Fold(Text(Get(*s.els[d], "name")));
    std::sort(rest.begin(), rest.end(), [&](size_t a, size_t b) {
        return folded[a] != folded[b] ? folded[a] < folded[b] : s.ids[a] < s.ids[b];
    });
    order.insert(order.end(), rest.begin(), rest.end());
    for (size_t d : order) {
        const OWJsonValue& body = s.served(d);
        out.domains.push_back({s.ids[d], PairType("domain"), Text(Get(body, "name")),
            FirstLine(Text(Get(body, "description"))), Hubs(s, s.ids[d], body)});
    }
    return out;
}

std::vector<OWCharterDomain> DomainsOf(const OWJsonValue& world, const OWJsonValue& reader) {
    return CharterOf(world, reader).domains;
}

std::optional<OWReadingAids> Served(const OWJsonValue& answer) {
    const OWJsonValue* aids = Get(answer, "reading_aids");
    if (!aids || !aids->IsObject()) return std::nullopt;
    OWReadingAids out;
    if (const OWJsonValue* c = Get(*aids, "charter"); c && c->IsObject()) {
        OWCharterLaw law{Text(Get(*c, "id")), TypeOr(*c, "law"), Text(Get(*c, "name"))};
        if (const OWJsonValue* rules = Get(*c, "rules"); rules && !rules->IsNull()) law.rules = *rules;
        if (const OWJsonValue* skill = Get(*c, "skill"); skill && skill->IsArray())
            for (const OWJsonValue& id : skill->Items())
                if (id.IsString()) law.skill.push_back(id.Text());
        out.charter = std::move(law);
    }
    if (const OWJsonValue* g = Get(*aids, "guide"); g && g->IsObject())
        out.guide = OWCharterGuide{Text(Get(*g, "id")), TypeOr(*g, "construct"), Text(Get(*g, "name"))};
    if (const OWJsonValue* list = Get(*aids, "domains"); list && list->IsArray())
        for (const OWJsonValue& d : list->Items()) {
            if (!d.IsObject()) continue;
            OWCharterDomain domain{
                Text(Get(d, "id")), TypeOr(d, "construct"), Text(Get(d, "name")), Text(Get(d, "sentence")), {}};
            if (const OWJsonValue* hubs = Get(d, "hubs"); hubs && hubs->IsArray())
                for (const OWJsonValue& h : hubs->Items())
                    if (h.IsObject()) domain.hubs.push_back({Text(Get(h, "type")), Text(Get(h, "id"))});
            out.domains.push_back(std::move(domain));
        }
    out.strand = OWStrand::Served(Get(*aids, "strand"));
    return out;
}

}  // namespace OWCharter
}  // namespace OnlyWorlds
