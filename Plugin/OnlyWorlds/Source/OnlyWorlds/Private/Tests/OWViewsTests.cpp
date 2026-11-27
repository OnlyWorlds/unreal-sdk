// How an element is shown, through UOWViews (OnlyWorlds.Views.*): every case of views.json's own fixture
// (codegen/schema-dist/views.fixture.json) and the core suite's vectors beside it, each through the node and held to
// the core's own answer on the same input, never to a literal copied from the fixture; the table node against the
// core's table; the section fields of every type; the door's own words; and every element of Sikelia through
// ViewOfElement against the core. Each node is called as a Blueprint calls it (OWBlueprintCall.h).
//
// The carry rule. ViewOf reads the baked table, so a case on a table of the fixture's own (its `table`) is not the
// node's: it is named, counted and left out by that rule. Every other case's element is first written as JSON text by
// this file and read back, and when the core answers that text otherwise than the element, the case is not carried
// either. Every other case is the node's. No count is pinned: carried plus left out must be the fixture's whole count,
// and at least one must be carried.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLens.h"
#include "OWLensDoor.h"
#include "OWOpenersLibrary.h"
#include "OWViews.h"
#include "OWViewsLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <string>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWBlueprintTest::FCall;
	using OWConvert::Str;
	using OWConvert::Utf8;
	namespace ViCore = OnlyWorlds::OWViews;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue ViParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string ViBytes(const OWJsonValue& V) { return OnlyWorlds::OWJson::Stringify(V, true); }

	std::string ViText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	OWJsonValue ViStrings(const TArray<FString>& Xs)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& X : Xs) Out.Push(OWJsonValue::String(Utf8(X)));
		return Out;
	}

	OWJsonValue ViStrings(const std::vector<std::string>& Xs)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const std::string& X : Xs) Out.Push(OWJsonValue::String(X));
		return Out;
	}

	// A row in one shape for both sides: every member, "" for none.
	OWJsonValue ViRowJson(const FOWViewRow& R)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("id", OWJsonValue::String(Utf8(R.Id)));
		Out.Set("kind", ViStrings(TArray<FString>{ R.Type, R.Supertype, R.Profile, R.Pair }));
		Out.Set("from", OWJsonValue::String(Utf8(R.From)));
		Out.Set("sentence", ViStrings(R.Sentence));
		Out.Set("lead", ViStrings(R.Lead));
		OWJsonValue Folds = OWJsonValue::Array();
		for (const FOWViewFold& F : R.Fold) Folds.Push(ViStrings(TArray<FString>{ F.Field, F.Label }));
		Out.Set("fold", std::move(Folds));
		Out.Set("layout", ViStrings(TArray<FString>{ R.Layout, R.LayoutBy }));
		Out.Set("banner", ViStrings(R.Banner));
		return Out;
	}

	OWJsonValue ViRowJson(const OnlyWorlds::OWViewRow& R)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("id", OWJsonValue::String(R.id));
		Out.Set("kind", ViStrings(std::vector<std::string>{ R.kind.type, R.kind.supertype, R.kind.profile, R.kind.pair }));
		Out.Set("from", OWJsonValue::String(R.from));
		Out.Set("sentence", ViStrings(R.sentence));
		Out.Set("lead", ViStrings(R.lead));
		OWJsonValue Folds = OWJsonValue::Array();
		for (const OnlyWorlds::OWViewFold& F : R.fold) Folds.Push(ViStrings(std::vector<std::string>{ F.field, F.label }));
		Out.Set("fold", std::move(Folds));
		Out.Set("layout", ViStrings(std::vector<std::string>{ R.layout.name, R.layout.by }));
		Out.Set("banner", ViStrings(R.banner));
		return Out;
	}

	// The node's answer and the core's in one shape: {row (the whole row, or null), sentence (null for none), banners, order}.
	OWJsonValue ViViewJson(const FOWView& V)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("row", V.bHasRow ? ViRowJson(V.Row) : OWJsonValue::Null());
		Out.Set("sentence", V.Sentence.IsEmpty() ? OWJsonValue::Null() : OWJsonValue::String(Utf8(V.Sentence)));
		Out.Set("banners", ViStrings(V.Banners));
		Out.Set("order", ViStrings(V.Order));
		return Out;
	}

	OWJsonValue ViViewJson(const OnlyWorlds::OWView& V)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("row", V.row ? ViRowJson(*V.row) : OWJsonValue::Null());
		Out.Set("sentence", V.sentence ? OWJsonValue::String(*V.sentence) : OWJsonValue::Null());
		Out.Set("banners", ViStrings(V.banners));
		Out.Set("order", ViStrings(V.order));
		return Out;
	}

	// ViewOf as a graph calls it.
	bool ViCallViewOf(FAutomationTestBase& Test, const FString& Text, FOWView& View, FString& Reason)
	{
		FCall Call(Test, UOWViews::StaticClass(), TEXT("ViewOf"));
		Call.SetString(TEXT("ElementJson"), Text);
		Call.Invoke();
		View = Call.OutAs<FOWView>(TEXT("View"), TEXT("FOWView"));
		Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		return Call.ReturnBool();
	}

	template <typename T>
	T ViReturn(FCall& Call)
	{
		const FProperty* Ret = Call.Return();
		const T* V = Ret && Call.GetFrame() ? Ret->ContainerPtrToValuePtr<T>(Call.GetFrame()) : nullptr;
		return V ? *V : T();
	}

	// One element's text through the node, held to the core's answer on the same text read back.
	bool ViHeld(FAutomationTestBase& Test, const FString& What, const FString& Text)
	{
		FOWView Got;
		FString Reason;
		if (!Test.TestTrue(What + TEXT(": the node reads it: ") + Reason, ViCallViewOf(Test, Text, Got, Reason))) return false;
		const std::string Want = ViBytes(ViViewJson(ViCore::ViewOf(ViParse(Utf8(Text)))));
		const std::string Have = ViBytes(ViViewJson(Got));
		return Test.TestEqual(What + TEXT(": the core's answer"), Str(Have), Str(Want));
	}

	bool ViReadFixture(FAutomationTestBase& Test, OWJsonValue& Out)
	{
		const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/views.fixture.json"));
		return Test.TestTrue(TEXT("the views fixture reads: ") + Path, OWTestPaths::ReadJson(Path, Out));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsCasesTest, "OnlyWorlds.Views.Cases", OWTestFlags)

