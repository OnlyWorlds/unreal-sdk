// A caller's string never changes which route is asked: every value OWClient puts in a request path (the element type,
// the id) is checked as one segment by Seg() and refused before sending when it is not one, and what passes is sent
// escaped, as the TypeScript SDK does since 4.123.3. keel decodes an escaped slash before it routes (measured on
// production: /character/abc%2Fhidden/ answers as /character/abc/hidden/ does), so the escape alone cannot hold a
// slash. Two halves, as the TypeScript SDK's test/path-segments.test.mjs: the source scan reads every request path the
// client builds, so a new route that puts a value in unchecked, or builds its path some way the scan cannot read, fails
// here; the calls show that a value that is not one segment is refused with nothing sent, and that one that is goes out
// as given.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;
using owtest::P;

namespace {

const std::string Base = "https://example.test/api/v2";
const std::string CoreDir = "/Plugin/OnlyWorlds/Core";
const std::string ClientSource = CoreDir + "/Private/OWClient.cpp";

std::string ReadText(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool IsIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The end of the string or char literal that opens at `i` (one past its closing quote).
size_t LiteralEnd(const std::string& s, size_t i) {
    char q = s[i];
    for (++i; i < s.size() && s[i] != q; ++i)
        if (s[i] == '\\') ++i;
    return i < s.size() ? i + 1 : s.size();
}

// The source with its comments blanked (newlines kept), its literals as written.
std::string Uncommented(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '"' || s[i] == '\'') {
            size_t e = LiteralEnd(s, i);
            out.append(s, i, e - i);
            i = e;
        } else if (s.compare(i, 2, "//") == 0) {
            while (i < s.size() && s[i] != '\n') ++i;
        } else if (s.compare(i, 2, "/*") == 0) {
            size_t e = s.find("*/", i + 2);
            e = e == std::string::npos ? s.size() : e + 2;
            for (size_t k = i; k < e; ++k)
                if (s[k] == '\n') out.push_back('\n');
            i = e;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

// The parts of `s` split at `sep` where no bracket is open and no literal runs.
std::vector<std::string> SplitTop(const std::string& s, char sep) {
    std::vector<std::string> parts;
    int depth = 0;
    size_t from = 0;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            i = LiteralEnd(s, i);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == sep && depth == 0) {
            parts.push_back(Trim(s.substr(from, i - from)));
            from = i + 1;
        }
        ++i;
    }
    parts.push_back(Trim(s.substr(from)));
    return parts;
}

// The text between the '(' at `open` and its matching ')'.
std::string Inside(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size();) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            i = LiteralEnd(s, i);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if ((c == ')' || c == ']' || c == '}') && --depth == 0) return s.substr(open + 1, i - open - 1);
        ++i;
    }
    return s.substr(open + 1);
}

// A top-level block of the source outside the namespaces: a function, a struct, an initializer; its header is the text
// before its brace.
struct Unit {
    std::string header;
    size_t begin = 0, end = 0;  // the body, braces included
};

std::vector<Unit> Units(const std::string& s) {
    std::vector<Unit> units;
    std::vector<bool> stack;  // true for a namespace's brace
    size_t mark = 0;          // just past the last ; { or }
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            i = LiteralEnd(s, i);
            continue;
        }
        if (c == '{') {
            std::string header = Trim(s.substr(mark, i - mark));
            bool top = std::find(stack.begin(), stack.end(), false) == stack.end();
            // The header runs from the last ; { or }, so a namespace's may carry the #include lines above it.
            static const std::regex Namespace(R"re((^|\s)namespace(\s+[A-Za-z_]\w*)?$)re");
            bool ns = std::regex_search(header, Namespace);
            if (top && !ns) units.push_back({header, i, 0});
            stack.push_back(ns);
            mark = i + 1;
        } else if (c == '}') {
            if (!stack.empty()) {
                stack.pop_back();
                bool top = std::find(stack.begin(), stack.end(), false) == stack.end();
                if (top && !units.empty() && units.back().end == 0) units.back().end = i + 1;
            }
            mark = i + 1;
        } else if (c == ';') {
            mark = i + 1;
        }
        ++i;
    }
    return units;
}

const Unit* UnitAt(const std::vector<Unit>& units, size_t at) {
    for (const Unit& u : units)
        if (at > u.begin && at < u.end) return &u;
    return nullptr;
}

// The functions a request path is handed through, each named with the place its path is: Request's own and the helpers
// that pass their path on (a read's, a write's). A path handed to any other function is a helper the scan does not read.
const std::vector<std::pair<std::string, size_t>> PathTakers = {{"Request", 2}, {"ObjectRequest", 1}, {"ElementWrite", 2}};

