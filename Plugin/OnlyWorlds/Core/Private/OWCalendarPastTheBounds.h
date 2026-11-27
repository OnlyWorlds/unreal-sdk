#pragma once
// Private to the core and its tests: OWCalendar::ReadConfig without the size bounds and the longest year. The cost
// tests read their shared cases through it (tests/data/calendar-cost.cases.json holds templates and names of thousands
// of characters, which the bounded reader refuses): an OWCalendarConfig built in code never meets the bounds, so what
// a label costs stays the formatter's to keep linear. Not exported, and no game calls it.

#include "OWCalendar.h"

#include <optional>

namespace OnlyWorlds {
namespace OWCalendar {

std::optional<OWCalendarConfig> ReadConfigPastTheBounds(const OWJsonValue& value);

}  // namespace OWCalendar
}  // namespace OnlyWorlds
