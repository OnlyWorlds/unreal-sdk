// The Open with menu's rows (OWOpeners.h): the npm SDK's openersFor, line for line where the language lets it.

#include "OWOpeners.h"

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

std::string SignInFirst(std::string_view name) { return "Sign in to " + std::string(name) + " first"; }

namespace {

const char* const SiteId = "onlyworlds";
const char* const SiteName = "onlyworlds.com";
const char* const Listed = "listed";
const char* const Account = "account";
const char* const Device = "device";
const char* const AddressKeys[] = {"form", "world", "type", "id", "lens", "moment", "world_is_slug", "canonical"};
const char* const SurfaceKeys[] = {"tool", "audience", "signed_in", "browser", "phone"};

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

// The rows, with the address, kind and audience already checked; a needs outside the list's format is refused here, in
// the list's order, as the npm SDK refuses it.
OWOpenersResult Rows(const std::vector<OWTool>& tools, bool element, const std::string& page, const std::string& world,
                     const std::string& type, const std::string& id, const OWOpenerSurface& surface) {
    OWOpenersResult out;
    OWAddressParts site;
    site.form = OWAddress::Human;
    site.world = world;
    out.rows.push_back({SiteId, SiteName, element ? page : OWAddress::Write(site).address, std::nullopt});
    for (const OWTool& r : tools) {
        if (!r.open) continue;
        const OWToolOpen& o = *r.open;
        if (o.needs && *o.needs != Account && *o.needs != Device)
            return Mistake("tool " + r.id + ": open.needs is 'account', 'device' or null; got " + Quoted(*o.needs));
        if (r.rung != Listed) continue;
        if (surface.audience == Public && o.needs) continue;
        const std::optional<std::string>& tpl = element ? (o.element ? o.element : o.world) : o.world;
        if (!tpl) continue;
        // The first that holds. A word the row needs (it cannot open now) comes before "Opens the world" (it opens,
        // somewhere else), so a world door alone that needs a sign-in says the sign-in.
        std::optional<std::string> state;
        const bool account = o.needs && *o.needs == Account;
        if (surface.tool && r.client && *r.client == *surface.tool) state = YouAreHere;
        else if (account && !surface.browser) state = OpensInYourBrowser;
        else if (account && surface.signed_in == false) state = SignInFirst(r.name);
        else if (o.needs && *o.needs == Device && surface.phone) state = NeedsTheWorldOnThisPhone;
        else if (element && !o.element) state = OpensTheWorld;
        out.rows.push_back({r.id, r.name, Fill(*tpl, world, type, id), state});
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

OWOpenersResult Typed(const std::vector<OWTool>& tools, std::string_view kind, const std::optional<std::string>& world,
                      const std::optional<std::string>& type, const std::optional<std::string>& id,
                      const OWOpenerSurface& surface) {
    if (auto bad = CheckKind(kind)) return *bad;
    std::string page, w, t, i;
    if (auto bad = CheckAddress(kind, world, type, id, page, w, t, i)) return *bad;
    if (auto bad = CheckAudience(surface.audience)) return *bad;
    return Rows(tools, kind == Element, page, w, t, i, surface);
}

}  // namespace

OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressParts& address,
                           const OWOpenerSurface& surface) {
    if (auto bad = CheckKind(kind)) return *bad;
    if (auto bad = CheckWhole(address)) return *bad;
    return Typed(tools, kind, address.world, address.type, address.id, surface);
}

OWOpenersResult OpenersFor(const std::vector<OWTool>& tools, std::string_view kind, const OWAddressReading& address,
                           const OWOpenerSurface& surface) {
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
    return Typed(tools, kind, address.world, address.type, address.id, surface);
}

OWOpenersResult OpenersForJson(const OWJsonValue& tools, std::string_view kind, const OWJsonValue& address,
                               const OWJsonValue& surface) {
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
            tool.open = door;
        }
        rows.push_back(std::move(tool));
    }
    return Rows(rows, kind == Element, page, w, t, i, s);
}

}  // namespace OWOpeners
}  // namespace OnlyWorlds