// A path the scan can read: a quoted literal of path characters alone, or the variable `path`.
const std::regex PathLiteral(R"re(^"(/[a-z_]+)*/"$)re");
// A term of a built path: a literal of path characters (a hyphen among them, for keel's /media/remove-ticket), or one
// variable checked by Seg() under its own name, which the refusal names (Seg("id", id, why)).
const std::regex Literal(R"re(^"[a-z_/-]*"$)re");
const std::regex Checked(R"re(^Seg\("([A-Za-z_][A-Za-z_0-9]*)", ([A-Za-z_][A-Za-z_0-9]*), why\)$)re");
// What follows a built path at once: the refusal, before anything is sent.
const std::string RefuseFirst = "if (!why.empty()) return Refuse(done, why)";

struct Scan {
    std::vector<std::string> problems;  // each a value sent raw, or a path the scan cannot read
    int calls = 0;                      // request calls read (a literal path or `path`)
    int built = 0;                      // `std::string path = ...` declarations read
    std::set<std::string> forwarders;   // the functions found handing their `path` parameter on
    std::set<std::string> takers;       // the path takers found defined
    int url_lines = 0;                  // request.url statements matched
};

Scan ScanClient(const std::string& source) {
    Scan out;
    const std::string s = Uncommented(source);
    const std::vector<Unit> units = Units(s);
    std::set<const Unit*> checked;

    for (const auto& [name, at] : PathTakers) {
        for (size_t i = s.find(name); i != std::string::npos; i = s.find(name, i + 1)) {
            if (i > 0 && (IsIdent(s[i - 1]) || s[i - 1] == ':')) continue;
            size_t open = i + name.size();
            while (open < s.size() && (s[open] == ' ' || s[open] == '\n' || s[open] == '\r')) ++open;
            if (open >= s.size() || s[open] != '(' || IsIdent(s[i + name.size()])) continue;
            // A definition: `void Request(`.
            size_t line = s.rfind('\n', i);
            std::string before = Trim(s.substr(line == std::string::npos ? 0 : line + 1, i - (line + 1)));
            if (before == "void") {
                out.takers.insert(name);
                continue;
            }
            std::vector<std::string> args = SplitTop(Inside(s, open), ',');
            if (args.size() <= at) {
                out.problems.push_back(name + " called with no path: " + Inside(s, open));
                continue;
            }
            const std::string& path = args[at];
            ++out.calls;
            if (std::regex_match(path, PathLiteral)) continue;
            if (path != "path") {
                out.problems.push_back("a path the scan cannot read, sent through " + name + ": " + path);
                continue;
            }
            const Unit* u = UnitAt(units, i);
            if (!u) {
                out.problems.push_back("`path` sent through " + name + " outside any function");
                continue;
            }
            if (!checked.insert(u).second) continue;
            const std::string body = s.substr(u->begin, u->end - u->begin);
            // A path taker handing its own parameter on: its callers are read above.
            bool param = u->header.find("const std::string& path") != std::string::npos;
            if (param) {
                bool taker = false;
                for (const auto& taken : PathTakers)
                    taker = taker || u->header.rfind("void " + taken.first + "(", 0) == 0;
                if (!taker) out.problems.push_back("a path handed on by a helper the scan does not read: " + u->header);
                else out.forwarders.insert(u->header.substr(5, u->header.find('(') - 5));
            }
            // Every place `path` is declared, assigned or changed in the function.
            static const std::regex Decl(R"re(std::string path = ([^;]*);)re");
            static const std::regex Assign(R"re((^|[^A-Za-z_0-9.>])path\s*(\+=|=[^=]))re");
            static const std::regex Changed(
                R"re(\bpath\s*\.\s*(append|push_back|insert|replace|assign|erase|resize|swap)\b)re");
            int decls = 0;
            for (std::sregex_iterator m(body.begin(), body.end(), Decl), e; m != e; ++m) {
                ++decls;
                ++out.built;
                std::string expr = (*m)[1].str();
                bool checks = false;
                for (const std::string& term : SplitTop(expr, '+')) {
                    std::smatch seg;
                    if (std::regex_match(term, Literal)) continue;
                    if (std::regex_match(term, seg, Checked) && seg[1].str() == seg[2].str()) {
                        checks = true;
                        continue;
                    }
                    out.problems.push_back("a segment Seg() does not check in a request path: " + term + " in " +
                                           Trim(expr));
                }
                // A path with a checked value refuses at once when Seg() did: the next statement.
                size_t after = static_cast<size_t>(m->position() + m->length());
                size_t semi = body.find(';', after);
                std::string next = Trim(body.substr(after, semi == std::string::npos ? std::string::npos : semi - after));
                if (checks && next != RefuseFirst)
                    out.problems.push_back("a checked path not refused at once (" + RefuseFirst + "): " + Trim(expr) +
                                           ", then " + next);
            }
            int assigns = static_cast<int>(std::distance(std::sregex_iterator(body.begin(), body.end(), Assign),
                                                         std::sregex_iterator()));
            if (std::regex_search(body, Changed) || assigns != decls)
                out.problems.push_back("`path` changed after it is built, which the scan cannot read: " + u->header);
            if (decls == 0 && !param)
                out.problems.push_back("`path` sent with no path built in the function: " + u->header);
            if (decls > 1) out.problems.push_back("more than one path built in one function: " + u->header);
        }
    }

    // The URL: the API root and the path, then each query name and value escaped; or, for the upload host's step
    // (HostSend), the URL a ticket names, whole, which carries no value of the caller's. Nothing else.
    static const std::regex Url(R"re(request\.url\s*(\+?=)\s*([^;]*);)re");
    const std::set<std::string> allowed = {
        "= s->base_url + path",
        "+= (i ? \"&\" : \"?\") + Escape(query[i].first) + \"=\" + Escape(query[i].second)",
        "= host_url",
    };
    for (std::sregex_iterator m(s.begin(), s.end(), Url), e; m != e; ++m) {
        std::string line = (*m)[1].str() + " " + Trim((*m)[2].str());
        if (allowed.count(line)) ++out.url_lines;
        else out.problems.push_back("a request URL built some other way: request.url " + line);
    }
    size_t mentions = 0;
    for (size_t i = s.find("request.url"); i != std::string::npos; i = s.find("request.url", i + 1)) ++mentions;
    if (mentions != static_cast<size_t>(out.url_lines))
        out.problems.push_back("request.url touched where the scan does not read it");
    return out;
}

