// OWNameFinder on the npm SDK's and the Python package's shared cases (tests/data/name-finder.cases.json, the same
// bytes, held by its sha256): every case's answer and shared list, the timed recipes, then what the cases cannot hold:
// the core's byte offsets, the element's place in the list, the shared list read once, the caller's mistakes, the
// word tables at Unicode 15.1, the one fact about folding the matcher stands on, and inputs the cases are silent on,
// each answer the TypeScript SDK's.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWNameFinder.h"
#include "OWUnicode.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <sstream>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/name-finder.cases.json at 4.198.0 (542060a), version 2; the Python package holds the
// same bytes.
const char* const CasesSha256 = "8e91e79981830511055976011f9fc6d4f4b62fb0183c29bad61c2d05fd4b13e4";

std::string CasesPath() { return P().root + "/tests/data/name-finder.cases.json"; }

const OWBytes& CasesBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(CasesPath(), bytes);
        read = true;
    }
    return bytes;
}

std::shared_ptr<OWJsonValue> Cases() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWJsonParseResult r = OWJson::Parse(CasesBytes());
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

int64_t Int(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsNumber() ? static_cast<int64_t>(x->AsDouble()) : -1;
}

// The cases' names option: an element with a names list is found by those names, one without by its name.
std::vector<OWJsonValue> NamesOf(const OWJsonValue& element) {
    const OWJsonValue* names = element.Find("names");
    if (names && names->IsArray()) return names->Items();
    const OWJsonValue* name = element.Find("name");
    return {name ? *name : OWJsonValue()};
}

OWNameCase CaseOf(const std::string& word) { return word == "folded" ? OWNameCase::Folded : OWNameCase::Exact; }

