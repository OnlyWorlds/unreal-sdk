// The calendar through UOWCalendars as a Blueprint calls it: each UFUNCTION found by name on the library's class, its
// parameters marshalled by property into a frame of their own, ProcessEvent on the class default object (the Kismet
// VM's path to a static library function), and the answer read back by property, a struct's members too. Never the
// C++ function directly. On a real world (Moppetopia's Treaty Reckoning; Sikelia holds no calendar), then every case
// of the profile's fixture placed on that world's calendar element.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#include "OWCalendar.h"
#include "OWCalendarLibrary.h"
#include "OWConvert.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWLibrary.h"
#include "OWModels.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <string>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWConvert::Str;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// Moppetopia's calendar: a Construct, version 1, no months; Before the Treaty (down, BT) and Years since Soft
	// Landings (up, YSL, year_base 0), both at step 250; format "Year {year} {era}".
	const TCHAR* const TreatyId = TEXT("01a11ff9-e0aa-7e43-9b13-1a1320cf9270");
	const TCHAR* const TreatyName = TEXT("The Treaty Reckoning");

	// One call of a UOWCalendars function as a Blueprint makes it (OWBlueprintCall.h). Each parameter is set through its
	// property, whose class is asserted (an int64 must cross as an FInt64Property, or no Blueprint can pass one).
	struct FBlueprintCall : OWBlueprintTest::FCall
	{
		FBlueprintCall(FAutomationTestBase& InTest, const TCHAR* InName) : FCall(InTest, UOWCalendars::StaticClass(), InName) {}
	};
	using OWBlueprintTest::BoolMember;
	using OWBlueprintTest::Member;

	struct FCalendarRow
	{
		FString Id, Name;
	};

	TArray<FCalendarRow> CallGetWorldCalendars(FAutomationTestBase& Test, const UOWWorld* World)
	{
		TArray<FCalendarRow> Out;
		FBlueprintCall Call(Test, TEXT("GetWorldCalendars"));
		Call.SetWorld(World);
		Call.Invoke();
		const FArrayProperty* Ret = CastField<FArrayProperty>(Call.Return());
		const FStructProperty* Inner = Ret ? CastField<FStructProperty>(Ret->Inner) : nullptr;
		if (!Inner || Inner->Struct != FOWWorldCalendar::StaticStruct())
		{
			Test.AddError(TEXT("GetWorldCalendars does not return an array of FOWWorldCalendar"));
			return Out;
		}
		FScriptArrayHelper Rows(Ret, Ret->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const void* Row = Rows.GetRawPtr(I);
			Out.Add({ Member<FStrProperty>(Test, Inner->Struct, Row, TEXT("Id")),
				Member<FStrProperty>(Test, Inner->Struct, Row, TEXT("Name")) });
		}
		return Out;
	}

	FString CallFormatStep(FAutomationTestBase& Test, const UOWWorld* World, const FString& CalendarId, int64 Step)
	{
		FBlueprintCall Call(Test, TEXT("FormatStep"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CalendarId"), CalendarId);
		Call.SetInt64(TEXT("Step"), Step);
		Call.Invoke();
		const FStrProperty* Ret = CastField<FStrProperty>(Call.Return());
		if (!Ret)
		{
			Test.AddError(TEXT("FormatStep does not return a string"));
			return FString();
		}
		return *Ret->ContainerPtrToValuePtr<FString>(Call.GetFrame());
	}

	// The breakdown as a Blueprint's Break node sees it: every member by its property.
	struct FBreakdownRead
	{
		bool bFound = false;
		int64 Step = 0, AbsYear = 0, Year = 0, Day = 0;
		int32 EraIndex = -1, Month = 0;
		FString Era, EraLabel, MonthName, Intercalary, Weekday;
		bool bIntercalary = false;
	};

	FBreakdownRead CallBreakdownStep(FAutomationTestBase& Test, const UOWWorld* World, const FString& CalendarId, int64 Step)
	{
		FBreakdownRead R;
		FBlueprintCall Call(Test, TEXT("BreakdownStep"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CalendarId"), CalendarId);
		Call.SetInt64(TEXT("Step"), Step);
		Call.Invoke();
		const FBoolProperty* Ret = CastField<FBoolProperty>(Call.Return());
		const FStructProperty* Out = CastField<FStructProperty>(Call.Property(TEXT("Breakdown")));
		if (!Ret || !Out || Out->Struct != FOWStepBreakdown::StaticStruct() || !Out->HasAnyPropertyFlags(CPF_OutParm))
		{
			Test.AddError(TEXT("BreakdownStep does not return a bool with an FOWStepBreakdown out parameter"));
			return R;
		}
		R.bFound = Ret->GetPropertyValue_InContainer(Call.GetFrame());
		const UScriptStruct* S = Out->Struct;
		const void* B = Out->ContainerPtrToValuePtr<void>(Call.GetFrame());
		R.Step = Member<FInt64Property>(Test, S, B, TEXT("Step"));
		R.AbsYear = Member<FInt64Property>(Test, S, B, TEXT("AbsYear"));
		R.Year = Member<FInt64Property>(Test, S, B, TEXT("Year"));
		R.EraIndex = Member<FIntProperty>(Test, S, B, TEXT("EraIndex"));
		R.Era = Member<FStrProperty>(Test, S, B, TEXT("Era"));
		R.EraLabel = Member<FStrProperty>(Test, S, B, TEXT("EraLabel"));
		R.Month = Member<FIntProperty>(Test, S, B, TEXT("Month"));
		R.MonthName = Member<FStrProperty>(Test, S, B, TEXT("MonthName"));
		R.Day = Member<FInt64Property>(Test, S, B, TEXT("Day"));
		R.bIntercalary = BoolMember(Test, S, B, TEXT("bIntercalary"));
		R.Intercalary = Member<FStrProperty>(Test, S, B, TEXT("Intercalary"));
		R.Weekday = Member<FStrProperty>(Test, S, B, TEXT("Weekday"));
		return R;
	}

	UOWWorld* OpenMoppetopia(FAutomationTestBase& Test)
	{
		const FString Root = OWTestPaths::Moppetopia();
		if (!FPaths::FileExists(Root / TEXT("world.json")))
		{
			Test.AddError(FString::Printf(TEXT("no world at %s (set OW_MOPPETOPIA): a missing world is a failure here, "
				"never a pass"), *Root));
			return nullptr;
		}
		FString Error;
		UOWWorld* World = UOWLibrary::OpenWorldFolder(Root, Error);
		Test.TestNotNull(TEXT("Moppetopia opens (") + Error + TEXT(")"), World);
		if (World && !World->HasElement(TreatyId))
		{
			Test.AddError(TEXT("Moppetopia no longer holds the Treaty Reckoning"));
			return nullptr;
		}
		return World;
	}

	// The Treaty Reckoning's field set to a value, in memory only (nothing is written).
	void SetCalendar(UOWWorld* World, const TCHAR* Id, OWJsonValue Value)
	{
		World->Native()->Get(OWConvert::Utf8(Id))->Set(OnlyWorlds::OWCalendar::Field, std::move(Value));
	}

	// The steps read by hand from the profile: no months, so a step is a year; the down era's year at step s is
	// 250 - s (year_base 1 in the year holding step 249), the up era's s - 250 (year_base 0).
	struct FTreatyStep
	{
		int64 Step;
		const TCHAR* Words;
		int32 EraIndex;
		int64 Year;
	};
	const FTreatyStep TreatySteps[] = {
		{ 249, TEXT("Year 1 BT"), 0, 1 },
		{ 250, TEXT("Year 0 YSL"), 1, 0 },
		{ 251, TEXT("Year 1 YSL"), 1, 1 },
		{ 200, TEXT("Year 50 BT"), 0, 50 },
		{ 0, TEXT("Year 250 BT"), 0, 250 },
		{ -1, TEXT("Year 251 BT"), 0, 251 },
		// Past int32 both ways: the step crosses whole as an int64.
		{ 5000000000LL, TEXT("Year 4999999750 YSL"), 1, 4999999750LL },
		{ -5000000000LL, TEXT("Year 5000000250 BT"), 0, 5000000250LL },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCalendarWorldCalendarsTest, "OnlyWorlds.Calendar.GetWorldCalendars", OWTestFlags)

bool FOWCalendarWorldCalendarsTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	UOWWorld* World = OpenMoppetopia(*this);
	if (!World) return false;

	TArray<FCalendarRow> Rows = CallGetWorldCalendars(*this, World);
	if (!TestEqual(TEXT("Moppetopia holds one calendar"), Rows.Num(), 1)) return false;
	TestEqual(TEXT("its id is the element's"), Rows[0].Id, FString(TreatyId));
	TestEqual(TEXT("its name is the element's"), Rows[0].Name, FString(TreatyName));

	// A value the profile refuses is no calendar, never an error.
	OWJsonValue Original = *World->Native()->Get(OWConvert::Utf8(TreatyId))->body.Find(OnlyWorlds::OWCalendar::Field);
	OWJsonValue Refused = Original;
	Refused.Set("version", OWJsonValue::Number(static_cast<int64_t>(3)));
	SetCalendar(World, TreatyId, Refused);
	TestEqual(TEXT("a refused value is no calendar"), CallGetWorldCalendars(*this, World).Num(), 0);

	// The same object as a JSON string reads the same.
	SetCalendar(World, TreatyId, OWJsonValue::String(OnlyWorlds::OWJson::Stringify(Original, true)));
	Rows = CallGetWorldCalendars(*this, World);
	TestTrue(TEXT("the value as a JSON string is the same calendar"), Rows.Num() == 1 && Rows[0].Id == TreatyId);

	// Found by the field, not the type: a Character carrying one is a second calendar, in the order read.
	const TArray<FString> Characters = World->GetElementIds(TEXT("character"));
	if (!TestTrue(TEXT("Moppetopia has characters"), Characters.Num() > 0)) return false;
	SetCalendar(World, *Characters[0], Original);
	Rows = CallGetWorldCalendars(*this, World);
	TArray<FString> Expected;
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		if (E.body.Has(OnlyWorlds::OWCalendar::Field)) Expected.Add(Str(E.Id()));
	}
	TArray<FString> Got;
	for (const FCalendarRow& Row : Rows) Got.Add(Row.Id);
	TestTrue(FString::Printf(TEXT("two calendars, in the world's order (%s; the world's %s)"), *FString::Join(Got, TEXT(", ")),
		*FString::Join(Expected, TEXT(", "))), Expected.Num() == 2 && Got == Expected);
	TestTrue(TEXT("the character's is one"), Got.Contains(Characters[0]));
	TestEqual(TEXT("no world, no calendars"), CallGetWorldCalendars(*this, nullptr).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCalendarFormatStepTest, "OnlyWorlds.Calendar.FormatStep", OWTestFlags)

bool FOWCalendarFormatStepTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	UOWWorld* World = OpenMoppetopia(*this);
	if (!World) return false;

	for (const FTreatyStep& S : TreatySteps)
	{
		const FString At = FString::Printf(TEXT("step %lld"), S.Step);
		TestEqual(At + TEXT(" by the calendar's id"), CallFormatStep(*this, World, TreatyId, S.Step), FString(S.Words));
		TestEqual(At + TEXT(" by \"\": the world's only calendar"), CallFormatStep(*this, World, FString(), S.Step), FString(S.Words));
		TestEqual(At + TEXT(" in no such calendar: the bare step"), CallFormatStep(*this, World, TEXT("no-such-id"), S.Step),
			FString::Printf(TEXT("%lld"), S.Step));
	}

	// Every Character's birth date, through the Blueprint path and through the core straight: the same words.
	const OWJsonValue* Field = World->Native()->Get(OWConvert::Utf8(TreatyId))->body.Find(OnlyWorlds::OWCalendar::Field);
	const std::optional<OnlyWorlds::OWCalendarConfig> Cal = OnlyWorlds::OWCalendar::ReadValue(*Field);
	if (!TestTrue(TEXT("the core reads the Treaty Reckoning"), Cal.has_value())) return false;
	int32 Dates = 0, Same = 0;
	for (const FOWCharacter& C : UOWElements::GetAllCharacters(World))
	{
		if (!C.bHasBirthDate) continue;
		++Dates;
		const FString Words = CallFormatStep(*this, World, TreatyId, C.BirthDate);
		const FString Core = Str(OnlyWorlds::OWCalendar::FormatStep(*Cal, C.BirthDate));
		if (Words == Core && !Words.IsEmpty()) ++Same;
		else if (Dates - Same <= 3) AddError(FString::Printf(TEXT("%s born %lld: \"%s\", the core \"%s\""), *C.Name,
			C.BirthDate, *Words, *Core));
	}
	TestTrue(TEXT("characters with a birth date"), Dates > 0);
	TestEqual(TEXT("every birth date as the core reads it"), Same, Dates);
	AddInfo(FString::Printf(TEXT("Moppetopia: %d birth dates read in the Treaty Reckoning"), Dates));

	// Two calendars: "" guesses none, and each id names its own.
	const TArray<FString> Characters = World->GetElementIds(TEXT("character"));
	if (!TestTrue(TEXT("Moppetopia has characters"), Characters.Num() > 0)) return false;
	OWJsonValue Other = *Field;
	Other.Set("format", OWJsonValue::String("{year} {era}"));
	SetCalendar(World, *Characters[0], Other);
	TestEqual(TEXT("two calendars: \"\" prints the bare step"), CallFormatStep(*this, World, FString(), 249), FString(TEXT("249")));
	TestEqual(TEXT("two calendars: the Treaty Reckoning by its id"), CallFormatStep(*this, World, TreatyId, 249), FString(TEXT("Year 1 BT")));
	TestEqual(TEXT("two calendars: the character's by its id"), CallFormatStep(*this, World, Characters[0], 249), FString(TEXT("1 BT")));
	SetCalendar(World, *Characters[0], OWJsonValue::Null());

	// A refused value: the bare step, never an error.
	OWJsonValue Refused = *Field;
	Refused.Set("grain", OWJsonValue::Number(static_cast<int64_t>(1)));
	SetCalendar(World, TreatyId, Refused);
	TestEqual(TEXT("a refused calendar prints the bare step"), CallFormatStep(*this, World, TreatyId, 249), FString(TEXT("249")));
	TestEqual(TEXT("so does \"\" then"), CallFormatStep(*this, World, FString(), -7), FString(TEXT("-7")));
	TestEqual(TEXT("no world: the bare step"), CallFormatStep(*this, nullptr, TreatyId, 5000000000LL), FString(TEXT("5000000000")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCalendarBreakdownStepTest, "OnlyWorlds.Calendar.BreakdownStep", OWTestFlags)

bool FOWCalendarBreakdownStepTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	UOWWorld* World = OpenMoppetopia(*this);
	if (!World) return false;

	const TCHAR* const EraNames[] = { TEXT("Before the Treaty"), TEXT("Years since Soft Landings") };
	const TCHAR* const EraLabels[] = { TEXT("BT"), TEXT("YSL") };
	for (const FTreatyStep& S : TreatySteps)
	{
		const FString At = FString::Printf(TEXT("step %lld: "), S.Step);
		const FBreakdownRead B = CallBreakdownStep(*this, World, TreatyId, S.Step);
		TestTrue(At + TEXT("found"), B.bFound);
		TestEqual(At + TEXT("Step"), B.Step, S.Step);
		TestEqual(At + TEXT("AbsYear (a step is a year)"), B.AbsYear, S.Step);
		TestEqual(At + TEXT("Year"), B.Year, S.Year);
		TestEqual(At + TEXT("EraIndex"), B.EraIndex, S.EraIndex);
		TestEqual(At + TEXT("Era"), B.Era, FString(EraNames[S.EraIndex]));
		TestEqual(At + TEXT("EraLabel"), B.EraLabel, FString(EraLabels[S.EraIndex]));
		TestEqual(At + TEXT("no months"), B.Month, 0);
		TestEqual(At + TEXT("no month name"), B.MonthName, FString());
		TestEqual(At + TEXT("no day"), B.Day, static_cast<int64>(0));
		TestFalse(At + TEXT("no day outside the months"), B.bIntercalary);
		TestEqual(At + TEXT("no weekday"), B.Weekday, FString());
	}

	const FBreakdownRead None = CallBreakdownStep(*this, World, TEXT("no-such-id"), 249);
	TestFalse(TEXT("no such calendar: false"), None.bFound);
	TestEqual(TEXT("no such calendar: the step alone"), None.Step, static_cast<int64>(249));
	TestEqual(TEXT("no such calendar: no era"), None.EraIndex, -1);
	TestTrue(TEXT("\"\": the world's only calendar"), CallBreakdownStep(*this, World, FString(), 249).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCalendarFixtureTest, "OnlyWorlds.Calendar.Fixture", OWTestFlags)

bool FOWCalendarFixtureTest::RunTest(const FString& Parameters)
{
	// profiles/calendar.fixture.json in the vendored schema-dist, two folders above the plugin's own.
	FString Path = FPaths::Combine(OWTestPaths::PluginDir(), TEXT("../../codegen/schema-dist/profiles/calendar.fixture.json"));
	FPaths::CollapseRelativeDirectories(Path);
	OnlyWorlds::OWBytes Bytes;
	if (!OnlyWorlds::OWFiles::Read(OWConvert::Path(Path), Bytes))
	{
		AddError(TEXT("no calendar fixture at ") + Path);
		return false;
	}
	OnlyWorlds::OWJsonParseResult Doc = OnlyWorlds::OWJson::Parse(Bytes);
	const OWJsonValue* Cases = Doc.ok ? Doc.value.Find("cases") : nullptr;
	if (!TestTrue(TEXT("the fixture parses and holds cases"), Cases && Cases->IsArray())) return false;

	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	UOWWorld* World = OpenMoppetopia(*this);
	if (!World) return false;

	auto TextOf = [](const OWJsonValue* V) { return V && V->IsString() ? Str(V->Text()) : FString(); };
	auto IntOf = [](const OWJsonValue* V) { return V && V->IsNumber() ? std::stoll(V->Text()) : 0LL; };

	int32 Valid = 0, Refused = 0, Steps = 0, Failed = 0;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const FString Name = TextOf(Case.Find("name"));
		const OWJsonValue* Value = Case.Find("value");
		const OWJsonValue* ValidFlag = Case.Find("valid");
		const int32 ErrorsBefore = ExecutionInfo.GetErrorTotal();
		SetCalendar(World, TreatyId, Value ? *Value : OWJsonValue::Null());

		const TArray<FCalendarRow> Rows = CallGetWorldCalendars(*this, World);
		if (!(ValidFlag && ValidFlag->IsBool() && ValidFlag->AsBool()))
		{
			++Refused;
			TestEqual(Name + TEXT(": refused, so no calendar"), Rows.Num(), 0);
			TestEqual(Name + TEXT(": refused, so the bare step"), CallFormatStep(*this, World, TreatyId, -12), FString(TEXT("-12")));
			TestFalse(Name + TEXT(": refused, so no breakdown"), CallBreakdownStep(*this, World, TreatyId, -12).bFound);
		}
		else
		{
			++Valid;
			TestTrue(Name + TEXT(": the world's one calendar"), Rows.Num() == 1 && Rows[0].Id == TreatyId && Rows[0].Name == TreatyName);
			const OWJsonValue* CaseSteps = Case.Find("steps");
			for (const OWJsonValue& S : CaseSteps ? CaseSteps->Items() : std::vector<OWJsonValue>())
			{
				++Steps;
				const int64 Step = IntOf(S.Find("step"));
				const FString At = FString::Printf(TEXT("%s step %lld: "), *Name, Step);
				const FString Words = TextOf(S.Find("words"));
				TestEqual(At + TEXT("words"), CallFormatStep(*this, World, TreatyId, Step), Words);
				TestEqual(At + TEXT("words by \"\""), CallFormatStep(*this, World, FString(), Step), Words);

				const OWJsonValue* E = S.Find("breakdown");
				const FBreakdownRead B = CallBreakdownStep(*this, World, TreatyId, Step);
				TestTrue(At + TEXT("found"), B.bFound);
				TestEqual(At + TEXT("Step"), B.Step, Step);
				TestEqual(At + TEXT("AbsYear"), B.AbsYear, static_cast<int64>(IntOf(E->Find("absYear"))));
				TestEqual(At + TEXT("Year"), B.Year, static_cast<int64>(IntOf(E->Find("year"))));
				TestEqual(At + TEXT("Era"), B.Era, TextOf(E->Find("era")));
				TestTrue(At + TEXT("an era, or none"), (B.EraIndex >= 0) == (E->Find("era") && E->Find("era")->IsString()));
				TestEqual(At + TEXT("Month"), B.Month, static_cast<int32>(IntOf(E->Find("month"))));
				TestEqual(At + TEXT("MonthName"), B.MonthName, TextOf(E->Find("monthName")));
				TestEqual(At + TEXT("Day"), B.Day, static_cast<int64>(IntOf(E->Find("day"))));
				TestTrue(At + TEXT("bIntercalary"), B.bIntercalary == (E->Find("intercalary") && E->Find("intercalary")->IsString()));
				TestEqual(At + TEXT("Intercalary"), B.Intercalary, TextOf(E->Find("intercalary")));
				TestEqual(At + TEXT("Weekday"), B.Weekday, TextOf(E->Find("weekday")));
			}
		}
		if (ExecutionInfo.GetErrorTotal() > ErrorsBefore) ++Failed;
	}
	TestEqual(TEXT("every case"), Valid + Refused, static_cast<int32>(Cases->Items().size()));
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("calendar fixture: %d cases (%d calendars with %d steps, %d refused) through the Blueprint path"),
		Valid + Refused, Valid, Steps, Refused));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
