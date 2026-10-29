#pragma once
// The calendar profile (schema-dist profiles/calendar.md, amended 2026-10-25: version 2's except and offset): the
// names a world gives its time line, read from the `atlas_calendar` field. A calendar changes no date: every date field
// stays an integer step, and the calendar says how to read one aloud ("3 Frost, 12 AG").
//
// ReadValue reads the field as the TypeScript SDK's readCalendarValue does (an object, or the same object as a strict
// JSON string), refusing every value the profile's "Not a calendar" list names: a refused value is std::nullopt, and a
// tool shows the bare step, never an error. Breakdown and FormatStep are the SDK's breakdownStep and formatStep, held to
// the profile's conformance fixture (profiles/calendar.fixture.json).
//
// Numbers are read as JavaScript reads them, since the SDK is the reference: a JSON number is a double, so `1.0` is 1,
// and a whole number is one Number.isSafeInteger takes (to 2^53 - 1; `1e400` is Infinity and refused). The arithmetic
// after the read is on int64: exact, and the same as the SDK's doubles while every step and year sum stays within
// 2^53. Strings are UTF-8.

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

struct OWCalendarEra {
    std::string name;
    // What the {era} token prints in place of the name: the abbrev as JavaScript's String() writes it (a number 7.0 as
    // "7", true as "true", ["a", 1] as "a,1"). Absent for a missing or null abbrev; "" prints empty.
    std::optional<std::string> abbrev;
    int64_t start = 0;  // an up era's first step; a down era's pivot (the first step it does not hold)
    std::optional<int64_t> end;  // an up era's first step past it; a down era's lowest step
    bool restart = true;  // false only for a JSON false: the year is then the absolute year
    int year_base = 1;    // 0 or 1
    bool down = false;    // direction "down"; any other direction, or none, counts up
    bool closed = false;  // read for a date-entry tool; it changes no reading
};

struct OWCalendarIntercalary {
    std::string name;
    // A whole number (Number.isInteger: not bounded by 2^53). One past +-2^62 is held as INT64_MAX or INT64_MIN, which
    // no month index equals: the day sits in no month either way.
    int64_t after_month = 0;
    std::optional<int64_t> every_years;  // a leap rule: the day is there in the years its rule selects
    std::vector<int64_t> except;         // version 2 only: empty in a version-1 calendar, whatever the value holds
    int64_t offset = 0;                  // version 2 only
    bool in_week = true;                 // false only for a JSON false
};

// A calendar as read. `raw` is the value itself (parsed, when the field held a JSON string): a writer keeps it whole,
// unknown keys included, and changes only what it means to.
struct OWCalendarConfig {
    int version = 1;
    std::string grain;
    std::optional<std::string> step_name;  // a string step_name; no reading here uses it
    bool has_months = false;
    std::vector<std::string> month_names;
    std::vector<int64_t> month_lengths;
    std::optional<int64_t> year_steps;  // bundles.year.steps (months win when both are there)
    bool has_week = false;
    int64_t week_length = 0;
    std::vector<std::string> week_names;  // fewer than week_length: no weekday is printed
    bool week_nested = false;             // mode "nested"; any other mode is free
    std::vector<OWCalendarEra> eras;
    std::vector<OWCalendarIntercalary> intercalary;
    // The templates when they are strings. A template that is neither a string nor null prints the default (the SDK
    // throws there, the Python package raises; "what a reader may ignore" lets a reader print its own way).
    std::optional<std::string> format, format_intercalary;
    OWJsonValue raw;
};

// A step taken apart (the SDK's StepBreakdown). Absent fields are the structure the calendar does not have.
struct OWCalendarBreakdown {
    int64_t step = 0;
    int64_t abs_year = 0;  // the year index from step 0 (zero-based, Euclidean)
    int64_t year = 0;      // the era's year, or abs_year with no era or restart false
    int era = -1;          // the era's index in OWCalendarConfig::eras, or -1
    std::optional<int64_t> month;  // 1-based; absent on a day outside the months or with no months
    std::optional<std::string> month_name;
    std::optional<int64_t> day;  // 1-based within the month
    std::optional<std::string> intercalary;  // the day's name when the step is a day outside the months
    std::optional<std::string> weekday;
};

// The values a template's known tokens take; absent or "" prints empty and closes up.
struct OWCalendarTokens {
    std::optional<std::string> day, month, weekday, year, era, step, intercalary;
};

// One calendar a world holds: the element carrying the field, and the calendar it reads as.
struct OWWorldCalendar {
    std::string id;
    std::string name;  // the element's name, or "(unnamed calendar)"
    OWCalendarConfig calendar;
};

namespace OWCalendar {

extern const char* const Field;      // "atlas_calendar": the field a reader finds a calendar by
extern const char* const Supertype;  // "calendar": the supertype Atlas gives a calendar Construct (convention only)

// The field's value: an object, or the same object as a JSON string (strict JSON: JSON.parse's grammar, no byte order
// mark). std::nullopt for a JSON null, any other kind, or a value the profile refuses.
std::optional<OWCalendarConfig> ReadValue(const OWJsonValue& raw);

// The object half: every "Not a calendar" rule (the SDK's readCalendarConfig).
std::optional<OWCalendarConfig> ReadConfig(const OWJsonValue& value);

// Every element (each a JSON object) with a valid `atlas_calendar`, in order: found by the field, never by type or
// supertype. A value that does not read is skipped.
std::vector<OWWorldCalendar> WorldCalendars(const std::vector<OWJsonValue>& elements);

// Euclidean floor division and its modulo (b > 0).
int64_t FloorDiv(int64_t a, int64_t b);
int64_t FloorMod(int64_t a, int64_t b);

// The year arithmetic (steps 1 and 3): step 0 opens year 0; a year is the months' lengths, each yearly day outside the
// months, and one step for each leap day its year index earns.
bool IntercalaryInYear(const OWCalendarConfig& cal, const OWCalendarIntercalary& ic, int64_t year);
int64_t YearStartStep(const OWCalendarConfig& cal, int64_t year);
int64_t YearLength(const OWCalendarConfig& cal, int64_t year);
int64_t AbsoluteYear(const OWCalendarConfig& cal, int64_t step);

// The era holding a step (step 4): its index, or -1. The latest start wins, then an up era over a down one, then the
// later in the list; an open up era ends at the next up era's start.
int EraForStep(const OWCalendarConfig& cal, int64_t step);
// The year a step reads in an era (or the absolute year for era -1 or a restart false).
int64_t DisplayYear(const OWCalendarConfig& cal, int64_t step, int era);

// The weekday's name, or absent: no week, fewer names than its length, a nested week outside the months, a day
// outside the week.
std::optional<std::string> WeekdayForStep(const OWCalendarConfig& cal, int64_t step);

OWCalendarBreakdown Breakdown(const OWCalendarConfig& cal, int64_t step);

// The step in words (step 5, with the default templates). With no calendar, the bare step.
std::string FormatStep(const OWCalendarConfig& cal, int64_t step);
std::string FormatStep(const std::optional<OWCalendarConfig>& cal, int64_t step);

// A template filled: {day} {month} {weekday} {year} {era} {step} {intercalary}; a known token with no value prints
// empty and one , ; U+00B7 or white space on each side of it closes up (a dash never does); an unknown token prints
// as written. White space is JavaScript's \s.
std::string RenderTemplate(std::string_view tmpl, const OWCalendarTokens& values);

// The name an era prints as ({era}): its abbrev, else its name.
std::string EraLabel(const OWCalendarEra& era);

}  // namespace OWCalendar
}  // namespace OnlyWorlds