// Code points in the UTF-8 (or WTF-8) bytes [from, to).
size_t Points(const std::string& s, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to; ++i)
        if ((static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

std::string IdAt(const OWJsonValue& elements, size_t i) { return Str(elements.Items()[i], "id"); }

// What Find answers as the cases write it: [{at, length, id}] in code points, compact.
std::string AnswerText(const OWJsonValue& elements, const std::string& text, const std::vector<OWNameFound>& found) {
    std::ostringstream s;
    s << "[";
    for (size_t k = 0; k < found.size(); ++k) {
        const OWNameFound& f = found[k];
        s << (k ? "," : "") << "{\"at\":" << Points(text, 0, f.start) << ",\"length\":" << Points(text, f.start, f.end)
          << ",\"id\":" << OWJson::Quote(IdAt(elements, f.element)) << "}";
    }
    s << "]";
    return s.str();
}

std::string WantText(const OWJsonValue& want) {
    std::ostringstream s;
    s << "[";
    for (size_t k = 0; k < want.Size(); ++k) {
        const OWJsonValue& w = want.Items()[k];
        s << (k ? "," : "") << "{\"at\":" << Int(w, "at") << ",\"length\":" << Int(w, "length")
          << ",\"id\":" << OWJson::Quote(Str(w, "id")) << "}";
    }
    s << "]";
    return s.str();
}

std::string SharedText(const OWJsonValue& elements, const std::vector<OWNameShared>& shared) {
    std::ostringstream s;
    s << "[";
    for (size_t k = 0; k < shared.size(); ++k) {
        s << (k ? "," : "") << "{\"name\":" << OWJson::Quote(shared[k].name) << ",\"ids\":[";
        for (size_t e = 0; e < shared[k].elements.size(); ++e)
            s << (e ? "," : "") << OWJson::Quote(IdAt(elements, shared[k].elements[e]));
        s << "]}";
    }
    s << "]";
    return s.str();
}

std::string WantSharedText(const OWJsonValue& want) {
    std::ostringstream s;
    s << "[";
    for (size_t k = 0; k < want.Size(); ++k) {
        const OWJsonValue& w = want.Items()[k];
        s << (k ? "," : "") << "{\"name\":" << OWJson::Quote(Str(w, "name")) << ",\"ids\":[";
        const OWJsonValue* ids = w.Find("ids");
        for (size_t e = 0; ids && e < ids->Size(); ++e) s << (e ? "," : "") << OWJson::Quote(ids->Items()[e].Text());
        s << "]}";
    }
    s << "]";
    return s.str();
}

OWJsonValue Element(const std::string& id, const std::string& name) {
    OWJsonValue e = OWJsonValue::Object();
    e.Set("id", OWJsonValue::String(id));
    e.Set("name", OWJsonValue::String(name));
    return e;
}

OWJsonValue List(std::initializer_list<OWJsonValue> items) {
    OWJsonValue a = OWJsonValue::Array();
    for (const OWJsonValue& v : items) a.Push(v);
    return a;
}

std::string Repeat(const std::string& s, int64_t times) {
    std::string out;
    out.reserve(s.size() * static_cast<size_t>(times));
    for (int64_t i = 0; i < times; ++i) out += s;
    return out;
}

std::string Pattern(const std::string& p, int64_t i) {
    std::string out = p;
    size_t at = out.find("{i}");
    if (at != std::string::npos) out.replace(at, 3, std::to_string(i));
    return out;
}

// The time each recipe may take: npm's line (the cases' underMs, 2 s) times this. npm's line stands here unchanged:
// the core is compiled (build.cmd's /O2) with the matcher in npm's shape, one read of the text from its end, so the
// matcher has no interpreter's ratio to answer for. Measured before this was set (three runs, a quiet machine): every
// recipe 3 to 66 ms but one, address prefixes with no address, 242 to 285 ms, of which OWAddress::FindAll alone takes
// 239 to 251 ms; so the slowest sits about seven times under the line, room for a loaded machine. What it catches is
// a matcher many times slower than this one; in compiled code it need not catch every quadratic one (a tight loop may
// take the long-name recipes' billion steps in about a second; estimated, not run).
const double Factor = 1.0;

}  // namespace

OW_TEST(name_finder_cases_file_is_the_pinned_one) {
    t.Eq(owtest::Sha256Hex(CasesBytes()), std::string(CasesSha256), "the cases file's sha256 (the npm SDK's bytes)");
    const OWJsonValue& doc = *Cases();
    t.Eq(Int(doc, "version"), int64_t(2), "version");
    const OWJsonValue* cases = doc.Find("cases");
    const OWJsonValue* timed = doc.Find("timed");
    t.Eq(cases ? cases->Size() : 0, size_t(62), "cases");
    t.Eq(timed ? timed->Size() : 0, size_t(10), "timed recipes");
    t.Eq(Int(doc, "underMs"), int64_t(2000), "underMs");
}

OW_CASES(name_finder_cases) {
    owtest::Cases out;
    const OWJsonValue* cases = Cases()->Find("cases");
    if (!cases) return out;
    for (const OWJsonValue& c : cases->Items()) {
        const OWJsonValue* cp = &c;
        out.push_back({"name_finder: " + Str(c, "name"), [cp](owtest::T& t) {
                           const OWJsonValue& elements = *cp->Find("elements");
                           OWNameFinder find = OWNameFinder::Of(elements, CaseOf(Str(*cp, "case")), NamesOf);
                           t.Eq(find.Error(), std::string(), "no mistake");
                           const std::string text = Str(*cp, "text");
                           t.Eq(AnswerText(elements, text, find.Find(text)), WantText(*cp->Find("want")), "find(text)");
                           t.Eq(SharedText(elements, find.Shared()), WantSharedText(*cp->Find("shared")), "shared");
                       }});
    }
    return out;
}

OW_TEST(name_finder_the_cases_hold_answers_of_both_kinds) {
    const OWJsonValue* cases = Cases()->Find("cases");
    if (!cases) return t.Fail("no cases");
    int found[2] = {0, 0}, none = 0, shared[2] = {0, 0}, sharedTwo = 0;
    for (const OWJsonValue& c : cases->Items()) {
        int k = Str(c, "case") == "folded" ? 1 : 0;
        if (c.Find("want")->Size() > 0) ++found[k]; else ++none;
        if (c.Find("shared")->Size() > 0) ++shared[k];
        if (c.Find("shared")->Size() > 1) ++sharedTwo;
    }
    t.Ok(found[0] > 0 && found[1] > 0 && none > 0, "a name found, of each case, and none found");
    t.Ok(shared[0] + shared[1] >= 10 && shared[0] > 0 && shared[1] > 0 && sharedTwo > 0,
         "ten cases with a shared name at least, of each case, one with two");
}

OW_TEST(name_finder_the_timed_recipes) {
    const OWJsonValue& doc = *Cases();
    const OWJsonValue* timed = doc.Find("timed");
    if (!timed) return t.Fail("no timed recipes");
    const double line = static_cast<double>(Int(doc, "underMs")) * Factor;
    int longNames = 0;
    for (const OWJsonValue& recipe : timed->Items()) {
        const OWJsonValue& e = *recipe.Find("elements");
        const OWJsonValue& tx = *recipe.Find("text");
        const OWJsonValue* longName = e.Find("long");
        if (longName && Int(*longName, "times") >= 2500) ++longNames;
        OWJsonValue elements = OWJsonValue::Array();
        const int64_t count = Int(e, "count");
        for (int64_t i = 0; i < count; ++i) {
            std::string name = longName ? Repeat(Str(*longName, "repeat"), Int(*longName, "times")) + Str(*longName, "then")
                                        : Pattern(Str(e, "pattern"), i);
            elements.Push(Element("e" + std::to_string(i), name));
        }
        std::string text;
        if (tx.Has("repeat")) {
            text = Repeat(Str(tx, "repeat"), Int(tx, "times"));
        } else {
            const int64_t times = Int(tx, "times"), cycle = Int(tx, "count");
            for (int64_t i = 0; i < times; ++i) text += Pattern(Str(tx, "cycle"), i % cycle);
        }
        OWNameFinder find = OWNameFinder::Of(elements, OWNameCase::Exact);
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<OWNameFound> found = find.Find(text);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const std::string name = Str(recipe, "name");
        // The address finder's share, read alone on the same text (it runs inside Find too).
        const auto t1 = std::chrono::steady_clock::now();
        const size_t links = OWAddress::FindAll(text).size();
        const double linksMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        std::printf("      timed: %s: %.1f ms (OWAddress::FindAll alone: %.1f ms, %zu found)\n", name.c_str(), ms,
                    linksMs, links);
        t.Eq(static_cast<int64_t>(found.size()), Int(recipe, "count"), name + ": names found");
        t.Ok(ms < line, name + ": " + std::to_string(static_cast<int64_t>(ms)) + " ms, under " +
                            std::to_string(static_cast<int64_t>(line)));
    }
    t.Ok(longNames >= 3, "three recipes with a name of thousands of characters");
}

OW_TEST(name_finder_start_and_end_are_utf8_byte_offsets) {
    // npm's dragon test: its answers are [3, 7] and [17, 21] in UTF-16 units; here the dragon is four bytes.
    const std::string dragon = "\xF0\x9F\x90\x89";
    const std::string text = dragon + " Kira met " + dragon + dragon + " KIRA.";
    OWNameFinder find = OWNameFinder::Of(List({Element("k", "Kira")}), OWNameCase::Folded);
    const std::vector<OWNameFound> found = find.Find(text);
    t.Eq(found.size(), size_t(2), "two found");
    if (found.size() != 2) return;
    t.Eq(found[0].start, size_t(5), "after one dragon and a space");
    t.Eq(text.substr(found[0].start, found[0].end - found[0].start), std::string("Kira"), "the slice is the name");
    t.Eq(found[1].start, size_t(23), "after three dragons");
    t.Eq(text.substr(found[1].start, found[1].end - found[1].start), std::string("KIRA"), "as written");
    // Folding keeps code points, not bytes: the Kelvin sign is three bytes and folds to k, one.
    const std::string kelvin = "\xE2\x84\xAA" "elvin";
    const std::vector<OWNameFound> k = OWNameFinder::Of(List({Element("k", "kelvin")}), OWNameCase::Folded).Find(kelvin);
    t.Eq(k.size(), size_t(1), "the Kelvin sign folds to k");
    if (!k.empty()) t.Eq(k[0].end, kelvin.size(), "end is the text's own bytes");
}

OW_TEST(name_finder_element_is_its_place_in_the_list) {
    OWJsonValue kira = Element("k", "Kira");
    kira.Set("type", OWJsonValue::String("character"));
    kira.Set("description", OWJsonValue::String("A watchman."));
    OWJsonValue elements = List({Element("a", "Iron Gate"), kira});
    const std::vector<OWNameFound> found = OWNameFinder::Of(elements, OWNameCase::Exact).Find("Kira.");
    t.Eq(found.size(), size_t(1), "found");
    if (!found.empty()) t.Eq(found[0].element, size_t(1), "the second element given");
}

OW_TEST(name_finder_shared_is_in_the_order_given_and_read_once) {
    OWJsonValue elements = List({Element("k", "Kira"), Element("b", "EMBER "), Element("a", "Ember")});
    const std::string before = OWJson::Stringify(elements);
    OWNameFinder find = OWNameFinder::Of(elements, OWNameCase::Folded);
    t.Eq(find.Shared().size(), size_t(1), "one shared name");
    if (find.Shared().size() == 1) {
        t.Eq(find.Shared()[0].name, std::string("ember"), "as compared: trimmed and folded");
        t.Ok(find.Shared()[0].elements == std::vector<size_t>({1, 2}), "in the order given");
    }
    elements.Push(Element("x", "Kira"));
    t.Eq(find.Shared().size(), size_t(1), "the names were read once, when the finder was made");
    t.Eq(OWNameFinder::Of(elements, OWNameCase::Exact).Shared().size(), size_t(1), "Kira, exact");
    t.Eq(OWNameFinder::Of(OWJsonValue::Array(), OWNameCase::Exact).Shared().size(), size_t(0), "no elements");
    const std::vector<OWNameFound> found = find.Find("Ember, ember, Kira");
    t.Eq(found.size(), size_t(1), "a shared name is never answered");
    if (!found.empty()) t.Eq(found[0].element, size_t(0), "Kira");
    elements.Items().pop_back();
    t.Eq(OWJson::Stringify(elements), before, "nothing given is changed");
    OWNameFinder copy = find;
    t.Eq(copy.Find("Kira").size(), size_t(1), "a copy finds as the finder does");
}

OW_TEST(name_finder_one_element_twice_or_by_two_names_is_one_element) {
    OWJsonValue kira = Element("k", "Kira");
    OWNameFinder twice = OWNameFinder::Of(List({kira, kira}), OWNameCase::Exact);
    t.Eq(twice.Shared().size(), size_t(0), "one element listed twice shares nothing");
    t.Eq(twice.Find("Kira").size(), size_t(1), "and is found");
    auto both = [](const OWJsonValue&) { return std::vector<OWJsonValue>{OWJsonValue::String("Kira"), OWJsonValue::String(" kira")}; };
    OWNameFinder two = OWNameFinder::Of(List({kira}), OWNameCase::Folded, both);
    t.Eq(two.Shared().size(), size_t(0), "two of its own names on one spelling");
    t.Eq(two.Find("KIRA").size(), size_t(1), "found once");
}

OW_TEST(name_finder_the_callers_mistakes) {
    auto refused = [&](const OWNameFinder& f, const std::string& word, const std::string& what) {
        t.Ok(f.Error().find(word) != std::string::npos, what + ": " + f.Error());
        t.Eq(f.Find("Kira and Kira").size(), size_t(0), what + ": finds nothing");
        t.Eq(f.Shared().size(), size_t(0), what + ": shares nothing");
    };
    const OWJsonValue kira = List({Element("k", "Kira")});
    refused(OWNameFinder::Of(Element("k", "Kira"), OWNameCase::Exact), "elements", "elements that are no list");
    refused(OWNameFinder::Of(OWJsonValue::String("Kira"), OWNameCase::Exact), "elements", "a string");
    refused(OWNameFinder::Of(List({OWJsonValue::String("k")}), OWNameCase::Exact), "string id", "an element that is no object");
    OWJsonValue noId = OWJsonValue::Object();
    noId.Set("name", OWJsonValue::String("Kira"));
    refused(OWNameFinder::Of(List({noId}), OWNameCase::Exact), "string id", "an element with no id");
    refused(OWNameFinder::Of(List({Element("", "Kira")}), OWNameCase::Exact), "string id", "an empty id");
    OWJsonValue numberId = Element("k", "Kira");
    numberId.Set("id", OWJsonValue::Number(int64_t(7)));
    refused(OWNameFinder::Of(List({numberId}), OWNameCase::Exact), "string id", "an id that is a number");
    refused(OWNameFinder::Of(kira, static_cast<OWNameCase>(0)), "Exact", "a case that is neither");
    refused(OWNameFinder::Of(kira, static_cast<OWNameCase>(3)), "Exact", "a case that is neither");
    refused(OWNameFinder::Of(kira, OWNameCase::Exact, OWNameFinder::NamesOf()), "names", "an empty names function");
    t.Eq(OWNameFinder::Of(kira, OWNameCase::Exact).Error(), std::string(), "a good call has no mistake");
}

OW_TEST(name_finder_the_word_tables_are_unicode_15_1) {
    // Each table as ranges, "<first>-<last>;" in upper-case hex, hashed. The two digests were computed apart from the
    // generator, from Python 3.13.1's unicodedata (Unicode 15.1.0): general category L or M, 725 ranges and 139,176
    // code points; Nd, 64 ranges and 680 code points.
    auto digest = [](bool (*in)(char32_t), size_t& ranges, size_t& points) {
        std::string s;
        ranges = points = 0;
        char buf[32];
        for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
            if (!in(cp)) continue;
            char32_t last = cp;
            while (last < 0x10FFFF && in(last + 1)) ++last;
            std::snprintf(buf, sizeof buf, "%X-%X;", static_cast<unsigned>(cp), static_cast<unsigned>(last));
            s += buf;
            ++ranges;
            points += last - cp + 1;
            cp = last;
        }
        return owtest::Sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    };
    size_t r = 0, n = 0;
    t.Eq(digest(OWUnicode::IsLetterOrMark, r, n), std::string("8898e86dc4cdc725710d4e287677c8854e5a8a69538a4eb23d7421fba7af2f30"), "L or M");
    t.Eq(r, size_t(725), "L or M: ranges");
    t.Eq(n, size_t(139176), "L or M: code points");
    t.Eq(digest(OWUnicode::IsDecimalDigit, r, n), std::string("cf75098334890f09f69cceaad74d1ca0b5855ce45840969defa6bbf5a017450e"), "Nd");
    t.Eq(r, size_t(64), "Nd: ranges");
    t.Eq(n, size_t(680), "Nd: code points");
    t.Ok(OWNameFinder::IsWord(U'_') && OWNameFinder::IsWord(U'7') && OWNameFinder::IsWord(0x0663) &&
             OWNameFinder::IsWord(0x0301) && OWNameFinder::IsWord(0x4E00),
         "a word character: an underscore, a digit of any script, a mark, a letter");
    t.Ok(!OWNameFinder::IsWord(U'-') && !OWNameFinder::IsWord(0x2160) && !OWNameFinder::IsWord(0xD800) &&
             !OWNameFinder::IsWord(0x1F409) && !OWNameFinder::IsWord(0x10D4A),
         "no word character: a hyphen, a letter number, a lone surrogate, a dragon, a letter assigned after 15.1");
}

