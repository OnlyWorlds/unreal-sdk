#include "OWCalendarLibrary.h"

#include "OWCalendar.h"
#include "OWConvert.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <vector>

using OWConvert::Str;

namespace
{
	// The world's calendars, as the core finds them: only the bodies carrying the field are handed over, since
	// WorldCalendars skips the others anyway.
	std::vector<OnlyWorlds::OWWorldCalendar> CalendarsOf(const UOWWorld* World)
	{
		const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
		if (!Native) return {};
		std::vector<OnlyWorlds::OWJsonValue> Carrying;
		for (const OnlyWorlds::OWElement& E : Native->elements)
		{
			if (E.body.Has(OnlyWorlds::OWCalendar::Field)) Carrying.push_back(E.body);
		}
		return OnlyWorlds::OWCalendar::WorldCalendars(Carrying);
	}

	// The calendar a CalendarId names: the first with that id, or for "" the only one.
	std::optional<OnlyWorlds::OWCalendarConfig> Pick(const UOWWorld* World, const FString& CalendarId)
	{
		std::vector<OnlyWorlds::OWWorldCalendar> All = CalendarsOf(World);
		if (CalendarId.IsEmpty())
		{
			if (All.size() == 1) return std::move(All[0].calendar);
			return std::nullopt;
		}
		const std::string Id = OWConvert::Utf8(CalendarId);
		for (OnlyWorlds::OWWorldCalendar& C : All)
		{
			if (C.id == Id) return std::move(C.calendar);
		}
		return std::nullopt;
	}
}

TArray<FOWWorldCalendar> UOWCalendars::GetWorldCalendars(const UOWWorld* World)
{
	TArray<FOWWorldCalendar> Out;
	for (const OnlyWorlds::OWWorldCalendar& C : CalendarsOf(World))
	{
		FOWWorldCalendar& Row = Out.AddDefaulted_GetRef();
		Row.Id = Str(C.id);
		Row.Name = Str(C.name);
	}
	return Out;
}

FString UOWCalendars::FormatStep(const UOWWorld* World, const FString& CalendarId, int64 Step)
{
	return Str(OnlyWorlds::OWCalendar::FormatStep(Pick(World, CalendarId), static_cast<int64_t>(Step)));
}

bool UOWCalendars::BreakdownStep(const UOWWorld* World, const FString& CalendarId, int64 Step, FOWStepBreakdown& Breakdown)
{
	Breakdown = FOWStepBreakdown();
	Breakdown.Step = Step;
	const std::optional<OnlyWorlds::OWCalendarConfig> Cal = Pick(World, CalendarId);
	if (!Cal) return false;

	const OnlyWorlds::OWCalendarBreakdown B = OnlyWorlds::OWCalendar::Breakdown(*Cal, static_cast<int64_t>(Step));
	Breakdown.Step = B.step;
	// Absent past the exact range (the core's IsPastExactRange): 0 here, as the other parts a date does not have, and
	// the flag says which 0 it is.
	Breakdown.bPastExactRange = !B.year.has_value();
	Breakdown.AbsYear = B.abs_year.value_or(0);
	Breakdown.Year = B.year.value_or(0);
	Breakdown.EraIndex = B.era;
	if (B.era >= 0)
	{
		const OnlyWorlds::OWCalendarEra& Era = Cal->eras[static_cast<size_t>(B.era)];
		Breakdown.Era = Str(Era.name);
		Breakdown.EraLabel = Str(OnlyWorlds::OWCalendar::EraLabel(Era));
	}
	Breakdown.Month = B.month ? static_cast<int32>(*B.month) : 0;
	Breakdown.MonthName = B.month_name ? Str(*B.month_name) : FString();
	Breakdown.Day = B.day ? *B.day : 0;
	Breakdown.bIntercalary = B.intercalary.has_value();
	Breakdown.Intercalary = B.intercalary ? Str(*B.intercalary) : FString();
	Breakdown.Weekday = B.weekday ? Str(*B.weekday) : FString();
	return true;
}
