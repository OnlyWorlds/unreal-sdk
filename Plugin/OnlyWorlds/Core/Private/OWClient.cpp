#include "OWClient.h"

#include "OWId.h"
#include "OWJoin.h"
#include "OWLens.h"
#include "OWMessage.h"
#include "OWUnicode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <utility>

namespace OnlyWorlds {

// The client's state, shared with every request in flight, so a client may go before its answers come.
struct OWClient::State {
    std::string error;
    std::string api_key;
    std::string api_pin;
    std::string base_url;
    int64_t page_size = 100;
    OWKeyKind kind = OWKeyKind::None;
    std::optional<std::string> world;
    std::shared_ptr<OWTransport> transport;
    // The name sent as X-OW-Client on element writes, and whether X-OW-Agent goes with them.
    std::string client_name;
    bool agent = false;
    // The last tip read's ETag ("" when it sent none) and the tip its 200 answered, which a 304 hands back.
    std::string head_tag;
    std::optional<OWChangesPage> head_held;
};

const char* const OWClient::DefaultBaseUrl = "https://www.onlyworlds.com/api/v2";

namespace {

using StatePtr = std::shared_ptr<OWClient::State>;
using Query = std::vector<std::pair<std::string, std::string>>;

// The reads that send world=<id>, as the refusals name them.
const char* const WorldReads = "List, ListAll, Get and GetWorld";
const char* const NothingSent = " Nothing was sent.";

// RFC 3986's percent-encoding, as the Unity SDK's Uri.EscapeDataString: ASCII letters, digits and - _ . ~ kept, every
// other byte of the UTF-8 escaped in capitals (a space is %20, never '+').
std::string Escape(std::string_view s) {
    static const char* const Hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.' || c == '~';
        if (keep) {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(Hex[c >> 4]);
            out.push_back(Hex[c & 15]);
        }
    }
    return out;
}

// One caller value as one path segment, escaped; else `why` names it (the first refusal kept) and the caller refuses
// before sending. keel decodes an escaped slash before it routes (/character/<id>%2Fhidden/ reaches another route), so
// the escape alone cannot keep a value in its segment: the TypeScript SDK's seg() since 4.123.3. A type is a lower-case
// word (^[a-z][a-z0-9_]*$); any other value letters, digits and _ . -, not starting with a dot or a dash
// (^[A-Za-z0-9][A-Za-z0-9_.-]*$). Read byte by byte, so a final newline is refused as any other byte is.
std::string Seg(const char* name, const std::string& value, std::string& why) {
    auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto letter = [&](char c) { return lower(c) || (c >= 'A' && c <= 'Z'); };
    const bool type = std::string_view(name) == "type";
    bool ok = !value.empty() && (type ? lower(value[0]) : (letter(value[0]) || digit(value[0])));
    for (size_t i = 1; ok && i < value.size(); ++i) {
        char c = value[i];
        ok = lower(c) || digit(c) || c == '_' || (!type && (letter(c) || c == '.' || c == '-'));
    }
    if (!ok && why.empty())
        why = std::string(name) + " " + OWJson::Quote(value) + " is not one path segment." + NothingSent;
    return Escape(value);
}

std::string Joined(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) out += (i ? "," : "") + names[i];
    return out;
}

// A schema field's shape (parent_location): a lowercase letter, then lowercase letters, digits and '_'.
bool IsFieldName(const std::string& s) {
    if (s.empty() || s[0] < 'a' || s[0] > 'z') return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// The account routes, /account and under it, which an account token sends with or without a world.
bool IsAccountRoute(const std::string& path) {
    return path == "/account" || path.rfind("/account/", 0) == 0 || path.rfind("/account?", 0) == 0;
}

// A whole number keel serves (head, epoch, change_seq): an integer literal read whole, past 2^53 too; a number spelled
// otherwise read when it is whole. Absent for anything else.
std::optional<int64_t> IntOf(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    const std::string& text = v->Text();
    int64_t n = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec == std::errc() && end == text.data() + text.size()) return n;
    double d = v->AsDouble();
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.2e18) return static_cast<int64_t>(d);
    return std::nullopt;
}

// A text keel serves: a string's, or a number's source (a cursor keel sends as a number reads "17", never "17.0").
// Absent for null, an absent member, or anything else.
std::optional<std::string> TextOf(const OWJsonValue* v) {
    if (!v || !(v->IsString() || v->IsNumber())) return std::nullopt;
    return v->Text();
}

template <typename T>
void Refuse(const OWHandler<T>& done, std::string why) {
    OWReply<T> r;
    r.error = OWApiError::Of(OWApiError::Kind::Refused, std::move(why));
    done(std::move(r));
}

// A reply of one type turned into another's: its error and response kept, its value made by `make` when it succeeded
// (which may answer an error instead).
template <typename T, typename F>
OWReply<T> Then(OWReply<OWJsonValue> r, F make) {
    OWReply<T> out;
    out.error = std::move(r.error);
    out.response = std::move(r.response);
    if (!out.error) make(r.value, out);
    return out;
}

template <typename T>
void Bad(OWReply<T>& r, const std::string& why) {
    int status = r.response ? r.response->status : 0;
    r.error = OWApiError::Of(OWApiError::Kind::Answer, why, status, r.response ? r.response->body : "");
    r.value = T{};
}

// What a request asks of the client's rules beside its method and path.
struct How {
    // False for a route keel serves with no credential (the health pulse): no key, PIN or Bearer, no world=, and no
    // client refused for the key it holds.
    bool auth = true;
    // A GET keel serves an account token, or no key, with world=<id> (an element list and get, the world GET).
    bool world_read = false;
    // A route keel declares the account token on beside the world key, naming the world with world=<id> (the roster).
    bool token_world = false;
    // A tag sent as If-None-Match exactly as given (the tip read).
    std::string if_none_match;
    // A 304 is answered, not an error (the tip read reads the status).
    bool not_modified = false;
    // The JSON body, compact, sent with Content-Type: application/json; absent on a read and a delete.
    std::optional<std::string> body;
    // Sent as Idempotency-Key when not empty (a create, a bulk).
    std::string idempotency_key;
    // A change_seq sent as If-Match, quoted (a patch, a delete, a link edit).
    std::optional<int64_t> if_match;
    // An element write: X-OW-Client, and X-OW-Agent on an agent client, the operations keel's document declares both on.
    bool write = false;
    // X-OW-Client alone, never X-OW-Agent: the proposal routes keel's document declares it on (a propose, an accept, a
    // review).
    bool named = false;
    // A write keel may answer 202 with {proposal} (an upsert, a patch, a link edit): a 202 is then a proposal, never
    // the element.
    bool proposable = false;
    // An empty 2xx body (or white space) answers null rather than an error (a delete's 204).
    bool allow_empty = false;
};

bool OnlyWhite(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
}

// A 202 on a write that may become a proposal: kind Api, status 202, the row in `proposal` (the Unity SDK's
// OWProposedError). A 202 whose body holds no proposal is kind Answer: never the element, never a made-up proposal.
OWApiError Proposed(const OWResponse& response, const std::string& route) {
    OWJsonParseResult parsed = OWJson::Parse(response.body);
    const OWJsonValue* row = parsed.ok && parsed.value.IsObject() ? parsed.value.Find("proposal") : nullptr;
    if (!row || !row->IsObject()) {
        return OWApiError::Of(OWApiError::Kind::Answer,
                              route + " answered 202 without a proposal: the write was not applied, and the answer is "
                                      "not an element",
                              202, response.body);
    }
    OWApiError e = OWApiError::Of(OWApiError::Kind::Api,
                                  route + " became a proposal (202): this world takes an agent seat's writes as "
                                          "proposals, so the element did not change. error.proposal is the row; the "
                                          "owner or a co-builder accepts or declines it.",
                                  202, response.body);
    e.proposal = *row;
    return e;
}

