// OWReport::ThreadsOf on the report profile's conformance fixture (schema-dist profiles/report.fixture.json, dist.136's
// copy, its sha256 held to MANIFEST.json): every thread case by name, its own test row, the want's three parts compared
// as the fixture says (`parents` per report, `groups` and `lints` each as a set, a group's reports in order), and every
// talk case (serial 135), the talks a reader shows a report with (OWReport::TalksOf), as a set. Then the
// corners dist.120's fixture held no case for, each answered as the npm SDK 4.159.0's reportThreadsOf and the Python
// package 0.120.0's report_threads_of answer it (their own tests' vectors, read, not run here); dist.121's fixture holds
// a case for the first four, named on each.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"
#include "OWMessage.h"
#include "OWReport.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.136's MANIFEST.json records it.
const char* const DistSha256 = "0d83ce1ee76f93c79b786bb10e1e97bfb94c6983a8b3b00d1867e65f03722e94";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/report.fixture.json"; }

OWJsonValue ParseText(const std::string& text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? std::move(r.value) : OWJsonValue();
}

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = [] {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }();
    return doc;
}

std::vector<const OWJsonValue*> Cases() {
    const OWJsonValue* cases = Fixture()->Find("thread_cases");
    return cases ? OWMessage::List(*cases) : std::vector<const OWJsonValue*>();
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }
std::string Str(const OWJsonValue& v, const char* key) { return Str(v.Find(key)); }

std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    return out;
}

// A list's items as compact JSON, sorted: the fixture's "compare as a set" (a group's reports keep their order inside
// its text).
std::vector<std::string> AsSet(const OWJsonValue* list) {
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& v : list->Items()) out.push_back(Compact(v));
    std::sort(out.begin(), out.end());
    return out;
}

std::string Joined(const std::vector<std::string>& items) {
    std::string s;
    for (const std::string& i : items) s += (s.empty() ? "" : " ") + i;
    return s;
}

// `parents` as id to kind.
std::map<std::string, std::string> Parents(const OWJsonValue* object) {
    std::map<std::string, std::string> out;
    if (object && object->IsObject())
        for (size_t i = 0; i < object->Size(); ++i) out[object->Keys()[i]] = Str(&object->Items()[i]);
    return out;
}

// The three parts, each as the fixture compares it.
void SameAnswer(owtest::T& t, const OWJsonValue& got, const OWJsonValue& want, const std::string& name) {
    t.Eq(Compact(*got.Find("parents")), Compact(*want.Find("parents")), name + ": parents, in the order given");
    t.Ok(Parents(got.Find("parents")) == Parents(want.Find("parents")), name + ": parents, per report");
    t.Eq(Joined(AsSet(got.Find("groups"))), Joined(AsSet(want.Find("groups"))), name + ": groups, as a set");
    t.Eq(Joined(AsSet(got.Find("lints"))), Joined(AsSet(want.Find("lints"))), name + ": lints, as a set");
}

void Case(owtest::T& t, const OWJsonValue& c) {
    const std::string name = Str(c, "name");
    const OWJsonValue* narratives = c.Find("narratives");
    const OWJsonValue* want = c.Find("want");
    if (!narratives || !narratives->IsArray() || !want || !want->Find("parents") || !want->Find("groups") ||
        !want->Find("lints"))
        return t.Fail(name + ": a case with narratives and a want of three parts");
    SameAnswer(t, OWReport::ThreadsOf(OWMessage::List(*narratives)).ToJson(), *want, name);
}

// A body list from JSON text, kept alive beside the pointers ThreadsOf reads.
struct Given {
    OWJsonValue doc;
    std::vector<const OWJsonValue*> list;
    explicit Given(const std::string& json_array) : doc(ParseText(json_array)), list(OWMessage::List(doc)) {}
};

std::string Answer(const std::string& json_array) {
    Given g(json_array);
    return Compact(OWReport::ThreadsOf(g.list).ToJson());
}

