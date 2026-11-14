// The lens twin's Unreal side: read a cases file of random small worlds, answer each through the core's lens and graph
// readers, and write the answers in the shape the npm SDK's twin runner writes, so one compare reads both.
//   build.cmd twin                       -> build\twin_run.exe
//   build\twin_run.exe <cases.jsonl> <out.jsonl>
// Each case is one JSON line: {i, kind: "resolve", world, lens, reader, as_of, clock} or
// {i, kind: "graph", world, reader, options} (options: lens, as_of, clock and the four flags). Each answer is {i, got}:
// for resolve {kept, cut, rows, notes_ignored, mail, unreadable} (OWLensReading::ToJson), for graph {nodes, edges,
// edges_cut, cut, unreadable} (OWWorldGraph::ToJson), and for a call the reader refuses (a graph's `refused`, or an
// `error` on either) {threw: "refused", names, msg}, names being the flags its message names (npm's TypeError).
// The layout twin's cases ride in the same shape: {i, kind: "by-kind", graph} or {i, kind: "around", graph, focus,
// steps}, answered {points} through OWLayout, each point [id, x, y] or [id, depth, x, y] with every coordinate as its
// float64 bits in 16 hex digits (so no decimal is parsed or printed on either side), or {threw: "refused", msg}.

#include "OWGraph.h"
#include "OWJson.h"
#include "OWLayout.h"
#include "OWLens.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

using namespace OnlyWorlds;

namespace {

// One line of JSON: the core's writer is section 5's (indented), and a JSON-lines file wants each value on one line.
void Compact(const OWJsonValue& v, std::string& out) {
    switch (v.kind()) {
        case OWJsonValue::Kind::Null: out += "null"; break;
        case OWJsonValue::Kind::Bool: out += v.AsBool() ? "true" : "false"; break;
        case OWJsonValue::Kind::Number: out += OWJson::CanonicalNumber(v.Text()); break;
        case OWJsonValue::Kind::String: out += OWJson::Quote(v.Text()); break;
        case OWJsonValue::Kind::Array:
            out += '[';
            for (size_t k = 0; k < v.Size(); ++k) {
                if (k) out += ',';
                Compact(v.Items()[k], out);
            }
            out += ']';
            break;
        case OWJsonValue::Kind::Object:
            out += '{';
            for (size_t k = 0; k < v.Size(); ++k) {
                if (k) out += ',';
                out += OWJson::Quote(v.Keys()[k]);
                out += ':';
                Compact(v.Items()[k], out);
            }
            out += '}';
            break;
    }
}

OWJsonValue Refused(const std::string& msg) {
    OWJsonValue names = OWJsonValue::Array();
    for (const char* flag : {"hidden", "proposed", "rooms", "map_space", "reading_aids"})
        if (msg.find(flag) != std::string::npos) names.Push(OWJsonValue::String(flag));
    OWJsonValue got = OWJsonValue::Object();
    got.Set("threw", OWJsonValue::String("refused"));
    got.Set("names", std::move(names));
    got.Set("msg", OWJsonValue::String(msg));
    return got;
}

std::optional<int64_t> AsOf(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    return static_cast<int64_t>(v->AsDouble());
}

std::optional<std::string> Clock(const OWJsonValue* v) {
    if (!v || !v->IsString()) return std::nullopt;
    return v->Text();
}

std::string Hex(double v) {
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    char s[17];
    std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(b));
    return s;
}