bool FOWViewsCasesTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	if (!ViReadFixture(*this, Fixture)) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray() && Cases->Size() > 0)) return false;
	int32 Carried = 0, Failed = 0;
	TArray<FString> NotCarried;
	for (const OWJsonValue& C : Cases->Items())
	{
		const FString Name = Str(ViText(C.Find("name")));
		const OWJsonValue* Element = C.Find("element");
		if (C.Find("table"))
		{
			NotCarried.Add(Name + TEXT(" (a table of the fixture's own)"));
			continue;
		}
		if (!Element)
		{
			NotCarried.Add(Name + TEXT(" (no element)"));
			continue;
		}
		const FString Text = Str(ViBytes(*Element));
		if (ViBytes(ViViewJson(ViCore::ViewOf(ViParse(Utf8(Text))))) != ViBytes(ViViewJson(ViCore::ViewOf(*Element))))
		{
			NotCarried.Add(Name + TEXT(" (its element does not read back from its text)"));
			continue;
		}
		++Carried;
		if (!ViHeld(*this, Name, Text)) ++Failed;
	}
	AddInfo(FString::Printf(TEXT("views cases: %d carried, %d not (%s), %d failed"), Carried, NotCarried.Num(),
		*FString::Join(NotCarried, TEXT("; ")), Failed));
	TestEqual(TEXT("carried plus not carried is every case"), Carried + NotCarried.Num(), static_cast<int32>(Cases->Size()));
	TestTrue(TEXT("at least one case carried"), Carried > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsVectorsTest, "OnlyWorlds.Views.Vectors", OWTestFlags)

