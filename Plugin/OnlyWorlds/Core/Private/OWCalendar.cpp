#include "OWCalendar.h"

#include "OWUnicode.h"

#include <algorithm>
#include <charconv>
#include <cmath>

// A port of the TypeScript SDK's calendar kernel (@onlyworlds/sdk src/calendar/calendar.ts and index.ts, 4.102.4),
// which the profile names as its reference. Where a line mirrors one of its rules, the comment says which.

namespace OnlyWorlds {
namespace OWCalendar {

const char* const Field = "atlas_calendar";
const char* const Supertype = "calendar";

namespace {

constexpr double MaxSafe = 9007199254740991.0;  // 2^53 - 1, Number.MAX_SAFE_INTEGER

// -- JSON values as JavaScript sees them ----------------------------------------------------------------------------

// Whether a number's magnitude is 1 or more, from its text alone (for a number past a double's range).
bool AtLeastOne(std::string_view text) {
    if (!text.empty() && text[0] == '-') text.remove_prefix(1);
    size_t e = text.find_first_of("eE");
    long long exponent = 0;
    if (e != std::string_view::npos) {
        std::string_view x = text.substr(e + 1);
        bool negative = !x.empty() && x[0] == '-';
        if (!x.empty() && (x[0] == '-' || x[0] == '+')) x.remove_prefix(1);
        for (char c : x) exponent = std::min(exponent * 10 + (c - '0'), 1000000000LL);
        if (negative) exponent = -exponent;
        text = text.substr(0, e);
    }
    size_t point = text.find('.');
    std::string_view whole = text.substr(0, point);
    std::string_view fraction = point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    long long place;
    if (size_t first = whole.find_first_not_of('0'); first != std::string_view::npos) {
        place = static_cast<long long>(whole.size() - first) - 1;
    } else if (size_t f = fraction.find_first_not_of('0'); f != std::string_view::npos) {
        place = -static_cast<long long>(f) - 1;
    } else {
        return false;
    }
    return place + exponent >= 0;
}

// A JSON number as JSON.parse reads it: the nearest double, Infinity past the range, 0 below it.
double JsNumber(const OWJsonValue& v) {
    const std::string& text = v.Text();
    double d = 0;
    std::from_chars_result r = std::from_chars(text.data(), text.data() + text.size(), d);
    if (r.ec == std::errc::result_out_of_range) {
        bool negative = !text.empty() && text[0] == '-';
        if (AtLeastOne(text)) return negative ? -INFINITY : INFINITY;
        return negative ? -0.0 : 0.0;
    }
    return d;
}

bool Nullish(const OWJsonValue* v) { return v == nullptr || v->IsNull(); }

// A property as JavaScript reads `obj.key`: only an object has keys (nullptr is undefined).
const OWJsonValue* Prop(const OWJsonValue* obj, std::string_view key) {
    return obj && obj->IsObject() ? obj->Find(key) : nullptr;
}

// JavaScript truthiness of a JSON value (undefined is false).
bool Truthy(const OWJsonValue* v) {
    if (!v) return false;
    switch (v->kind()) {
        case OWJsonValue::Kind::Null: return false;
        case OWJsonValue::Kind::Bool: return v->AsBool();
        case OWJsonValue::Kind::Number: return JsNumber(*v) != 0;
        case OWJsonValue::Kind::String: return !v->Text().empty();
        default: return true;
    }
}

bool IsString(const OWJsonValue* v) { return v && v->IsString(); }
bool IsArray(const OWJsonValue* v) { return v && v->IsArray(); }

// Number.isInteger: a finite whole number (no bound).
bool IsInteger(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return false;
    double d = JsNumber(*v);
    return std::isfinite(d) && std::floor(d) == d;
}

// Number.isSafeInteger: a whole number within 2^53 - 1.
bool IsSafeInteger(const OWJsonValue* v) {
    if (!IsInteger(v)) return false;
    return std::fabs(JsNumber(*v)) <= MaxSafe;
}

bool IsPositiveInt(const OWJsonValue* v) { return IsSafeInteger(v) && JsNumber(*v) >= 1; }

int64_t Int(const OWJsonValue* v) { return static_cast<int64_t>(JsNumber(*v)); }

// A JavaScript number equal (===) to n.
bool NumberIs(const OWJsonValue* v, double n) { return v && v->IsNumber() && JsNumber(*v) == n; }

// String(value), as JavaScript writes one: a number by Number::toString, an array joined by commas (null as ""), an
// object as "[object Object]".
std::string JsString(const OWJsonValue& v) {
    switch (v.kind()) {
        case OWJsonValue::Kind::Null: return "null";
        case OWJsonValue::Kind::Bool: return v.AsBool() ? "true" : "false";
        case OWJsonValue::Kind::Number: {
            double d = JsNumber(v);
            if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
            return OWJson::NumberToString(d);
        }
        case OWJsonValue::Kind::String: return v.Text();
        case OWJsonValue::Kind::Array: {
            std::string out;
            for (size_t i = 0; i < v.Items().size(); ++i) {
                if (i) out += ',';
                if (!v.Items()[i].IsNull()) out += JsString(v.Items()[i]);
            }
            return out;
        }
        case OWJsonValue::Kind::Object: return "[object Object]";
    }
    return std::string();
}

// The text JSON.parse takes: a byte order mark is not JSON white space there (OWJson::Parse reads past one).
std::optional<OWJsonValue> JsonParse(const std::string& text) {
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) return std::nullopt;
    OWJsonParseResult r = OWJson::Parse(std::string_view(text));
    if (!r.ok) return std::nullopt;
    return std::move(r.value);
}

// -- the leap rule -----------------------------------------------------------------------------------------------

struct LeapRule {
    int64_t n = 1;
    std::vector<int64_t> except;
    int64_t offset = 0;
};

// calendar.ts leapRule / leapRules: the entries with every_years, in list order.
std::vector<LeapRule> LeapRules(const OWCalendarConfig& cal, bool only_out_of_week) {
    std::vector<LeapRule> out;
    for (const OWCalendarIntercalary& ic : cal.intercalary) {
        if (only_out_of_week && ic.in_week) continue;
        if (!ic.every_years) continue;
        out.push_back({*ic.every_years, ic.except, ic.offset});
    }
    return out;
}

// ruleHolds: floorMod(y + offset, n) === 0, flipped by each except dividing it.
bool RuleHolds(const LeapRule& r, int64_t y) {
    int64_t Y = y + r.offset;
    bool on = FloorMod(Y, r.n) == 0;
    for (int64_t d : r.except)
        if (FloorMod(Y, d) == 0) on = !on;
    return on;
}

// multiplesIn: how many k in [a, b) have floorMod(k + offset, m) === 0.
int64_t MultiplesIn(int64_t a, int64_t b, int64_t m, int64_t offset) {
    return FloorDiv(b - 1 + offset, m) - FloorDiv(a - 1 + offset, m);
}

// leapYearsBefore: the signed count of the years the rule selects in [0, y), or minus those in [y, 0).
int64_t LeapYearsBefore(int64_t y, const LeapRule& r) {
    int64_t a = y >= 0 ? 0 : y, b = y >= 0 ? y : 0, sign = y >= 0 ? 1 : -1;
    int64_t count = MultiplesIn(a, b, r.n, r.offset);
    int64_t s = -1;
    for (int64_t d : r.except) {
        count += s * MultiplesIn(a, b, d, r.offset);
        s = -s;
    }
    return sign * count;
}

double MeanLeap(const LeapRule& r) {
    double m = 1.0 / static_cast<double>(r.n);
    double s = -1;
    for (int64_t d : r.except) {
        m += s / static_cast<double>(d);
        s = -s;
    }
    return m;
}

// yearShape's base: the months and every day outside them with no leap rule (a day past the last month counts a step
// here though no slot holds it, as the SDK counts it); without months, bundles.year.steps or 1.
int64_t BaseLength(const OWCalendarConfig& cal) {
    if (!cal.has_months) return cal.year_steps.value_or(1);
    int64_t base = 0;
    for (int64_t len : cal.month_lengths) base += len;
    for (const OWCalendarIntercalary& ic : cal.intercalary)
        if (!ic.every_years) ++base;
    return base;
}

constexpr int MaxYearCorrection = 10000;

// -- the year's days ----------------------------------------------------------------------------------------------

struct Position {
    int64_t abs_year = 0;
    int64_t offset = 0;
    enum class Kind { None, Month, Intercalary } kind = Kind::None;
    int64_t month = 0, day = 0;
    size_t ic = 0;
    int64_t skipped_in_year = 0;  // the days outside the week before this one in its year (in_week false)
};

// positionOf with yearSlots walked by arithmetic: the days before month 1, then each month and the days after it, in
// the list's order. A step past every slot (a day after a month the calendar does not have) has no slot.
Position PositionOf(const OWCalendarConfig& cal, int64_t step) {
    Position p;
    p.abs_year = AbsoluteYear(cal, step);
    p.offset = step - YearStartStep(cal, p.abs_year);
    if (!cal.has_months) return p;
    int64_t at = 0;
    auto days_after = [&](int64_t month) -> bool {
        for (size_t i = 0; i < cal.intercalary.size(); ++i) {
            const OWCalendarIntercalary& ic = cal.intercalary[i];
            if (ic.after_month != month || !IntercalaryInYear(cal, ic, p.abs_year)) continue;
            if (at == p.offset) {
                p.kind = Position::Kind::Intercalary;
                p.ic = i;
                return true;
            }
            if (!ic.in_week) ++p.skipped_in_year;
            ++at;
        }
        return false;
    };
    if (days_after(0)) return p;
    for (size_t m = 0; m < cal.month_lengths.size(); ++m) {
        int64_t len = cal.month_lengths[m];
        if (p.offset >= at && p.offset - at < len) {
            p.kind = Position::Kind::Month;
            p.month = static_cast<int64_t>(m) + 1;
            p.day = p.offset - at + 1;
            return p;
        }
        at += len;
        if (days_after(static_cast<int64_t>(m) + 1)) return p;
    }
    return p;
}

std::optional<std::string> Weekday(const OWCalendarConfig& cal, int64_t step, const Position& pos) {
    if (!cal.has_week || cal.week_length < 1 || static_cast<int64_t>(cal.week_names.size()) != cal.week_length)
        return std::nullopt;
    if (cal.week_nested) {
        if (pos.kind != Position::Kind::Month) return std::nullopt;
        return cal.week_names[static_cast<size_t>(FloorMod(pos.day - 1, cal.week_length))];
    }
    // Free: the counted steps leave out the in_week false days; counted 0 is names[0].
    if (pos.kind == Position::Kind::Intercalary && !cal.intercalary[pos.ic].in_week) return std::nullopt;
    int64_t base = 0;
    for (const OWCalendarIntercalary& ic : cal.intercalary)
        if (!ic.in_week && !ic.every_years) ++base;
    int64_t before = pos.abs_year * base;
    for (const LeapRule& r : LeapRules(cal, true)) before += LeapYearsBefore(pos.abs_year, r);
    int64_t counted = step - before - pos.skipped_in_year;
    return cal.week_names[static_cast<size_t>(FloorMod(counted, cal.week_length))];
}

// -- the template -------------------------------------------------------------------------------------------------

constexpr char32_t EmptyKnown = 0xE000;  // the SDK's marker for a known token with no value (never NUL)
constexpr char32_t MiddleDot = 0x00B7;

// JavaScript's \s (and String.prototype.trim's set): WhiteSpace and LineTerminator. Not U+0085, which Unicode's
// White_Space holds; U+FEFF, which it does not.
bool JsSpace(char32_t c) {
    switch (c) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680: case 0x2028:
        case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}
bool Sep(char32_t c) { return c == ',' || c == ';' || c == MiddleDot; }

const std::optional<std::string>* TokenValue(const OWCalendarTokens& v, std::string_view token) {
    if (token == "day") return &v.day;
    if (token == "month") return &v.month;
    if (token == "weekday") return &v.weekday;
    if (token == "year") return &v.year;
    if (token == "era") return &v.era;
    if (token == "step") return &v.step;
    if (token == "intercalary") return &v.intercalary;
    return nullptr;
}

}  // namespace

// -- reading the value --------------------------------------------------------------------------------------------

std::optional<OWCalendarConfig> ReadValue(const OWJsonValue& raw) {
    if (raw.IsNull()) return std::nullopt;
    if (raw.IsString()) {
        std::optional<OWJsonValue> parsed = JsonParse(raw.Text());
        if (!parsed) return std::nullopt;
        return ReadConfig(*parsed);
    }
    return ReadConfig(raw);
}

std::optional<OWCalendarConfig> ReadConfig(const OWJsonValue& value) {
    // readCalendarConfig, rule by rule; an array is an object to typeof, and has no version.
    if (!value.IsObject()) return std::nullopt;
    const OWJsonValue* cfg = &value;
    const OWJsonValue* version = cfg->Find("version");
    if (!NumberIs(version, 1) && !NumberIs(version, 2)) return std::nullopt;
    const OWJsonValue* grain = cfg->Find("grain");
    // The profile's grain is a string, and an empty string is no grain.
    if (!IsString(grain) || grain->Text().empty()) return std::nullopt;

    const OWJsonValue* bundles = cfg->Find("bundles");
    // months: absent when falsy (null, false, 0, ""), as `if (months)` reads it.
    const OWJsonValue* months = Prop(bundles, "months");
    const OWJsonValue *names = nullptr, *lengths = nullptr;
    if (Truthy(months)) {
        names = Prop(months, "names");
        lengths = Prop(months, "lengths");
        if (!IsArray(names) || !IsArray(lengths) || names->Size() != lengths->Size() || names->Size() == 0)
            return std::nullopt;
        for (const OWJsonValue& n : names->Items())
            if (!n.IsString()) return std::nullopt;
    }
    // eras and intercalary: absent only for null; anything else that is not a list refuses (false, 0, "" too).
    const OWJsonValue* eras = cfg->Find("eras");
    if (!Nullish(eras) && !eras->IsArray()) return std::nullopt;
    if (!Nullish(eras)) {
        for (const OWJsonValue& e : eras->Items()) {
            const OWJsonValue* end = Prop(&e, "end");
            const OWJsonValue* base = Prop(&e, "year_base");
            if (!IsString(Prop(&e, "name")) || !IsSafeInteger(Prop(&e, "start")) ||
                (!Nullish(end) && !IsSafeInteger(end)) || (!Nullish(base) && !NumberIs(base, 0) && !NumberIs(base, 1)))
                return std::nullopt;
        }
    }
    const OWJsonValue* inter = cfg->Find("intercalary");
    if (!Nullish(inter)) {
        if (!inter->IsArray()) return std::nullopt;
        for (const OWJsonValue& i : inter->Items())
            if (!IsString(Prop(&i, "name")) || !IsInteger(Prop(&i, "after_month"))) return std::nullopt;
    }
    // Every divisor and step size a whole number from 1.
    if (names)
        for (const OWJsonValue& l : lengths->Items())
            if (!IsPositiveInt(&l)) return std::nullopt;
    const OWJsonValue* year = Prop(bundles, "year");
    if (!Nullish(year) && !IsPositiveInt(Prop(year, "steps"))) return std::nullopt;
    const OWJsonValue* week = Prop(bundles, "week");
    const OWJsonValue* week_names = Prop(week, "names");
    if (!Nullish(week)) {
        if (!IsPositiveInt(Prop(week, "length")) || !IsArray(week_names)) return std::nullopt;
        for (const OWJsonValue& n : week_names->Items())
            if (!n.IsString()) return std::nullopt;
    }
    bool v2 = NumberIs(version, 2);
    if (!Nullish(inter)) {
        for (const OWJsonValue& i : inter->Items()) {
            const OWJsonValue* every = Prop(&i, "every_years");
            if (!Nullish(every) && !IsPositiveInt(every)) return std::nullopt;
            if (!v2) continue;  // version 1 reads except and offset as unknown keys
            // leapRuleProblem: neither key without every_years; offset whole; except a strictly ascending chain of
            // multiples, the first above every_years.
            const OWJsonValue* ex = Prop(&i, "except");
            const OWJsonValue* off = Prop(&i, "offset");
            if (Nullish(ex) && Nullish(off)) continue;
            if (Nullish(every)) return std::nullopt;
            if (!Nullish(off) && !IsSafeInteger(off)) return std::nullopt;
            if (!Nullish(ex)) {
                if (!ex->IsArray()) return std::nullopt;
                int64_t prev = Int(every);
                for (const OWJsonValue& d : ex->Items()) {
                    if (!IsPositiveInt(&d)) return std::nullopt;
                    int64_t n = Int(&d);
                    if (n <= prev || n % prev != 0) return std::nullopt;
                    prev = n;
                }
            }
        }
    }

    // Read: every value checked above.
    OWCalendarConfig cal;
    cal.version = v2 ? 2 : 1;
    cal.grain = grain->Text();
    if (const OWJsonValue* sn = cfg->Find("step_name"); IsString(sn)) cal.step_name = sn->Text();
    if (names) {
        cal.has_months = true;
        for (const OWJsonValue& n : names->Items()) cal.month_names.push_back(n.Text());
        for (const OWJsonValue& l : lengths->Items()) cal.month_lengths.push_back(Int(&l));
    }
    if (!Nullish(year)) cal.year_steps = Int(Prop(year, "steps"));
    if (!Nullish(week)) {
        cal.has_week = true;
        cal.week_length = Int(Prop(week, "length"));
        for (const OWJsonValue& n : week_names->Items()) cal.week_names.push_back(n.Text());
        const OWJsonValue* mode = Prop(week, "mode");
        cal.week_nested = IsString(mode) && mode->Text() == "nested";
    }
    if (!Nullish(eras)) {
        for (const OWJsonValue& e : eras->Items()) {
            OWCalendarEra era;
            era.name = Prop(&e, "name")->Text();
            if (const OWJsonValue* a = Prop(&e, "abbrev"); !Nullish(a)) era.abbrev = JsString(*a);
            era.start = Int(Prop(&e, "start"));
            if (const OWJsonValue* end = Prop(&e, "end"); !Nullish(end)) era.end = Int(end);
            const OWJsonValue* restart = Prop(&e, "restart");
            era.restart = !(restart && restart->IsBool() && !restart->AsBool());
            if (const OWJsonValue* base = Prop(&e, "year_base"); !Nullish(base)) era.year_base = NumberIs(base, 0) ? 0 : 1;
            const OWJsonValue* dir = Prop(&e, "direction");
            era.down = IsString(dir) && dir->Text() == "down";
            const OWJsonValue* closed = Prop(&e, "closed");
            era.closed = Truthy(closed);
            cal.eras.push_back(std::move(era));
        }
    }
    if (!Nullish(inter)) {
        for (const OWJsonValue& i : inter->Items()) {
            OWCalendarIntercalary ic;
            ic.name = Prop(&i, "name")->Text();
            double am = JsNumber(*Prop(&i, "after_month"));
            constexpr double Limit = 4611686018427387904.0;  // 2^62
            ic.after_month = am > Limit ? INT64_MAX : am < -Limit ? INT64_MIN : static_cast<int64_t>(am);
            if (const OWJsonValue* every = Prop(&i, "every_years"); !Nullish(every)) ic.every_years = Int(every);
            if (v2) {
                if (const OWJsonValue* ex = Prop(&i, "except"); !Nullish(ex))
                    for (const OWJsonValue& d : ex->Items()) ic.except.push_back(Int(&d));
                if (const OWJsonValue* off = Prop(&i, "offset"); !Nullish(off)) ic.offset = Int(off);
            }
            const OWJsonValue* in_week = Prop(&i, "in_week");
            ic.in_week = !(in_week && in_week->IsBool() && !in_week->AsBool());
            cal.intercalary.push_back(std::move(ic));
        }
    }
    if (const OWJsonValue* f = cfg->Find("format"); IsString(f)) cal.format = f->Text();
    if (const OWJsonValue* f = cfg->Find("format_intercalary"); IsString(f)) cal.format_intercalary = f->Text();
    cal.raw = value;
    return cal;
}

std::vector<OWWorldCalendar> WorldCalendars(const std::vector<OWJsonValue>& elements) {
    std::vector<OWWorldCalendar> out;
    for (const OWJsonValue& el : elements) {
        const OWJsonValue* field = Prop(&el, Field);
        if (!field) continue;
        std::optional<OWCalendarConfig> cal = ReadValue(*field);
        if (!cal) continue;
        const OWJsonValue* id = el.Find("id");
        const OWJsonValue* name = el.Find("name");
        // `el.name || '(unnamed calendar)'`: an empty or missing name.
        std::string shown = IsString(name) && !name->Text().empty() ? name->Text()
                            : Truthy(name)                          ? JsString(*name)
                                                                    : "(unnamed calendar)";
        out.push_back({IsString(id) ? id->Text() : std::string(), shown, std::move(*cal)});
    }
    return out;
}

// -- arithmetic ---------------------------------------------------------------------------------------------------

int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int64_t FloorMod(int64_t a, int64_t b) { return a - FloorDiv(a, b) * b; }

bool IntercalaryInYear(const OWCalendarConfig& /*cal*/, const OWCalendarIntercalary& ic, int64_t year) {
    if (!ic.every_years) return true;
    return RuleHolds({*ic.every_years, ic.except, ic.offset}, year);
}

int64_t YearStartStep(const OWCalendarConfig& cal, int64_t year) {
    int64_t start = year * BaseLength(cal);
    for (const LeapRule& r : LeapRules(cal, false)) start += LeapYearsBefore(year, r);
    return start;
}

int64_t YearLength(const OWCalendarConfig& cal, int64_t year) {
    int64_t len = BaseLength(cal);
    for (const LeapRule& r : LeapRules(cal, false))
        if (RuleHolds(r, year)) ++len;
    return len;
}

int64_t AbsoluteYear(const OWCalendarConfig& cal, int64_t step) {
    // A seed on the mean year length (leap days included), then bounded laps of correction.
    double mean = static_cast<double>(BaseLength(cal));
    for (const LeapRule& r : LeapRules(cal, false)) mean += MeanLeap(r);
    int64_t y = static_cast<int64_t>(std::floor(static_cast<double>(step) / mean));
    for (int i = 0; i < MaxYearCorrection && step < YearStartStep(cal, y); ++i) --y;
    for (int i = 0; i < MaxYearCorrection && step >= YearStartStep(cal, y + 1); ++i) ++y;
    return y;
}

int EraForStep(const OWCalendarConfig& cal, int64_t step) {
    const std::vector<OWCalendarEra>& eras = cal.eras;
    if (eras.empty()) return -1;
    // resolveEraDomains: sorted by start (a stable sort, so list order within a start).
    std::vector<int> sorted(eras.size());
    for (size_t i = 0; i < eras.size(); ++i) sorted[i] = static_cast<int>(i);
    std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) { return eras[a].start < eras[b].start; });
    int best = -1;
    for (size_t k = 0; k < sorted.size(); ++k) {
        const OWCalendarEra& era = eras[sorted[k]];
        bool holds;
        if (era.down) {
            // [end or -infinity, pivot)
            holds = step < era.start && (!era.end || step >= *era.end);
        } else {
            // [start, end or the next up era's start or infinity): a down era never ends an up one.
            std::optional<int64_t> hi = era.end;
            if (!hi)
                for (size_t j = k + 1; j < sorted.size(); ++j)
                    if (!eras[sorted[j]].down) {
                        hi = eras[sorted[j]].start;
                        break;
                    }
            holds = step >= era.start && (!hi || step < *hi);
        }
        if (!holds) continue;
        // The latest start, then up over down, then the later in the list.
        int i = sorted[k];
        if (best < 0) {
            best = i;
            continue;
        }
        const OWCalendarEra& b = eras[best];
        if (era.start != b.start ? era.start > b.start : era.down != b.down ? !era.down : i > best) best = i;
    }
    return best;
}