// Every call's request: the credential rules, then the send, then keel's envelope on a non-2xx, else the body parsed.
// The rule is the Unity SDK's RequestWithResponseAsync and the TypeScript SDK's request(): what an account token or no
// key may send is read from the request itself, and anything else, which keel answers such a client with a 401, is
// refused here with nothing sent.
void Request(const StatePtr& s, const std::string& method, const std::string& path, Query query, const How& how,
             OWHandler<OWJsonValue> done) {
    const std::string route = method + " " + path;
    if (!s->error.empty()) return Refuse(done, "OWClient was not built: " + s->error + NothingSent);
    bool keyless = s->kind == OWKeyKind::None;
    bool reads = how.auth && how.world_read && method == "GET";
    bool token_world = how.auth && s->kind == OWKeyKind::Account && how.token_world;
    if (how.auth && keyless && !(reads && s->world)) {
        return Refuse(done, route + " needs a key, and this client holds none: without one, only Health works, and " +
                                WorldReads + " with a world set (OWClientConfig::world)." + NothingSent);
    }
    if (how.auth && s->kind == OWKeyKind::Account && !IsAccountRoute(path)) {
        if (!reads && !token_world) {
            return Refuse(done, route + " needs a world key: an account token sends Members, and with a world set reads "
                                        "a world through " + WorldReads + " only." + NothingSent);
        }
        if (reads && !s->world) {
            return Refuse(done, route + ": an account token reads a world only with a world set "
                                        "(OWClientConfig::world), through " + WorldReads + "." + NothingSent);
        }
        if (token_world && !s->world) {
            return Refuse(done, route + ": an account token names the world it sends to with a world set "
                                        "(OWClientConfig::world)." + NothingSent);
        }
    }
    if ((reads || token_world) && s->world) query.emplace_back("world", *s->world);

    OWRequest request;
    request.method = method;
    request.url = s->base_url + path;
    for (size_t i = 0; i < query.size(); ++i)
        request.url += (i ? "&" : "?") + Escape(query[i].first) + "=" + Escape(query[i].second);
    if (!how.auth || keyless) {
        // The health pulse, or a keyless reader's world read: no credential at all.
    } else if (s->kind == OWKeyKind::Account) {
        request.headers.emplace_back("Authorization", "Bearer " + s->api_key);
    } else {
        request.headers.emplace_back("API-Key", s->api_key);
        // The PIN only where keel checks it: keel verifies a PIN wherever it is sent (argon2, 64 MiB each), and reads
        // sent with one exhausted a server's memory on a cold cache. On every write, on GET /me (the connection test),
        // and on every request of a 10-digit legacy key, which needs it on reads of a world with a PIN.
        bool me = path == "/me/" || path == "/me";
        if (!s->api_pin.empty() && (method != "GET" || me || s->kind == OWKeyKind::Legacy))
            request.headers.emplace_back("API-Pin", s->api_pin);
    }
    if (how.body) request.headers.emplace_back("Content-Type", "application/json");
    if (!how.idempotency_key.empty()) request.headers.emplace_back("Idempotency-Key", how.idempotency_key);
    // Presence is the declaration, the value is never stored: opt-in, and on the element writes alone, where keel's
    // document declares it. The client's name goes on the same writes, and on the three proposal routes keel declares it
    // on, and never on a read.
    if (s->agent && how.write) request.headers.emplace_back("X-OW-Agent", "1");
    if (how.write || how.named) request.headers.emplace_back("X-OW-Client", s->client_name);
    if (!how.if_none_match.empty()) request.headers.emplace_back("If-None-Match", how.if_none_match);
    if (how.if_match) request.headers.emplace_back("If-Match", "\"" + std::to_string(*how.if_match) + "\"");
    request.body = how.body;

    // The seam promises one answer; a second is dropped, so a walk never steps twice.
    auto answered = std::make_shared<bool>(false);
    bool not_modified = how.not_modified, proposable = how.proposable, allow_empty = how.allow_empty;
    s->transport->Send(std::move(request), [s, route, not_modified, proposable, allow_empty, answered,
                                            done](OWResponse response) {
        if (*answered) return;
        *answered = true;
        OWReply<OWJsonValue> r;
        if (response.status == 0 || !response.failure.empty()) {
            std::string why = response.failure.empty() ? "no answer" : response.failure;
            r.error = OWApiError::Of(OWApiError::Kind::Transport, "OnlyWorlds request failed: " + route + ": " + why);
        } else if (not_modified && response.status == 304) {
            // A 304 has no body: the caller that sent a tag reads the status.
        } else if (!response.IsSuccess()) {
            r.error = OWApiError::FromResponse(response, static_cast<int64_t>(std::time(nullptr)));
        } else if (proposable && response.status == 202) {
            r.error = Proposed(response, route);
        } else if (allow_empty && OnlyWhite(response.body)) {
            // A delete's 204: no value.
        } else {
            OWJsonParseResult parsed = OWJson::Parse(response.body);
            if (parsed.ok) {
                r.value = std::move(parsed.value);
            } else {
                r.error = OWApiError::Of(OWApiError::Kind::Answer,
                                         route + " answered " + std::to_string(response.status) +
                                             " with a body that is not JSON",
                                         response.status, response.body);
            }
        }
        r.response = std::move(response);
        done(std::move(r));
    });
}

// A reply whose value must be a JSON object: any other answer is an error of kind Answer.
OWReply<OWJsonValue> Object(OWReply<OWJsonValue> r, const std::string& route) {
    if (r.Ok() && !r.value.IsObject()) Bad(r, route + " answered a body that is not a JSON object");
    return r;
}

void ObjectRequest(const StatePtr& s, const std::string& path, const How& how, OWHandler<OWJsonValue> done) {
    std::string route = "GET " + path;
    Request(s, "GET", path, {}, how, [route, done](OWReply<OWJsonValue> r) { done(Object(std::move(r), route)); });
}

void ListOn(const StatePtr& s, const std::string& type, const OWListOptions& o, OWHandler<OWPage> done) {
    if (!s->error.empty()) return Refuse(done, "OWClient was not built: " + s->error + NothingSent);
    // Each typed parameter as (name, value), in order; an empty value is not sent.
    const Query typed = {
        {"limit", std::to_string(o.limit.value_or(s->page_size))},
        {"cursor", o.cursor},
        {"expand", Joined(o.expand)},
        {"fields", Joined(o.fields)},
        {"ids", Joined(o.ids)},
        {"created_by", o.created_by},
        {"name", o.name},
        {"name__icontains", o.name_contains},
        {"supertype", o.supertype},
        {"subtype", o.subtype},
    };
    Query query;
    for (const auto& p : typed)
        if (!p.second.empty()) query.push_back(p);
    // world is the client's, set when it is built: in a filter it would go out twice beside the config's world=, or to
    // a world key that 422s it.
    if (o.filter.count("world")) {
        return Refuse(done, "List: world is not a filter; an account token or no key names its world with "
                            "OWClientConfig::world." + std::string(NothingSent));
    }
    for (const auto& [field, value] : o.filter) {  // in field order (std::map's, byte order)
        if (!IsFieldName(field))
            return Refuse(done, "List: '" + field + "' is not a field name to filter on." + NothingSent);
        // A key a typed member sends here would go out twice, and keel reads one of the two.
        bool twice = (field == "as" && !o.view_as.empty()) ||
                     std::any_of(query.begin(), query.end(), [&](const auto& p) { return p.first == field; });
        if (twice) {
            return Refuse(done, "List: '" + field + "' is sent by its own option in this request; give it there or in "
                                "filter, not both." + NothingSent);
        }
        query.emplace_back(field, value);
    }
    if (!o.view_as.empty()) query.emplace_back("as", o.view_as);
    // The type one path segment (keel's {type}), checked and escaped, never a route or a query.
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    std::string route = "GET " + path;
    How how;
    how.world_read = true;
    Request(s, "GET", path, std::move(query), how, [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWPage>(Object(std::move(raw), route), [&](OWJsonValue& body, OWReply<OWPage>& r) {
            if (const OWJsonValue* rows = body.Find("data"); rows && !rows->IsNull()) {
                if (!rows->IsArray()) return Bad(r, route + " answered data that is not a list");
                for (OWJsonValue& row : body.Find("data")->Items()) {
                    if (!row.IsObject()) return Bad(r, route + " answered a row that is not an element");
                    r.value.data.push_back(std::move(row));
                }
            }
            const OWJsonValue* more = body.Find("has_more");
            r.value.has_more = more && more->IsBool() && more->AsBool();
            r.value.next_cursor = TextOf(body.Find("next_cursor")).value_or("");
        }));
    });
}

