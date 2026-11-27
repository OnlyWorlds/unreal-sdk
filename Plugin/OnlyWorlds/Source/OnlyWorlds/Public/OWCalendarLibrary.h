#pragma once
// The calendar profile (schema-dist profiles/calendar.md), for Blueprints and C++: the calendars a world holds, and a
// step read aloud in one of them ("3 Frost, 12 AG"). The core (OWCalendar.h) does the work; this is its door. A
// calendar changes no date: every date field stays an integer step, and a step with no calendar to read it, or one
// whose value the profile refuses, prints as the bare step, never an error.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWCalendarLibrary.generated.h"

class UOWWorld;

/** One calendar a world holds: the element carrying its atlas_calendar field. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWWorldCalendar
{
	GENERATED_BODY()

	/** The element's id: the CalendarId to read steps in. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString Id;

	/** The element's name, or "(unnamed calendar)". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString Name;
};

/**
 * A step taken apart. A part the calendar does not have is 0 or "". A date past the exact range (its step, year index
 * plus a leap rule's offset, or shown year outside -(2^53 - 1) to 2^53 - 1) holds only its step: every other part is
 * 0, "" or -1, and FormatStep prints the plain step.
 */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWStepBreakdown
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int64 Step = 0;

	/**
	 * True for a date past the exact range: the step alone is the date, and AbsYear and Year are 0 because the date has
	 * no year, not because its year is 0 (a calendar whose era counts from year_base 0 has a real year 0).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	bool bPastExactRange = false;

	/** The year index from step 0 (step 0 opens year 0; negative steps count back). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int64 AbsYear = 0;

	/** The year as the date prints it: the era's year, or AbsYear outside every era. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int64 Year = 0;

	/** The era's position in the calendar's list, or -1 outside every era. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int32 EraIndex = -1;

	/** The era's name ("" outside every era). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString Era;

	/** What the date prints for the era: its abbrev, else its name. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString EraLabel;

	/** The month, from 1; 0 on a day outside the months, or with no months. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int32 Month = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString MonthName;

	/** The day within the month, from 1; 0 when Month is 0. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	int64 Day = 0;

	/** True on a day that belongs to no month (a festival, a leap day); Intercalary is its name. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	bool bIntercalary = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString Intercalary;

	/** The weekday's name; "" with no week, or fewer names than the week's length. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Calendar")
	FString Weekday;
};

/**
 * A world's calendars. A calendar is found by its field, never by type or supertype, and a value the profile refuses
 * is no calendar. CalendarId "" reads the world's only calendar when it holds exactly one (as Atlas does), else none.
 */
UCLASS()
class ONLYWORLDS_API UOWCalendars : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every calendar the world holds, in the order its elements were read. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Calendar")
	static TArray<FOWWorldCalendar> GetWorldCalendars(const UOWWorld* World);

	/** A step in words, by the calendar's own template ("Year 1 BT"); the bare step when there is no such calendar. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Calendar")
	static FString FormatStep(const UOWWorld* World, const FString& CalendarId, int64 Step);

	/** A step taken apart. False when there is no such calendar (Breakdown then holds only the step). */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Calendar")
	static bool BreakdownStep(const UOWWorld* World, const FString& CalendarId, int64 Step, FOWStepBreakdown& Breakdown);
};
