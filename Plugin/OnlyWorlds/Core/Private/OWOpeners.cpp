// The Open with menu's rows (OWOpeners.h): the npm SDK's openersFor, line for line where the language lets it.

#include "OWOpeners.h"

#include "OWGraph.h"
#include "OWLens.h"
#include "OWMessage.h"

#include <algorithm>
#include <utility>

namespace OnlyWorlds {
namespace OWOpeners {

const char* const World = "world";
const char* const Element = "element";
const char* const Member = "member";
const char* const Public = "public";
const char* const YouAreHere = "You are here";
const char* const OpensInYourBrowser = "Opens in your browser";
const char* const NeedsTheWorldOnThisPhone = "Needs the world on this phone";
const char* const OpensTheWorld = "Opens the world";
const char* const NeedsAConnection = "Needs a connection";
const char* const NeedsAnAccount = "Needs an account";

std::string SignInFirst(std::string_view name) { return "Sign in to " + std::string(name) + " first"; }

const std::vector<std::string>& ElementMarks() {
    static const std::vector<std::string> marks = {"room", "hidden", "proposed", "map_space", "reading_aid"};
    return marks;
}

std::vector<std::string> MarksOf(const OWJsonValue& element) {
    std::vector<std::string> out;
    if (OWLens::IsRoomElement(element)) out.push_back("room");
    if (OWLens::IsHidden(element)) out.push_back("hidden");
    if (OWLens::IsProposed(element)) out.push_back("proposed");
    const OWJsonValue* type = element.IsObject() ? element.Find("type") : nullptr;
    if (type && type->IsString()) {
        const std::vector<std::string>& space = OWGraph::MapSpaceTypes();
        if (std::find(space.begin(), space.end(), OWMessage::Fold(type->Text())) != space.end()) out.push_back("map_space");
    }
    if (OWLens::IsReadingAid(element)) out.push_back("reading_aid");
    return out;
}

namespace {

const char* const SiteId = "onlyworlds";
const char* const SiteName = "onlyworlds.com";
const char* const Listed = "listed";
const char* const Account = "account";
const char* const Device = "device";
const char* const AddressKeys[] = {"form", "world", "type", "id", "lens", "moment", "world_is_slug", "canonical"};
const char* const SurfaceKeys[] = {"tool", "audience", "signed_in", "browser", "phone", "online"};

OWOpenersResult Mistake(const std::string& what) {
    OWOpenersResult r;
    r.message = "OpenersFor: " + what;
    return r;
}

template <size_t N>
bool OneOf(const std::string& key, const char* const (&keys)[N]) {
    for (const char* k : keys)
        if (key == k) return true;
    return false;
}

template <size_t N>
std::string Joined(const char* const (&keys)[N]) {
    std::string out;
    for (const char* k : keys) out += (out.empty() ? "" : ", ") + std::string(k);
    return out;
}

std::string Quoted(std::string_view s) { return OWJson::Quote(s); }

// A JSON scalar on one line, for a message (Stringify ends in a newline); an array or an object by its kind.
std::string Show(const OWJsonValue& v) {
    if (v.IsArray()) return "an array";
    if (v.IsObject()) return "an object";
    std::string s = OWJson::Stringify(v);
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::string AsciiLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}

bool IsKind(std::string_view kind) { return kind == World || kind == Element; }

// A web address: http: or https: at the start, in any case (the npm SDK's /^https?:/i). A link with no scheme, or one
// that opens an app (vault://), is not one.
bool IsWeb(std::string_view href) {
    auto starts = [&](std::string_view scheme) {
        if (href.size() < scheme.size()) return false;
        for (size_t i = 0; i < scheme.size(); ++i) {
            char c = href[i];
            if (c >= 'A' && c <= 'Z') c = char(c + 32);
            if (c != scheme[i]) return false;
        }
        return true;
    };
    return starts("http:") || starts("https:");
}

// Each {world}, {type} and {id} in one pass; any other brace is kept as written.
std::string Fill(const std::string& tpl, const std::string& world, const std::string& type, const std::string& id) {
    std::string out;
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == '{') {
            const std::pair<const char*, const std::string*> names[] = {{"{world}", &world}, {"{type}", &type}, {"{id}", &id}};
            bool hit = false;
            for (const auto& [name, value] : names) {
                std::string_view n(name);
                if (tpl.compare(i, n.size(), n) == 0) {
                    out += *value;
                    i += n.size();
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        out.push_back(tpl[i++]);
    }
    return out;
}

// Schemes that open no app: a row whose link used one would run what it carries when pressed.
const char* const NoApp[] = {"javascript", "data", "vbscript", "file", "blob", "about"};

// A link template begins with its scheme (the npm SDK's /^([A-Za-z][A-Za-z0-9+.-]*):/; a browser drops leading spaces
// before reading one), and a scheme that opens no app is refused, in any case: the npm SDK's templateOf. None is none.
std::optional<OWOpenersResult> CheckTemplate(const std::string& row, const char* key,
                                             const std::optional<std::string>& tpl) {
    if (!tpl) return std::nullopt;
    const std::string& s = *tpl;
    auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    auto more = [&](char c) { return letter(c) || (c >= '0' && c <= '9') || c == '+' || c == '.' || c == '-'; };
    size_t n = 0;
    if (!s.empty() && letter(s[0]))
        for (n = 1; n < s.size() && more(s[n]);) ++n;
    if (n == 0 || n >= s.size() || s[n] != ':')
        return Mistake("tool " + row + ": open." + key + " begins with its scheme");
    const std::string scheme = AsciiLower(s.substr(0, n));
    if (OneOf(scheme, NoApp))
        return Mistake("tool " + row + ": open." + key + " is a " + scheme + ": link, which opens no app");
    return std::nullopt;
}

// The tools list's refusal of an element_types that is not one: got is what the row holds, as written.
OWOpenersResult TypesMistake(const std::string& row, const std::string& got) {
    std::string words;
    for (const char* t : OWAddress::TypeWords) words += (words.empty() ? "" : ", ") + std::string(t);
    return Mistake("tool " + row + ": open.element_types is null or a non-empty list of distinct type words (" + words +
                   "); got " + got);
}

// A list of words as JSON writes it, on one line.
std::string Words(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& w : words) out += (out.empty() ? "" : ", ") + Quoted(w);
    return "[" + out + "]";
}

bool IsTypeWord(const std::string& word) {
    for (const char* t : OWAddress::TypeWords)
        if (word == t) return true;
    return false;
}

// The rows, with the address, kind and audience already checked; a link template that is not one (CheckTemplate) or a
// needs outside the list's format is refused here, in the list's order, as the npm SDK refuses it.
OWOpenersResult Rows(const std::vector<OWTool>& tools, bool element, const std::string& page, const std::string& world,
                     const std::string& type, const std::string& id, const OWOpenerSurface& surface,
                     const std::vector<std::string>& marks) {
    OWOpenersResult out;
    OWAddressParts site;
    site.form = OWAddress::Human;
    site.world = world;
    // With no connection a web link opens nothing, the site's own row included, whatever else it needs.
    auto offline = [&surface](const std::string& href) { return !surface.online && IsWeb(href); };
    const std::string site_href = element ? page : OWAddress::Write(site).address;
    std::optional<std::string> site_state;
    // The site itself as the surface (its tool `onlyworlds`): its own row is where the reader is.
    if (surface.tool && *surface.tool == SiteId) site_state = YouAreHere;
    else if (offline(site_href)) site_state = NeedsAConnection;
    out.rows.push_back({SiteId, SiteName, site_href, site_state});
    for (const OWTool& r : tools) {
        if (!r.open) continue;
        const OWToolOpen& o = *r.open;
        if (auto bad = CheckTemplate(r.id, "world", o.world)) return *bad;
        if (auto bad = CheckTemplate(r.id, "element", o.element)) return *bad;
        if (o.needs && *o.needs != Account && *o.needs != Device)
            return Mistake("tool " + r.id + ": open.needs is 'account', 'device' or null; got " + Quoted(*o.needs));
        if (o.opens_public && !(o.needs && *o.needs == Account))
            return Mistake("tool " + r.id + ": open.opens_public is true only beside needs 'account'; got needs " +
                           (o.needs ? Quoted(*o.needs) : std::string("null")));
        const std::vector<std::string>& words = ElementMarks();
        for (const std::string& s : o.element_skips)
            if (std::find(words.begin(), words.end(), s) == words.end()) {
                std::string list;
                for (const std::string& word : words) list += (list.empty() ? "" : ", ") + word;
                return Mistake("tool " + r.id + ": open.element_skips is a list of " + list + "; got " + Quoted(s));
            }
        if (!o.element && !o.element_skips.empty())
            return Mistake("tool " + r.id + ": open.element_skips names what an element door leaves out, and the row has none");
        // A list of the 22 type words, each once, or absent for every type; never an empty list.
        if (o.element_types) {
            const std::vector<std::string>& types = *o.element_types;
            bool fits = !types.empty();
            for (size_t k = 0; fits && k < types.size(); ++k)
                fits = IsTypeWord(types[k]) && std::find(types.begin(), types.begin() + k, types[k]) == types.begin() + k;
            if (!fits) return TypesMistake(r.id, Words(types));
        }
        if (!o.element && o.element_types)
            return Mistake("tool " + r.id + ": open.element_types names what an element door opens, and the row has none");
        if (r.rung != Listed) continue;
        // The public is the signed-out reader: a visitor's device holds no world of this one's, so a row that needs the
        // device is not there; a row that needs an account is, and says so.
        const bool visitor = surface.audience == Public;
        if (visitor && o.needs && *o.needs == Device) continue;
        const std::optional<std::string>& tpl = element ? (o.element ? o.element : o.world) : o.world;
        if (!tpl) continue;
        // An element door that cannot open this element is no door (a row with skips has one: refused above).
        if (element && std::any_of(marks.begin(), marks.end(), [&](const std::string& m) {
                return std::find(o.element_skips.begin(), o.element_skips.end(), m) != o.element_skips.end();
            }))
            continue;
        // A tool that opens other kinds of element is no door for this one, and offers no world fallback either.
        if (element && o.element_types &&
            std::find(o.element_types->begin(), o.element_types->end(), type) == o.element_types->end())
            continue;
        // The first that holds. A word the row needs (it cannot open now) comes before "Opens the world" (it opens,
        // somewhere else), so a world door alone that needs a sign-in says the sign-in.
        std::optional<std::string> state;
        // What the row needs on this surface: a public page shows a public world, which a row that opens one opens with
        // nothing signed in.
        const bool account = o.needs && *o.needs == Account && !(visitor && o.opens_public);
        const std::string href = Fill(*tpl, world, type, id);
        if (surface.tool && r.client && *r.client == *surface.tool) state = YouAreHere;
        else if (offline(href)) state = NeedsAConnection;
        else if (visitor && account) state = NeedsAnAccount;
        else if (account && !surface.browser) state = OpensInYourBrowser;
        else if (account && surface.signed_in == false) state = SignInFirst(r.name);
        else if (o.needs && *o.needs == Device && surface.phone) state = NeedsTheWorldOnThisPhone;
        else if (element && !o.element) state = OpensTheWorld;
        out.rows.push_back({r.id, r.name, href, state});
    }
    out.ok = true;
    return out;
}

// The address's checks are the writer's: the whole address in its own form (the site's when it has none), so a bad
// form, a slug, a bad type, id, moment or lens refuse as there. CheckAddress then writes the site's page.
std::optional<OWOpenersResult> CheckWhole(const OWAddressWritten& written) {
    if (written.ok) return std::nullopt;
    OWOpenersResult r;
    r.reason = written.reason;
    r.message = written.message;
    return r;
}

std::optional<OWOpenersResult> CheckWhole(OWAddressParts whole) {
    if (whole.form.empty()) whole.form = OWAddress::Human;
    return CheckWhole(OWAddress::Write(whole));
}

// The site's page, where a missing world refuses. Then an element's openers need an element. On success fills page
// and the lowercase parts; else returns the refusal.
std::optional<OWOpenersResult> CheckAddress(std::string_view kind, const std::optional<std::string>& world,
                                            const std::optional<std::string>& type, const std::optional<std::string>& id,
                                            std::string& page, std::string& w, std::string& t, std::string& i) {
    OWAddressParts p;
    p.form = OWAddress::Human;
    p.world = world;
    p.type = type;
    p.id = id;
    OWAddressWritten written = OWAddress::Write(p);
    if (!written.ok) {
        OWOpenersResult r;
        r.reason = written.reason;
        r.message = written.message;
        return r;
    }
    if (kind == Element && !type) return Mistake("an element's openers need an address with its type and id");
    page = written.address;
    w = AsciiLower(*world);
    t = type ? *type : std::string();
    i = id ? AsciiLower(*id) : std::string();
    return std::nullopt;
}

std::optional<OWOpenersResult> CheckKind(std::string_view kind) {
    if (!IsKind(kind)) return Mistake("kind is 'world' or 'element'; got " + Quoted(kind));
    return std::nullopt;
}

std::optional<OWOpenersResult> CheckAudience(const std::string& audience) {
    if (audience != Member && audience != Public)
        return Mistake("surface audience is 'member' or 'public'; got " + Quoted(audience));
    return std::nullopt;
}

// The element given for the menu: only an element's, and only the one the address names (its id compared lowercased,
// as the address's is); on success fills its marks (MarksOf). Null is no element.
std::optional<OWOpenersResult> CheckElement(std::string_view kind, const OWJsonValue* element, const std::string& id,
                                            std::vector<std::string>& marks) {
    if (!element || element->IsNull()) return std::nullopt;
    if (!element->IsObject()) return Mistake("options element is the element as served, an object, or null");
    if (kind != Element) return Mistake("options element is for an element's menu; a world's menu takes none");
    const OWJsonValue* eid = element->Find("id");
    if (!eid || !eid->IsString() || AsciiLower(eid->Text()) != id)
        return Mistake("options element is the element the address names: its id differs");
    marks = MarksOf(*element);
    return std::nullopt;
}

OWOpenersResult Typed(const std::vector<OWTool>& tools, std::string_view kind, const std::optional<std::string>& world,
                      const std::optional<std::string>& type, const std::optional<std::string>& id,
                      const OWOpenerSurface& surface, const OWOpenersOptions& options) {
    if (auto bad = CheckKind(kind)) return *bad;
    std::string page, w, t, i;
    if (auto bad = CheckAddress(kind, world, type, id, page, w, t, i)) return *bad;
    if (auto bad = CheckAudience(surface.audience)) return *bad;
    std::vector<std::string> marks;
    if (auto bad = CheckElement(kind, options.element, i, marks)) return *bad;
    return Rows(tools, kind == Element, page, w, t, i, surface, marks);
}

}  // namespace

OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressParts& address,
                           const OWOpenerSurface& surface, const OWOpenersOptions& options) {
    if (auto bad = CheckKind(kind)) return *bad;
    if (auto bad = CheckWhole(address)) return *bad;
    return Typed(tools, kind, address.world, address.type, address.id, surface, options);
}

OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressReading& address,
                           const OWOpenerSurface& surface, const OWOpenersOptions& options) {
    if (auto bad = CheckKind(kind)) return *bad;
    if (!address.ok) {
        OWOpenersResult r;
        r.reason = address.reason;
        return r;
    }
    OWAddressParts whole;
    whole.form = address.form;
    whole.world = address.world;
    whole.type = address.type;
    whole.id = address.id;
    whole.lens = address.lens;
    whole.moment = address.moment;
    if (auto bad = CheckWhole(whole)) return *bad;
    return Typed(tools, kind, address.world, address.type, address.id, surface, options);
}

