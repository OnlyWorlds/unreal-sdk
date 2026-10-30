// The names profile through UOWNames as a Blueprint calls it (OWBlueprintCall.h): TypeWord found by name, its
// parameters set by property, ProcessEvent, the bool and the word read back. On a real world (Sikelia, which carries no
// x_names: the standard's name everywhere), then a map placed on its World in memory, then every case of the
// profile's fixture placed there the same way.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWNames.h"
#include "OWNamesLibrary.h"
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
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	struct FWord
	{
		bool bFound = false;
		FString Word;
	};

	FWord CallTypeWord(FAutomationTestBase& Test, const UOWWorld* World, const FString& Type, const FString& Supertype, bool bMany)
	{
		FWord R;
		OWBlueprintTest::FCall Call(Test, UOWNames::StaticClass(), TEXT("TypeWord"));
		Call.SetWorld(World);
		Call.SetString(TEXT("Type"), Type);
		Call.SetString(TEXT("Supertype"), Supertype);
		Call.SetBool(TEXT("bMany"), bMany);
		Call.Invoke();
		R.bFound = Call.ReturnBool();
		R.Word = Call.Out<FStrProperty>(TEXT("Word"));
		return R;
	}

	// The core's answer for the same question, as the door gives it.
	FWord CoreWord(const UOWWorld* World, const FString& Type, const FString& Supertype, bool bMany)
	{
		const std::string S = Utf8(Supertype);
		const std::optional<std::string> W = OnlyWorlds::OWNames::TypeWord(World->Native()->info, Utf8(Type),
			S.empty() ? std::nullopt : std::optional<std::string_view>(S), bMany);
		return { W.has_value(), W ? Str(*W) : FString() };
	}

	void SetNames(UOWWorld* World, OWJsonValue Map) { World->Native()->SetInfo(OnlyWorlds::OWNames::Field, std::move(Map)); }

	OWJsonValue J(const char* Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	FString TextOf(const OWJsonValue* V) { return V && V->IsString() ? Str(V->Text()) : FString(); }

	FString SupertypeOf(const OnlyWorlds::OWElement& E) { return TextOf(E.body.Find("supertype")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWNamesWorldTest, "OnlyWorlds.Names.World", OWTestFlags)

bool FOWNamesWorldTest::RunTest(const FString& Parameters)
{
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	if (!TestFalse(TEXT("Sikelia carries no x_names"), World->Native()->info.Has(OnlyWorlds::OWNames::Field))) return false;

	// No map: the standard's name for every element, singular and plural, by its type and supertype.
	int32 Asked = 0, Standard = 0, WithSupertype = 0;
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		const FString Supertype = SupertypeOf(E);
		WithSupertype += !Supertype.IsEmpty();
		for (bool bMany : { false, true })
		{
			++Asked;
			const FWord W = CallTypeWord(*this, World, Str(E.Type()), Supertype, bMany);
			if (!W.bFound && W.Word.IsEmpty()) ++Standard;
		}
	}
	TestTrue(TEXT("Sikelia holds elements"), Asked > 0);
	TestEqual(TEXT("no map: the standard's name for every element"), Standard, Asked);

	// A map on the World (in memory; nothing is written).
	SetNames(World, J(R"({"version": 1,
		"types": {"character": {"one": "Employee", "many": "Employees"}, "institution": {"one": " Team "}, "location": {"one": "  "}},
		"supertypes": {"character": {" agent ": {"one": "AI agent", "many": "AI agents"}, "Ghost": {"many": "Ghosts"}}}})"));
	const struct { const TCHAR* Type; const TCHAR* Supertype; bool bMany; const TCHAR* Word; } Asks[] = {
		{ TEXT("character"), TEXT(""), false, TEXT("Employee") },
		{ TEXT("character"), TEXT(""), true, TEXT("Employees") },
		{ TEXT("institution"), TEXT(""), true, TEXT("Team") },  // trimmed; no plural prints the singular
		{ TEXT("character"), TEXT("AGENT"), false, TEXT("AI agent") },  // the supertype first, trimmed and folded
		{ TEXT("character"), TEXT("Agent"), true, TEXT("AI agents") },
		{ TEXT("character"), TEXT("ghost"), false, TEXT("Employee") },  // an entry with no one: the type's word
		{ TEXT("character"), TEXT("Soldier"), true, TEXT("Employees") },
		{ TEXT("character"), TEXT("   "), false, TEXT("Employee") },  // a blank supertype is none
		{ TEXT("location"), TEXT(""), false, nullptr },  // a blank one: the standard's name
		{ TEXT("species"), TEXT(""), false, nullptr },
		{ TEXT("Character"), TEXT(""), false, nullptr },  // a slug as the schema spells it
		{ TEXT(""), TEXT(""), false, nullptr },
	};
	for (const auto& A : Asks)
	{
		const FWord W = CallTypeWord(*this, World, A.Type, A.Supertype, A.bMany);
		const FString At = FString::Printf(TEXT("%s / \"%s\"%s"), A.Type, A.Supertype, A.bMany ? TEXT(" (many)") : TEXT(""));
		if (A.Word) TestTrue(At + TEXT(": ") + A.Word + TEXT(", got ") + W.Word, W.bFound && W.Word == A.Word);
		else TestTrue(At + TEXT(": the standard's name, got ") + W.Word, !W.bFound && W.Word.IsEmpty());
	}
	TestFalse(TEXT("no world: the standard's name"), CallTypeWord(*this, nullptr, TEXT("character"), TEXT(""), false).bFound);

	// Every element again, with the map: the core's answer, by its type and supertype.
	int32 Same = 0, Words = 0;
	Asked = 0;
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		for (bool bMany : { false, true })
		{
			++Asked;
			const FString Type = Str(E.Type()), Supertype = SupertypeOf(E);
			const FWord W = CallTypeWord(*this, World, Type, Supertype, bMany);
			const FWord C = CoreWord(World, Type, Supertype, bMany);
			Words += W.bFound;
			if (W.bFound == C.bFound && W.Word == C.Word) ++Same;
			else if (Asked - Same <= 3) AddError(FString::Printf(TEXT("%s %s \"%s\": \"%s\", the core \"%s\""), *Str(E.Id()), *Type,
				*Supertype, *W.Word, *C.Word));
		}
	}
	TestEqual(TEXT("every element with the map, as the core reads it"), Same, Asked);
	TestTrue(TEXT("and some of them in the world's words"), Words > 0);
	AddInfo(FString::Printf(TEXT("Sikelia: %d elements (%d with a supertype), %d lookups each way; %d in the map's words"),
		Asked / 2, WithSupertype, Asked, Words));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWNamesFixtureTest, "OnlyWorlds.Names.Fixture", OWTestFlags)