int64_t DisplayYear(const OWCalendarConfig& cal, int64_t step, int era) {
    int64_t abs = AbsoluteYear(cal, step);
    if (era < 0 || era >= static_cast<int>(cal.eras.size())) return abs;
    const OWCalendarEra& e = cal.eras[era];
    if (!e.restart) return abs;
    // A down era counts back from the year holding the step before its pivot, which is year_base.
    if (e.down) return AbsoluteYear(cal, e.start - 1) - abs + e.year_base;
    return abs - AbsoluteYear(cal, e.start) + e.year_base;
}

std::optional<std::string> WeekdayForStep(const OWCalendarConfig& cal, int64_t step) {
    return Weekday(cal, step, PositionOf(cal, step));
}

OWCalendarBreakdown Breakdown(const OWCalendarConfig& cal, int64_t step) {
    Position pos = PositionOf(cal, step);
    OWCalendarBreakdown b;
    b.step = step;
    b.abs_year = pos.abs_year;
    b.era = EraForStep(cal, step);
    b.year = DisplayYear(cal, step, b.era);
    if (pos.kind == Position::Kind::Month) {
        b.month = pos.month;
        b.month_name = cal.month_names[static_cast<size_t>(pos.month - 1)];
        b.day = pos.day;
    } else if (pos.kind == Position::Kind::Intercalary) {
        b.intercalary = cal.intercalary[pos.ic].name;
    }
    b.weekday = Weekday(cal, step, pos);
    return b;
}