// A report body: the pair, the mark, a time; `extra` is more JSON members, written after a comma.
std::string R(const std::string& id, const std::string& extra = "") {
    return R"({"id": ")" + id + R"(", "type": "narrative", "supertype": "Report", "x_room_of": "w1", )" +
           R"("created_at": "2026-10-26T09:00:00Z")" + (extra.empty() ? "" : ", " + extra) + "}";
}
std::string B(const std::string& id) {
    return R"({"id": ")" + id + R"(", "type": "narrative", "supertype": "Board", "x_room_of": "w1"})";
}

}  // namespace

OW_TEST(report_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the report fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.136's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/report.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("report"), "the report profile's fixture");
    const OWJsonValue* pair = doc->Find("pair");
    const OWJsonValue* board = doc->Find("board_pair");
    const OWJsonValue* mark = doc->Find("mark");
    t.Ok(pair && Str(*pair, "type") == OWReport::Type && Str(*pair, "supertype") == OWReport::Supertype, "the pair");
    t.Ok(board && Str(*board, "type") == OWReport::Type && Str(*board, "supertype") == OWReport::BoardSupertype,
         "the board pair");
    t.Eq(mark ? Str(*mark, "field") : std::string(), std::string(OWLens::RoomMark), "the mark is the room's");
    const std::vector<std::string>& rooms = OWLens::RoomPairs();
    t.Ok(std::find(rooms.begin(), rooms.end(), "narrative/report") != rooms.end() &&
             std::find(rooms.begin(), rooms.end(), "narrative/board") != rooms.end(),
         "both pairs are room pairs");
}

OW_TEST(report_fixture_holds_twenty_nine_thread_cases) {
    std::set<std::string> names;
    size_t whole = 0;
    for (const OWJsonValue* c : Cases()) {
        names.insert(Str(*c, "name"));
        const OWJsonValue* want = c->Find("want");
        if (c->Find("narratives") && want && want->Find("parents") && want->Find("groups") && want->Find("lints")) ++whole;
    }
    t.Eq(Cases().size(), size_t(29), "29 thread cases (dist.122)");
    t.Eq(names.size(), size_t(29), "each name its own");
    t.Eq(whole, size_t(29), "every case has narratives and a want of three parts");
}

OW_CASES(report_fixture_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (Cases().empty()) {
        out.emplace_back("report_fixture:not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue* c : Cases())
        out.emplace_back("report_fixture:" + Str(*c, "name"), [doc, c](owtest::T& t) { Case(t, *c); });
    return out;
}

// -- the talk cases (serial 135): the talks on an element a reader shows a report with ------------------------------

namespace {

std::vector<const OWJsonValue*> TalkCases() {
    const OWJsonValue* cases = Fixture()->Find("talk_cases");
    return cases ? OWMessage::List(*cases) : std::vector<const OWJsonValue*>();
}

}  // namespace

// The talk list is read whole: a case added later must not pass unseen.
OW_TEST(report_fixture_holds_six_talk_cases) {
    std::set<std::string> names;
    for (const OWJsonValue* c : TalkCases()) names.insert(Str(*c, "name"));
    t.Eq(TalkCases().size(), size_t(6), "6 talk cases (serial 135)");
    t.Eq(names.size(), size_t(6), "each name its own");
}

OW_CASES(report_talk_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    if (TalkCases().empty()) {
        out.emplace_back("report_talk:not-found", [](owtest::T& t) { t.Fail("the talk cases did not load"); });
        return out;
    }
    for (const OWJsonValue* c : TalkCases())
        out.emplace_back("report_talk:" + Str(*c, "name"), [doc, c](owtest::T& t) {
            const OWJsonValue* report = c->Find("report");
            if (!report) return t.Fail("no report");
            std::set<std::string> got, want;
            bool twice = false;
            for (const OWLens::OWTalk& k : OWReport::TalksOf(*report))
                if (!got.insert(k.type + " " + k.id).second) twice = true;
            if (const OWJsonValue* talks = c->Find("talks"); talks && talks->IsArray())
                for (const OWJsonValue& k : talks->Items()) want.insert(Str(k, "type") + " " + Str(k, "id"));
            t.Ok(!twice, "each talk once");
            t.Ok(got == want, "the talks, as a set");
        });
    return out;
}

// -- the corners dist.120's fixture held no case for (dist.121's holds the first four) ----------------------------

// A created_at with no offset reads as UTC (the message profile's order, OWMessage::InCreatedOrder): 09:30 with no
// zone comes after 10:00+02:00 (08:00Z) and before 09:45Z. The fixture's case:
// thread-created-at-with-no-offset-is-read-as-utc.
OW_TEST(report_threads_a_created_at_with_no_offset_reads_as_utc) {
    std::string got = Answer("[" + R("r0") + ", " +
                             R("a", R"("parent_narrative": "r0", "created_at": "2026-10-26T09:45:00Z")") + ", " +
                             R("b", R"("parent_narrative": "r0", "created_at": "2026-10-26T09:30:00")") + ", " +
                             R("c", R"("parent_narrative": "r0", "created_at": "2026-10-26T10:00:00+02:00")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r0": "none","a": "report","b": "report","c": "report"},"groups": [{"parent": "r0","kind": "report","reports": ["c","b","a"]}],"lints": []})"),
         "the answer");
}