OWJsonValue Layout(const OWJsonValue& c, bool byKind) {
    static const OWJsonValue none;
    const OWJsonValue* graph = c.Find("graph");
    OWJsonValue points = OWJsonValue::Array();
    std::string error;
    if (byKind) {
        OWLayoutByKind r = OWLayout::LayoutByKind(graph ? *graph : none);
        error = r.error;
        for (const auto& p : r.points) {
            OWJsonValue row = OWJsonValue::Array();
            row.Push(OWJsonValue::String(p.id));
            row.Push(OWJsonValue::String(Hex(p.x)));
            row.Push(OWJsonValue::String(Hex(p.y)));
            points.Push(std::move(row));
        }
    } else {
        const OWJsonValue* focus = c.Find("focus");
        const OWJsonValue* steps = c.Find("steps");
        OWLayoutAround r = OWLayout::LayoutAround(graph ? *graph : none, focus && focus->IsString() ? focus->Text() : std::string(),
                                                  steps ? static_cast<int>(steps->AsDouble()) : 1);
        error = r.error;
        for (const auto& p : r.points) {
            OWJsonValue row = OWJsonValue::Array();
            row.Push(OWJsonValue::String(p.id));
            row.Push(OWJsonValue::Number(static_cast<int64_t>(p.depth)));
            row.Push(OWJsonValue::String(Hex(p.x)));
            row.Push(OWJsonValue::String(Hex(p.y)));
            points.Push(std::move(row));
        }
    }
    OWJsonValue out = OWJsonValue::Object();
    if (!error.empty()) {
        out.Set("threw", OWJsonValue::String("refused"));
        out.Set("msg", OWJsonValue::String(error));
    } else {
        out.Set("points", std::move(points));
    }
    return out;
}

OWJsonValue Answer(const OWJsonValue& c) {
    static const OWJsonValue none;
    const std::string& kind = c.Find("kind")->Text();
    if (kind == "by-kind" || kind == "around") return Layout(c, kind == "by-kind");
    const OWJsonValue* world = c.Find("world");
    const OWJsonValue* reader = c.Find("reader");
    if (c.Find("kind")->Text() == "resolve") {
        const OWJsonValue* lens = c.Find("lens");
        OWLensOptions o;
        o.as_of = AsOf(c.Find("as_of"));
        o.clock = Clock(c.Find("clock"));
        OWLensReading r = OWLens::Resolve(world ? *world : none, lens ? *lens : none, reader ? *reader : none, o);
        return r.error.empty() ? r.ToJson() : Refused(r.error);
    }
    OWGraphOptions o;
    if (const OWJsonValue* opt = c.Find("options"); opt && opt->IsObject()) {
        if (const OWJsonValue* lens = opt->Find("lens")) o.lens = *lens;
        o.as_of = AsOf(opt->Find("as_of"));
        o.clock = Clock(opt->Find("clock"));
        for (auto [key, flag] : {std::pair<const char*, bool*>{"rooms", &o.rooms}, {"hidden", &o.hidden},
                                 {"proposed", &o.proposed}, {"map_space", &o.map_space},
                                 {"reading_aids", &o.reading_aids}})
            if (const OWJsonValue* v = opt->Find(key); v && v->IsBool()) *flag = v->AsBool();
    }
    OWWorldGraph g = OWGraph::WorldGraph(world ? *world : none, reader ? *reader : none, o);
    if (!g.refused.empty()) return Refused(g.refused);
    if (!g.error.empty()) return Refused(g.error);
    return g.ToJson();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: twin_run <cases.jsonl> <out.jsonl>\n");
        return 2;
    }
    auto started = std::chrono::steady_clock::now();
    std::ifstream in(argv[1], std::ios::binary);
    std::ofstream out(argv[2], std::ios::binary);
    if (!in || !out) {
        std::fprintf(stderr, "twin_run: cannot open %s or %s\n", argv[1], argv[2]);
        return 2;
    }
    std::string line, buf;
    size_t count = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        OWJsonParseResult p = OWJson::Parse(line);
        if (!p.ok) {
            std::fprintf(stderr, "twin_run: line %zu does not parse: %s\n", count + 1, p.error.c_str());
            return 1;
        }
        OWJsonValue row = OWJsonValue::Object();
        row.Set("i", OWJsonValue::Number(static_cast<int64_t>(p.value.Find("i")->AsDouble())));
        row.Set("got", Answer(p.value));
        buf.clear();
        Compact(row, buf);
        out << buf << '\n';
        ++count;
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("unreal: %zu results in %.1f s\n", count, s);
    return 0;
}