std::string Join(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += "\n         " + l;
    return out;
}

}  // namespace

OW_TEST(path_segments_source_every_value_checked) {
    const std::string source = ReadText(P().root + ClientSource);
    if (source.empty()) return t.Fail("the client's source did not load: " + P().root + ClientSource);
    Scan scan = ScanClient(source);
    t.Eq(Join(scan.problems), std::string(), "every request path a literal or built from literals and Seg(), refused at once");
    // A scan that lost its shape fails rather than passing empty: the sixteen request calls (Health, GetWorld, Me,
    // Members, the read helper's own, List, Get, Changes, ChangesHead; Create, Upsert, Patch, EditLinks, the write
    // helper's own, Delete, Bulk), the seven built paths (List's, Get's and each write's but Bulk's literal), the two
    // helpers, the three path takers defined, the URL's two lines and the upload host's one.
    t.Ok(scan.calls >= 16, "found " + std::to_string(scan.calls) + " request calls; the scan lost its shape");
    t.Ok(scan.built >= 7, "found " + std::to_string(scan.built) + " built paths; the scan lost its shape");
    t.Eq(Join(std::vector<std::string>(scan.forwarders.begin(), scan.forwarders.end())),
         Join({"ElementWrite", "ObjectRequest"}), "the functions that hand their path on");
    t.Eq(Join(std::vector<std::string>(scan.takers.begin(), scan.takers.end())),
         Join({"ElementWrite", "ObjectRequest", "Request"}), "the path takers defined");
    t.Eq(scan.url_lines, 3, "the URL built in two lines, and the upload host's in one");
}

// Only the client builds a request: no other source of the core makes an OWRequest.
OW_TEST(path_segments_source_one_request_builder) {
    std::vector<std::string> builders;
    int files = 0;
    for (const char* dir : {"/Private", "/Public"}) {
        std::error_code ec;
        const std::filesystem::path folder = std::filesystem::u8path(P().root + CoreDir + dir);
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            std::string ext = entry.path().extension().u8string();
            if (ext != ".cpp" && ext != ".h") continue;
            ++files;
            std::string s = Uncommented(ReadText(entry.path().u8string()));
            if (std::regex_search(s, std::regex(R"re(\bOWRequest\b)re")))
                builders.push_back(entry.path().filename().u8string());
        }
    }
    t.Ok(files >= 40, "read " + std::to_string(files) + " core sources; the scan lost its shape");
    std::sort(builders.begin(), builders.end());
    t.Eq(Join(builders), Join({"OWClient.cpp", "OWTransport.h"}), "the sources that name OWRequest");
    std::string client = Uncommented(ReadText(P().root + ClientSource));
    size_t n = 0;
    for (size_t i = client.find("OWRequest"); i != std::string::npos; i = client.find("OWRequest", i + 1)) ++n;
    t.Eq(n, size_t{2}, "OWClient.cpp makes two OWRequests: keel's in Request, the upload host's in HostSend");
}