// Of two elements with one id, the first is read and the later copy is ignored entirely: no report and no parent.
// Here the first p is a board entry, so r1 reads board; the second r1 (naming x) is not read at all. The npm SDK's
// vector. The fixture's case (two reports under one id): one-id-given-twice-keeps-the-first.
OW_TEST(report_threads_of_two_with_one_id_the_first_is_read) {
    std::string got = Answer("[" + B("p") + ", " + R("p") + ", " + R("r1", R"("parent_narrative": "p")") + ", " +
                             R("r1", R"("parent_narrative": "x")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r1": "board"},"groups": [{"parent": "p","kind": "board","reports": ["r1"]}],"lints": []})"),
         "the answer");
    // The later copy of a report is not a second report either: one row in parents, one in the group.
    got = Answer("[" + R("r0") + ", " + R("r1", R"("parent_narrative": "r0")") + ", " +
                 R("r1", R"("parent_narrative": "r0", "created_at": "2026-10-26T08:00:00Z")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r0": "none","r1": "report"},"groups": [{"parent": "r0","kind": "report","reports": ["r1"]}],"lints": []})"),
         "a repeated report");
}

// A parent that is no string reads none and is never matched as text: a number, true, a list, and an expanded parent
// object ({"id": "r0"}, the shape an expanded link takes), beside r0 itself. The fixture's case (a number, a list, an
// object): parent-that-is-not-a-string.
OW_TEST(report_threads_a_parent_that_is_no_string_reads_none) {
    std::string got = Answer("[" + R("r0") + ", " + R("r1", R"("parent_narrative": 7)") + ", " +
                             R("r2", R"("parent_narrative": true)") + ", " + R("r3", R"("parent_narrative": ["r0"])") +
                             ", " + R("r4", R"("parent_narrative": {"id": "r0"})") + ", " +
                             R("r5", R"("parent_narrative": null)") + ", " + R("r6") + "]");
    t.Eq(got, std::string(R"({"parents": {"r0": "none","r1": "none","r2": "none","r3": "none","r4": "none","r5": "none","r6": "none"},"groups": [],"lints": []})"),
         "the answer (r6: no parent_narrative key)");
}

// A parent with content is read as written, never trimmed before the lookup: " r0 " and "r0\n" read missing beside an
// r0; one empty once trimmed of White_Space (a tab, U+3000, U+00A0, a line break) reads none. The fixture's case:
// parent-is-a-held-id-with-a-space-at-each-end.
OW_TEST(report_threads_a_padded_parent_is_read_as_written) {
    std::string got = Answer("[" + R("r0") + ", " + R("r1", R"("parent_narrative": " r0 ")") + ", " +
                             R("r2", R"("parent_narrative": "r0\n")") + ", " +
                             R("r3", R"("parent_narrative": "\t\u3000\u00a0\n")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r0": "none","r1": "missing","r2": "missing","r3": "none"},"groups": [],"lints": []})"),
         "the answer");
}

