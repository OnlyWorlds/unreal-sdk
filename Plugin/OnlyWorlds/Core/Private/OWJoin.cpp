#include "OWJoin.h"

#include "OWUnicode.h"

#include <cmath>
#include <ctime>
#include <utility>

namespace OnlyWorlds {

const char* const OWJoin::CodePrefix = "ow_j_";

namespace {

const char* const NothingSent = " Nothing was sent.";

// Unicode's White_Space, the set C#'s char.IsWhiteSpace and Godot's trim read: OWUnicode::IsWhite is Python's
// str.strip set, which adds U+001C to U+001F (bidi separators that are no White_Space).
bool IsWhiteSpace(char32_t cp) { return OWUnicode::IsWhite(cp) && !(cp >= 0x1C && cp <= 0x1F); }

std::u32string Trim(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsWhiteSpace(s[a])) ++a;
    while (b > a && IsWhiteSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// A string as served, or "" (a plain string field).
std::string Str(const OWJsonValue& v, std::string_view key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : "";
}

// A string as served, or absent (null, left out, or not a string).
std::optional<std::string> Maybe(const OWJsonValue& v, std::string_view key) {
    const OWJsonValue* x = v.Find(key);
    if (!x || !x->IsString()) return std::nullopt;
    return x->Text();
}

// A whole number keel serves: an integer literal read whole, or a number spelled otherwise when it is whole (1.0);
// absent for anything else.
std::optional<int64_t> Whole(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    double d = v->AsDouble();
    if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) >= 9007199254740992.0) return std::nullopt;
    return static_cast<int64_t>(d);
}

// A place in a list: a whole number not below 0, else -1.
int64_t Place(const OWJsonValue* v) {
    std::optional<int64_t> n = Whole(v);
    return n && *n >= 0 ? *n : -1;
}

// The objects of the list under `key`, in order; anything else is left out.
std::vector<const OWJsonValue*> Rows(const OWJsonValue& v, std::string_view key) {
    std::vector<const OWJsonValue*> out;
    const OWJsonValue* list = v.Find(key);
    if (!list || !list->IsArray()) return out;
    for (const OWJsonValue& x : list->Items())
        if (x.IsObject()) out.push_back(&x);
    return out;
}

std::optional<OWJoinWorld> WorldOf(const OWJsonValue* v) {
    if (!v || !v->IsObject()) return std::nullopt;
    OWJoinWorld w;
    w.id = Maybe(*v, "id");
    w.name = Str(*v, "name");
    w.description = Maybe(*v, "description");
    return w;
}

std::optional<OWJoinGlimpse> GlimpseOf(const OWJsonValue* v) {
    if (!v || !v->IsObject()) return std::nullopt;
    OWJoinGlimpse g;
    g.picked_by = Str(*v, "picked_by");
    for (const OWJsonValue* x : Rows(*v, "things")) g.things.push_back({Str(*x, "name"), Str(*x, "type")});
    for (const OWJsonValue* x : Rows(*v, "links")) {
        OWJoinGlimpseLink link;
        link.from = Place(x->Find("from"));
        link.to = Place(x->Find("to"));
        link.field = Str(*x, "field");
        link.label = Str(*x, "label");
        g.links.push_back(std::move(link));
    }
    return g;
}

template <typename T>
void Refuse(const OWHandler<T>& done, std::string why) {
    OWReply<T> r;
    r.error = OWApiError::Of(OWApiError::Kind::Refused, std::move(why));
    done(std::move(r));
}

// "" when the call may be sent; else the refusal.
std::string Check(const char* call, const std::string& code, const OWJoinConfig& where) {
    if (code.empty()) {
        return std::string(call) + ": a join code is required (the part of the link after #: OWJoin::ParseLink reads it "
                                   "out of a pasted link)." + NothingSent;
    }
    if (!where.transport) {
        return std::string(call) + ": a transport is required: the core has no HTTP of its own (the plugin's sends "
                                   "through Unreal's)." + NothingSent;
    }
    return "";
}

// The one keyless request: no API-Key, no API-Pin, no Bearer (keel declares no security on either route), the body
// compact JSON with Content-Type alone; keel's envelope on a non-2xx, and a 2xx whose body is no JSON object an error
// of kind Answer. `read` makes the answer from the object. The handler is called once: a second answer is dropped.
template <typename T, typename Read>
void Post(const std::string& path, std::string body, const OWJoinConfig& where, OWHandler<T> done, Read read) {
    std::string base = where.base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) base = OWClient::DefaultBaseUrl;
    OWRequest request;
    request.method = "POST";
    request.url = base + path;
    request.headers.emplace_back("Content-Type", "application/json");
    request.body = std::move(body);
    const std::string route = "POST " + path;
    auto answered = std::make_shared<bool>(false);
    where.transport->Send(std::move(request), [route, answered, done, read](OWResponse response) {
        if (*answered) return;
        *answered = true;
        OWReply<T> r;
        if (response.status == 0 || !response.failure.empty()) {
            std::string why = response.failure.empty() ? "no answer" : response.failure;
            r.error = OWApiError::Of(OWApiError::Kind::Transport, "OnlyWorlds request failed: " + route + ": " + why);
        } else if (!response.IsSuccess()) {
            r.error = OWApiError::FromResponse(response, static_cast<int64_t>(std::time(nullptr)));
        } else {
            OWJsonParseResult parsed = OWJson::Parse(response.body);
            if (parsed.ok && parsed.value.IsObject()) {
                r.value = read(parsed.value);
            } else {
                r.error = OWApiError::Of(OWApiError::Kind::Answer,
                                         route + " answered " + std::to_string(response.status) +
                                             " with a body that is not a JSON object",
                                         response.status, response.body);
            }
        }
        r.response = std::move(response);
        done(std::move(r));
    });
}

}  // namespace