// The cursor walk: one page after another, each on the last page's cursor, by has_more. A transport that answers
// before Send returns would make each page a deeper call; the walk notices (`sending`) and loops instead, so a long
// world never deepens the stack.
struct Walk : std::enable_shared_from_this<Walk> {
    StatePtr state;
    std::string type;
    OWListOptions options;
    OWHandler<std::vector<OWJsonValue>> done;
    std::function<void(int, size_t)> on_page;
    std::vector<OWJsonValue> all;
    std::string cursor;
    int pages = 0;
    bool sending = false;
    bool again = false;

    void Run() {
        do {
            again = false;
            sending = true;
            options.cursor = cursor;
            auto self = shared_from_this();
            ListOn(state, type, options, [self](OWReply<OWPage> r) { self->Page(std::move(r)); });
            sending = false;
        } while (again);
    }

    void Page(OWReply<OWPage> r) {
        OWReply<std::vector<OWJsonValue>> out;
        out.response = r.response;
        if (!r.Ok()) {
            out.error = std::move(r.error);
            return done(std::move(out));
        }
        for (OWJsonValue& e : r.value.data) all.push_back(std::move(e));
        ++pages;
        if (on_page) on_page(pages, all.size());
        // has_more with no cursor is an end, rather than a loop.
        if (!r.value.has_more || r.value.next_cursor.empty()) {
            out.value = std::move(all);
            return done(std::move(out));
        }
        // A page that hands back the cursor it was sent (the caller's starting cursor included) would be read again
        // forever.
        if (r.value.next_cursor == cursor) {
            out.error = OWApiError::Of(OWApiError::Kind::Transport,
                                       "ListAll: GET /" + Escape(type) +
                                           "/ answered the cursor it was sent, with more to come; the walk stopped "
                                           "rather than read the same page again",
                                       r.response ? r.response->status : 0, r.response ? r.response->body : "");
            return done(std::move(out));
        }
        cursor = r.value.next_cursor;
        if (sending) again = true;
        else Run();
    }
};

void FeedPage(const OWJsonValue& body, OWReply<OWChangesPage>& r) {
    OWChangesPage& page = r.value;
    page.cursor = TextOf(body.Find("cursor")).value_or("");
    const OWJsonValue* more = body.Find("has_more");
    page.has_more = more && more->IsBool() && more->AsBool();
    page.head = IntOf(body.Find("head")).value_or(0);
    page.epoch = IntOf(body.Find("epoch"));
    if (const OWJsonValue* why = body.Find("epoch_reason"); why && why->IsString()) page.epoch_reason = why->Text();
    const OWJsonValue* rows = body.Find("changes");
    if (!rows || rows->IsNull()) return;
    if (!rows->IsArray()) return Bad(r, "GET /changes/ answered changes that are not a list");
    for (const OWJsonValue& row : rows->Items()) {
        if (!row.IsObject()) return Bad(r, "GET /changes/ answered a row that is not a change");
        OWChange c;
        c.op = TextOf(row.Find("op")).value_or("");
        c.type = TextOf(row.Find("type")).value_or("");
        c.id = TextOf(row.Find("id")).value_or("");
        c.change_seq = IntOf(row.Find("change_seq")).value_or(0);
        c.updated_at = TextOf(row.Find("updated_at"));
        c.changed_at = TextOf(row.Find("changed_at"));
        c.changed_by = TextOf(row.Find("changed_by"));
        c.changed_via = TextOf(row.Find("changed_via"));
        if (const OWJsonValue* e = row.Find("element"); e && e->IsObject()) c.element = *e;
        c.deleted_at = TextOf(row.Find("deleted_at"));
        c.deleted_by = TextOf(row.Find("deleted_by"));
        c.deleted_via = TextOf(row.Find("deleted_via"));
        c.deleted_client = TextOf(row.Find("deleted_client"));
        c.reason = TextOf(row.Find("reason"));
        page.changes.push_back(std::move(c));
    }
}

// A 409 resync_required from a feed read drops the tip read's tag, so the next tip read is a full 200.
void DropOnResync(const StatePtr& s, const OWReply<OWJsonValue>& r) {
    if (r.error && r.error->IsResyncRequired()) {
        s->head_tag.clear();
        s->head_held.reset();
    }
}

// -- The writes' helpers ---------------------------------------------------------------------------------------------------

