// A caller's string never changes which route is asked: every value OWClient puts in a request path (the element type,
// the id) is sent as one escaped segment, as the TypeScript SDK sends it since 4.122.1. keel declares every element id
// a UUID, so the escape changes nothing on a real id or type; on `abc/hidden` the raw form asked a different route.
// Two halves, as the TypeScript SDK's test/path-segments.test.mjs: the source scan reads every request path the client
// builds, so a new route that puts a value in raw, or builds its path some way the scan cannot read, fails here; the
// calls show the wire, through the fake transport, for the routes that sent the type raw.

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

// The functions a request path is handed through, each named with the place its path is: Request's own and the helper
// that passes its path on. A path handed to any other function is a helper the scan does not read.
const std::vector<std::pair<std::string, size_t>> PathTakers = {{"Request", 2}, {"ObjectRequest", 1}};

// A path the scan can read: a quoted literal of path characters alone, or the variable `path`.
const std::regex PathLiteral(R"re(^"(/[a-z_]+)*/"$)re");
// A term of a built path: a literal of path characters, or one escaped variable.
const std::regex Literal(R"re(^"[a-z_/]*"$)re");
const std::regex Escaped(R"re(^Escape\([A-Za-z_][A-Za-z_0-9]*\)$)re");

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
                for (const std::string& term : SplitTop(expr, '+')) {
                    if (std::regex_match(term, Literal) || std::regex_match(term, Escaped)) continue;
                    out.problems.push_back("a raw segment in a request path: " + term + " in " + Trim(expr));
                }
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

    // The URL: the API root and the path, then each query name and value escaped. Nothing else.
    static const std::regex Url(R"re(request\.url\s*(\+?=)\s*([^;]*);)re");
    const std::set<std::string> allowed = {
        "= s->base_url + path",
        "+= (i ? \"&\" : \"?\") + Escape(query[i].first) + \"=\" + Escape(query[i].second)",
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

OW_TEST(path_segments_source_every_value_escaped) {
    const std::string source = ReadText(P().root + ClientSource);
    if (source.empty()) return t.Fail("the client's source did not load: " + P().root + ClientSource);
    Scan scan = ScanClient(source);
    t.Eq(Join(scan.problems), std::string(), "every request path a literal or built from literals and Escape()");
    // A scan that lost its shape fails rather than passing empty: the nine request calls (Health, GetWorld, Me,
    // Members, the helper's own, List, Get, Changes, ChangesHead), the two built paths (List's and Get's), the one
    // helper, both path takers defined, the URL's two lines.
    t.Ok(scan.calls >= 9, "found " + std::to_string(scan.calls) + " request calls; the scan lost its shape");
    t.Ok(scan.built >= 2, "found " + std::to_string(scan.built) + " built paths; the scan lost its shape");
    t.Eq(Join(std::vector<std::string>(scan.forwarders.begin(), scan.forwarders.end())), Join({"ObjectRequest"}),
         "the functions that hand their path on");
    t.Eq(Join(std::vector<std::string>(scan.takers.begin(), scan.takers.end())), Join({"ObjectRequest", "Request"}),
         "the path takers defined");
    t.Eq(scan.url_lines, 2, "the URL built in two lines");
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
    t.Eq(n, size_t{1}, "OWClient.cpp makes one OWRequest, in Request");
}

namespace {

const std::string ID = "abc/hidden?x=1#y";
const std::string ESC = "abc%2Fhidden%3Fx%3D1%23y";

struct Sent {
    size_t calls = 0;
    std::string path;   // the URL past the API root, up to its query
    std::string query;  // past the '?', "" for none
};

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
    call(client);
    Sent out;
    out.calls = fake->calls.size();
    if (out.calls == 0) return out;
    std::string url = fake->calls[0].url;
    if (url.rfind(Base, 0) == 0) url = url.substr(Base.size());
    size_t q = url.find('?');
    out.path = url.substr(0, q);
    out.query = q == std::string::npos ? "" : url.substr(q + 1);
    return out;
}

void Expect(owtest::T& t, const Sent& got, const std::string& path, const std::string& name) {
    t.Eq(got.calls, size_t{1}, name + ": one request sent");
    t.Eq(got.path, path, name + ": the caller's string is one segment");
}

template <typename T>
OWHandler<T> Ignore() {
    return [](OWReply<T>) {};
}

}  // namespace

OW_TEST(path_segments_wire_get_id) {
    Expect(t, Send([](OWClient& c) { c.Get("character", ID, {}, Ignore<OWJsonValue>()); }), "/character/" + ESC + "/",
           "get");
}

OW_TEST(path_segments_wire_get_type) {
    Expect(t, Send([](OWClient& c) { c.Get("character/x", "y", {}, Ignore<OWJsonValue>()); }), "/character%2Fx/y/",
           "get (type)");
}

OW_TEST(path_segments_wire_list_type) {
    Expect(t, Send([](OWClient& c) { c.List("character/x", {}, Ignore<OWPage>()); }), "/character%2Fx/",
           "list (type)");
}

OW_TEST(path_segments_wire_list_type_query) {
    // A type carrying ? and # adds no query and cuts nothing off: the page's own query is all there is.
    Sent got = Send([](OWClient& c) { c.List("character?x=1#y", {}, Ignore<OWPage>()); });
    Expect(t, got, "/character%3Fx%3D1%23y/", "list (type with ? and #)");
    t.Eq(got.query, std::string("limit=100"), "list (type with ? and #): the page's query alone");
}

OW_TEST(path_segments_wire_list_all_type) {
    Expect(t, Send([](OWClient& c) { c.ListAll("character/x", {}, Ignore<std::vector<OWJsonValue>>()); }),
           "/character%2Fx/", "listAll (type)");
}

OW_TEST(path_segments_wire_real_values_unchanged) {
    // keel's ids are UUIDs and its types lowercase words: the escape leaves both as they were.
    const std::string uuid = "0193b2e4-7c1d-7a3e-9f2b-5d6c8e9a0b1c";
    Expect(t, Send([&](OWClient& c) { c.Get("character", uuid, {}, Ignore<OWJsonValue>()); }),
           "/character/" + uuid + "/", "get (a real id)");
    Expect(t, Send([](OWClient& c) { c.List("character", {}, Ignore<OWPage>()); }), "/character/",
           "list (a real type)");
}