bool FOWNamesFixtureTest::RunTest(const FString& Parameters)
{
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/names.fixture.json"));
	OWJsonValue Doc;
	if (!OWTestPaths::ReadJson(Path, Doc))
	{
		AddError(TEXT("no names fixture at ") + Path);
		return false;
	}
	const OWJsonValue* Cases = Doc.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Moppetopia(), TEXT("OW_MOPPETOPIA"));
	if (!World) return false;

	int32 Words = 0, Standard = 0, Failed = 0;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const FString Name = TextOf(Case.Find("name"));
		const OWJsonValue* Map = Case.Find("map");
		if (!TestTrue(Name + TEXT(": a map"), Map != nullptr)) continue;
		SetNames(World, *Map);
		const OWJsonValue* Many = Case.Find("many");
		const OWJsonValue* Want = Case.Find("word");
		const FWord W = CallTypeWord(*this, World, TextOf(Case.Find("type")), TextOf(Case.Find("supertype")),
			Many && Many->IsBool() && Many->AsBool());
		if (Want && Want->IsString())
		{
			++Words;
			if (!TestTrue(Name + TEXT(": ") + Str(Want->Text()) + TEXT(", got ") + W.Word, W.bFound && W.Word == Str(Want->Text()))) ++Failed;
		}
		else
		{
			++Standard;
			if (!TestTrue(Name + TEXT(": the standard's name, got ") + W.Word, !W.bFound && W.Word.IsEmpty())) ++Failed;
		}
	}
	TestEqual(TEXT("29 cases"), Words + Standard, 29);
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("names fixture: %d cases on Moppetopia's World through the Blueprint path (%d words, %d the "
		"standard's name)"), Words + Standard, Words, Standard));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