// keel's rule for X-OW-Client, exact: lower case letters, digits and hyphens, 1 to 32 characters. Read byte by byte, as
// the Unity SDK reads it, so nothing past the name (a final newline) gets through.
bool IsClientName(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// A JSON value as JavaScript's JSON.stringify writes it, compact: keys in order, no white space, a number in section 5's
// spelling (OWJson::CanonicalNumber: an integer literal as written, any other the shortest digits that read back the
// same), a string escaped as OWJson::Quote escapes it. OWJson::Stringify is the folder spec's indented form.
void Compact(const OWJsonValue& v, std::string& out) {
    switch (v.kind()) {
        case OWJsonValue::Kind::Null: out += "null"; break;
        case OWJsonValue::Kind::Bool: out += v.AsBool() ? "true" : "false"; break;
        case OWJsonValue::Kind::Number: out += OWJson::CanonicalNumber(v.Text()); break;
        case OWJsonValue::Kind::String: out += OWJson::Quote(v.Text()); break;
        case OWJsonValue::Kind::Array:
            out.push_back('[');
            for (size_t i = 0; i < v.Size(); ++i) {
                if (i) out.push_back(',');
                Compact(v.Items()[i], out);
            }
            out.push_back(']');
            break;
        case OWJsonValue::Kind::Object:
            out.push_back('{');
            for (size_t i = 0; i < v.Size(); ++i) {
                if (i) out.push_back(',');
                out += OWJson::Quote(v.Keys()[i]);
                out.push_back(':');
                Compact(v.Items()[i], out);
            }
            out.push_back('}');
            break;
    }
}
std::string Compact(const OWJsonValue& v) {
    std::string out;
    Compact(v, out);
    return out;
}

// The deepest request body keel's reader takes, its top counted as 1 (it answers 422 past it).
constexpr int BodyMaxDepth = 256;

// A request body as the compact text sent, or "" with `why` set, in the TypeScript SDK's words (4.219.2 and 4.219.3),
// for the two bodies keel cannot read as written: one nested more than BodyMaxDepth deep, an object or array at the top
// at 1, the envelope of a bulk included (what keel stores is held to the standard's 200 counted from the element a value
// sits in, and keel refuses past that by name, so no value keel would store is refused here); and one holding a NaN or an
// infinity (OWJsonValue::NonFinite), named with its place (items[1].element.x_weight), which would be written null and
// clear the field without a word. A body with both is refused for its depth. Walked with a stack of its own, in the
// order Compact writes it, so Compact's own recursion is held to the line.
std::string BodyText(const OWJsonValue& body, std::string& why) {
    struct At {
        const OWJsonValue* value;
        int parent;
        std::string place;
    };
    std::string non_finite;
    std::vector<At> stack{{&body, 0, ""}};
    while (!stack.empty()) {
        At at = std::move(stack.back());
        stack.pop_back();
        const OWJsonValue& v = *at.value;
        if (v.IsObject() || v.IsArray()) {
            int depth = at.parent + 1;
            if (depth > BodyMaxDepth) {
                why = "a request body is nested more than " + std::to_string(BodyMaxDepth) +
                      " deep (an object or list at the top is depth 1), past what onlyworlds.com reads." + NothingSent;
                return "";
            }
            for (size_t i = v.Size(); i-- > 0;) {
                std::string place = v.IsArray() ? at.place + "[" + std::to_string(i) + "]"
                                    : at.place.empty() ? v.Keys()[i]
                                                       : at.place + "." + v.Keys()[i];
                stack.push_back({&v.Items()[i], depth, std::move(place)});
            }
        } else if (non_finite.empty() && v.NonFinite()) {
            non_finite = std::string(v.NonFinite()) + " at " + (at.place.empty() ? "the top" : at.place);
        }
    }
    if (!non_finite.empty()) {
        why = "a request body holds " + non_finite +
              ", which JSON cannot carry: send a finite number, or null to clear the field." + NothingSent;
        return "";
    }
    return Compact(body);
}

const char* KindName(const OWJsonValue& v) {
    switch (v.kind()) {
        case OWJsonValue::Kind::Null: return "null";
        case OWJsonValue::Kind::Bool: return "a boolean";
        case OWJsonValue::Kind::Number: return "a number";
        case OWJsonValue::Kind::String: return "a string";
        case OWJsonValue::Kind::Array: return "an array";
        case OWJsonValue::Kind::Object: return "an object";
    }
    return "";
}

// A whole finite number: an integer literal, or a number whose text reads as a double with no fraction (2.0, 1e3).
bool IsWholeNumber(const OWJsonValue& v) {
    if (!v.IsNumber()) return false;
    const std::string& text = v.Text();
    if (text.find_first_of(".eE") == std::string::npos) return true;
    double d = 0;
    auto res = std::from_chars(text.data(), text.data() + text.size(), d);
    return res.ec == std::errc() && std::isfinite(d) && std::floor(d) == d;
}

// A value as a refusal names it, the TypeScript SDK's shownValue (4.220.2): as Compact writes it, but a number that is
// not finite by its own name (NaN, Infinity, -Infinity: OWJsonValue::NonFinite), never the null JSON writes in its place.
std::string ShownValue(const OWJsonValue& v) {
    if (const char* own = v.NonFinite()) return own;
    return Compact(v);
}

// "" when every standard field of `element` holds its kind, else the refusal, in the TypeScript SDK's words (4.220.0's
// checkKinds, the Python package's check_kinds): keel takes a value of another kind with a 200 and changes it (2.5, "7"
// or true in an integer field stored as 2, 7 or 1, a list or a number in a text field as its text). An integer field
// (OWLens::FieldsOf, kind Int) takes a whole number, never a boolean; a text field (the base strings, kind Text, and the
// generic link's type key, kind GenericType, which the wire carries as text) a string; each or null. A null that was a
// NaN or an infinity (OWJsonValue::NonFinite) is no null here: the kind is checked first, as the TypeScript SDK checks
// it, and the value is named by ShownValue. The body's own keys only; an extension, a link field and a type outside the
// standard are not checked.
std::string KindsWhy(const std::string& where, const OWJsonValue& element, std::string_view type) {
    const std::vector<OWLens::OWLensField> fields = OWLens::FieldsOf(type);
    if (fields.empty()) return "";
    static const std::vector<std::string> base = {"name", "description", "supertype", "subtype", "image_url"};
    for (size_t i = 0; i < element.Size(); ++i) {
        const std::string& key = element.Keys()[i];
        const OWJsonValue& v = element.Items()[i];
        if (v.IsNull() && !v.NonFinite()) continue;
        bool integer = false, text = std::find(base.begin(), base.end(), key) != base.end();
        for (const OWLens::OWLensField& f : fields) {
            if (key != f.name) continue;
            integer = f.kind == OWLens::OWFieldKind::Int;
            text = f.kind == OWLens::OWFieldKind::Text || f.kind == OWLens::OWFieldKind::GenericType;
        }
        const std::string field = std::string(type) + "." + key;
        if (integer && !IsWholeNumber(v))
            return where + ": " + field + " is an integer field; " + ShownValue(v) + " would be stored as another value." +
                   NothingSent;
        if (text && !v.IsString())
            return where + ": " + field + " is a text field; " + ShownValue(v) + " is not a string." + NothingSent;
    }
    return "";
}

// The body of an element write: Sanitize's copy of `element`; else `why` says what is refused (a body that is not an
// object; a known-of stub, with the words of the Unity SDK's RefuseKnownOf and the TypeScript SDK's refuseKnownOf; a
// value of another kind, KindsWhy).
OWJsonValue WriteBody(const std::string& where, const OWJsonValue& element, std::string_view type, std::string& why) {
    if (!element.IsObject()) {
        why = where + ": the element is a JSON object; got " + KindName(element) + "." + NothingSent;
        return {};
    }
    if (OWClient::IsKnownOf(element)) {
        why = where + ": this body is a known-of stub (known_of: true), not the element: its empty fields are not its "
                      "values, and writing it would blank them. Write from a whole copy, or send only the fields you "
                      "mean to change." + NothingSent;
        return {};
    }
    why = KindsWhy(where, element, type);
    if (!why.empty()) return {};
    return OWClient::Sanitize(element, type);
}

// True when a body carries no id to keep: absent, null or "" (the TypeScript SDK's create).
bool NoId(const OWJsonValue& body) {
    const OWJsonValue* id = body.Find("id");
    return !id || id->IsNull() || (id->IsString() && id->Text().empty());
}

// An If-Match is an element's change_seq, a whole number; a negative one is refused before sending (the Unity SDK's
// RefuseNegative). "" when there is none to refuse.
std::string IfMatchWhy(const char* call, const OWWriteOptions& o) {
    if (!o.if_match || *o.if_match >= 0) return "";
    return std::string(call) + ": if_match must be a whole number, an element's change_seq; got " +
           std::to_string(*o.if_match) + "." + NothingSent;
}

std::string NotBuilt(const OWClient::State& s) { return "OWClient was not built: " + s.error + NothingSent; }

// An element write answering the element: the body as served, a JSON object.
void ElementWrite(const StatePtr& s, const std::string& method, const std::string& path, const How& how,
                  OWHandler<OWJsonValue> done) {
    std::string route = method + " " + path;
    Request(s, method, path, {}, how, [route, done](OWReply<OWJsonValue> r) { done(Object(std::move(r), route)); });
}

// keel's answer to POST /bulk/, read into its slots.
void BulkOf(const OWJsonValue& body, OWReply<OWBulkResult>& r) {
    const char* const route = "POST /bulk/";
    OWBulkResult& out = r.value;
    const OWJsonValue* errors = body.Find("errors");
    out.errors = errors && errors->IsBool() && errors->AsBool();
    const OWJsonValue* rows = body.Find("items");
    if (rows && !rows->IsNull()) {
        if (!rows->IsArray()) return Bad(r, std::string(route) + " answered items that are not a list");
        for (const OWJsonValue& row : rows->Items()) {
            if (!row.IsObject()) return Bad(r, std::string(route) + " answered a slot that is not an object");
            OWBulkSlot slot;
            slot.status = static_cast<int>(IntOf(row.Find("status")).value_or(0));
            slot.id = TextOf(row.Find("id"));
            slot.created_at = TextOf(row.Find("created_at"));
            slot.updated_at = TextOf(row.Find("updated_at"));
            if (const OWJsonValue* e = row.Find("error"); e && e->IsObject()) {
                OWResponse as_answer;
                as_answer.status = slot.status;
                as_answer.body = "{\"error\":" + Compact(*e) + "}";
                slot.error = OWApiError::FromResponse(as_answer, static_cast<int64_t>(std::time(nullptr)));
            }
            if (const OWJsonValue* p = row.Find("proposal"); p && p->IsObject()) slot.proposal = *p;
            out.items.push_back(std::move(slot));
        }
    }
    const std::string* replay = r.response ? r.response->Header("Idempotent-Replay") : nullptr;
    std::string word = replay ? *replay : "";
    for (char& c : word)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    out.was_replay = word == "true";
}

}  // namespace

const std::vector<std::string>& OWPage::Keys() {
    static const std::vector<std::string> keys = {"data", "has_more", "next_cursor"};
    return keys;
}
const std::vector<std::string>& OWChange::Keys() {
    static const std::vector<std::string> keys = {"op",         "type",        "id",          "change_seq",
                                                  "updated_at", "changed_at",  "changed_by",  "changed_via",
                                                  "element",    "deleted_at",  "deleted_by",  "deleted_via",
                                                  "deleted_client", "reason"};
    return keys;
}
const std::vector<std::string>& OWChangesPage::Keys() {
    static const std::vector<std::string> keys = {"cursor", "changes", "has_more", "head", "epoch", "epoch_reason"};
    return keys;
}

