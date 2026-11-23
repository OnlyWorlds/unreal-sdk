// The lens profile through UOWLens: a world built here in memory (a body that names no type, read as its folder's; a
// hidden, a proposed, a room and a reading-aid element; five lenses, one a template, one unreadable, one no lens at
// all), each call and refusal; then every shape, keys, uses, resolve and invariant case of the profile's fixture
// (codegen/schema-dist/profiles/lens.fixture.json), each case's world placed in a world of its own (the fixture's
// world-level hidden list as hidden: true on each listed body, the one mark the profile reads both as; its now as
// world.json's time_current; its members list as the JSON text a Blueprint passes), compared as the core's own test
// compares it; then every function called as a Blueprint calls it (OWBlueprintCall.h), its structs set member by member
// and read back by property, against the direct call. The graph and degree cases are OWWorldGraphTests.cpp's.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWBlueprintTest::FCall;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue LensParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string LensText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	FString LensShow(const TArray<FString>& Ids) { return TEXT("[") + FString::Join(Ids, TEXT(", ")) + TEXT("]"); }

	// Two lists of ids, compared exactly (TArray<FString>'s own == ignores case).
	bool LensSame(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.Num() != B.Num()) return false;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			if (!A[I].Equals(B[I], ESearchCase::CaseSensitive)) return false;
		}
		return true;
	}

	TArray<FString> LensIds(std::initializer_list<const TCHAR*> L)
	{
		TArray<FString> Out;
		for (const TCHAR* S : L) Out.Add(S);
		return Out;
	}

	std::string LensCompact(const OWJsonValue& V)
	{
		const std::string Full = OnlyWorlds::OWJson::Stringify(V);
		std::string Out;
		for (size_t I = 0; I < Full.size(); ++I)
		{
			if (Full[I] != '\n')
			{
				Out.push_back(Full[I]);
				continue;
			}
			while (I + 1 < Full.size() && Full[I + 1] == ' ') ++I;
		}
		return Out;
	}

	// Equal as JSON values: an object's keys in any order, a number by its value (2 and 2.0 alike), as the core's test.
	bool LensEqual(const OWJsonValue& A, const OWJsonValue& B)
	{
		if (A.kind() != B.kind()) return false;
		switch (A.kind())
		{
		case OWJsonValue::Kind::Null: return true;
		case OWJsonValue::Kind::Bool: return A.AsBool() == B.AsBool();
		case OWJsonValue::Kind::Number:
			return OnlyWorlds::OWJson::CanonicalNumber(A.Text()) == OnlyWorlds::OWJson::CanonicalNumber(B.Text());
		case OWJsonValue::Kind::String: return A.Text() == B.Text();
		case OWJsonValue::Kind::Array:
			if (A.Size() != B.Size()) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				if (!LensEqual(A.Items()[I], B.Items()[I])) return false;
			}
			return true;
		case OWJsonValue::Kind::Object:
			if (A.Size() != B.Size()) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				const OWJsonValue* Other = B.Find(A.Keys()[I]);
				if (!Other || !LensEqual(A.Items()[I], *Other)) return false;
			}
			return true;
		}
		return false;
	}

	OWJsonValue LensStrings(const TArray<FString>& Ids)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& Id : Ids) Out.Push(OWJsonValue::String(Utf8(Id)));
		return Out;
	}

	OWJsonValue LensCounts(const TArray<FOWLensCut>& Cut)
	{
		OWJsonValue Out = OWJsonValue::Object();
		for (const FOWLensCut& C : Cut) Out.Set(Utf8(C.Reason), OWJsonValue::Number(static_cast<int64_t>(C.Count)));
		return Out;
	}

	OWJsonValue LensRows(const TArray<FString>& Rows)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& Row : Rows) Out.Push(LensParse(Utf8(Row)));
		return Out;
	}

	// A reading read back to the fixture's shape: {kept, rows, cut, notes_ignored, mail, unreadable}, each row through
	// its JSON text.
	OWJsonValue LensReadingJson(const FOWLensReading& R)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("kept", LensStrings(R.Kept));
		Out.Set("rows", LensRows(R.Rows));
		Out.Set("cut", LensCounts(R.Cut));
		Out.Set("notes_ignored", OWJsonValue::Number(static_cast<int64_t>(R.NotesIgnored)));
		if (R.bHasMail)
		{
			OWJsonValue Mail = OWJsonValue::Object();
			Mail.Set("unread", LensStrings(R.Mail.Unread));
			Mail.Set("read", LensStrings(R.Mail.Read));
			Mail.Set("rows", LensRows(R.Mail.Rows));
			Mail.Set("cut", LensCounts(R.Mail.Cut));
			OWJsonValue Totals = OWJsonValue::Object();
			Totals.Set("unread", OWJsonValue::Number(static_cast<int64_t>(R.Mail.TotalUnread)));
			Totals.Set("read", OWJsonValue::Number(static_cast<int64_t>(R.Mail.TotalRead)));
			Mail.Set("totals", std::move(Totals));
			Out.Set("mail", std::move(Mail));
		}
		else
		{
			Out.Set("mail", OWJsonValue::Null());
		}
		Out.Set("unreadable", R.Unreadable.IsEmpty() ? OWJsonValue::Null() : OWJsonValue::String(Utf8(R.Unreadable)));
		return Out;
	}

	// -- worlds made here ---------------------------------------------------------------------------------------------

	// A world in memory: world.json's id (and time_current when given), then each body added under its folder's type,
	// in the order given.
	UOWWorld* LensMakeWorld(std::vector<std::pair<std::string, OWJsonValue>> Bodies, const OWJsonValue* Now = nullptr)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f000"));
		if (Now && !Now->IsNull()) Native->info.Set("time_current", *Now);
		for (std::pair<std::string, OWJsonValue>& B : Bodies) Native->Add(B.first, std::move(B.second));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}

	// A body's folder: the type it names as a string, "" for none (it then reads as no type, as the bare body does).
	std::string LensFolderOf(const OWJsonValue& Body)
	{
		const OWJsonValue* T = Body.IsObject() ? Body.Find("type") : nullptr;
		return T && T->IsString() ? T->Text() : std::string();
	}

	// A fixture world placed in a world: its bodies in order, each id of its hidden list as hidden: true on the body,
	// its now as time_current. nullptr and why when an element is not an object (no world holds one).
	UOWWorld* LensPlace(const OWJsonValue& Fixture, FString& Why)
	{
		std::set<std::string> Hidden;
		if (const OWJsonValue* H = Fixture.Find("hidden"); H && H->IsArray())
		{
			for (const OWJsonValue& Id : H->Items())
			{
				if (Id.IsString()) Hidden.insert(Id.Text());
			}
		}
		const OWJsonValue* Elements = Fixture.Find("elements");
		if (!Elements || !Elements->IsArray())
		{
			Why = TEXT("no elements list");
			return nullptr;
		}
		std::vector<std::pair<std::string, OWJsonValue>> Bodies;
		for (const OWJsonValue& E : Elements->Items())
		{
			if (!E.IsObject())
			{
				Why = TEXT("an element that is not an object");
				return nullptr;
			}
			OWJsonValue B = E;
			if (Hidden.count(LensText(B.Find("id")))) B.Set("hidden", OWJsonValue::Bool(true));
			Bodies.emplace_back(LensFolderOf(B), std::move(B));
		}
		return LensMakeWorld(std::move(Bodies), Fixture.Find("now"));
	}

	// A fixture reader as the struct holds it; false and why for one it cannot hold.
	bool LensReaderOf(const OWJsonValue& R, FOWLensReader& Out, FString& Why)
	{
		if (!R.IsObject())
		{
			Why = TEXT("a reader that is not an object");
			return false;
		}
		const std::string Rung = LensText(R.Find("rung"));
		const OWJsonValue* As = R.Find("as");
		if (As && !As->IsNull())
		{
			if (Rung != "owner" || !As->IsObject() || (As->Find("as") && !As->Find("as")->IsNull()))
			{
				Why = TEXT("an as the struct cannot hold");
				return false;
			}
			if (!LensReaderOf(*As, Out, Why)) return false;
			Out.bOwnerCounts = true;
			return true;
		}
		if (Rung == "owner") Out.Rung = EOWLensRung::Owner;
		else if (Rung == "contributor") Out.Rung = EOWLensRung::Contributor;
		else if (Rung == "guest") Out.Rung = EOWLensRung::Guest;
		else if (Rung == "public") Out.Rung = EOWLensRung::Public;
		else
		{
			Why = TEXT("a rung outside the four");
			return false;
		}
		const OWJsonValue* C = R.Find("character");
		if (C && !C->IsNull() && !C->IsString())
		{
			Why = TEXT("a character that is not a string");
			return false;
		}
		Out.Character = Str(LensText(C));
		for (const char* Key : { "whole", "known_of" })
		{
			const OWJsonValue* L = R.Find(Key);
			if (!L || L->IsNull()) continue;
			if (!L->IsArray())
			{
				Why = TEXT("a list that is not a list");
				return false;
			}
			TArray<FString>& Into = std::string(Key) == "whole" ? Out.Whole : Out.KnownOf;
			for (const OWJsonValue& Id : L->Items())
			{
				if (!Id.IsString())
				{
					Why = TEXT("an id that is not a string");
					return false;
				}
				Into.Add(Str(Id.Text()));
			}
		}
		return true;
	}

	// A fixture reader by name, `as` (a reader's name) resolved to that reader.
	OWJsonValue LensReaderByName(const OWJsonValue& World, const std::string& Name)
	{
		const OWJsonValue* Readers = World.Find("readers");
		const OWJsonValue* R = Readers ? Readers->Find(Name) : nullptr;
		if (!R) return OWJsonValue::Null();
		OWJsonValue Out = *R;
		if (const OWJsonValue* As = R->Find("as"); As && As->IsString()) Out.Set("as", LensReaderByName(World, As->Text()));
		return Out;
	}

	FString LensJsonText(const OWJsonValue& V) { return Str(OnlyWorlds::OWJson::Stringify(V, true)); }

	// A case's lens through the door: an id by Resolve, a body by ResolveJson (its JSON text).
	bool LensResolveCase(const UOWWorld* World, const OWJsonValue& Lens, const FOWLensReader& Reader, const FOWLensOptions& Options,
		FOWLensReading& Reading, FString& Reason)
	{
		if (Lens.IsString()) return UOWLens::Resolve(World, Str(Lens.Text()), Reader, Options, Reading, Reason);
		return UOWLens::ResolveJson(World, LensJsonText(Lens), Reader, Options, Reading, Reason);
	}

	// -- the hand-made world ------------------------------------------------------------------------------------------

	// Out of type order; c2 names no type (its folder's: character). c3 hidden, o1 proposed, n1 a room element, g1 a
	// reading aid; k1 a lens on Ada, k2 a template, k3 unreadable (depth "far"), k4 no lens ("Lenses"), k5 names on Keep.
	UOWWorld* LensHandWorld()
	{
		const char* const Bodies[][2] = {
			{ "character", R"({"id": "c1", "type": "character", "name": "Ada", "birth_date": 40, "location": "l1", "friends": ["c2", "c3"], "x_lens_uses": ["k2", "", "k1", "k2"]})" },
			{ "character", R"({"id": "c2", "name": "Bo", "birth_date": 60.0})" },
			{ "character", R"({"id": "c3", "type": "character", "name": "Cass", "hidden": true, "friends": ["c1"]})" },
			{ "location", R"({"id": "l1", "type": "location", "name": "Keep"})" },
			{ "event", R"({"id": "e1", "type": "event", "name": "Siege", "start_date": 7.0, "characters": ["c1", "c2"]})" },
			{ "object", R"({"id": "o1", "type": "object", "name": "Rumour", "x_review_proposed": {"by": "m2"}, "location": "l1"})" },
			{ "narrative", R"({"id": "n1", "type": "narrative", "supertype": "Message", "name": "A line", "x_room_of": "w", "characters": ["c1"]})" },
			{ "law", R"({"id": "g1", "type": "law", "supertype": "Charter", "name": "House rules", "x_charter_of": "w"})" },
			{ "construct", R"({"id": "k1", "type": "construct", "supertype": "Lens", "name": "Ada's circle", "characters": ["c1"], "x_lens": {"depth": 1}})" },
			{ "construct", R"({"id": "k2", "type": "construct", "supertype": " lens ", "subtype": "Template", "name": "A template", "characters": ["c1"]})" },
			{ "construct", R"({"id": "k3", "type": "construct", "supertype": "Lens", "name": "Bad", "x_lens": {"depth": "far"}})" },
			{ "construct", R"({"id": "k4", "type": "construct", "supertype": "Lenses", "name": "Not one"})" },
			{ "construct", R"({"id": "k5", "type": "construct", "supertype": "Lens", "name": "Names", "locations": ["l1"], "x_lens": {"depth": 1, "show": "name", "budget": 2}})" },
		};
		std::vector<std::pair<std::string, OWJsonValue>> B;
		for (const auto& Row : Bodies) B.emplace_back(Row[0], LensParse(Row[1]));
		const OWJsonValue Now = OWJsonValue::Number(static_cast<int64_t>(100));
		return LensMakeWorld(std::move(B), &Now);
	}

	FOWLensReader LensReader(EOWLensRung Rung, bool bOwnerCounts = false)
	{
		FOWLensReader R;
		R.Rung = Rung;
		R.bOwnerCounts = bOwnerCounts;
		return R;
	}

	FString LensCutText(const TArray<FOWLensCut>& Cut) { return Str(LensCompact(LensCounts(Cut))); }

	// -- the calls, each as a graph makes it ------------------------------------------------------------------------

	// An array of strings read by its property (the return when Param is nullptr), as an array pin holds it.
	TArray<FString> LensCallStrings(FAutomationTestBase& Test, FCall& Call, const TCHAR* Param)
	{
		TArray<FString> Out;
		const FArrayProperty* P = CastField<FArrayProperty>(Param ? Call.Property(Param) : Call.Return());
		const FStrProperty* Inner = P ? CastField<FStrProperty>(P->Inner) : nullptr;
		if (!Inner || !Call.GetFrame() || !OWBlueprintTest::IsOutputPin(P))
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FString> %s"), Call.GetName(), Param ? Param : TEXT("return")));
			return Out;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		for (int32 I = 0; I < Array.Num(); ++I) Out.Add(*reinterpret_cast<const FString*>(Array.GetRawPtr(I)));
		return Out;
	}

	// An input array of strings set in the frame by its property, as a Make Array node feeds a pin.
	void LensCallSetStrings(FAutomationTestBase& Test, FCall& Call, const TCHAR* Param, const TArray<FString>& Values)
	{
		const FArrayProperty* P = CastField<FArrayProperty>(Call.Property(Param));
		const FStrProperty* Inner = P ? CastField<FStrProperty>(P->Inner) : nullptr;
		if (!Inner || !Call.GetFrame() || OWBlueprintTest::IsOutputPin(P))
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FString> input %s"), Call.GetName(), Param));
			return;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		Array.EmptyAndAddValues(Values.Num());
		for (int32 I = 0; I < Values.Num(); ++I) *reinterpret_cast<FString*>(Array.GetRawPtr(I)) = Values[I];
	}

	// A struct's array-of-strings member, set or read by its property.
	void LensSetStringsMember(FAutomationTestBase& Test, const UScriptStruct* Struct, void* Data, const TCHAR* Name,
		const TArray<FString>& Values)
	{
		const FArrayProperty* P = CastField<FArrayProperty>(Struct->FindPropertyByName(Name));
		if (!P || !CastField<FStrProperty>(P->Inner) || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FString> member %s"), *Struct->GetName(), Name));
			return;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Data));
		Array.EmptyAndAddValues(Values.Num());
		for (int32 I = 0; I < Values.Num(); ++I) *reinterpret_cast<FString*>(Array.GetRawPtr(I)) = Values[I];
	}

	TArray<FString> LensStringsMember(FAutomationTestBase& Test, const UScriptStruct* Struct, const void* Data, const TCHAR* Name)
	{
		TArray<FString> Out;
		const FArrayProperty* P = CastField<FArrayProperty>(Struct->FindPropertyByName(Name));
		if (!P || !CastField<FStrProperty>(P->Inner) || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FString> member %s"), *Struct->GetName(), Name));
			return Out;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Data));
		for (int32 I = 0; I < Array.Num(); ++I) Out.Add(*reinterpret_cast<const FString*>(Array.GetRawPtr(I)));
		return Out;
	}

	// The reader and the options set member by member, as Make nodes set them.
	void LensMakeReader(FAutomationTestBase& Test, void* Data, const FOWLensReader& R)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* S = FOWLensReader::StaticStruct();
		SetEnumMember(Test, S, Data, TEXT("Rung"), StaticEnum<EOWLensRung>(), static_cast<int64>(R.Rung));
		SetMember<FStrProperty>(Test, S, Data, TEXT("Character"), R.Character);
		LensSetStringsMember(Test, S, Data, TEXT("Whole"), R.Whole);
		LensSetStringsMember(Test, S, Data, TEXT("KnownOf"), R.KnownOf);
		SetBoolMember(Test, S, Data, TEXT("bOwnerCounts"), R.bOwnerCounts);
	}

	void LensMakeOptions(FAutomationTestBase& Test, void* Data, const FOWLensOptions& O)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* S = FOWLensOptions::StaticStruct();
		SetBoolMember(Test, S, Data, TEXT("bHasAsOf"), O.bHasAsOf);
		SetMember<FInt64Property>(Test, S, Data, TEXT("AsOf"), O.AsOf);
		SetMember<FStrProperty>(Test, S, Data, TEXT("Clock"), O.Clock);
		SetMember<FStrProperty>(Test, S, Data, TEXT("MembersJson"), O.MembersJson);
	}

	// The element calls of one id: IsLens, IsTemplate, IsHidden, IsProposed, IsRoomElement, IsReadingAid.
	bool LensCallIs(FAutomationTestBase& Test, const TCHAR* Name, const UOWWorld* World, const FString& ElementId)
	{
		FCall Call(Test, UOWLens::StaticClass(), Name);
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), ElementId);
		Call.Invoke();
		return Call.ReturnBool();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLensWorldTest, "OnlyWorlds.Lens.World", OWTestFlags)