const std::vector<std::string>& OWJoinWorld::Keys() {
    static const std::vector<std::string> keys = {"id", "name", "description"};
    return keys;
}
const std::vector<std::string>& OWJoinCharacter::Keys() {
    static const std::vector<std::string> keys = {"id", "name"};
    return keys;
}
const std::vector<std::string>& OWJoinPick::Keys() {
    static const std::vector<std::string> keys = {"id", "name", "image_url", "taken"};
    return keys;
}
const std::vector<std::string>& OWJoinSeat::Keys() {
    static const std::vector<std::string> keys = {"id", "role", "agent_name"};
    return keys;
}
const std::vector<std::string>& OWJoinGlimpseThing::Keys() {
    static const std::vector<std::string> keys = {"name", "type"};
    return keys;
}
const std::vector<std::string>& OWJoinGlimpseLink::Keys() {
    static const std::vector<std::string> keys = {"from", "to", "field", "label"};
    return keys;
}
const std::vector<std::string>& OWJoinGlimpse::Keys() {
    static const std::vector<std::string> keys = {"picked_by", "things", "links"};
    return keys;
}
const std::vector<std::string>& OWJoinPreview::Keys() {
    static const std::vector<std::string> keys = {"reader",     "world",       "invited_by", "owner_character",
                                                  "role",       "with_character", "expires_at", "note",
                                                  "glimpse",    "max_uses",    "uses_left",  "name_required",
                                                  "characters"};
    return keys;
}
const std::vector<std::string>& OWJoined::Keys() {
    static const std::vector<std::string> keys = {"kind", "key",      "pin", "world",      "character",   "role",
                                                  "seat", "base_url", "env", "front_door", "reading_aids"};
    return keys;
}
const std::vector<std::string>& OWJoinedMembership::Keys() {
    static const std::vector<std::string> keys = {"id",   "world_id",  "world_name",
                                                  "role", "joined_at", "roster_character"};
    return keys;
}

OWJsonValue OWJoinGlimpse::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("picked_by", OWJsonValue::String(picked_by));
    OWJsonValue list = OWJsonValue::Array();
    for (const OWJoinGlimpseThing& x : things) {
        OWJsonValue one = OWJsonValue::Object();
        one.Set("name", OWJsonValue::String(x.name));
        one.Set("type", OWJsonValue::String(x.type));
        list.Push(std::move(one));
    }
    out.Set("things", std::move(list));
    list = OWJsonValue::Array();
    for (const OWJoinGlimpseLink& x : links) {
        OWJsonValue one = OWJsonValue::Object();
        one.Set("from", OWJsonValue::Number(x.from));
        one.Set("to", OWJsonValue::Number(x.to));
        one.Set("field", OWJsonValue::String(x.field));
        one.Set("label", OWJsonValue::String(x.label));
        list.Push(std::move(one));
    }
    out.Set("links", std::move(list));
    return out;
}

std::optional<std::string> OWJoined::RoleInWorld() const {
    if (role) return role;
    if (seat) return seat->role;
    return std::nullopt;
}