OWOpenersResult OpenersForJson(const OWJsonValue& tools, std::string_view kind, const OWJsonValue& address,
                               const OWJsonValue& surface, const OWJsonValue* options) {
    if (!tools.IsArray()) return Mistake("tools is the tools list's rows, an array");
    if (auto bad = CheckKind(kind)) return *bad;
    if (!address.IsObject()) return Mistake("the address is an object ({ form, world, type, id }), as Parse reads it");
    for (const std::string& key : address.Keys())
        if (!OneOf(key, AddressKeys))
            return Mistake("unknown key '" + key + "' in the address. The keys are " + Joined(AddressKeys) + ".");
    OWJsonValue whole = address;
    if (const OWJsonValue* f = address.Find("form"); !f || f->IsNull()) whole.Set("form", OWJsonValue::String(std::string(OWAddress::Human)));
    if (auto bad = CheckWhole(OWAddress::WriteJson(whole))) return *bad;
    // A part as OWAddress::WriteJson reads one: absent for a missing key or JSON null; a string's text; anything else
    // present as its JSON text, which refuses as that part would.
    auto part = [&](const char* key) -> std::optional<std::string> {
        const OWJsonValue* v = address.Find(key);
        if (!v || v->IsNull()) return std::nullopt;
        if (v->IsString()) return v->Text();
        return OWJson::Stringify(*v);
    };
    std::optional<std::string> world = part("world"), type = part("type"), id = part("id");
    std::string page, w, t, i;
    if (auto bad = CheckAddress(kind, world, type, id, page, w, t, i)) return *bad;

    if (!surface.IsObject()) return Mistake("the surface is an object ({ audience, ... })");
    for (const std::string& key : surface.Keys())
        if (!OneOf(key, SurfaceKeys))
            return Mistake("unknown key '" + key + "' in the surface. The keys are " + Joined(SurfaceKeys) + ".");
    OWOpenerSurface s;
    const OWJsonValue* audience = surface.Find("audience");
    if (!audience || !audience->IsString() || (audience->Text() != Member && audience->Text() != Public))
        return Mistake("surface audience is 'member' or 'public'; got " +
                       (audience ? Show(*audience) : std::string("nothing")));
    s.audience = audience->Text();
    if (const OWJsonValue* v = surface.Find("tool"); v && !v->IsNull()) {
        if (!v->IsString()) return Mistake("surface tool is a client name or null");
        s.tool = v->Text();
    }
    if (const OWJsonValue* v = surface.Find("signed_in"); v && !v->IsNull()) {
        if (!v->IsBool()) return Mistake("surface signed_in is true, false or null");
        s.signed_in = v->AsBool();
    }
    if (const OWJsonValue* v = surface.Find("browser")) {
        if (!v->IsBool()) return Mistake("surface browser is a boolean");
        s.browser = v->AsBool();
    }
    if (const OWJsonValue* v = surface.Find("phone")) {
        if (!v->IsBool()) return Mistake("surface phone is a boolean");
        s.phone = v->AsBool();
    }
    if (const OWJsonValue* v = surface.Find("online")) {
        if (!v->IsBool()) return Mistake("surface online is a boolean");
        s.online = v->AsBool();
    }

    std::vector<std::string> marks;
    if (options) {
        if (!options->IsObject()) return Mistake("the options are an object ({ element })");
        for (const std::string& key : options->Keys())
            if (key != "element") return Mistake("unknown key '" + key + "' in the options. The keys are element.");
        if (auto bad = CheckElement(kind, options->Find("element"), i, marks)) return *bad;
    }

    std::vector<OWTool> rows;
    rows.reserve(tools.Size());
    for (const OWJsonValue& r : tools.Items()) {
        const OWJsonValue* rid = r.IsObject() ? r.Find("id") : nullptr;
        const OWJsonValue* name = r.IsObject() ? r.Find("name") : nullptr;
        if (!rid || !rid->IsString() || !name || !name->IsString())
            return Mistake("each tools row is an object with an id and a name");
        OWTool tool;
        tool.id = rid->Text();
        tool.name = name->Text();
        auto text = [&](const OWJsonValue* v) -> std::optional<std::string> {
            return v && v->IsString() ? std::optional<std::string>(v->Text()) : std::nullopt;
        };
        tool.url = text(r.Find("url")).value_or(std::string());
        tool.client = text(r.Find("client"));        // a client that is not a string is no surface's
        tool.rung = text(r.Find("rung")).value_or(std::string());  // a rung that is not a string is not listed
        const OWJsonValue* o = r.Find("open");
        if (o && !o->IsNull()) {
            if (!o->IsObject()) return Mistake("tool " + tool.id + ": open is an object or null");
            OWToolOpen door;
            for (const char* key : {"world", "element"}) {
                const OWJsonValue* v = o->Find(key);
                if (v && !v->IsNull() && !v->IsString())
                    return Mistake("tool " + tool.id + ": open." + key + " is a link template or null");
            }
            door.world = text(o->Find("world"));
            door.element = text(o->Find("element"));
            if (const OWJsonValue* v = o->Find("needs"); v && !v->IsNull()) {
                if (!v->IsString() || (v->Text() != Account && v->Text() != Device))
                    return Mistake("tool " + tool.id + ": open.needs is 'account', 'device' or null; got " + Show(*v));
                door.needs = v->Text();
            }
            // True or false, absent for false; null, like any other value, is refused (the TypeScript reader's rule).
            if (const OWJsonValue* v = o->Find("opens_public")) {
                if (!v->IsBool())
                    return Mistake("tool " + tool.id + ": open.opens_public is true or false; got " + Show(*v));
                door.opens_public = v->AsBool();
            }
            // A list of words, absent or null for none; which words is Rows' check, as for a row built in code.
            if (const OWJsonValue* v = o->Find("element_skips"); v && !v->IsNull()) {
                bool words = v->IsArray();
                if (words)
                    for (const OWJsonValue& word : v->Items()) {
                        if (!word.IsString()) {
                            words = false;
                            break;
                        }
                        door.element_skips.push_back(word.Text());
                    }
                if (!words)
                    return Mistake("tool " + tool.id + ": open.element_skips is a list of room, hidden, proposed, map_space, reading_aid; got " +
                                   Show(*v));
            }
            // A list of strings, absent or null for every type; which words, and each once, is Rows' check.
            if (const OWJsonValue* v = o->Find("element_types"); v && !v->IsNull()) {
                std::vector<std::string> types;
                bool words = v->IsArray();
                if (words)
                    for (const OWJsonValue& word : v->Items()) {
                        if (!word.IsString()) {
                            words = false;
                            break;
                        }
                        types.push_back(word.Text());
                    }
                if (!words) return TypesMistake(tool.id, Show(*v));
                door.element_types = std::move(types);
            }
            tool.open = door;
        }
        rows.push_back(std::move(tool));
    }
    return Rows(rows, kind == Element, page, w, t, i, s, marks);
}

}  // namespace OWOpeners
}  // namespace OnlyWorlds