bool FOWViewsVectorsTest::RunTest(const FString& Parameters)
{
	// The core suite's vectors beside the fixture (tests/test_views.cpp): the Pin's generic link, what holds
	// something, the served-trimmed banner, the lone carriage return, a row's own banner on the room line.
	const TCHAR* Texts[] = {
		TEXT(R"({"type": "pin", "name": "The ferry", "map": "m1", "element_type": "location", "element_id": "l1", "x": 0, "y": 12, "z": 3})"),
		TEXT(R"({"type": "pin", "map": "m1", "element_type": " location ", "element_id": " l1", "x": 0})"),
		TEXT(R"({"type": "pin", "map": "m1", "element_type": "location", "element_id": "　", "x": 0})"),
		TEXT(R"({"type": "pin", "map": "m1", "element_type": null, "element_id": "l1"})"),
		TEXT(R"({"type": "pin", "map": "m1", "element_type": "location", "element_id": 7})"),
		TEXT(R"({"type": "Pin", "map": "m1", "element": "l1", "x": 0})"),
		TEXT(R"({"type": " Character ", "physicality": "　 ", "mentality": [" "], "height": 0, "weight": false, "species": [], "background": {}, "motivations": {"a": 1}, "birthplace": null, "x_note": "x", "name": "Sol"})"),
		TEXT(R"({"type": "narrative", "supertype": "Message", "story": "Hi.", "x_served_trimmed": [" ", "characters"]})"),
		TEXT(R"({"type": "narrative", "supertype": "Message", "story": "Hi.", "x_served_trimmed": [" \t", "　"]})"),
		TEXT(R"({"type": "narrative", "supertype": "Message", "story": "Hi.", "x_served_trimmed": "characters"})"),
		TEXT(R"({"type": "narrative", "supertype": "Message", "story": "One.\rTwo.", "hidden": true})"),
		TEXT(R"({"type": "narrative", "supertype": " message ", "description": "  ", "story": "\r\n One. \rTwo.", "known_of": true})"),
		TEXT(R"({"type": "narrative", "supertype": "Message", "story": "Hi.", "x_room_of": 7, "x_review_proposed": {"by": "x"}})"),
		TEXT(R"({"type": "narrative", "supertype": "Report", "x_room_of": "w1", "description": "A.\nB.", "story": "S", "consequences": "C"})"),
		TEXT(R"({"type": "law", "supertype": "Charter", "x_charter_of": "w1", "description": "The rules.", "declaration": "D"})"),
	};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Texts); ++I) ViHeld(*this, FString::Printf(TEXT("vector %d"), I), Texts[I]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsTableTest, "OnlyWorlds.Views.Table", OWTestFlags)