std::string OWJoin::ParseLink(std::string_view utf8) {
    if (!OWUnicode::IsUtf8(utf8)) return "";
    std::u32string s = Trim(OWUnicode::Decode(utf8));
    size_t hash = s.rfind(U'#');
    if (hash != std::u32string::npos) s = Trim(s.substr(hash + 1));
    const std::u32string prefix = U"ow_j_";
    if (s.size() <= prefix.size() || s.compare(0, prefix.size(), prefix) != 0) return "";
    for (char32_t cp : s)
        if (IsWhiteSpace(cp)) return "";
    return OWUnicode::Encode(s);
}

OWJoinPreview OWJoin::PreviewOf(const OWJsonValue& v) {
    OWJoinPreview p;
    p.reader = Str(v, "reader");
    p.world = WorldOf(v.Find("world"));
    p.invited_by = Maybe(v, "invited_by");
    p.owner_character = Maybe(v, "owner_character");
    p.role = Str(v, "role");
    const OWJsonValue* with = v.Find("with_character");
    p.with_character = with && with->IsBool() && with->AsBool();
    p.expires_at = Str(v, "expires_at");
    p.note = Maybe(v, "note");
    p.glimpse = GlimpseOf(v.Find("glimpse"));
    p.max_uses = Whole(v.Find("max_uses"));
    p.uses_left = Whole(v.Find("uses_left"));
    if (const OWJsonValue* n = v.Find("name_required"); n && n->IsBool()) p.name_required = n->AsBool();
    if (const OWJsonValue* list = v.Find("characters"); list && list->IsArray()) {
        p.characters.emplace();
        for (const OWJsonValue* x : Rows(v, "characters")) {
            OWJoinPick pick;
            pick.id = Str(*x, "id");
            pick.name = Str(*x, "name");
            pick.image_url = Maybe(*x, "image_url");
            const OWJsonValue* taken = x->Find("taken");
            pick.taken = taken && taken->IsBool() && taken->AsBool();
            p.characters->push_back(std::move(pick));
        }
    }
    return p;
}

OWJoined OWJoin::JoinedOf(const OWJsonValue& v) {
    OWJoined j;
    j.kind = Str(v, "kind");
    j.key = Str(v, "key");
    j.pin = Maybe(v, "pin");
    j.world = WorldOf(v.Find("world"));
    if (const OWJsonValue* c = v.Find("character"); c && c->IsObject()) j.character = OWJoinCharacter{Str(*c, "id"), Str(*c, "name")};
    j.role = Maybe(v, "role");
    if (const OWJsonValue* s = v.Find("seat"); s && s->IsObject())
        j.seat = OWJoinSeat{Str(*s, "id"), Str(*s, "role"), Str(*s, "agent_name")};
    j.base_url = Maybe(v, "base_url");
    j.env = Maybe(v, "env");
    if (const OWJsonValue* door = v.Find("front_door"); door && door->IsObject()) j.front_door = *door;
    j.reading_aids = OWCharter::Served(v);
    j.served = v;
    return j;
}

OWJoinedMembership OWJoin::MembershipOf(const OWJsonValue& v) {
    OWJoinedMembership m;
    m.id = Str(v, "id");
    m.world_id = Str(v, "world_id");
    m.world_name = Str(v, "world_name");
    m.role = Str(v, "role");
    m.joined_at = Maybe(v, "joined_at");
    m.roster_character = Maybe(v, "roster_character");
    return m;
}

void OWJoin::Preview(const std::string& code, const OWJoinConfig& where, OWHandler<OWJoinPreview> done) {
    std::string why = Check("OWJoin::Preview", code, where);
    if (!why.empty()) return Refuse(done, why);
    Post<OWJoinPreview>("/join/preview", "{\"code\":" + OWJson::Quote(code) + "}", where, std::move(done),
                        [](const OWJsonValue& v) { return PreviewOf(v); });
}

void OWJoin::Redeem(const std::string& code, const OWRedeemOptions& options, const OWJoinConfig& where,
                    OWHandler<OWJoined> done) {
    std::string why = Check("OWJoin::Redeem", code, where);
    if (!why.empty()) return Refuse(done, why);
    // In the order keel's tests and the TypeScript SDK send them; each only when set.
    std::string body = "{\"code\":" + OWJson::Quote(code);
    for (auto [name, value] : {std::make_pair("name", &options.name), std::make_pair("agent_name", &options.agent_name),
                               std::make_pair("character", &options.character)}) {
        if (*value) body += std::string(",\"") + name + "\":" + OWJson::Quote(**value);
    }
    body += "}";
    Post<OWJoined>("/join", std::move(body), where, std::move(done), [](const OWJsonValue& v) { return JoinedOf(v); });
}

}  // namespace OnlyWorlds