namespace {

const std::string UUID = "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f";

struct Sent {
    size_t calls = 0;
    std::string path;     // the first request's URL past the API root, up to its query
    bool answered = false;
    bool refused = false;  // answered with an error of kind Refused
    std::string message;  // the error's message, "" for none
};

template <typename T>
OWHandler<T> Keep(Sent& out) {
    return [&out](OWReply<T> r) {
        out.answered = true;
        if (r.error) {
            out.refused = r.error->kind == OWApiError::Kind::Refused;
            out.message = r.error->message;
        }
    };
}

template <typename Call>
Sent Send(Call call) {
    auto fake = std::make_shared<FakeTransport>();
    fake->body = "{\"data\":[],\"has_more\":false,\"next_cursor\":null,\"id\":\"x\"}";
    OWClientConfig config;
    config.api_key = "ow_w_test";
    config.api_pin = "1234";
    config.base_url = Base;
    config.transport = fake;
    OWClient client(std::move(config));
    Sent out;
    call(client, out);
    out.calls = fake->calls.size();
    if (out.calls == 0) return out;
    std::string url = fake->calls[0].url;
    if (url.rfind(Base, 0) == 0) url = url.substr(Base.size());
    out.path = url.substr(0, url.find('?'));
    return out;
}

using Call = std::function<void(OWClient&, Sent&)>;

Call GetOf(std::string type, std::string id) {
    return [type, id](OWClient& c, Sent& out) { c.Get(type, id, {}, Keep<OWJsonValue>(out)); };
}
Call ListOf(std::string type) {
    return [type](OWClient& c, Sent& out) { c.List(type, {}, Keep<OWPage>(out)); };
}
Call ListAllOf(std::string type) {
    return [type](OWClient& c, Sent& out) { c.ListAll(type, {}, Keep<std::vector<OWJsonValue>>(out)); };
}

struct Refusal {
    std::string name;
    Call call;
    std::string names;  // the value the refusal names: its message starts "<names> "
};

// The TypeScript SDK's refusal cases where this client has the call (its Get, List and ListAll; the write, link,
// adopt, hide, draught and account routes are not in the core yet), with the type's and the newline's.
std::vector<Refusal> Refusals() {
    return {
        {"get", GetOf("character", UUID + "/hidden"), "id"},
        {"get (id with ? and #)", GetOf("character", "abc/hidden?x=1#y"), "id"},
        {"get (escaped slash)", GetOf("character", UUID + "%2Fhidden"), "id"},
        {"get (dot segment)", GetOf("character", ".."), "id"},
        {"get (empty)", GetOf("character", ""), "id"},
        {"get (a final newline)", GetOf("character", "abc\n"), "id"},
        {"get (type)", GetOf("Character", UUID), "type"},
        {"get (type with a final newline)", GetOf("character\n", UUID), "type"},
        {"list (type)", ListOf("character/x"), "type"},
        {"list (type with ? and #)", ListOf("character?x=1#y"), "type"},
        {"listAll (type)", ListAllOf("character/x"), "type"},
    };
}

}  // namespace

OW_CASES(path_segments_refused) {
    owtest::Cases out;
    for (const Refusal& r : Refusals()) {
        out.emplace_back("path_segments:" + r.name + " is refused before sending", [r](owtest::T& t) {
            Sent got = Send(r.call);
            t.Eq(got.calls, size_t{0}, r.name + ": nothing was sent");
            t.Ok(got.answered && got.refused, r.name + ": answered with a refusal: " + got.message);
            t.Ok(got.message.rfind(r.names + " ", 0) == 0, r.name + ": the refusal names " + r.names + ": " + got.message);
            t.Ok(got.message.find("Nothing was sent.") != std::string::npos, r.name + ": the refusal says so");
        });
    }
    return out;
}

OW_TEST(path_segments_refused_cases_counted) {
    t.Eq(Refusals().size(), size_t{11}, "eleven refusal cases");
}

OW_TEST(path_segments_one_segment_goes_out_as_given) {
    // A uuid, letters with dots, a dash and an underscore inside, and a lower-case type: each one segment, sent as given.
    const std::vector<std::pair<Call, std::string>> sent = {
        {GetOf("character", UUID), "/character/" + UUID + "/"},
        {GetOf("character", "a.b_c-D9"), "/character/a.b_c-D9/"},
        {ListOf("character"), "/character/"},
        {ListAllOf("character"), "/character/"},
    };
    for (const auto& [call, path] : sent) {
        Sent got = Send(call);
        t.Eq(got.calls, size_t{1}, path + ": one request sent");
        t.Eq(got.path, path, path + ": as given");
        t.Ok(!got.refused, path + ": not refused: " + got.message);
    }
}
