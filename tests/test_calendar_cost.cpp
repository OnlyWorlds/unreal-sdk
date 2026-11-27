// A stored calendar does not set what one date label costs: the npm SDK's and the Python package's shared cases
// (tests/data/calendar-cost.cases.json, the same bytes, held by its sha256): a long run of white space in a template or
// a name, a long run of marks at a label's edge, and a month of millions of days, each answered with its words inside
// the cases' line. A roundtrip case reads its label back with parseDate, which this core does not have: its format
// half is replayed and timed, and the read is left to the packages that hold it.
// Since serial 149 the calendar profile bounds a template or a name at 256 code points, and five of the cases hold one
// of thousands: the bounded reader refuses them, so each case is read past the bounds (ReadConfigPastTheBounds, private
// to the core). A config built in code never meets the bounds, so the formatter must stay linear on its own.

#include "ow_test.h"
#include "sha256.h"

#include "../Plugin/OnlyWorlds/Core/Private/OWCalendarPastTheBounds.h"
#include "OWCalendar.h"
#include "OWFiles.h"
#include "OWJson.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/calendar-cost.cases.json at 4.201.2 (69bd5bb); the Python package holds the same bytes.
const char* const CasesSha256 = "1e326d378cbdc6db91deba2219ed07cf67ad72211653bd25219f963fc81ebc0f";

const OWBytes& CasesBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/calendar-cost.cases.json", bytes);
        read = true;
    }
    return bytes;
}

std::shared_ptr<OWJsonValue> Doc() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWJsonParseResult r = OWJson::Parse(CasesBytes());
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

const OWJsonValue& Get(const OWJsonValue& v, const char* key) {
    static const OWJsonValue none;
    const OWJsonValue* x = v.Find(key);
    return x ? *x : none;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

// A value with each {"$repeat": [[piece, times], ...]} made its pieces joined, each repeated.
OWJsonValue Expand(const OWJsonValue& v) {
    if (v.IsArray()) {
        OWJsonValue out = OWJsonValue::Array();
        for (const OWJsonValue& x : v.Items()) out.Push(Expand(x));
        return out;
    }
    if (!v.IsObject()) return v;
    if (const OWJsonValue* rep = v.Find("$repeat")) {
        std::string s;
        for (const OWJsonValue& p : rep->Items()) {
            const std::string& piece = p.Items()[0].Text();
            for (int64_t i = 0, n = static_cast<int64_t>(p.Items()[1].AsDouble()); i < n; ++i) s += piece;
        }
        return OWJsonValue::String(s);
    }
    OWJsonValue out = OWJsonValue::Object();
    for (size_t i = 0; i < v.Keys().size(); ++i) out.Set(v.Keys()[i], Expand(v.Items()[i]));
    return out;
}

double LineMs() { return Get(*Doc(), "underMs").AsDouble(); }

}  // namespace

OW_TEST(calendar_cost_cases_file_is_the_pinned_one) {
    t.Eq(owtest::Sha256Hex(CasesBytes()), std::string(CasesSha256), "the cases file's sha256 (the npm SDK's bytes)");
    t.Ok(Get(*Doc(), "cases").Size() >= 8, "at least 8 cases");
    t.Eq(LineMs(), 50.0, "the npm SDK's line, 50 ms");
    // The bounded reader refuses the cases past its bounds (serial 149), and the unbounded one reads every case.
    size_t bounded = 0, unbounded = 0;
    for (const OWJsonValue& c : Get(*Doc(), "cases").Items()) {
        const OWJsonValue cfg = Expand(Get(c, "cfg"));
        if (OWCalendar::ReadConfig(cfg)) ++bounded;
        if (OWCalendar::ReadConfigPastTheBounds(cfg)) ++unbounded;
    }
    t.Eq(unbounded, Get(*Doc(), "cases").Size(), "every case reads past the bounds");
    t.Eq(Get(*Doc(), "cases").Size() - bounded, size_t(5), "five cases are past the bounds (a template or a name of thousands)");
}

OW_CASES(calendar_cost_cases) {
    owtest::Cases out;
    auto doc = Doc();
    for (const OWJsonValue& c : Get(*doc, "cases").Items()) {
        const OWJsonValue* cp = &c;
        out.push_back({"calendar cost: " + Str(c, "name"), [doc, cp](owtest::T& t) {
                           std::optional<OWCalendarConfig> cal = OWCalendar::ReadConfigPastTheBounds(Expand(Get(*cp, "cfg")));
                           if (!cal) return t.Fail("the case is a calendar the reader accepts, the size bounds aside");
                           const int64_t step = static_cast<int64_t>(Get(*cp, "step").AsDouble());
                           const std::string call = Str(*cp, "call");
                           const OWJsonValue want = Expand(Get(*cp, "want"));
                           auto t0 = std::chrono::steady_clock::now();
                           std::optional<std::string> got;
                           if (call == "weekday") got = OWCalendar::WeekdayForStep(*cal, step);
                           else got = OWCalendar::FormatStep(*cal, step);
                           double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                           // A roundtrip's want is [label, {step}]: its label is held here, its read back is not.
                           const OWJsonValue& words = call == "roundtrip" ? want.Items()[0] : want;
                           t.Ok(got.has_value(), "an answer");
                           t.Eq(got.value_or(std::string("(none)")), words.Text(), "its words");
                           char line[96];
                           std::snprintf(line, sizeof line, "%.3f ms, the line %.0f ms", ms, LineMs());
                           t.Ok(ms < LineMs(), line);
                           std::printf("       %.3f ms: %s\n", ms, Str(*cp, "name").c_str());
                       }});
    }
    return out;
}