OW_TEST(name_finder_folding_never_moves_a_character_into_or_out_of_a_word) {
    // The matcher lays a mark before every character of a name that is not a word character, read from the name as
    // folded, and before every such character of the text, read as written. The two agree only while no character
    // changes kind when folded: held on every code point of the 15.1 table.
    int folds = 0;
    std::string moved, crossed;
    for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        const char32_t f = OWUnicode::SimpleFold(cp);
        if (f == cp) continue;
        ++folds;
        if (OWNameFinder::IsWord(cp) != OWNameFinder::IsWord(f)) moved += " U+" + std::to_string(cp);
        if ((cp >> 16) != (f >> 16)) crossed += " U+" + std::to_string(cp);
    }
    t.Ok(folds > 1400, std::to_string(folds) + " code points fold");
    t.Eq(moved, std::string(), "none moves");
    t.Eq(crossed, std::string(), "none leaves its plane");
    t.Eq(uint32_t(OWUnicode::SimpleFold(0x212A)), uint32_t(U'k'), "the Kelvin sign");
    t.Eq(uint32_t(OWUnicode::SimpleFold(0x00DF)), uint32_t(0x00DF), "the sharp s has only a full folding");
    t.Eq(uint32_t(OWUnicode::SimpleFold(0x10400)), uint32_t(0x10428), "a Deseret capital");
}