std::string EraLabel(const OWCalendarEra& era) { return era.abbrev ? *era.abbrev : era.name; }

std::string FormatStep(const OWCalendarConfig& cal, int64_t step) {
    OWCalendarBreakdown b = Breakdown(cal, step);
    OWCalendarTokens v;
    if (b.day) v.day = std::to_string(*b.day);
    v.month = b.month_name;
    v.weekday = b.weekday;
    v.year = std::to_string(b.year);
    if (b.era >= 0) v.era = EraLabel(cal.eras[static_cast<size_t>(b.era)]);
    v.step = std::to_string(step);
    v.intercalary = b.intercalary;
    std::string tmpl = b.intercalary ? cal.format_intercalary.value_or("{intercalary}, {year} {era}")
                                     : cal.format.value_or(cal.has_months ? "{day} {month}, {year} {era}" : "{year} {era}");
    return RenderTemplate(tmpl, v);
}

std::string FormatStep(const std::optional<OWCalendarConfig>& cal, int64_t step) {
    return cal ? FormatStep(*cal, step) : std::to_string(step);
}

std::string RenderTemplate(std::string_view tmpl, const OWCalendarTokens& values) {
    // 1. /\{([a-z_]+)\}/g: a known token takes its value, or the marker when it has none; an unknown one stays.
    std::string filled;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '{') {
            size_t j = i + 1;
            while (j < tmpl.size() && ((tmpl[j] >= 'a' && tmpl[j] <= 'z') || tmpl[j] == '_')) ++j;
            if (j > i + 1 && j < tmpl.size() && tmpl[j] == '}') {
                std::string_view token = tmpl.substr(i + 1, j - i - 1);
                if (const std::optional<std::string>* v = TokenValue(values, token)) {
                    if (*v && !(*v)->empty()) filled += **v;
                    else OWUnicode::AppendUtf8(filled, EmptyKnown);
                } else {
                    filled.append(tmpl.substr(i, j - i + 1));
                }
                i = j + 1;
                continue;
            }
        }
        filled.push_back(tmpl[i]);
        ++i;
    }
    std::u32string s = OWUnicode::Decode(filled);

    // 2. /\s*[,;·]?\s*\s*[,;·]?\s*/g -> ' ': each marker with the white space and one separator on each side.
    std::u32string a;
    size_t cursor = 0;  // where the regex resumes (the end of the last match)
    for (size_t e = 0; e < s.size(); ++e) {
        if (s[e] != EmptyKnown) continue;
        // The earliest start at or after the cursor from which \s*[,;·]?\s* reaches the marker.
        size_t q = e;
        while (q > cursor && JsSpace(s[q - 1])) --q;
        if (q > cursor && Sep(s[q - 1])) {
            --q;
            while (q > cursor && JsSpace(s[q - 1])) --q;
        }
        size_t r = e + 1;
        while (r < s.size() && JsSpace(s[r])) ++r;
        if (r < s.size() && Sep(s[r])) ++r;
        while (r < s.size() && JsSpace(s[r])) ++r;
        a.append(s, cursor, q - cursor);
        a.push_back(U' ');
        cursor = r;
        e = r - 1;
    }
    a.append(s, cursor, std::u32string::npos);

    // 3. /\s{2,}/g -> ' '.
    std::u32string b;
    for (size_t i = 0; i < a.size();) {
        if (JsSpace(a[i])) {
            size_t j = i;
            while (j < a.size() && JsSpace(a[j])) ++j;
            if (j - i >= 2) b.push_back(U' ');
            else b.push_back(a[i]);
            i = j;
        } else {
            b.push_back(a[i++]);
        }
    }

    // 4. .trim(), then /^[\s,;·]+|[\s,;·]+$/g -> '': the edges' white space and separators.
    size_t lo = 0, hi = b.size();
    while (lo < hi && JsSpace(b[lo])) ++lo;
    while (hi > lo && JsSpace(b[hi - 1])) --hi;
    while (lo < hi && (JsSpace(b[lo]) || Sep(b[lo]))) ++lo;
    while (hi > lo && (JsSpace(b[hi - 1]) || Sep(b[hi - 1]))) --hi;
    return OWUnicode::Encode(std::u32string_view(b).substr(lo, hi - lo));
}

}  // namespace OWCalendar
}  // namespace OnlyWorlds