bool FOWViewsTableTest::RunTest(const FString& Parameters)
{
	FCall Call(*this, UOWViews::StaticClass(), TEXT("GetViewsTable"));
	Call.Invoke();
	const FOWViewsTable Got = ViReturn<FOWViewsTable>(Call);
	TestEqual(TEXT("the version, the core's"), Got.Version, static_cast<int32>(ViCore::Version));
	TestEqual(TEXT("the sha256, the core's"), Got.Sha256, Str(ViCore::Sha256));
	const auto& Banners = ViCore::Banners();
	if (TestEqual(TEXT("a banner for each of the core's"), Got.Banners.Num(), static_cast<int32>(Banners.size())))
		for (int32 I = 0; I < Got.Banners.Num(); ++I)
		{
			TestEqual(TEXT("banner mark"), Got.Banners[I].Mark, Str(Banners[I].mark));
			TestEqual(TEXT("banner defined_by"), Got.Banners[I].DefinedBy, Str(Banners[I].defined_by));
		}
	const auto& Layouts = ViCore::Layouts();
	if (TestEqual(TEXT("a layout for each of the core's"), Got.Layouts.Num(), static_cast<int32>(Layouts.size())))
		for (int32 I = 0; I < Got.Layouts.Num(); ++I)
		{
			TestEqual(TEXT("layout name"), Got.Layouts[I].Name, Str(Layouts[I].first));
			TestEqual(TEXT("layout sentence"), Got.Layouts[I].Sentence, Str(Layouts[I].second));
		}
	const auto& Rows = ViCore::Rows();
	TestTrue(TEXT("the core holds rows"), !Rows.empty());
	if (TestEqual(TEXT("a row for each of the core's"), Got.Rows.Num(), static_cast<int32>(Rows.size())))
		for (int32 I = 0; I < Got.Rows.Num(); ++I)
			TestEqual(TEXT("row ") + Str(Rows[I].id), Str(ViBytes(ViRowJson(Got.Rows[I]))), Str(ViBytes(ViRowJson(Rows[I]))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsSectionFieldsTest, "OnlyWorlds.Views.SectionFields", OWTestFlags)

bool FOWViewsSectionFieldsTest::RunTest(const FString& Parameters)
{
	std::vector<std::string> Words = OnlyWorlds::OWLens::TypeOrder();
	TestTrue(TEXT("the schema's types"), !Words.empty());
	Words.insert(Words.end(), { " Character ", "PIN", "people", "" });
	for (const std::string& Word : Words)
	{
		FCall Call(*this, UOWViews::StaticClass(), TEXT("SectionFieldsOf"));
		Call.SetString(TEXT("Type"), Str(Word));
		Call.Invoke();
		TestEqual(TEXT("SectionFieldsOf \"") + Str(Word) + TEXT("\": the core's"), Str(ViBytes(ViStrings(ViReturn<TArray<FString>>(Call)))),
			Str(ViBytes(ViStrings(ViCore::SectionFields(Word)))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsDoorTest, "OnlyWorlds.Views.Door", OWTestFlags)

bool FOWViewsDoorTest::RunTest(const FString& Parameters)
{
	FOWView View;
	FString Reason;
	TestFalse(TEXT("text that does not parse is refused"), ViCallViewOf(*this, TEXT("{\"type\": "), View, Reason));
	TestTrue(TEXT("its reason names the element: ") + Reason, Reason.StartsWith(TEXT("element: ")));
	TestFalse(TEXT("and the view is empty"), View.bHasRow || View.Banners.Num() || View.Order.Num());
	for (const TCHAR* NoElement : { TEXT(""), TEXT("7"), TEXT("\"narrative\""), TEXT("[]"), TEXT("null") })
	{
		Reason = TEXT("unset");
		TestTrue(FString(TEXT("no element answers: ")) + NoElement, ViCallViewOf(*this, NoElement, View, Reason));
		TestEqual(FString(TEXT("no reason: ")) + NoElement, Reason, FString());
		TestEqual(FString(TEXT("the empty view, the core's: ")) + NoElement, Str(ViBytes(ViViewJson(View))),
			Str(ViBytes(ViViewJson(OnlyWorlds::OWView()))));
	}
	// ViewOfElement with no world, and an id an opened world does not hold.
	{
		FCall Call(*this, UOWViews::StaticClass(), TEXT("ViewOfElement"));
		Call.SetWorld(nullptr);
		Call.SetString(TEXT("ElementId"), TEXT("e1"));
		Call.Invoke();
		TestEqual(TEXT("no world: the empty view"), Str(ViBytes(ViViewJson(ViReturn<FOWView>(Call)))),
			Str(ViBytes(ViViewJson(OnlyWorlds::OWView()))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWViewsSikeliaTest, "OnlyWorlds.Views.Sikelia", OWTestFlags)

bool FOWViewsSikeliaTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_SIKELIA"))) return true;
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	int32 Read = 0, WithRow = 0, Failed = 0;
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		const OWJsonValue Served = UOWOpeners::ServedElement(E);
		const FString Id = Str(ViText(Served.IsObject() ? Served.Find("id") : nullptr));
		if (Id.IsEmpty()) continue;
		FCall Call(*this, UOWViews::StaticClass(), TEXT("ViewOfElement"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), Id);
		Call.Invoke();
		const FOWView Got = ViReturn<FOWView>(Call);
		const OnlyWorlds::OWView Want = ViCore::ViewOf(OWLensDoor::ServedById(World, Id));
		++Read;
		WithRow += Want.row != nullptr;
		if (ViBytes(ViViewJson(Got)) != ViBytes(ViViewJson(Want)))
		{
			++Failed;
			AddError(TEXT("Sikelia ") + Id + TEXT(": the node gave ") + Str(ViBytes(ViViewJson(Got))) + TEXT(", the core ")
				+ Str(ViBytes(ViViewJson(Want))));
		}
	}
	TestTrue(TEXT("Sikelia holds elements"), Read > 0);
	{
		FCall Call(*this, UOWViews::StaticClass(), TEXT("ViewOfElement"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), TEXT("no-such-element"));
		Call.Invoke();
		TestEqual(TEXT("an id the world does not hold: the empty view"), Str(ViBytes(ViViewJson(ViReturn<FOWView>(Call)))),
			Str(ViBytes(ViViewJson(OnlyWorlds::OWView()))));
	}
	AddInfo(FString::Printf(TEXT("Sikelia's views: %d elements, %d with a row, %d off the core"), Read, WithRow, Failed));
	return true;
}

#endif