// What is no element: a value that is no object, and a body with no non-empty string id, are passed over (no report,
// no parent: a report naming one reads missing, or none for ""). The npm SDK's and the Python package's vectors.
OW_TEST(report_threads_what_is_no_element_is_passed_over) {
    std::string got = Answer(R"([null, 7, "r1", ["r1"], true, )" + R("r1") + ", " +
                             R("r2", R"("parent_narrative": "r1")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r1": "none","r2": "report"},"groups": [{"parent": "r1","kind": "report","reports": ["r2"]}],"lints": []})"),
         "values that are no object");
    got = Answer(R"([{"type": "narrative", "supertype": "Report", "x_room_of": "w1"}, )" + R("") + ", " +
                 R"({"id": 42, "type": "narrative", "supertype": "Report", "x_room_of": "w1"}, )" +
                 R("r1", R"("parent_narrative": "")") + ", " + R("r2", R"("parent_narrative": "42")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r1": "none","r2": "missing"},"groups": [],"lints": []})"), "bodies with no id");
}

// The pair is trimmed and folded, the whole word, and the mark read by presence, as OWLens::IsRoomElement reads the
// room profile's pairs: " Narrative " + "REPORT" is a report, "Reports" is not, a mark "  " is none and 0 is one. A
// message naming a report, with the mark or without, is in no part of the answer. The npm SDK's vectors.
OW_TEST(report_threads_the_pair_and_mark_are_the_room_profiles) {
    std::string got = Answer("[" + R("r0") + ", " +
                             R("r1", R"("type": " Narrative ", "supertype": "REPORT", "parent_narrative": "r0")") + ", " +
                             R("r2", R"("supertype": "Reports", "parent_narrative": "r0")") + ", " +
                             R("r3", R"("x_room_of": "  ", "parent_narrative": "r0")") + ", " +
                             R("r4", R"("x_room_of": 0, "parent_narrative": "r0")") + ", " +
                             R("r5", R"("type": "event", "parent_narrative": "r0")") + ", " +
                             R"({"id": "m1", "type": "narrative", "supertype": "Message", "x_room_of": "w1", "parent_narrative": "r0"}, )" +
                             R"({"id": "m2", "type": "narrative", "supertype": "Message", "parent_narrative": "r0"}])");
    t.Eq(got, std::string(R"({"parents": {"r0": "none","r1": "report","r4": "report"},"groups": [{"parent": "r0","kind": "report","reports": ["r1","r4"]}],"lints": []})"),
         "the answer");
}

// A known-of stub served without the pair and the mark is neither a report nor a board entry: as a parent it reads
// other. The Python package's vector.
OW_TEST(report_threads_a_stub_is_read_as_served) {
    std::string got = Answer(R"([{"id": "s1", "type": "narrative", "name": "Earlier", "known_of": true}, )"
                             R"({"id": "b1", "type": "narrative", "supertype": "Board", "known_of": true}, )" +
                             R("r1", R"("parent_narrative": "s1")") + ", " + R("r2", R"("parent_narrative": "b1")") + "]");
    t.Eq(got, std::string(R"({"parents": {"r1": "other","r2": "other"},"groups": [],"lints": []})"), "the answer");
}

// No chain when the parent report's own parent is not a report the reader holds: unheld, lore, a Report without the
// mark, or a board entry. The Python package's four vectors.
OW_TEST(report_threads_a_chain_needs_a_held_report_above) {
    const std::vector<std::pair<std::string, std::string>> above = {
        {"", "missing"},
        {R"({"id": "a", "type": "narrative", "supertype": "Chapter"}, )", "other"},
        {R"({"id": "a", "type": "narrative", "supertype": "Report"}, )", "other"},
        {B("a") + ", ", "board"},
    };
    for (const auto& [first, reads] : above) {
        std::string got = Answer("[" + first + R("b", R"("parent_narrative": "a")") + ", " +
                                 R("c", R"("parent_narrative": "b")") + "]");
        t.Eq(got.substr(0, got.find(R"(,"groups")")),
             std::string(R"({"parents": {"b": ")") + reads + R"(","c": "report"})", reads + ": parents");
        t.Ok(got.find(R"("lints": [])") != std::string::npos, reads + ": no lint (" + got + ")");
    }
}