OWClient::OWClient(OWClientConfig config) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.kind = KindOf(config.api_key);
    bool keyless = s.kind == OWKeyKind::None;
    if (config.world) {
        if (OWUnicode::StripWhite(*config.world).empty()) {
            s.error = "world is a world's id, not empty or white space; got \"" + *config.world + "\". With it, an "
                      "account token or no key reads that world through " + WorldReads + ".";
        } else if (!keyless && s.kind != OWKeyKind::Account) {
            s.error = std::string("A world key reads its own world; world is for an account token (ow_a_) or no key, "
                                  "which read the world it names through ") + WorldReads + ".";
        }
    }
    if (s.error.empty() && !config.transport)
        s.error = "A transport is required: the core has no HTTP of its own (the plugin's sends through Unreal's).";
    s.api_key = std::move(config.api_key);
    s.api_pin = std::move(config.api_pin);
    s.world = std::move(config.world);
    s.base_url = std::move(config.base_url);
    while (!s.base_url.empty() && s.base_url.back() == '/') s.base_url.pop_back();
    s.page_size = config.page_size > 0 ? config.page_size : 100;
    s.transport = std::move(config.transport);
    s.agent = config.agent;
    s.client_name = config.client.value_or(DefaultClientName);
    if (s.error.empty() && config.client && !IsClientName(*config.client)) {
        s.error = "client must be lower case letters, digits and hyphens, 1 to 32 characters ([a-z0-9-]{1,32}), such "
                  "as 'atlas'; got " + OWJson::Quote(*config.client) + ". It is sent as X-OW-Client on element "
                  "writes; leave it absent for '" + std::string(DefaultClientName) + "'.";
    }
}

OWClient::OWClient(OWClient&&) noexcept = default;
OWClient& OWClient::operator=(OWClient&&) noexcept = default;
OWClient::~OWClient() = default;

const std::string& OWClient::Error() const { return state_->error; }
const std::string& OWClient::BaseUrl() const { return state_->base_url; }
int64_t OWClient::PageSize() const { return state_->page_size; }
OWKeyKind OWClient::KeyKind() const { return state_->kind; }
const std::optional<std::string>& OWClient::World() const { return state_->world; }

OWKeyKind OWClient::KindOf(std::string_view key) {
    if (key.empty()) return OWKeyKind::None;
    if (key.rfind("ow_w_", 0) == 0) return OWKeyKind::Write;
    if (key.rfind("ow_r_", 0) == 0) return OWKeyKind::Read;
    if (key.rfind("ow_a_", 0) == 0) return OWKeyKind::Account;
    if (key.size() == 10 && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return OWKeyKind::Legacy;
    return OWKeyKind::Unknown;
}

void OWClient::Health(OWHandler<OWJsonValue> done) {
    How how;
    how.auth = false;
    ObjectRequest(state_, "/health/", how, std::move(done));
}

void OWClient::GetWorld(OWHandler<OWJsonValue> done) {
    How how;
    how.world_read = true;
    ObjectRequest(state_, "/world/", how, std::move(done));
}

void OWClient::Me(OWHandler<OWJsonValue> done) { ObjectRequest(state_, "/me/", How{}, std::move(done)); }

void OWClient::Members(OWHandler<OWJsonValue> done) {
    How how;
    how.token_world = true;
    ObjectRequest(state_, "/members/", how, [done](OWReply<OWJsonValue> r) {
        if (r.Ok()) {
            const OWJsonValue* rows = r.value.Find("data");
            r.value = rows && rows->IsArray() ? *rows : OWJsonValue::Array();
        }
        done(std::move(r));
    });
}

void OWClient::List(const std::string& type, const OWListOptions& options, OWHandler<OWPage> done) {
    ListOn(state_, type, options, std::move(done));
}

void OWClient::ListAll(const std::string& type, const OWListOptions& options, OWHandler<std::vector<OWJsonValue>> done,
                       std::function<void(int, size_t)> on_page) {
    auto walk = std::make_shared<Walk>();
    walk->state = state_;
    walk->type = type;
    walk->options = options;
    walk->done = std::move(done);
    walk->on_page = std::move(on_page);
    walk->cursor = options.cursor;
    walk->Run();
}

void OWClient::Get(const std::string& type, const std::string& id, const OWGetOptions& options,
                   OWHandler<OWJsonValue> done) {
    Query query;
    if (!options.expand.empty()) query.emplace_back("expand", Joined(options.expand));
    if (!options.fields.empty()) query.emplace_back("fields", Joined(options.fields));
    if (!options.view_as.empty()) query.emplace_back("as", options.view_as);
    // The type and the id each one path segment (keel's {type} and {id}), checked and escaped: never a route or a
    // query.
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    std::string route = "GET " + path;
    How how;
    how.world_read = true;
    Request(state_, "GET", path, std::move(query), how,
            [route, done](OWReply<OWJsonValue> r) { done(Object(std::move(r), route)); });
}

void OWClient::Changes(const OWChangesOptions& options, OWHandler<OWChangesPage> done) {
    Query query;
    if (!options.since.empty()) query.emplace_back("since", options.since);
    query.emplace_back("limit", std::to_string(std::min(state_->page_size, MaxChangesLimit)));
    if (!options.after.empty()) query.emplace_back("after", options.after);
    StatePtr s = state_;
    Request(s, "GET", "/changes/", std::move(query), How{}, [s, done](OWReply<OWJsonValue> raw) {
        DropOnResync(s, raw);
        done(Then<OWChangesPage>(Object(std::move(raw), "GET /changes/"),
                                 [](OWJsonValue& body, OWReply<OWChangesPage>& r) { FeedPage(body, r); }));
    });
}

void OWClient::ChangesHead(OWHandler<OWChangesPage> done) {
    StatePtr s = state_;
    // What was held when the read was sent: a 304 answers that tag's tip.
    std::optional<OWChangesPage> held = s->head_held;
    How how;
    how.if_none_match = s->head_tag;
    how.not_modified = true;
    Request(s, "GET", "/changes/", {{"head", "true"}}, how, [s, held, done](OWReply<OWJsonValue> raw) {
        if (raw.Ok() && raw.response && raw.response->status == 304) {
            OWReply<OWChangesPage> r;
            r.response = std::move(raw.response);
            if (!held) {
                r.error = OWApiError::Of(OWApiError::Kind::Api,
                                         "GET /changes?head=true answered 304 Not Modified while nothing is held: no tip "
                                         "read has answered 200 on this client, so there is no head to return",
                                         304, r.response->body);
                return done(std::move(r));
            }
            r.value.cursor = held->cursor;
            r.value.head = held->head;
            r.value.epoch = held->epoch;
            r.value.unchanged = true;
            return done(std::move(r));
        }
        DropOnResync(s, raw);
        const OWResponse* response = raw.response ? &*raw.response : nullptr;
        std::string tag = response && response->Header("ETag") ? *response->Header("ETag") : "";
        OWReply<OWChangesPage> r = Then<OWChangesPage>(Object(std::move(raw), "GET /changes/?head=true"),
                                                       [](OWJsonValue& body, OWReply<OWChangesPage>& out) {
                                                           FeedPage(body, out);
                                                       });
        if (r.Ok()) {
            s->head_tag = tag;
            OWChangesPage keep;
            keep.cursor = r.value.cursor;
            keep.head = r.value.head;
            keep.epoch = r.value.epoch;
            s->head_held = std::move(keep);
        }
        done(std::move(r));
    });
}

// -- Joining by link -------------------------------------------------------------------------------------------------

void OWClient::AcceptJoin(const std::string& code, OWHandler<OWJoinedMembership> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    if (state_->kind != OWKeyKind::Account) {
        const char* held = "none";
        switch (state_->kind) {
            case OWKeyKind::Write: held = "a write key"; break;
            case OWKeyKind::Read: held = "a read key"; break;
            case OWKeyKind::Legacy: held = "a legacy key"; break;
            case OWKeyKind::Unknown: held = "a key of no known shape"; break;
            default: break;
        }
        return Refuse(done, std::string("AcceptJoin needs an account token (ow_a_); this client holds ") + held +
                                ". A world key cannot join a world; a player's or an agent's code is redeemed with no "
                                "key, OWJoin::Redeem." + NothingSent);
    }
    if (OWMessage::Trim(code).empty()) {
        return Refuse(done, std::string("AcceptJoin: a join code is required (OWJoin::ParseLink reads it out of a pasted "
                                        "link).") + NothingSent);
    }
    // An account route: the Bearer and the media type, never the PIN, no X-OW-* (no element is written), no world=.
    How how;
    how.body = "{\"code\":" + OWJson::Quote(code) + "}";
    const std::string route = "POST /account/join/";
    Request(state_, "POST", "/account/join/", {}, how, [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWJoinedMembership>(Object(std::move(raw), route), [](OWJsonValue& body, OWReply<OWJoinedMembership>& r) {
            r.value = OWJoin::MembershipOf(body);
        }));
    });
}