bool FOWLensWorldTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = LensHandWorld();
	auto SameIds = [&](const FString& What, const TArray<FString>& Got, const TArray<FString>& Want)
	{
		TestTrue(What + TEXT(": ") + LensShow(Want) + TEXT(", got ") + LensShow(Got), LensSame(Got, Want));
	};

	// The lenses: k2's supertype trimmed and folded, k4's "Lenses" not the word.
	SameIds(TEXT("GetLenses"), UOWLens::GetLenses(World), LensIds({ TEXT("k1"), TEXT("k2"), TEXT("k3"), TEXT("k5") }));
	TestEqual(TEXT("GetLenses, no world"), UOWLens::GetLenses(nullptr).Num(), 0);
	for (const TCHAR* Id : { TEXT("k1"), TEXT("k2"), TEXT("k3"), TEXT("k5") })
		TestTrue(FString::Printf(TEXT("IsLens %s"), Id), UOWLens::IsLens(World, Id));
	for (const TCHAR* Id : { TEXT("k4"), TEXT("c1"), TEXT("nope"), TEXT("") })
		TestFalse(FString::Printf(TEXT("IsLens %s"), Id), UOWLens::IsLens(World, Id));
	TestFalse(TEXT("IsLens, no world"), UOWLens::IsLens(nullptr, TEXT("k1")));
	TestTrue(TEXT("IsTemplate k2"), UOWLens::IsTemplate(World, TEXT("k2")));
	TestFalse(TEXT("IsTemplate k1"), UOWLens::IsTemplate(World, TEXT("k1")));
	TestFalse(TEXT("IsTemplate k4 (no lens)"), UOWLens::IsTemplate(World, TEXT("k4")));

	// Where a seat's choice lives: the membership wins whole; else Ada's x_lens_uses, each once, "" dropped.
	SameIds(TEXT("LensesFor Ada, no choice"), UOWLens::LensesFor(World, TEXT("c1"), false, {}), LensIds({ TEXT("k2"), TEXT("k1") }));
	SameIds(TEXT("LensesFor Ada, an empty choice"), UOWLens::LensesFor(World, TEXT("c1"), true, {}), {});
	SameIds(TEXT("LensesFor Ada, chose k3 twice and \"\""),
		UOWLens::LensesFor(World, TEXT("c1"), true, LensIds({ TEXT("k3"), TEXT("k3"), TEXT("") })), LensIds({ TEXT("k3") }));
	SameIds(TEXT("LensesFor Bo (no x_lens_uses)"), UOWLens::LensesFor(World, TEXT("c2"), false, {}), {});
	SameIds(TEXT("LensesFor nope"), UOWLens::LensesFor(World, TEXT("nope"), false, {}), {});
	SameIds(TEXT("LensesFor, no world: the membership alone"), UOWLens::LensesFor(nullptr, TEXT("c1"), true, LensIds({ TEXT("k5") })),
		LensIds({ TEXT("k5") }));

	// The marks.
	TestTrue(TEXT("IsHidden c3"), UOWLens::IsHidden(World, TEXT("c3")));
	TestFalse(TEXT("IsHidden c1"), UOWLens::IsHidden(World, TEXT("c1")));
	TestTrue(TEXT("IsProposed o1"), UOWLens::IsProposed(World, TEXT("o1")));
	TestFalse(TEXT("IsProposed c1"), UOWLens::IsProposed(World, TEXT("c1")));
	TestTrue(TEXT("IsRoomElement n1"), UOWLens::IsRoomElement(World, TEXT("n1")));
	TestFalse(TEXT("IsRoomElement c1"), UOWLens::IsRoomElement(World, TEXT("c1")));
	TestTrue(TEXT("IsReadingAid g1"), UOWLens::IsReadingAid(World, TEXT("g1")));
	TestFalse(TEXT("IsReadingAid k1"), UOWLens::IsReadingAid(World, TEXT("k1")));
	TestFalse(TEXT("IsHidden nope"), UOWLens::IsHidden(World, TEXT("nope")));

	// Dates by the profile's table: Bo's 60.0 whole (his type his folder's), the Event's 7.0 whole; a Location with no
	// founding_date and a Law with no date have none.
	{
		int64 Date = -1;
		TestTrue(TEXT("DateOf c1: 40"), UOWLens::DateOf(World, TEXT("c1"), Date) && Date == 40);
		TestTrue(TEXT("DateOf c2: 60"), UOWLens::DateOf(World, TEXT("c2"), Date) && Date == 60);
		TestTrue(TEXT("DateOf e1: 7"), UOWLens::DateOf(World, TEXT("e1"), Date) && Date == 7);
		TestTrue(TEXT("DateOf l1: none, 0"), !UOWLens::DateOf(World, TEXT("l1"), Date) && Date == 0);
		TestFalse(TEXT("DateOf g1: none"), UOWLens::DateOf(World, TEXT("g1"), Date));
		TestFalse(TEXT("DateOf nope: none"), UOWLens::DateOf(World, TEXT("nope"), Date));
	}

	// k1 on Ada, one hop both ways: the owner keeps Cass (hidden) and the template that names Ada, the room line is cut
	// by types; a contributor and the public neither see Cass nor count her; the owner as a contributor counts her.
	const FOWLensOptions NoOptions = FOWLensOptions();
	auto Read = [&](const FString& What, const FString& Lens, const FOWLensReader& Reader, FOWLensReading& Reading)
	{
		FString Reason;
		const bool bOk = UOWLens::Resolve(World, Lens, Reader, NoOptions, Reading, Reason);
		TestTrue(What + TEXT(": answered, got ") + Reason, bOk && Reason.IsEmpty());
	};
	{
		FOWLensReading R;
		Read(TEXT("Resolve k1, the owner"), TEXT("k1"), LensReader(EOWLensRung::Owner), R);
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("c1"), TEXT("c2"), TEXT("c3"), TEXT("k2"), TEXT("e1"), TEXT("l1") }));
		TestEqual(TEXT("its cut"), LensCutText(R.Cut), FString(TEXT("{\"types\": 1}")));
		TestTrue(TEXT("no stubs, no mail, readable"), R.KnownOf.Num() == 0 && !R.bHasMail && R.Unreadable.IsEmpty() && R.NotesIgnored == 0);
		TestEqual(TEXT("a row per kept id"), R.Rows.Num(), 6);
		if (R.Rows.Num() == 6)
		{
			// Rows are full bodies as served: Bo's type his folder's, Cass's hidden mark on her body.
			const OWJsonValue Bo = LensParse(Utf8(R.Rows[1]));
			TestEqual(TEXT("Bo's row names his folder's type"), Str(LensText(Bo.Find("type"))), FString(TEXT("character")));
			const OWJsonValue Cass = LensParse(Utf8(R.Rows[2]));
			const OWJsonValue* Mark = Cass.Find("hidden");
			TestTrue(TEXT("Cass's row carries hidden: true"), Mark && Mark->IsBool() && Mark->AsBool());
		}
	}
	{
		FOWLensReading R;
		Read(TEXT("Resolve k1, a contributor"), TEXT("k1"), LensReader(EOWLensRung::Contributor), R);
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("c1"), TEXT("c2"), TEXT("k2"), TEXT("e1"), TEXT("l1") }));
		TestEqual(TEXT("its cut"), LensCutText(R.Cut), FString(TEXT("{\"types\": 1}")));
		const OWJsonValue Ada = R.Rows.Num() ? LensParse(Utf8(R.Rows[0])) : OWJsonValue();
		TestTrue(TEXT("Ada's friends as a contributor is served them: Cass left out"),
			LensEqual(Ada.Find("friends") ? *Ada.Find("friends") : OWJsonValue(), LensParse(R"(["c2"])")));
	}
	{
		FOWLensReading R;
		Read(TEXT("Resolve k1, the owner as a contributor"), TEXT("k1"), LensReader(EOWLensRung::Contributor, true), R);
		SameIds(TEXT("its kept, the contributor's"), R.Kept, LensIds({ TEXT("c1"), TEXT("c2"), TEXT("k2"), TEXT("e1"), TEXT("l1") }));
		TestEqual(TEXT("its cut, with the owner's not_visible"), LensCutText(R.Cut), FString(TEXT("{\"types\": 1,\"not_visible\": 1}")));
	}
	{
		FOWLensReading R;
		Read(TEXT("Resolve k1, the public"), TEXT("k1"), LensReader(EOWLensRung::Public), R);
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("c1"), TEXT("c2"), TEXT("k2"), TEXT("e1"), TEXT("l1") }));
		TestEqual(TEXT("no cut it may count"), R.Cut.Num(), 0);
	}
	{
		// A guest holding Ada, k1 and the Siege whole, told of Bo, Cass and the Keep: Cass is hidden, so not even a stub.
		FOWLensReader Guest = LensReader(EOWLensRung::Guest);
		Guest.Whole = LensIds({ TEXT("c1"), TEXT("k1"), TEXT("e1") });
		Guest.KnownOf = LensIds({ TEXT("c2"), TEXT("c3"), TEXT("l1") });
		FOWLensReading R;
		Read(TEXT("Resolve k1, a guest"), TEXT("k1"), Guest, R);
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("c1"), TEXT("c2"), TEXT("e1"), TEXT("l1") }));
		SameIds(TEXT("its stubs"), R.KnownOf, LensIds({ TEXT("c2"), TEXT("l1") }));
		const OWJsonValue Bo = R.Rows.Num() > 1 ? LensParse(Utf8(R.Rows[1])) : OWJsonValue();
		const OWJsonValue* Stub = Bo.Find("known_of");
		TestTrue(TEXT("Bo's row is a stub"), Stub && Stub->IsBool() && Stub->AsBool());
	}
	{
		FOWLensReading R;
		Read(TEXT("Resolve k2 (a template)"), TEXT("k2"), LensReader(EOWLensRung::Owner), R);
		TestTrue(TEXT("a template resolves to nothing"), R.Kept.Num() == 0 && R.Cut.Num() == 0 && R.Unreadable.IsEmpty());
		Read(TEXT("Resolve k3 (depth \"far\")"), TEXT("k3"), LensReader(EOWLensRung::Owner), R);
		TestTrue(TEXT("k3 unreadable by depth, got ") + R.Unreadable, R.Unreadable.Equals(TEXT("depth"), ESearchCase::CaseSensitive) && R.Kept.Num() == 0);
		Read(TEXT("Resolve nope"), TEXT("nope"), LensReader(EOWLensRung::Owner), R);
		TestTrue(TEXT("an id the world does not hold reads as nothing"), R.Kept.Num() == 0 && R.Unreadable.IsEmpty());
		Read(TEXT("Resolve k4 (no lens)"), TEXT("k4"), LensReader(EOWLensRung::Owner), R);
		TestEqual(TEXT("an element that is no lens reads as nothing"), R.Kept.Num(), 0);
	}
	{
		// k5 on the Keep, names only, a budget of two: the Keep and Ada; the proposed Rumour is the owner's not_visible.
		FOWLensReading R;
		Read(TEXT("Resolve k5"), TEXT("k5"), LensReader(EOWLensRung::Owner), R);
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("l1"), TEXT("c1") }));
		TestEqual(TEXT("its cut"), LensCutText(R.Cut), FString(TEXT("{\"not_visible\": 1}")));
		TestTrue(TEXT("a name row is {id, type, name}"), R.Rows.Num() == 2
			&& LensEqual(LensParse(Utf8(R.Rows[1])), LensParse(R"({"id": "c1", "type": "character", "name": "Ada"})")));
	}
	{
		// A lens body from elsewhere, read over this world; a body that is no lens reads as nothing.
		FOWLensReading R;
		FString Reason;
		TestTrue(TEXT("ResolveJson, a body on the Siege"), UOWLens::ResolveJson(World,
			TEXT("{\"type\": \"construct\", \"supertype\": \"Lens\", \"events\": [\"e1\"], \"x_lens\": {\"depth\": 0, \"show\": \"first_line\"}}"),
			LensReader(EOWLensRung::Owner), NoOptions, R, Reason));
		SameIds(TEXT("its kept"), R.Kept, LensIds({ TEXT("e1") }));
		TestTrue(TEXT("a first_line row with no description invents none"),
			R.Rows.Num() == 1 && LensEqual(LensParse(Utf8(R.Rows[0])), LensParse(R"({"id": "e1", "type": "event", "name": "Siege"})")));
		TestTrue(TEXT("ResolveJson, a Spec"), UOWLens::ResolveJson(World, TEXT("{\"type\": \"construct\", \"supertype\": \"Spec\", \"events\": [\"e1\"]}"),
			LensReader(EOWLensRung::Owner), NoOptions, R, Reason) && R.Kept.Num() == 0);
	}

	// Refusals: the reading is the default and the reason names the mistake.
	{
		FOWLensReading R;
		FString Reason;
		TestTrue(TEXT("Resolve, no world"), !UOWLens::Resolve(nullptr, TEXT("k1"), LensReader(EOWLensRung::Owner), NoOptions, R, Reason)
			&& Reason == TEXT("no world"));
		FOWLensOptions Clock;
		Clock.Clock = TEXT("yesterday");
		TestTrue(TEXT("Resolve, a clock that does not read: refused, got ") + Reason,
			!UOWLens::Resolve(World, TEXT("k1"), LensReader(EOWLensRung::Owner), Clock, R, Reason)
			&& Reason == TEXT("resolve: clock is an RFC 3339 date-time; got yesterday") && R.Kept.Num() == 0);
		FOWLensOptions Members;
		Members.MembersJson = TEXT("[");
		TestTrue(TEXT("Resolve, members that do not parse: refused, got ") + Reason,
			!UOWLens::Resolve(World, TEXT("k1"), LensReader(EOWLensRung::Owner), Members, R, Reason) && Reason.StartsWith(TEXT("members: ")));
		TestTrue(TEXT("ResolveJson, a body that does not parse: refused, got ") + Reason,
			!UOWLens::ResolveJson(World, TEXT("{"), LensReader(EOWLensRung::Owner), NoOptions, R, Reason) && Reason.StartsWith(TEXT("lens: ")));
		TestTrue(TEXT("ResolveJson, a number: the core's mistake, got ") + Reason,
			!UOWLens::ResolveJson(World, TEXT("7"), LensReader(EOWLensRung::Owner), NoOptions, R, Reason)
			&& Reason == TEXT("resolve: lens is a lens body or its id"));
		TestTrue(TEXT("ResolveJson, no world"), !UOWLens::ResolveJson(nullptr, TEXT("{}"), LensReader(EOWLensRung::Owner), NoOptions, R, Reason)
			&& Reason == TEXT("no world"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLensFixtureTest, "OnlyWorlds.Lens.Fixture", OWTestFlags)

bool FOWLensFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/lens.fixture.json"));
	if (!TestTrue(TEXT("the lens fixture at ") + Path, OWTestPaths::ReadJson(Path, Fixture))) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	const OWJsonValue* Worlds = Fixture.Find("worlds");
	if (!TestTrue(TEXT("the fixture holds cases and worlds"), Cases && Cases->IsArray() && Worlds && Worlds->IsObject())) return false;

	int32 Carried = 0, Failed = 0, Others = 0;
	TArray<FString> Left;
	const FOWLensOptions NoOptions = FOWLensOptions();
	for (const OWJsonValue& Case : Cases->Items())
	{
		const std::string Kind = LensText(Case.Find("kind"));
		const FString Name = Str(Kind) + TEXT("/") + Str(LensText(Case.Find("name")));
		bool bHeld = true;
		auto Check = [&](bool bCond, const FString& What)
		{
			if (!TestTrue(Name + TEXT(": ") + What, bCond)) bHeld = false;
		};
		auto Same = [&](const OWJsonValue& Got, const OWJsonValue* Want, const FString& What)
		{
			Check(Want && LensEqual(Got, *Want), What + TEXT(": gave ") + Str(LensCompact(Got)) + TEXT(", the case says ")
				+ (Want ? Str(LensCompact(*Want)) : FString(TEXT("nothing"))));
		};
		auto NotCarried = [&](const FString& Why) { Left.Add(Name + TEXT(" (") + Why + TEXT(")")); };

		if (Kind == "shape")
		{
			// The pair and the template on an element the world holds, asked by id.
			OWJsonValue Body = Case.Find("element") ? *Case.Find("element") : OWJsonValue();
			if (!Body.IsObject())
			{
				NotCarried(TEXT("its element is not an object"));
				continue;
			}
			if (!Body.Find("id")) Body.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f001"));
			const FString Id = Str(LensText(Body.Find("id")));
			std::vector<std::pair<std::string, OWJsonValue>> Bodies;
			Bodies.emplace_back(LensFolderOf(Body), std::move(Body));
			const UOWWorld* World = LensMakeWorld(std::move(Bodies));
			Same(OWJsonValue::Bool(UOWLens::IsLens(World, Id)), Case.Find("is_lens"), TEXT("is_lens"));
			Same(OWJsonValue::Bool(UOWLens::IsTemplate(World, Id)), Case.Find("is_template"), TEXT("is_template"));
		}
		else if (Kind == "keys")
		{
			// x_lens on a lens body read by the owner over an empty world: the key refused is Unreadable.
			OWJsonValue Lens = LensParse(R"({"id": "019a3b10-0000-7000-8000-00000000f0a1", "type": "construct", "supertype": "Lens"})");
			if (const OWJsonValue* X = Case.Find("x_lens")) Lens.Set("x_lens", *X);
			const UOWWorld* World = LensMakeWorld({});
			FOWLensReading R;
			FString Reason;
			Check(UOWLens::ResolveJson(World, LensJsonText(Lens), LensReader(EOWLensRung::Owner), NoOptions, R, Reason),
				TEXT("answered, got the refusal ") + Reason);
			const OWJsonValue* Bad = Case.Find("bad");
			Check(R.Unreadable.Equals(Str(LensText(Bad)), ESearchCase::CaseSensitive), TEXT("the key refused, got ") + R.Unreadable);
			const OWJsonValue* Legal = Case.Find("legal");
			Check(Legal && Legal->IsBool() && Legal->AsBool() == R.Unreadable.IsEmpty(), TEXT("legal"));
		}
		else if (Kind == "uses")
		{
			// The membership as the Blueprint's choice (a list chose; null or another value no choice), x_lens_uses on a
			// Character of the world.
			const OWJsonValue* M = Case.Find("membership");
			const bool bChose = M && M->IsArray();
			TArray<FString> Membership;
			if (bChose)
			{
				for (const OWJsonValue& Id : M->Items())
				{
					if (Id.IsString()) Membership.Add(Str(Id.Text()));
				}
			}
			OWJsonValue Seat = LensParse(R"({"id": "019a3b10-0000-7000-8000-00000000f0c1", "type": "character", "name": "Seat"})");
			if (const OWJsonValue* U = Case.Find("x_lens_uses")) Seat.Set("x_lens_uses", *U);
			std::vector<std::pair<std::string, OWJsonValue>> Bodies;
			Bodies.emplace_back("character", std::move(Seat));
			const UOWWorld* World = LensMakeWorld(std::move(Bodies));
			Same(LensStrings(UOWLens::LensesFor(World, TEXT("019a3b10-0000-7000-8000-00000000f0c1"), bChose, Membership)),
				Case.Find("lenses"), TEXT("the lenses read"));
		}
		else if (Kind == "resolve" || Kind == "invariant")
		{
			const OWJsonValue* FixtureWorld = Worlds->Find(LensText(Case.Find("world")));
			if (!FixtureWorld)
			{
				Check(false, TEXT("a world the fixture names"));
				++Carried;
				++Failed;
				continue;
			}
			FString Why;
			const UOWWorld* World = LensPlace(*FixtureWorld, Why);
			if (!World)
			{
				NotCarried(Why);
				continue;
			}
			const OWJsonValue* Members = FixtureWorld->Find("members");
			FOWLensOptions Options;
			if (Members && !Members->IsNull()) Options.MembersJson = LensJsonText(*Members);
			if (const OWJsonValue* A = Case.Find("as_of"); A && A->IsNumber())
			{
				Options.bHasAsOf = true;
				Options.AsOf = static_cast<int64>(A->AsDouble());
			}
			Options.Clock = Str(LensText(Case.Find("clock")));
			const OWJsonValue* Lens = Case.Find("lens");
			if (!Lens || !(Lens->IsString() || Lens->IsObject()))
			{
				NotCarried(TEXT("its lens is neither an id nor a body"));
				continue;
			}
			auto ReadAs = [&](const std::string& ReaderName, FOWLensReading& R) -> bool
			{
				FOWLensReader Reader;
				FString Cannot;
				if (!LensReaderOf(LensReaderByName(*FixtureWorld, ReaderName), Reader, Cannot))
				{
					Check(false, Str(ReaderName) + TEXT(": a reader the struct holds (") + Cannot + TEXT(")"));
					return false;
				}
				FString Reason;
				const bool bOk = LensResolveCase(World, *Lens, Reader, Options, R, Reason);
				Check(bOk && Reason.IsEmpty(), Str(ReaderName) + TEXT(": answered, got the refusal ") + Reason);
				return bOk;
			};
			if (Kind == "resolve")
			{
				FOWLensReading R;
				ReadAs(LensText(Case.Find("reader")), R);
				const OWJsonValue GotJson = LensReadingJson(R);
				Check(R.Unreadable.IsEmpty(), TEXT("readable"));
				for (const char* Key : { "kept", "cut", "rows", "notes_ignored" })
				{
					if (const OWJsonValue* Want = Case.Find(Key)) Same(*GotJson.Find(Key), Want, Str(Key));
				}
				if (const OWJsonValue* Want = Case.Find("mail"))
				{
					const OWJsonValue* Mail = GotJson.Find("mail");
					if (Want->IsNull() || Mail->IsNull()) Same(*Mail, Want, TEXT("mail"));
					else
					{
						for (size_t I = 0; I < Want->Size(); ++I)
						{
							const OWJsonValue* Part = Mail->Find(Want->Keys()[I]);
							Same(Part ? *Part : OWJsonValue::Null(), &Want->Items()[I], TEXT("mail.") + Str(Want->Keys()[I]));
						}
					}
				}
			}
			else
			{
				const std::string Claim = LensText(Case.Find("claim"));
				const OWJsonValue* Readers = Case.Find("readers");
				if (!Readers || !Readers->IsArray() || Readers->Size() == 0)
				{
					Check(false, TEXT("its readers"));
				}
				else if (Claim == "subset")
				{
					// Every reader's kept is a subset of the owner's; no reader below the owner names a secret, by id
					// or by name, anywhere in its reading, nor carries the owner's counts.
					FOWLensReading Owner;
					ReadAs("owner", Owner);
					std::vector<std::string> Secrets;
					for (const OWJsonValue& S : Case.Find("secrets")->Items()) Secrets.push_back(S.Text());
					for (const OWJsonValue& E : FixtureWorld->Find("elements")->Items())
					{
						for (const OWJsonValue& S : Case.Find("secrets")->Items())
						{
							if (LensText(E.Find("id")) == S.Text()) Secrets.push_back(LensText(E.Find("name")));
						}
					}
					for (const OWJsonValue& ReaderName : Readers->Items())
					{
						FOWLensReading R;
						ReadAs(ReaderName.Text(), R);
						for (const FString& Id : R.Kept)
							Check(Owner.Kept.ContainsByPredicate([&Id](const FString& K) { return K.Equals(Id, ESearchCase::CaseSensitive); }),
								Str(ReaderName.Text()) + TEXT(" keeps ") + Id + TEXT(", which the owner does not"));
						if (LensText(LensReaderByName(*FixtureWorld, ReaderName.Text()).Find("rung")) == "owner") continue;
						const std::string ReadingText = OnlyWorlds::OWJson::Stringify(LensReadingJson(R), true);
						for (const std::string& S : Secrets)
							Check(ReadingText.find(S) == std::string::npos, Str(ReaderName.Text()) + TEXT("'s reading names the secret ") + Str(S));
						Check(Owner.Cut.Num() == 0 || LensCutText(Owner.Cut) != LensCutText(R.Cut), Str(ReaderName.Text()) + TEXT("'s counts are the owner's"));
					}
				}
				else if (Claim == "same_without")
				{
					// The reading is the same, byte for byte, in a world without the ids.
					std::set<std::string> Without;
					for (const OWJsonValue& S : Case.Find("without")->Items()) Without.insert(S.Text());
					OWJsonValue Less = *FixtureWorld;
					OWJsonValue Kept = OWJsonValue::Array();
					for (const OWJsonValue& E : FixtureWorld->Find("elements")->Items())
					{
						if (!Without.count(LensText(E.Find("id")))) Kept.Push(E);
					}
					Less.Set("elements", std::move(Kept));
					FString Why2;
					const UOWWorld* LessWorld = LensPlace(Less, Why2);
					for (const OWJsonValue& ReaderName : Readers->Items())
					{
						FOWLensReading A, B;
						ReadAs(ReaderName.Text(), A);
						FOWLensReader Reader;
						FString Cannot, Reason;
						LensReaderOf(LensReaderByName(*FixtureWorld, ReaderName.Text()), Reader, Cannot);
						LensResolveCase(LessWorld, *Lens, Reader, Options, B, Reason);
						Check(OnlyWorlds::OWJson::Stringify(LensReadingJson(A)) == OnlyWorlds::OWJson::Stringify(LensReadingJson(B)),
							Str(ReaderName.Text()) + TEXT(" reads the same without the ids"));
					}
				}
				else if (Claim == "unchanged")
				{
					// No reading moves the world: every body's text the same after every reader read.
					auto Bodies = [&]()
					{
						std::string Joined;
						for (const OnlyWorlds::OWElement& E : World->Native()->elements) Joined += OnlyWorlds::OWJson::Stringify(E.body, true);
						return Joined;
					};
					const std::string Before = Bodies();
					for (const OWJsonValue& ReaderName : Readers->Items())
					{
						FOWLensReading R;
						ReadAs(ReaderName.Text(), R);
					}
					Check(Bodies() == Before, TEXT("no reading moved the world"));
				}
				else
				{
					Check(false, TEXT("an invariant of a claim this test knows"));
				}
			}
		}
		else
		{
			// graph and degree: OWWorldGraphTests.cpp.
			++Others;
			continue;
		}
		++Carried;
		if (!bHeld) ++Failed;
	}
	// The fixture as vendored at dist.128: 6 shape, 25 keys, 5 uses, 134 resolve, 4 invariant, every case of these
	// kinds; the 56 graph and 5 degree cases are the graph's.
	TestEqual(TEXT("the 174 shape, keys, uses, resolve and invariant cases carried"), Carried, 174);
	TestEqual(TEXT("the graph's 61 left to OWWorldGraphTests.cpp"), Others, 61);
	TestTrue(TEXT("none left out: ") + FString::Join(Left, TEXT("; ")), Left.Num() == 0);
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("lens fixture: %d cases through UOWLens, %d failed"), Carried, Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLensReflectionTest, "OnlyWorlds.Lens.Reflection", OWTestFlags)

bool FOWLensReflectionTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = LensHandWorld();

	// GetLenses and the element calls, against the direct call.
	{
		FCall Call(*this, UOWLens::StaticClass(), TEXT("GetLenses"));
		Call.SetWorld(World);
		Call.Invoke();
		TestTrue(TEXT("GetLenses through the graph's path"), LensSame(LensCallStrings(*this, Call, nullptr), UOWLens::GetLenses(World)));
	}
	struct FLensIsCall
	{
		const TCHAR* Name;
		bool (*Direct)(const UOWWorld*, const FString&);
	};
	const FLensIsCall IsCalls[] = {
		{ TEXT("IsLens"), &UOWLens::IsLens }, { TEXT("IsTemplate"), &UOWLens::IsTemplate }, { TEXT("IsHidden"), &UOWLens::IsHidden },
		{ TEXT("IsProposed"), &UOWLens::IsProposed }, { TEXT("IsRoomElement"), &UOWLens::IsRoomElement },
		{ TEXT("IsReadingAid"), &UOWLens::IsReadingAid },
	};
	int32 Asked = 0, Held = 0;
	for (const FLensIsCall& Is : IsCalls)
	{
		for (const FString& Id : World->GetElementIds(TEXT("")))
		{
			++Asked;
			if (LensCallIs(*this, Is.Name, World, Id) == Is.Direct(World, Id)) ++Held;
			else AddError(FString::Printf(TEXT("%s %s: the graph's path and the direct call part"), Is.Name, *Id));
		}
	}
	TestTrue(TEXT("each element call on every element"), Asked == 6 * 13 && Held == Asked);
	TestTrue(TEXT("and on true ones"), LensCallIs(*this, TEXT("IsTemplate"), World, TEXT("k2")) && LensCallIs(*this, TEXT("IsHidden"), World, TEXT("c3")));

	{
		FCall Call(*this, UOWLens::StaticClass(), TEXT("LensesFor"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CharacterId"), TEXT("c1"));
		Call.SetBool(TEXT("bMembershipChose"), true);
		LensCallSetStrings(*this, Call, TEXT("Membership"), LensIds({ TEXT("k5"), TEXT("k5"), TEXT("k1") }));
		Call.Invoke();
		TestTrue(TEXT("LensesFor through the graph's path: [k5, k1]"), LensSame(LensCallStrings(*this, Call, nullptr), LensIds({ TEXT("k5"), TEXT("k1") })));
	}
	{
		FCall Call(*this, UOWLens::StaticClass(), TEXT("DateOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), TEXT("e1"));
		Call.Invoke();
		TestTrue(TEXT("DateOf e1 through the graph's path: 7"), Call.ReturnBool() && Call.Out<FInt64Property>(TEXT("Date")) == 7);
	}

	// Resolve and ResolveJson: the reader and the options made member by member, the reading broken back by property
	// and compared whole with the direct call's.
	auto CallResolve = [&](const TCHAR* Name, const TCHAR* LensParam, const FString& Lens, const FOWLensReader& Reader,
		const FOWLensOptions& Options)
	{
		FCall Call(*this, UOWLens::StaticClass(), Name);
		Call.SetWorld(World);
		Call.SetString(LensParam, Lens);
		if (void* R = Call.StructSlot(TEXT("Reader"), FOWLensReader::StaticStruct())) LensMakeReader(*this, R, Reader);
		if (void* O = Call.StructSlot(TEXT("Options"), FOWLensOptions::StaticStruct())) LensMakeOptions(*this, O, Options);
		Call.Invoke();
		FOWLensReading Direct;
		FString DirectReason;
		const bool bDirect = FString(Name) == TEXT("Resolve")
			? UOWLens::Resolve(World, Lens, Reader, Options, Direct, DirectReason)
			: UOWLens::ResolveJson(World, Lens, Reader, Options, Direct, DirectReason);
		const FString What = FString(Name) + TEXT(" ") + Lens;
		TestTrue(What + TEXT(": the bool"), Call.ReturnBool() == bDirect);
		TestEqual(What + TEXT(": the reason"), Call.Out<FStrProperty>(TEXT("Reason")), DirectReason);
		const UScriptStruct* S = FOWLensReading::StaticStruct();
		const void* Data = Call.OutStruct(TEXT("Reading"), S);
		if (!Data) return;
		TestTrue(What + TEXT(": Kept by property"), LensSame(LensStringsMember(*this, S, Data, TEXT("Kept")), Direct.Kept));
		TestTrue(What + TEXT(": KnownOf by property"), LensSame(LensStringsMember(*this, S, Data, TEXT("KnownOf")), Direct.KnownOf));
		TestTrue(What + TEXT(": Rows by property"), LensSame(LensStringsMember(*this, S, Data, TEXT("Rows")), Direct.Rows));
		TestEqual(What + TEXT(": Unreadable by property"), OWBlueprintTest::Member<FStrProperty>(*this, S, Data, TEXT("Unreadable")), Direct.Unreadable);
		TestTrue(What + TEXT(": NotesIgnored by property"), OWBlueprintTest::Member<FInt64Property>(*this, S, Data, TEXT("NotesIgnored")) == Direct.NotesIgnored);
		TestTrue(What + TEXT(": bHasMail by property"), OWBlueprintTest::BoolMember(*this, S, Data, TEXT("bHasMail")) == Direct.bHasMail);
		TestTrue(What + TEXT(": the reading whole, as the direct call's"), S->CompareScriptStruct(Data, &Direct, PPF_None));
	};
	const FOWLensOptions NoOptions = FOWLensOptions();
	FOWLensReader Guest = LensReader(EOWLensRung::Guest);
	Guest.Whole = LensIds({ TEXT("c1"), TEXT("k1"), TEXT("e1") });
	Guest.KnownOf = LensIds({ TEXT("c2"), TEXT("l1") });
	Guest.Character = TEXT("c1");
	FOWLensOptions Dated;
	Dated.bHasAsOf = true;
	Dated.AsOf = 50;
	Dated.Clock = TEXT("2026-10-27T12:00:00Z");
	Dated.MembersJson = TEXT("[]");
	CallResolve(TEXT("Resolve"), TEXT("LensId"), TEXT("k1"), LensReader(EOWLensRung::Owner), NoOptions);
	CallResolve(TEXT("Resolve"), TEXT("LensId"), TEXT("k1"), LensReader(EOWLensRung::Contributor, true), Dated);
	CallResolve(TEXT("Resolve"), TEXT("LensId"), TEXT("k1"), Guest, NoOptions);
	CallResolve(TEXT("Resolve"), TEXT("LensId"), TEXT("k3"), LensReader(EOWLensRung::Owner), NoOptions);
	CallResolve(TEXT("ResolveJson"), TEXT("LensJson"), TEXT("{\"type\": \"construct\", \"supertype\": \"Lens\", \"events\": [\"e1\"]}"),
		LensReader(EOWLensRung::Public), NoOptions);
	FOWLensOptions BadClock;
	BadClock.Clock = TEXT("soon");
	CallResolve(TEXT("Resolve"), TEXT("LensId"), TEXT("k1"), LensReader(EOWLensRung::Owner), BadClock);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