OW_TEST(name_finder_where_the_cases_are_silent_the_answer_is_the_typescript_sdks) {
    // Inputs the cases file does not hold, each answer the TypeScript SDK's (4.203.0's dist, name-finder.ts as at
    // 4.198.0), converted to code points: [at, length, id].
    const char* const silences = R"json([
    {"name": "a lone high surrogate before a name", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "\ud800Kira met \ud800 Kira", "want": [[1, 4, "k"], [12, 4, "k"]]},
    {"name": "a name holding a lone surrogate, paired then alone", "case": "exact", "elements": [{"id": "s", "name": "Ki\udc00ra"}], "text": "Ki🐀ra Ki\udc00ra", "want": [[6, 5, "s"]]},
    {"name": "a name of no word characters", "case": "exact", "elements": [{"id": "q", "name": "???"}], "text": "Who ??? said ????, and ???", "want": [[4, 3, "q"], [13, 3, "q"], [23, 3, "q"]]},
    {"name": "a leading combining mark", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "́Kira and ́ Kira", "want": [[12, 4, "k"]]},
    {"name": "a name that starts with a combining mark", "case": "exact", "elements": [{"id": "m", "name": "́Kira"}], "text": "x́Kira ́Kira", "want": [[7, 5, "m"]]},
    {"name": "[Kira] at the end", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "see [Kira]", "want": [[5, 4, "k"]]},
    {"name": "[Kira]( at the end", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "see [Kira](", "want": [[5, 4, "k"]]},
    {"name": "a target with nested parentheses", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "[a](b(Kira)) Kira) Kira", "want": [[13, 4, "k"], [19, 4, "k"]]},
    {"name": "an empty label and target", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "Kira []() Kira [](Kira) Kira", "want": [[0, 4, "k"], [10, 4, "k"], [24, 4, "k"]]},
    {"name": "a letter number after the name", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "KiraⅠ and KiraⅧx", "want": [[0, 4, "k"], [10, 4, "k"]]},
    {"name": "a decimal digit of another script after the name", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "Kira٣ and Kira१ and Kira.", "want": [[20, 4, "k"]]},
    {"name": "folded, a name ending in a Deseret letter", "case": "folded", "elements": [{"id": "d", "name": "Ab𐐀"}], "text": "ab𐐨 AB𐐀 ab𐐀x", "want": [[0, 3, "d"], [4, 3, "d"]]},
    {"name": "folded, U+A7C4 inside a name", "case": "folded", "elements": [{"id": "a", "name": "aꟄbc"}], "text": "aꟄbc aꞔbc", "want": [[0, 4, "a"], [5, 4, "a"]]},
    {"name": "exact, abc beside U+A7C4", "case": "exact", "elements": [{"id": "a", "name": "abc"}], "text": "Ꞔabc x abc", "want": [[7, 3, "a"]]},
    {"name": "folded, the Kelvin sign is three bytes and k one", "case": "folded", "elements": [{"id": "k", "name": "Kelvin"}], "text": "Kelvin and kelvin", "want": [[0, 6, "k"], [11, 6, "k"]]},
    {"name": "an address beside a name", "case": "exact", "elements": [{"id": "k", "name": "Kira"}], "text": "Kira ow://character/Kira Kira", "want": [[0, 4, "k"], [20, 4, "k"], [25, 4, "k"]]}
    ])json";
    OWJsonParseResult r = OWJson::Parse(std::string_view(silences));
    if (!r.ok) return t.Fail("the silences parse: " + r.error);
    t.Eq(r.value.Size(), size_t(16), "sixteen silences");
    for (const OWJsonValue& c : r.value.Items()) {
        const OWJsonValue& elements = *c.Find("elements");
        const std::string text = Str(c, "text");
        const std::vector<OWNameFound> found = OWNameFinder::Of(elements, CaseOf(Str(c, "case"))).Find(text);
        std::ostringstream got, want;
        for (const OWNameFound& f : found)
            got << "[" << Points(text, 0, f.start) << "," << Points(text, f.start, f.end) << "," << IdAt(elements, f.element) << "]";
        for (const OWJsonValue& w : c.Find("want")->Items())
            want << "[" << static_cast<int64_t>(w.Items()[0].AsDouble()) << "," << static_cast<int64_t>(w.Items()[1].AsDouble())
                 << "," << w.Items()[2].Text() << "]";
        t.Eq(got.str(), want.str(), Str(c, "name"));
    }
}

OW_TEST(name_finder_bytes_that_are_not_utf8) {
    // No other package reads bytes: here a byte that does not begin a whole UTF-8 sequence is one U+FFFD, one byte
    // wide, no word character. A lead byte whose continuation is missing never takes the ASCII after it.
    OWNameFinder find = OWNameFinder::Of(List({Element("k", "Kira")}), OWNameCase::Exact);
    const std::string a = "\xFF" "Kira";
    const std::string b = "\xE2" "Kira \x80Kira \xF0\x9F" "Kira";
    std::vector<OWNameFound> fa = find.Find(a), fb = find.Find(b);
    t.Eq(fa.size(), size_t(1), "after a byte that begins nothing");
    if (!fa.empty()) t.Eq(fa[0].start, size_t(1), "at byte 1");
    t.Eq(fb.size(), size_t(3), "after a lead with no continuation, a lone continuation, a sequence cut short");
    if (fb.size() == 3) {
        t.Eq(fb[0].start, size_t(1), "first");
        t.Eq(fb[1].start, size_t(7), "second");
        t.Eq(fb[2].start, size_t(14), "third");
    }
}
