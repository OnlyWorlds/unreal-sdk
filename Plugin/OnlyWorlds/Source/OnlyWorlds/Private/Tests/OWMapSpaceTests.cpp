// The map-space profile through UOWMapSpace (OnlyWorlds.MapSpace.*): every case of the profile's fixture
// (codegen/schema-dist/profiles/map-space.fixture.json: its cases, layout_cases and frame_cases) and the packages'
// shared pins-known-to cases (tests/data/pins-known-to.cases.json), each through the node and held to the core's own
// answer on the same input, never to a literal copied from a fixture; the door's own words; each node called as a
// Blueprint calls it (OWBlueprintCall.h); then Sikelia's map through ServedElements against the core's.
//
// The carry rule. A node takes the elements as JSON texts, a map by its id and a point as an FVector2D: a case the
// shape cannot hold (a Map named by its position, a point that is not two numbers, a list that is no list, a text that
// does not read back as its row) is not the node's to answer. Such a case is named, counted and left out by a rule
// read from the case; and every case's input is first written in the node's shape by this file's own code and read
// back, and when the core answers that input otherwise than the fixture's, the case is not carried either. Every other
// case is the node's, and its answer must be the core's. No count is pinned: every suite's cases are counted from the
// fixture, carried plus not carried must be all of them, and at least one must be carried.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensDoor.h"
#include "OWLensLibrary.h"
#include "OWMapSpace.h"
#include "OWMapSpaceLibrary.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
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
	namespace MsCore = OnlyWorlds::OWMapSpace;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue MsParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string MsText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	std::string MsBytes(const OWJsonValue& V) { return OnlyWorlds::OWJson::Stringify(V, true); }

	uint64_t MsBits(double D)
	{
		uint64_t B = 0;
		std::memcpy(&B, &D, sizeof B);
		return B;
	}

	// Equal as JSON values: an object's keys in the same order, a number by its value.
	bool MsEqual(const OWJsonValue& A, const OWJsonValue& B)
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
		case OWJsonValue::Kind::Object:
			if (A.Size() != B.Size() || A.Keys() != B.Keys()) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				if (!MsEqual(A.Items()[I], B.Items()[I])) return false;
			}
			return true;
		}
		return false;
	}

	// The rows as the node takes them: each one's JSON text, its numbers as spelled.
	TArray<FString> MsTexts(const OWJsonValue& Rows)
	{
		TArray<FString> Out;
		for (const OWJsonValue& R : Rows.Items()) Out.Add(Str(MsBytes(R)));
		return Out;
	}

	// The texts read back as rows by this file (a text that does not read is a null row: the case is then not carried).
	OWJsonValue MsBack(const TArray<FString>& Texts)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& T : Texts) Out.Push(MsParse(Utf8(T)));
		return Out;
	}

	OWJsonValue MsTextOrNull(const FString& S) { return S.IsEmpty() ? OWJsonValue::Null() : OWJsonValue::String(Utf8(S)); }

	OWJsonValue MsInt(int64 N) { return OWJsonValue::Number(static_cast<int64_t>(N)); }

	OWJsonValue MsCounts(const TArray<FOWLensCut>& Cut)
	{
		OWJsonValue Out = OWJsonValue::Object();
		for (const FOWLensCut& C : Cut) Out.Set(Utf8(C.Reason), MsInt(C.Count));
		return Out;
	}

	// The node's answers back in the core's ToJson shapes (OWMapSpace.cpp), written here from the structs alone.
	OWJsonValue MsMapSpaceJson(const FOWWorldMapSpace& M)
	{
		OWJsonValue Out = OWJsonValue::Object();
		OWJsonValue Maps = OWJsonValue::Array();
		for (const FOWMapSpaceMap& R : M.Maps)
		{
			OWJsonValue Row = OWJsonValue::Object();
			Row.Set("id", OWJsonValue::String(Utf8(R.Id)));
			Row.Set("width", R.bHasWidth ? MsInt(R.Width) : OWJsonValue::Null());
			Row.Set("height", R.bHasHeight ? MsInt(R.Height) : OWJsonValue::Null());
			Row.Set("depth", R.bHasDepth ? MsInt(R.Depth) : OWJsonValue::Null());
			Row.Set("z_max", R.bHasZMax ? MsInt(R.ZMax) : OWJsonValue::Null());
			Row.Set("scale", R.bHasScale ? OWJsonValue::Number(R.Scale) : OWJsonValue::Null());
			Row.Set("location", MsTextOrNull(R.Location));
			Row.Set("parent_map", MsTextOrNull(R.ParentMap));
			if (R.bMeasured) Row.Set("extent", OWJsonValue::String("points"));
			Maps.Push(std::move(Row));
		}
		Out.Set("maps", std::move(Maps));
		OWJsonValue Zones = OWJsonValue::Array();
		for (const FOWMapSpaceOutline& O : M.Zones)
		{
			OWJsonValue Row = OWJsonValue::Object();
			Row.Set("zone", OWJsonValue::String(Utf8(O.Zone)));
			Row.Set("map", OWJsonValue::String(Utf8(O.Map)));
			Row.Set("shape", OWJsonValue::String(Utf8(O.Shape)));
			OWJsonValue Points = OWJsonValue::Array();
			for (const FOWMapSpacePoint& P : O.Points)
			{
				OWJsonValue At = OWJsonValue::Array();
				At.Push(MsInt(P.X));
				At.Push(MsInt(P.Y));
				At.Push(MsInt(P.Z));
				Points.Push(std::move(At));
			}
			Row.Set("points", std::move(Points));
			Zones.Push(std::move(Row));
		}
		Out.Set("zones", std::move(Zones));
		OWJsonValue Pins = OWJsonValue::Array();
		for (const FOWMapSpacePin& P : M.Pins)
		{
			OWJsonValue Row = OWJsonValue::Object();
			Row.Set("pin", OWJsonValue::String(Utf8(P.Pin)));
			Row.Set("map", OWJsonValue::String(Utf8(P.Map)));
			Row.Set("element_type", P.bHasElement ? OWJsonValue::String(Utf8(P.ElementType)) : OWJsonValue::Null());
			Row.Set("element_id", P.bHasElement ? OWJsonValue::String(Utf8(P.ElementId)) : OWJsonValue::Null());
			Row.Set("x", MsInt(P.X));
			Row.Set("y", MsInt(P.Y));
			Row.Set("z", MsInt(P.Z));
			Pins.Push(std::move(Row));
		}
		Out.Set("pins", std::move(Pins));
		Out.Set("out", MsCounts(M.Out));
		return Out;
	}

	OWJsonValue MsLayoutJson(const FOWMapLayout& L)
	{
		OWJsonValue Out = OWJsonValue::Object();
		Out.Set("layout", L.bHasLayout ? OWJsonValue::String(Utf8(L.Layout)) : OWJsonValue::Null());
		Out.Set("ground", MsTextOrNull(L.Ground));
		OWJsonValue Hand = OWJsonValue::Array();
		for (const FString& H : L.Hand) Hand.Push(OWJsonValue::String(Utf8(H)));
		Out.Set("hand", std::move(Hand));
		return Out;
	}

	OWJsonValue MsKnownJson(const FOWPinsKnownTo& K)
	{
		OWJsonValue Out = OWJsonValue::Object();
		OWJsonValue Rows = OWJsonValue::Array();
		for (const FOWPinKnown& P : K.Known)
		{
			OWJsonValue Row = OWJsonValue::Object();
			Row.Set("pin", OWJsonValue::String(Utf8(P.Pin)));
			Row.Set("map", MsTextOrNull(P.Map));
			Row.Set("element_type", OWJsonValue::String(Utf8(P.ElementType)));
			Row.Set("element_id", OWJsonValue::String(Utf8(P.ElementId)));
			Row.Set("how", OWJsonValue::String(Utf8(P.How)));
			Rows.Push(std::move(Row));
		}
		Out.Set("known", std::move(Rows));
		Out.Set("unknown", MsInt(K.Unknown));
		Out.Set("out", MsCounts(K.Out));
		return Out;
	}

	// A carry held to the core's, every double by its bits. "" when the same, else what differs.
	FString MsCarryDiff(const FOWMapCarry& Got, const OnlyWorlds::OWMapCarry& Want)
	{
		if (Got.bHasParentFrame != Want.parent_frame.has_value()) return TEXT("parent_frame there or not");
		if (Want.parent_frame)
		{
			const std::array<double, 4>& F = *Want.parent_frame;
			if (MsBits(Got.ParentFrameMin.X) != MsBits(F[0]) || MsBits(Got.ParentFrameMin.Y) != MsBits(F[1])
				|| MsBits(Got.ParentFrameMax.X) != MsBits(F[2]) || MsBits(Got.ParentFrameMax.Y) != MsBits(F[3]))
				return TEXT("parent_frame's numbers");
		}
		if (Utf8(Got.Lint) != Want.lint.value_or(std::string())) return TEXT("lint");
		if (Utf8(Got.Stop) != Want.stop.value_or(std::string())) return TEXT("stop");
		if (Got.Points.Num() != static_cast<int32>(Want.points.size())) return TEXT("how many points");
		for (int32 I = 0; I < Got.Points.Num(); ++I)
		{
			const OnlyWorlds::OWMapCarryPoint& W = Want.points[I];
			if (Utf8(Got.Points[I].Map) != W.map || MsBits(Got.Points[I].Point.X) != MsBits(W.x) || MsBits(Got.Points[I].Point.Y) != MsBits(W.y))
				return FString::Printf(TEXT("point %d"), I);
		}
		return FString();
	}

	bool MsReadFixture(FAutomationTestBase& Test, const TCHAR* Rel, OWJsonValue& Out)
	{
		const FString Path = OWTestPaths::InRepo(Rel);
		return Test.TestTrue(TEXT("the fixture at ") + Path, OWTestPaths::ReadJson(Path, Out));
	}

	// One suite's tally: carried, not carried (named), failed.
	struct FMsTally
	{
		int32 Carried = 0;
		int32 Failed = 0;
		TArray<FString> NotCarried;

		void Close(FAutomationTestBase& Test, const TCHAR* Suite, size_t InFixture)
		{
			Test.TestEqual(FString(Suite) + TEXT(": carried and not carried are every case of the fixture"),
				Carried + NotCarried.Num(), static_cast<int32>(InFixture));
			Test.TestTrue(FString(Suite) + TEXT(": at least one case carried"), Carried > 0);
			Test.TestEqual(FString(Suite) + TEXT(": cases failed"), Failed, 0);
			Test.AddInfo(FString::Printf(TEXT("%s: %d of %d cases through the node, %d failed; not carried: %s"), Suite, Carried,
				static_cast<int32>(InFixture), Failed, NotCarried.Num() ? *FString::Join(NotCarried, TEXT("; ")) : TEXT("none")));
		}
	};

	// The one Map row of a case with that id (as the core's own test finds it: the last row whose type is exactly map).
	const OWJsonValue* MsMapRow(const OWJsonValue& Elements, const std::string& Id)
	{
		const OWJsonValue* Found = nullptr;
		for (const OWJsonValue& E : Elements.Items())
		{
			if (MsText(E.Find("type")) == "map" && !Id.empty() && MsText(E.Find("id")) == Id) Found = &E;
		}
		return Found;
	}

	OWJsonValue MsOfType(const OWJsonValue& Elements, const char* Type)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const OWJsonValue& E : Elements.Items())
		{
			if (MsText(E.Find("type")) == Type) Out.Push(E);
		}
		return Out;
	}

	UOWWorld* MsMakeWorld(std::initializer_list<std::pair<const char*, const char*>> Bodies)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f001"));
		for (const std::pair<const char*, const char*>& B : Bodies) Native->Add(B.first, MsParse(B.second));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}

	// A small world with every kind of row the readings read: two Maps, a child framed on its parent, a layout map, a
	// Zone outlined by two Markers, Pins (one with no point), and an element the Pins place.
	UOWWorld* MsHandWorld()
	{
		return MsMakeWorld({
			{ "map", R"({"id": "m1", "type": "map", "name": "Land", "width": 800, "height": 600, "x_metres_per_pixel": 2.5})" },
			{ "map", R"({"id": "m2", "type": "map", "name": "Town", "width": 200, "height": 100, "parent_map": "m1", "x_parent_frame": [100, 50, 300, 150]})" },
			{ "map", R"({"id": "m3", "type": "map", "name": "Board", "x_layout": {"method": "hand"}})" },
			{ "zone", R"({"id": "z1", "type": "zone", "name": "Wood", "shape": "line"})" },
			{ "marker", R"({"id": "k1", "type": "marker", "zone": "z1", "map": "m1", "x": 10, "y": 20, "order": 1})" },
			{ "marker", R"({"id": "k2", "type": "marker", "zone": "z1", "map": "m1", "x": 30, "y": 40, "z": 5, "order": 0})" },
			{ "pin", R"({"id": "p1", "type": "pin", "map": "m1", "element_type": "location", "element_id": "l1", "x": 400, "y": 300})" },
			{ "pin", R"({"id": "p2", "type": "pin", "map": "m3", "element_type": "location", "element_id": "l1", "x": 1, "y": 2})" },
			{ "pin", R"({"id": "p3", "type": "pin", "map": "m1"})" },
			{ "location", R"({"id": "l1", "type": "location", "name": "Keep"})" },
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMapSpaceFixtureTest, "OnlyWorlds.MapSpace.Fixture", OWTestFlags)

bool FOWMapSpaceFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	if (!MsReadFixture(*this, TEXT("codegen/schema-dist/profiles/map-space.fixture.json"), Fixture)) return false;

	// The cases: MapSpaceOf over the elements.
	{
		const OWJsonValue* Cases = Fixture.Find("cases");
		if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;
		FMsTally T;
		for (const OWJsonValue& C : Cases->Items())
		{
			const FString Name = Str(MsText(C.Find("name")));
			const OWJsonValue* Elements = C.Find("elements");
			if (!Elements || !Elements->IsArray())
			{
				T.NotCarried.Add(Name + TEXT(" (elements is no list)"));
				continue;
			}
			const TArray<FString> Texts = MsTexts(*Elements);
			const OnlyWorlds::OWWorldMapSpace Want = MsCore::MapSpaceOf(*Elements);
			if (MsBytes(MsCore::MapSpaceOf(MsBack(Texts)).ToJson()) != MsBytes(Want.ToJson()))
			{
				T.NotCarried.Add(Name + TEXT(" (its rows do not read back from their texts)"));
				continue;
			}
			++T.Carried;
			FOWWorldMapSpace Got;
			FString Reason;
			const bool bOk = UOWMapSpace::MapSpaceOf(Texts, Got, Reason);
			bool bHeld = bOk == Want.error.empty();
			if (bHeld && bOk) bHeld = MsEqual(MsMapSpaceJson(Got), Want.ToJson());
			if (bHeld && !bOk) bHeld = Reason == Str(Want.error);
			if (!TestTrue(Name + TEXT(": the core's answer; the node gave ") + (bOk ? Str(MsBytes(MsMapSpaceJson(Got))) : Reason)
				+ TEXT(", the core ") + Str(MsBytes(Want.ToJson())), bHeld))
				++T.Failed;
		}
		T.Close(*this, TEXT("cases"), Cases->Size());
	}

	// The layout cases: LayoutOf on the Map the case names by its id.
	{
		const OWJsonValue* Cases = Fixture.Find("layout_cases");
		if (!TestTrue(TEXT("the fixture holds layout_cases"), Cases && Cases->IsArray())) return false;
		FMsTally T;
		for (const OWJsonValue& C : Cases->Items())
		{
			const FString Name = Str(MsText(C.Find("name")));
			const OWJsonValue* Elements = C.Find("elements");
			const OWJsonValue* MapId = C.Find("map");
			if (!Elements || !Elements->IsArray() || !MapId || !MapId->IsString())
			{
				T.NotCarried.Add(Name + TEXT(" (names its Map by position, not by id)"));
				continue;
			}
			const OWJsonValue* Row = MsMapRow(*Elements, MapId->Text());
			if (!Row)
			{
				T.NotCarried.Add(Name + TEXT(" (no Map row with its id)"));
				continue;
			}
			const OnlyWorlds::OWMapLayout Want = MsCore::LayoutOf(*Row, MsOfType(*Elements, "map"), MsOfType(*Elements, "pin"));
			const TArray<FString> Texts = MsTexts(*Elements);
			const OWJsonValue Back = MsBack(Texts);
			const OWJsonValue* BackRow = MsMapRow(Back, MapId->Text());
			if (!BackRow || MsBytes(MsCore::LayoutOf(*BackRow, MsOfType(Back, "map"), MsOfType(Back, "pin")).ToJson()) != MsBytes(Want.ToJson()))
			{
				T.NotCarried.Add(Name + TEXT(" (its rows do not read back from their texts)"));
				continue;
			}
			++T.Carried;
			FOWMapLayout Got;
			FString Reason;
			const bool bOk = UOWMapSpace::LayoutOf(Texts, Str(MapId->Text()), Got, Reason);
			bool bHeld = bOk == Want.error.empty();
			if (bHeld && bOk) bHeld = MsBytes(MsLayoutJson(Got)) == MsBytes(Want.ToJson());
			if (bHeld && !bOk) bHeld = Reason == Str(Want.error);
			if (!TestTrue(Name + TEXT(": the core's answer; the node gave ") + (bOk ? Str(MsBytes(MsLayoutJson(Got))) : Reason)
				+ TEXT(", the core ") + Str(MsBytes(Want.ToJson())), bHeld))
				++T.Failed;
		}
		T.Close(*this, TEXT("layout_cases"), Cases->Size());
	}

	// The frame cases: CarryOf from the Map the case names by its id, at its point.
	{
		const OWJsonValue* Cases = Fixture.Find("frame_cases");
		if (!TestTrue(TEXT("the fixture holds frame_cases"), Cases && Cases->IsArray())) return false;
		FMsTally T;
		int32 DoorRefused = 0;
		for (const OWJsonValue& C : Cases->Items())
		{
			const FString Name = Str(MsText(C.Find("name")));
			const OWJsonValue* Elements = C.Find("elements");
			const OWJsonValue* MapId = C.Find("map");
			const OWJsonValue* Point = C.Find("point");
			const std::optional<double> X = Point && Point->IsArray() && Point->Size() == 2 ? MsCore::NumberOf(&Point->Items()[0]) : std::nullopt;
			const std::optional<double> Y = Point && Point->IsArray() && Point->Size() == 2 ? MsCore::NumberOf(&Point->Items()[1]) : std::nullopt;
			if (!Elements || !Elements->IsArray() || !MapId || !MapId->IsString())
			{
				T.NotCarried.Add(Name + TEXT(" (names its Map by position, not by id)"));
				continue;
			}
			if (!X || !Y)
			{
				T.NotCarried.Add(Name + TEXT(" (a point that is not two numbers)"));
				continue;
			}
			const OWJsonValue* Row = MsMapRow(*Elements, MapId->Text());
			const TArray<FString> Texts = MsTexts(*Elements);
			FOWMapCarry Got;
			FString Reason;
			if (!Row)
			{
				// A Map id no Map row carries: the core answers no Map; the node refuses by its own word.
				const OnlyWorlds::OWMapCarry Want = MsCore::CarryOf(OWJsonValue(), *X, *Y, *Elements);
				const bool bOk = UOWMapSpace::CarryOf(Texts, Str(MapId->Text()), FVector2D(*X, *Y), Got, Reason);
				if (!TestTrue(Name + TEXT(": a Map id no row carries is the door's refusal, where the core reads no Map"),
					!bOk && Reason == TEXT("no map ") + Str(MapId->Text()) && Want.points.empty()))
					++T.Failed;
				++DoorRefused;
				++T.Carried;
				continue;
			}
			const OnlyWorlds::OWMapCarry Want = MsCore::CarryOf(*Row, *X, *Y, *Elements);
			const OWJsonValue Back = MsBack(Texts);
			const OWJsonValue* BackRow = MsMapRow(Back, MapId->Text());
			if (!BackRow || MsBytes(MsCore::CarryOf(*BackRow, *X, *Y, Back).ToJson()) != MsBytes(Want.ToJson()))
			{
				T.NotCarried.Add(Name + TEXT(" (its rows do not read back from their texts)"));
				continue;
			}
			++T.Carried;
			const bool bOk = UOWMapSpace::CarryOf(Texts, Str(MapId->Text()), FVector2D(*X, *Y), Got, Reason);
			const FString Diff = bOk ? MsCarryDiff(Got, Want) : TEXT("refused: ") + Reason;
			const bool bHeld = Want.error.empty() ? Diff.IsEmpty() : (!bOk && Reason == Str(Want.error));
			if (!TestTrue(Name + TEXT(": the core's answer to the bit (") + Diff + TEXT("); the core ") + Str(MsBytes(Want.ToJson())), bHeld))
				++T.Failed;
		}
		T.Close(*this, TEXT("frame_cases"), Cases->Size());
		AddInfo(FString::Printf(TEXT("frame_cases: %d answered by the door's no map"), DoorRefused));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMapSpacePinsKnownToTest, "OnlyWorlds.MapSpace.PinsKnownTo", OWTestFlags)

bool FOWMapSpacePinsKnownToTest::RunTest(const FString& Parameters)
{
	OWJsonValue Doc;
	if (!MsReadFixture(*this, TEXT("tests/data/pins-known-to.cases.json"), Doc)) return false;
	const OWJsonValue* Cases = Doc.Find("cases");
	const OWJsonValue* Refusals = Doc.Find("refusals");
	if (!TestTrue(TEXT("the file holds cases and refusals"), Cases && Cases->IsArray() && Refusals && Refusals->IsArray())) return false;
	FMsTally T;
	auto Run = [&](const OWJsonValue& C)
	{
		const FString Name = Str(MsText(C.Find("name")));
		const OWJsonValue* Pins = C.Find("pins");
		const OWJsonValue* Served = C.Find("served");
		if (!Pins || !Pins->IsArray() || !Served || !Served->IsArray())
		{
			T.NotCarried.Add(Name + TEXT(" (pins or served is no list)"));
			return;
		}
		const OnlyWorlds::OWPinsKnownTo Want = MsCore::PinsKnownTo(*Pins, *Served);
		const TArray<FString> PinTexts = MsTexts(*Pins), ServedTexts = MsTexts(*Served);
		if (MsBytes(MsCore::PinsKnownTo(MsBack(PinTexts), MsBack(ServedTexts)).ToJson()) != MsBytes(Want.ToJson()))
		{
			T.NotCarried.Add(Name + TEXT(" (its rows do not read back from their texts)"));
			return;
		}
		++T.Carried;
		FOWPinsKnownTo Got;
		FString Reason;
		const bool bOk = UOWMapSpace::PinsKnownTo(PinTexts, ServedTexts, Got, Reason);
		bool bHeld = bOk == Want.error.empty();
		if (bHeld && bOk) bHeld = MsBytes(MsKnownJson(Got)) == MsBytes(Want.ToJson());
		if (bHeld && !bOk) bHeld = Reason == Str(Want.error);
		if (!TestTrue(Name + TEXT(": the core's answer; the node gave ") + (bOk ? Str(MsBytes(MsKnownJson(Got))) : Reason)
			+ TEXT(", the core ") + Str(MsBytes(Want.ToJson())), bHeld))
			++T.Failed;
	};
	for (const OWJsonValue& C : Cases->Items()) Run(C);
	for (const OWJsonValue& C : Refusals->Items()) Run(C);
	T.Close(*this, TEXT("pins-known-to"), Cases->Size() + Refusals->Size());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMapSpaceDoorTest, "OnlyWorlds.MapSpace.Door", OWTestFlags)

bool FOWMapSpaceDoorTest::RunTest(const FString& Parameters)
{
	FOWWorldMapSpace M;
	FOWMapLayout L;
	FOWMapCarry K;
	FOWPinsKnownTo P;
	FString Reason;
	// A text that does not parse is refused by its place in the list, before anything is read.
	TestFalse(TEXT("a row that does not parse"), UOWMapSpace::MapSpaceOf({ TEXT(R"({"type": "map", "id": "m1"})"), TEXT("{") }, M, Reason));
	TestTrue(TEXT("named by its place: ") + Reason, Reason.StartsWith(TEXT("elements[1]: ")));
	TestFalse(TEXT("a served row that does not parse"), UOWMapSpace::PinsKnownTo({}, { TEXT("[") }, P, Reason));
	TestTrue(TEXT("served, by its place: ") + Reason, Reason.StartsWith(TEXT("served[0]: ")));
	// A row that is no element is the core's to pass over, never the door's to refuse.
	TestTrue(TEXT("rows that are no element"), UOWMapSpace::MapSpaceOf({ TEXT("7"), TEXT("null"), TEXT(R"({"type": "map"})") }, M, Reason));
	TestEqual(TEXT("and nothing read of them"), M.Maps.Num(), 0);

	const UOWWorld* World = MsHandWorld();
	const TArray<FString> Rows = UOWMapSpace::ServedElements(World);
	// A Map id the rows do not carry, and one that differs only by case: no map.
	TestFalse(TEXT("LayoutOf, no such Map"), UOWMapSpace::LayoutOf(Rows, TEXT("m9"), L, Reason));
	TestEqual(TEXT("its word"), Reason, FString(TEXT("no map m9")));
	TestFalse(TEXT("CarryOf, an id that differs only by case"), UOWMapSpace::CarryOf(Rows, TEXT("M2"), FVector2D(1.0, 1.0), K, Reason));
	TestEqual(TEXT("its word"), Reason, FString(TEXT("no map M2")));
	TestFalse(TEXT("an element that is no Map is no map"), UOWMapSpace::LayoutOf(Rows, TEXT("p1"), L, Reason));

	// ServedElements: none for no world; for a world, the snapshot's elements, row for row.
	TestEqual(TEXT("no world, no rows"), UOWMapSpace::ServedElements(nullptr).Num(), 0);
	const OWJsonValue Snapshot = OWLensDoor::SnapshotOf(*World->Native(), nullptr, false);
	const OWJsonValue* Bodies = Snapshot.Find("elements");
	bool bSame = Bodies && static_cast<int32>(Bodies->Size()) == Rows.Num();
	for (int32 I = 0; bSame && I < Rows.Num(); ++I) bSame = MsBytes(MsParse(Utf8(Rows[I]))) == MsBytes(Bodies->Items()[I]);
	TestTrue(TEXT("ServedElements is the snapshot's elements, in order"), bSame);

	// Each reading over the hand world, the core's answer.
	if (TestTrue(TEXT("MapSpaceOf the hand world: ") + Reason, UOWMapSpace::MapSpaceOf(Rows, M, Reason)))
	{
		TestEqual(TEXT("MapSpaceOf, the core's"), Str(MsBytes(MsMapSpaceJson(M))), Str(MsBytes(MsCore::MapSpaceOf(*Bodies).ToJson())));
		TestTrue(TEXT("the hand world counts a Pin with no point"), M.Out.Num() > 0);
	}
	if (TestTrue(TEXT("LayoutOf the board: ") + Reason, UOWMapSpace::LayoutOf(Rows, TEXT("m3"), L, Reason)))
	{
		const OnlyWorlds::OWMapLayout Want = MsCore::LayoutOf(*MsMapRow(*Bodies, "m3"), MsOfType(*Bodies, "map"), MsOfType(*Bodies, "pin"));
		TestEqual(TEXT("LayoutOf, the core's"), Str(MsBytes(MsLayoutJson(L))), Str(MsBytes(Want.ToJson())));
		TestTrue(TEXT("a board keeps its Pin"), L.bHasLayout && L.Hand.Num() == 1);
	}
	if (TestTrue(TEXT("CarryOf the town: ") + Reason, UOWMapSpace::CarryOf(Rows, TEXT("m2"), FVector2D(50.0, 25.0), K, Reason)))
	{
		const OnlyWorlds::OWMapCarry Want = MsCore::CarryOf(*MsMapRow(*Bodies, "m2"), 50, 25, *Bodies);
		TestEqual(TEXT("CarryOf, the core's to the bit"), MsCarryDiff(K, Want), FString());
		TestTrue(TEXT("carried up one Map"), K.Points.Num() == 2 && K.bHasParentFrame);
	}
	if (TestTrue(TEXT("PinsKnownTo: ") + Reason, UOWMapSpace::PinsKnownTo(Rows, { Rows.Last() }, P, Reason)))
	{
		TestEqual(TEXT("PinsKnownTo, the core's"), Str(MsBytes(MsKnownJson(P))),
			Str(MsBytes(MsCore::PinsKnownTo(*Bodies, MsBack({ Rows.Last() })).ToJson())));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMapSpaceReflectionTest, "OnlyWorlds.MapSpace.Reflection", OWTestFlags)

bool FOWMapSpaceReflectionTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = MsHandWorld();
	const TArray<FString> Rows = UOWMapSpace::ServedElements(World);
	const TCHAR* Texts = TEXT("TArray<FString>");
	FString Reason;
	{
		FCall Call(*this, UOWMapSpace::StaticClass(), TEXT("ServedElements"));
		Call.SetWorld(World);
		Call.Invoke();
		const TArray<FString>* Got = Call.Return() ? Call.Return()->ContainerPtrToValuePtr<TArray<FString>>(Call.GetFrame()) : nullptr;
		TestTrue(TEXT("ServedElements through the graph's path, the C++ call's"), Got && *Got == Rows);
	}
	{
		FCall Call(*this, UOWMapSpace::StaticClass(), TEXT("MapSpaceOf"));
		Call.SetAs(TEXT("Elements"), Texts, Rows);
		Call.Invoke();
		FOWWorldMapSpace Direct;
		UOWMapSpace::MapSpaceOf(Rows, Direct, Reason);
		TestTrue(TEXT("MapSpaceOf answers"), Call.ReturnBool());
		TestEqual(TEXT("MapSpaceOf through the graph's path, the C++ call's"),
			Str(MsBytes(MsMapSpaceJson(Call.OutAs<FOWWorldMapSpace>(TEXT("MapSpace"), TEXT("FOWWorldMapSpace"))))), Str(MsBytes(MsMapSpaceJson(Direct))));
	}
	{
		FCall Call(*this, UOWMapSpace::StaticClass(), TEXT("LayoutOf"));
		Call.SetAs(TEXT("Elements"), Texts, Rows);
		Call.SetString(TEXT("MapId"), TEXT("m3"));
		Call.Invoke();
		FOWMapLayout Direct;
		UOWMapSpace::LayoutOf(Rows, TEXT("m3"), Direct, Reason);
		TestTrue(TEXT("LayoutOf answers"), Call.ReturnBool());
		TestEqual(TEXT("LayoutOf through the graph's path, the C++ call's"),
			Str(MsBytes(MsLayoutJson(Call.OutAs<FOWMapLayout>(TEXT("Layout"), TEXT("FOWMapLayout"))))), Str(MsBytes(MsLayoutJson(Direct))));
	}
	{
		FCall Call(*this, UOWMapSpace::StaticClass(), TEXT("CarryOf"));
		Call.SetAs(TEXT("Elements"), Texts, Rows);
		Call.SetString(TEXT("MapId"), TEXT("m2"));
		Call.SetAs(TEXT("Point"), TEXT("FVector2D"), FVector2D(50.0, 25.0));
		Call.Invoke();
		TestTrue(TEXT("CarryOf answers"), Call.ReturnBool());
		const FOWMapCarry Got = Call.OutAs<FOWMapCarry>(TEXT("Carry"), TEXT("FOWMapCarry"));
		const OWJsonValue Snapshot = OWLensDoor::SnapshotOf(*World->Native(), nullptr, false);
		const OWJsonValue& Bodies = *Snapshot.Find("elements");
		TestEqual(TEXT("CarryOf through the graph's path, the core's to the bit"),
			MsCarryDiff(Got, MsCore::CarryOf(*MsMapRow(Bodies, "m2"), 50, 25, Bodies)), FString());
	}
	{
		FCall Call(*this, UOWMapSpace::StaticClass(), TEXT("PinsKnownTo"));
		Call.SetAs(TEXT("Pins"), Texts, Rows);
		Call.SetAs(TEXT("Served"), Texts, TArray<FString>{ Rows.Last() });
		Call.Invoke();
		FOWPinsKnownTo Direct;
		UOWMapSpace::PinsKnownTo(Rows, { Rows.Last() }, Direct, Reason);
		TestTrue(TEXT("PinsKnownTo answers"), Call.ReturnBool());
		TestEqual(TEXT("PinsKnownTo through the graph's path, the C++ call's"),
			Str(MsBytes(MsKnownJson(Call.OutAs<FOWPinsKnownTo>(TEXT("Known"), TEXT("FOWPinsKnownTo"))))), Str(MsBytes(MsKnownJson(Direct))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMapSpaceSikeliaTest, "OnlyWorlds.MapSpace.Sikelia", OWTestFlags)

bool FOWMapSpaceSikeliaTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_SIKELIA"))) return true;
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	// The core over the world as served, read here on its own.
	OWJsonValue Bodies = OWJsonValue::Array();
	for (const OnlyWorlds::OWElement& E : World->Native()->elements) Bodies.Push(UOWOpeners::ServedElement(E));
	const OnlyWorlds::OWWorldMapSpace Want = MsCore::MapSpaceOf(Bodies);
	FOWWorldMapSpace Got;
	FString Reason;
	if (!TestTrue(TEXT("MapSpaceOf Sikelia: ") + Reason, UOWMapSpace::MapSpaceOf(UOWMapSpace::ServedElements(World), Got, Reason))) return false;
	TestTrue(TEXT("Sikelia's map space, the core's"), MsEqual(MsMapSpaceJson(Got), Want.ToJson()));
	AddInfo(FString::Printf(TEXT("Sikelia's map space: %d maps, %d outlines, %d pins"), Got.Maps.Num(), Got.Zones.Num(), Got.Pins.Num()));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