// OWJoin's two keyless calls are made here, through Request, so the core keeps one request builder and one path scan
// (tests/test_path_segments.cpp). Each goes on a bare state of its own: no key, no world, the config's transport and
// API root; `auth` off, so no API-Key, API-Pin or Bearer and no client refused for its key (keel declares no security
// on either route), and no element write, so no X-OW-*.
namespace {

// "" when the call may be sent; else the refusal.
std::string JoinWhy(const char* call, const std::string& code, const OWJoinConfig& where) {
    if (OWMessage::Trim(code).empty()) {
        return std::string(call) + ": a join code is required (the part of the link after #: OWJoin::ParseLink reads it "
                                   "out of a pasted link)." + NothingSent;
    }
    if (!where.transport) {
        return std::string(call) + ": a transport is required: the core has no HTTP of its own (the plugin's sends "
                                   "through Unreal's)." + NothingSent;
    }
    return "";
}

StatePtr Keyless(const OWJoinConfig& where) {
    auto s = std::make_shared<OWClient::State>();
    s->base_url = where.base_url;
    while (!s->base_url.empty() && s->base_url.back() == '/') s->base_url.pop_back();
    if (s->base_url.empty()) s->base_url = OWClient::DefaultBaseUrl;
    s->transport = where.transport;
    return s;
}

}  // namespace

void OWJoin::Preview(const std::string& code, const OWJoinConfig& where, OWHandler<OWJoinPreview> done) {
    std::string why = JoinWhy("OWJoin::Preview", code, where);
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.auth = false;
    how.body = "{\"code\":" + OWJson::Quote(code) + "}";
    const std::string route = "POST /join/preview/";
    Request(Keyless(where), "POST", "/join/preview/", {}, how, [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWJoinPreview>(Object(std::move(raw), route), [](OWJsonValue& body, OWReply<OWJoinPreview>& r) {
            r.value = OWJoin::PreviewOf(body);
        }));
    });
}

void OWJoin::Redeem(const std::string& code, const OWRedeemOptions& options, const OWJoinConfig& where,
                    OWHandler<OWJoined> done) {
    std::string why = JoinWhy("OWJoin::Redeem", code, where);
    if (!why.empty()) return Refuse(done, why);
    // In the order keel's tests and the TypeScript SDK send them; each only when set.
    OWJsonValue body = OWJsonValue::Object();
    body.Set("code", OWJsonValue::String(code));
    if (options.name) body.Set("name", OWJsonValue::String(*options.name));
    if (options.agent_name) body.Set("agent_name", OWJsonValue::String(*options.agent_name));
    if (options.character) body.Set("character", OWJsonValue::String(*options.character));
    How how;
    how.auth = false;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    const std::string route = "POST /join/";
    Request(Keyless(where), "POST", "/join/", {}, how, [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWJoined>(Object(std::move(raw), route), [](OWJsonValue& answer, OWReply<OWJoined>& r) {
            r.value = OWJoin::JoinedOf(answer);
        }));
    });
}

// -- Element writes ---------------------------------------------------------------------------------------------------

const char* const OWClient::DefaultClientName = "unreal-sdk";
const std::string& OWClient::ClientName() const { return state_->client_name; }
bool OWClient::Agent() const { return state_->agent; }

const std::vector<std::string>& OWClient::ReadOnlyFields() {
    static const std::vector<std::string> fields = {"world", "type", "created_at", "updated_at", "change_seq", "known_of"};
    return fields;
}

bool OWClient::IsKnownOf(const OWJsonValue& body) {
    const OWJsonValue* marker = body.IsObject() ? body.Find("known_of") : nullptr;
    return marker && marker->IsBool() && marker->AsBool();
}

OWJsonValue OWClient::Sanitize(const OWJsonValue& payload, std::string_view type) {
    if (!payload.IsObject()) return payload;
    // The base's scalar strings (schema.json's base, id and world aside: an id's null is kept, world is left out) and
    // the type's own (OWLens::FieldsOf, kind Text: schema.json's scalar_str), the Unity SDK's IsStringField.
    static const std::vector<std::string> base = {"name", "description", "supertype", "subtype", "image_url"};
    std::vector<std::string> strings = base;
    for (const OWLens::OWLensField& f : OWLens::FieldsOf(type))
        if (f.kind == OWLens::OWFieldKind::Text) strings.emplace_back(f.name);
    OWJsonValue body = payload;
    for (const std::string& f : ReadOnlyFields()) body.Erase(f);
    for (size_t i = 0; i < body.Size(); ++i) {
        // string-empty-is-unset: "" is the wire's unset, and keel has no null to store. A null that was a NaN or an
        // infinity is kept, for the body's own refusal (BodyText) to name.
        if (body.Items()[i].IsNull() && !body.Items()[i].NonFinite() && std::find(strings.begin(), strings.end(), body.Keys()[i]) != strings.end())
            body.Items()[i] = OWJsonValue::String("");
    }
    return body;
}

const std::vector<std::string>& OWBulkSlot::Keys() {
    static const std::vector<std::string> keys = {"status", "id", "created_at", "updated_at", "error", "proposal"};
    return keys;
}
const std::vector<std::string>& OWBulkResult::Keys() {
    static const std::vector<std::string> keys = {"errors", "items"};
    return keys;
}
std::vector<size_t> OWBulkResult::Failed() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < items.size(); ++i)
        if (!items[i].Succeeded()) out.push_back(i);
    return out;
}
size_t OWBulkResult::SucceededCount() const { return items.size() - Failed().size(); }
bool OWBulkResult::AllSucceeded() const { return !errors && Failed().empty(); }

void OWClient::Create(const std::string& type, const OWJsonValue& element, const OWCreateOptions& options,
                      OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why;
    OWJsonValue body = WriteBody("create", element, type, why);
    if (!why.empty()) return Refuse(done, why);
    // An id minted here, so a retry can name the element: keyed under a key, so the retry sends the same body.
    if (NoId(body)) body.Set("id", OWJsonValue::String(OWId::Mint(options.idempotency_key, 0)));
    std::string path = "/" + Seg("type", type, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.write = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    how.idempotency_key = options.idempotency_key;
    ElementWrite(state_, "POST", path, how, std::move(done));
}

void OWClient::Upsert(const std::string& type, const std::string& id, const OWJsonValue& element,
                      OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why;
    OWJsonValue body = WriteBody("upsert", element, type, why);
    if (!why.empty()) return Refuse(done, why);
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.write = true;
    how.proposable = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    ElementWrite(state_, "PUT", path, how, std::move(done));
}

void OWClient::Patch(const std::string& type, const std::string& id, const OWJsonValue& partial,
                     const OWWriteOptions& options, OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why = IfMatchWhy("patch", options);
    if (!why.empty()) return Refuse(done, why);
    OWJsonValue body = WriteBody("patch", partial, type, why);
    if (!why.empty()) return Refuse(done, why);
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.write = true;
    how.proposable = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    how.if_match = options.if_match;
    ElementWrite(state_, "PATCH", path, how, std::move(done));
}

void OWClient::Delete(const std::string& type, const std::string& id, const OWWriteOptions& options,
                      OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why = IfMatchWhy("delete", options);
    if (!why.empty()) return Refuse(done, why);
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/";
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.write = true;
    how.allow_empty = true;
    how.if_match = options.if_match;
    Request(state_, "DELETE", path, {}, how, [done](OWReply<OWJsonValue> r) {
        if (r.Ok()) r.value = OWJsonValue();
        done(std::move(r));
    });
}

void OWClient::EditLinks(const std::string& type, const std::string& id, const std::string& field,
                         const OWLinkEdit& edit, const OWWriteOptions& options, OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why = IfMatchWhy("edit links", options);
    if (!why.empty()) return Refuse(done, why);
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/links/" + Seg("field", field, why);
    if (!why.empty()) return Refuse(done, why);
    OWJsonValue body = OWJsonValue::Object();
    for (auto [name, ids] : {std::make_pair("add", &edit.add), std::make_pair("remove", &edit.remove)}) {
        if (!*ids) continue;
        OWJsonValue list = OWJsonValue::Array();
        for (const std::string& one : **ids) list.Push(OWJsonValue::String(one));
        body.Set(name, std::move(list));
    }
    How how;
    how.write = true;
    how.proposable = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    how.if_match = options.if_match;
    ElementWrite(state_, "POST", path, how, std::move(done));
}

void OWClient::Bulk(const std::vector<OWBulkItem>& items, const OWBulkOptions& options, OWHandler<OWBulkResult> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    if (items.empty()) return Refuse(done, std::string("bulk: needs at least one item.") + NothingSent);
    OWJsonValue list = OWJsonValue::Array();
    for (size_t i = 0; i < items.size(); ++i) {
        std::string where = "bulk: items[" + std::to_string(i) + "]";
        if (items[i].type.empty()) return Refuse(done, where + " needs a type." + NothingSent);
        std::string why;
        // A null element is an empty one, as the Unity SDK's BulkAsync reads it.
        OWJsonValue element = WriteBody(where, items[i].element.IsNull() ? OWJsonValue::Object() : items[i].element,
                                        items[i].type, why);
        if (!why.empty()) return Refuse(done, why);
        if (NoId(element))
            element.Set("id", OWJsonValue::String(OWId::Mint(options.idempotency_key, static_cast<int64_t>(i))));
        OWJsonValue slot = OWJsonValue::Object();
        slot.Set("type", OWJsonValue::String(items[i].type));
        slot.Set("element", std::move(element));
        list.Push(std::move(slot));
    }
    OWJsonValue body = OWJsonValue::Object();
    body.Set("items", std::move(list));
    body.Set("atomic", OWJsonValue::Bool(options.atomic));
    if (options.create_only) body.Set("create_only", OWJsonValue::Bool(true));
    std::string why;
    How how;
    how.write = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    how.idempotency_key = options.idempotency_key;
    Request(state_, "POST", "/bulk/", {}, how, [done](OWReply<OWJsonValue> raw) {
        done(Then<OWBulkResult>(Object(std::move(raw), "POST /bulk/"),
                                [](OWJsonValue& answer, OWReply<OWBulkResult>& r) { BulkOf(answer, r); }));
    });
}


// -- Proposed changes ------------------------------------------------------------------------------------------------

namespace {

// A JSON value keel serves, as itself; absent for null or no key.
std::optional<OWJsonValue> ValueOf(const OWJsonValue* v) {
    if (!v || v->IsNull()) return std::nullopt;
    return *v;
}

OWJsonValue TextOrNull(const std::optional<std::string>& s) { return s ? OWJsonValue::String(*s) : OWJsonValue(); }
OWJsonValue ValueOrNull(const std::optional<OWJsonValue>& v) { return v ? *v : OWJsonValue(); }

// A JSON value as text: a string as itself, any other value its compact JSON.
std::optional<std::string> AsText(const std::optional<OWJsonValue>& v) {
    if (!v) return std::nullopt;
    return v->IsString() ? v->Text() : Compact(*v);
}

// A proposal list's query, in the Unity SDK's order: state, since, element, limit (the page size when absent), cursor.
Query ProposalQueryOf(const OWProposalQuery& q, int64_t page_size) {
    Query out;
    if (!q.state.empty()) out.emplace_back("state", q.state);
    if (!q.since.empty()) out.emplace_back("since", q.since);
    if (!q.element.empty()) out.emplace_back("element", q.element);
    out.emplace_back("limit", std::to_string(q.limit.value_or(page_size)));
    if (!q.cursor.empty()) out.emplace_back("cursor", q.cursor);
    return out;
}

// A list's request: the query's tag sent as If-None-Match exactly as given, and a 304 read as an answer only when a tag
// was sent (else it is the non-2xx it is: an error of kind Api, status 304).
How ListHow(const OWProposalQuery& q) {
    How how;
    how.if_none_match = q.if_none_match;
    how.not_modified = !q.if_none_match.empty();
    return how;
}

// A list's answer: {data, has_more, next_cursor} read into rows, the ETag beside it; a 304 to a tagged call is
// unchanged, with no rows.
OWHandler<OWJsonValue> PageAnswer(std::string route, OWHandler<OWProposalPage> done) {
    return [route, done](OWReply<OWJsonValue> raw) {
        std::optional<std::string> etag;
        if (raw.response)
            if (const std::string* tag = raw.response->Header("ETag")) etag = *tag;
        if (raw.Ok() && raw.response && raw.response->status == 304) {
            OWReply<OWProposalPage> r;
            r.response = std::move(raw.response);
            r.value.unchanged = true;
            r.value.etag = etag;
            return done(std::move(r));
        }
        OWReply<OWProposalPage> r =
            Then<OWProposalPage>(Object(std::move(raw), route), [&](OWJsonValue& body, OWReply<OWProposalPage>& out) {
                if (const OWJsonValue* rows = body.Find("data"); rows && !rows->IsNull()) {
                    if (!rows->IsArray()) return Bad(out, route + " answered data that is not a list");
                    for (const OWJsonValue& row : rows->Items()) {
                        std::optional<OWProposal> p = OWProposal::Of(row);
                        if (!p) return Bad(out, route + " answered a row that is not a proposal");
                        out.value.data.push_back(std::move(*p));
                    }
                }
                const OWJsonValue* more = body.Find("has_more");
                out.value.has_more = more && more->IsBool() && more->AsBool();
                out.value.next_cursor = TextOf(body.Find("next_cursor")).value_or("");
            });
        if (r.Ok()) r.value.etag = etag;
        done(std::move(r));
    };
}

// An answer whose `proposal` is the row (a propose, a decline): the row, else an error of kind Answer.
OWHandler<OWJsonValue> RowAnswer(std::string route, OWHandler<OWProposal> done) {
    return [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWProposal>(Object(std::move(raw), route), [&](OWJsonValue& body, OWReply<OWProposal>& r) {
            std::optional<OWProposal> row = OWProposal::Of(body.Find("proposal") ? *body.Find("proposal") : OWJsonValue());
            if (!row) return Bad(r, route + " answered no proposal");
            r.value = std::move(*row);
        }));
    };
}

}  // namespace

bool OWProposal::IsNewElement() const { return kind == KindNewElement; }
std::optional<std::string> OWProposal::EvidenceText() const { return AsText(evidence); }
std::optional<std::string> OWProposal::ExpiresText() const { return AsText(expires); }

const std::vector<std::string>& OWProposal::Keys() {
    static const std::vector<std::string> keys = {
        "kind",     "id",       "element_type", "element_id", "house",      "state",          "fields",
        "omitted",  "base",     "base_seq",     "evidence",   "expires",    "by",             "via",
        "client",   "decided_by", "decided_at", "decline_reason", "created_at", "updated_at"};
    return keys;
}

std::optional<OWProposal> OWProposal::Of(const OWJsonValue& row) {
    if (!row.IsObject()) return std::nullopt;
    OWProposal p;
    p.kind = TextOf(row.Find("kind")).value_or("");
    p.id = TextOf(row.Find("id")).value_or("");
    p.element_type = TextOf(row.Find("element_type")).value_or("");
    p.element_id = TextOf(row.Find("element_id")).value_or("");
    p.house = TextOf(row.Find("house"));
    p.state = TextOf(row.Find("state")).value_or("");
    if (const OWJsonValue* f = row.Find("fields"); f && f->IsObject()) p.fields = *f;
    if (const OWJsonValue* o = row.Find("omitted"); o && o->IsArray()) {
        p.omitted.emplace();
        for (const OWJsonValue& name : o->Items())
            if (name.IsString()) p.omitted->push_back(name.Text());
    }
    if (const OWJsonValue* b = row.Find("base"); b && b->IsObject()) p.base = *b;
    p.base_seq = IntOf(row.Find("base_seq")).value_or(0);
    p.evidence = ValueOf(row.Find("evidence"));
    p.expires = ValueOf(row.Find("expires"));
    p.by = TextOf(row.Find("by"));
    p.via = TextOf(row.Find("via"));
    p.client = TextOf(row.Find("client"));
    p.decided_by = TextOf(row.Find("decided_by"));
    p.decided_at = TextOf(row.Find("decided_at"));
    p.decline_reason = TextOf(row.Find("decline_reason"));
    p.created_at = TextOf(row.Find("created_at"));
    p.updated_at = TextOf(row.Find("updated_at"));
    return p;
}

OWJsonValue OWProposal::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("kind", kind.empty() ? OWJsonValue() : OWJsonValue::String(kind));
    out.Set("id", OWJsonValue::String(id));
    out.Set("element_type", OWJsonValue::String(element_type));
    out.Set("element_id", OWJsonValue::String(element_id));
    out.Set("house", TextOrNull(house));
    out.Set("state", OWJsonValue::String(state));
    out.Set("fields", fields);
    OWJsonValue names;
    if (omitted) {
        names = OWJsonValue::Array();
        for (const std::string& name : *omitted) names.Push(OWJsonValue::String(name));
    }
    out.Set("omitted", std::move(names));
    out.Set("base", base);
    out.Set("base_seq", OWJsonValue::Number(base_seq));
    out.Set("evidence", ValueOrNull(evidence));
    out.Set("expires", ValueOrNull(expires));
    out.Set("by", TextOrNull(by));
    out.Set("via", TextOrNull(via));
    out.Set("client", TextOrNull(client));
    out.Set("decided_by", TextOrNull(decided_by));
    out.Set("decided_at", TextOrNull(decided_at));
    out.Set("decline_reason", TextOrNull(decline_reason));
    out.Set("created_at", TextOrNull(created_at));
    out.Set("updated_at", TextOrNull(updated_at));
    return out;
}

const std::vector<std::string>& OWProposalPage::Keys() {
    static const std::vector<std::string> keys = {"data", "has_more", "next_cursor"};
    return keys;
}
const std::vector<std::string>& OWProposalAccepted::Keys() {
    static const std::vector<std::string> keys = {"proposal", "element"};
    return keys;
}
const std::vector<std::string>& OWReviewed::Keys() {
    static const std::vector<std::string> keys = {"element", "deleted"};
    return keys;
}

void OWClient::Propose(const std::string& type, const std::string& id, const OWJsonValue& fields,
                       const std::string& evidence, const OWProposeOptions& options, OWHandler<OWProposal> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    if (!fields.IsObject()) {
        return Refuse(done, std::string("propose: fields must be an object of the fields to change and their new "
                                        "values, as in a PATCH body.") + NothingSent);
    }
    std::string why;
    OWJsonValue cleaned = WriteBody("propose", fields, type, why);
    if (!why.empty()) return Refuse(done, why);
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/proposals";
    if (!why.empty()) return Refuse(done, why);
    OWJsonValue body = OWJsonValue::Object();
    body.Set("fields", std::move(cleaned));
    body.Set("evidence", OWJsonValue::String(evidence));
    if (!options.expires.empty()) body.Set("expires", OWJsonValue::String(options.expires));
    How how;
    how.named = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    how.idempotency_key = options.idempotency_key;
    Request(state_, "POST", path, {}, how, RowAnswer("POST " + path, std::move(done)));
}

void OWClient::ListProposals(const std::string& type, const std::string& id, const OWProposalQuery& query,
                             OWHandler<OWProposalPage> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    if (!query.element.empty()) {
        return Refuse(done, std::string("ListProposals: element filters the world's queue (WorldProposals); one "
                                        "element's list is already one element's.") + NothingSent);
    }
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/proposals";
    if (!why.empty()) return Refuse(done, why);
    Request(state_, "GET", path, ProposalQueryOf(query, state_->page_size), ListHow(query),
            PageAnswer("GET " + path, std::move(done)));
}

void OWClient::WorldProposals(const OWProposalQuery& query, OWHandler<OWProposalPage> done) {
    std::string path = "/world/proposals";
    Request(state_, "GET", path, ProposalQueryOf(query, state_->page_size), ListHow(query),
            PageAnswer("GET " + path, std::move(done)));
}

void OWClient::AcceptProposal(const std::string& type, const std::string& id, const std::string& pid,
                              OWHandler<OWProposalAccepted> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/proposals/" + Seg("pid", pid, why) + "/accept";
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.named = true;
    const std::string route = "POST " + path;
    Request(state_, "POST", path, {}, how, [route, done](OWReply<OWJsonValue> raw) {
        done(Then<OWProposalAccepted>(Object(std::move(raw), route), [&](OWJsonValue& body, OWReply<OWProposalAccepted>& r) {
            std::optional<OWProposal> row = OWProposal::Of(body.Find("proposal") ? *body.Find("proposal") : OWJsonValue());
            const OWJsonValue* element = body.Find("element");
            if (!row) return Bad(r, route + " answered no proposal");
            if (!element || !element->IsObject()) return Bad(r, route + " answered no element");
            r.value.proposal = std::move(*row);
            r.value.element = *element;
        }));
    });
}

void OWClient::DeclineProposal(const std::string& type, const std::string& id, const std::string& pid,
                               const std::optional<std::string>& reason, OWHandler<OWProposal> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/proposals/" + Seg("pid", pid, why) + "/decline";
    if (!why.empty()) return Refuse(done, why);
    How how;
    if (reason) {
        OWJsonValue body = OWJsonValue::Object();
        body.Set("reason", OWJsonValue::String(*reason));
        how.body = BodyText(body, why);
    }
    Request(state_, "POST", path, {}, how, RowAnswer("POST " + path, std::move(done)));
}

void OWClient::WithdrawProposal(const std::string& type, const std::string& id, const std::string& pid,
                                OWHandler<OWJsonValue> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/proposals/" + Seg("pid", pid, why);
    if (!why.empty()) return Refuse(done, why);
    How how;
    how.allow_empty = true;
    Request(state_, "DELETE", path, {}, how, [done](OWReply<OWJsonValue> r) {
        if (r.Ok()) r.value = OWJsonValue();
        done(std::move(r));
    });
}

void OWClient::ReviewNewElement(const std::string& type, const std::string& id, const std::string& decision,
                                const std::optional<std::string>& reason, OWHandler<OWReviewed> done) {
    if (!state_->error.empty()) return Refuse(done, NotBuilt(*state_));
    const bool accept = decision == OWReviewed::Accept;
    if (!accept && decision != OWReviewed::Decline) {
        return Refuse(done, "ReviewNewElement: decision must be 'accept' or 'decline', got " + OWJson::Quote(decision) +
                                "." + NothingSent);
    }
    // keel takes {decision: accept} or {decision: decline, reason?}: a reason with an accept is its 422.
    if (reason && accept)
        return Refuse(done, std::string("ReviewNewElement: a reason goes with a decline only.") + NothingSent);
    std::string why;
    std::string path = "/" + Seg("type", type, why) + "/" + Seg("id", id, why) + "/review";
    if (!why.empty()) return Refuse(done, why);
    OWJsonValue body = OWJsonValue::Object();
    body.Set("decision", OWJsonValue::String(decision));
    if (reason) body.Set("reason", OWJsonValue::String(*reason));
    How how;
    how.named = true;
    how.body = BodyText(body, why);
    if (!why.empty()) return Refuse(done, why);
    const std::string route = "POST " + path;
    Request(state_, "POST", path, {}, how, [route, accept, done](OWReply<OWJsonValue> raw) {
        done(Then<OWReviewed>(Object(std::move(raw), route), [&](OWJsonValue& answer, OWReply<OWReviewed>& r) {
            if (const OWJsonValue* e = answer.Find("element"); e && e->IsObject()) r.value.element = *e;
            if (const OWJsonValue* d = answer.Find("deleted"); d && d->IsString()) r.value.deleted = d->Text();
            // keel answers exactly one of the two; a 200 without the one this decision settles is never read as settled.
            if (accept ? !r.value.element : !r.value.deleted) {
                int status = r.response ? r.response->status : 0;
                Bad(r, route + " answered " + std::to_string(status) + " without its " + (accept ? "element" : "deleted") +
                           ": the review's outcome is unknown");
            }
        }));
    });
}

}  // namespace OnlyWorlds
