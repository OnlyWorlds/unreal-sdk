// The three layout methods through UOWLayout (OnlyWorlds.Layout.*): every case of the layout fixture
// (codegen/schema-dist/profiles/layout.fixture.json: cases, around, unplaced) and of the by-links-3d fixture
// (profiles/layout-by-links-3d.fixture.json: cases, unplaced), each graph made as a Blueprint holds one (FOWWorldGraph:
// ids, types, names, edges), each through the node and held to the core's own answer on the case's own input, every
// coordinate by its bits (one build runs both, so the bits are the same, the 6e-8 the core allows between runtimes
// included); each node called as a Blueprint calls it (OWBlueprintCall.h); Sikelia's owner graph by kind.
//
// The carry rule, as OWMapSpaceTests.cpp's: a case whose input this file, writing it in the node's shape and back in
// its own code (never the node's converter), cannot hand the core so that the core answers as it answers the case's
// own input, is not carried, and is named. A graph whose node names a number, an edge with a null field, a held value
// that is not three numbers are such inputs. No count is pinned: carried and not carried are every case of the fixture.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWConvert.h"
#include "OWGraph.h"
#include "OWGraphLibrary.h"
#include "OWJson.h"
#include "OWLayout.h"
#include "OWLayoutLibrary.h"
#include "OWLensDoor.h"
#include "OWLensLibrary.h"
#include "OWMapSpace.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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
	namespace LoCore = OnlyWorlds::OWLayout;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue LoParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string LoText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	std::string LoBytes(const OWJsonValue& V) { return OnlyWorlds::OWJson::Stringify(V, true); }

	uint64_t LoBits(double D)
	{
		uint64_t B = 0;
		std::memcpy(&B, &D, sizeof B);
		return B;
	}

	FString LoBitsText(double D) { return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(LoBits(D))); }

	// A case's graph as a Blueprint holds one: each node's id, type and name (a name only when a text), each edge's
	// source, field, target and index (0 when absent). What the struct cannot hold is lost here, and the carry rule sees it.
	FOWWorldGraph LoGraphOf(const OWJsonValue& Graph)
	{
		FOWWorldGraph Out;
		if (const OWJsonValue* Nodes = Graph.IsObject() ? Graph.Find("nodes") : nullptr; Nodes && Nodes->IsArray())
		{
			for (const OWJsonValue& N : Nodes->Items())
			{
				FOWGraphNode& Node = Out.Nodes.AddDefaulted_GetRef();
				Node.Id = Str(LoText(N.IsObject() ? N.Find("id") : nullptr));
				Node.Type = Str(LoText(N.IsObject() ? N.Find("type") : nullptr));
				const OWJsonValue* Name = N.IsObject() ? N.Find("name") : nullptr;
				Node.bHasName = Name && Name->IsString();
				Node.Name = Str(LoText(Name));
			}
		}
		if (const OWJsonValue* Edges = Graph.IsObject() ? Graph.Find("edges") : nullptr; Edges && Edges->IsArray())
		{
			for (const OWJsonValue& E : Edges->Items())
			{
				FOWGraphEdge& Edge = Out.Edges.AddDefaulted_GetRef();
				Edge.Source = Str(LoText(E.IsObject() ? E.Find("source") : nullptr));
				Edge.Field = Str(LoText(E.IsObject() ? E.Find("field") : nullptr));
				Edge.Target = Str(LoText(E.IsObject() ? E.Find("target") : nullptr));
				const OWJsonValue* At = E.IsObject() ? E.Find("index") : nullptr;
				Edge.Index = At && At->IsNumber() ? static_cast<int64>(At->AsDouble()) : 0;
			}
		}
		return Out;
	}

	// The struct written back as a graph by this file's own code: {nodes: [{id, type, name}], edges: [{source, field,
	// target, index}]}, a name null when the node has none.
	OWJsonValue LoGraphJson(const FOWWorldGraph& G)
	{
		OWJsonValue Out = OWJsonValue::Object();
		OWJsonValue Nodes = OWJsonValue::Array();
		for (const FOWGraphNode& N : G.Nodes)
		{
			OWJsonValue Node = OWJsonValue::Object();
			Node.Set("id", OWJsonValue::String(Utf8(N.Id)));
			Node.Set("type", OWJsonValue::String(Utf8(N.Type)));
			Node.Set("name", N.bHasName ? OWJsonValue::String(Utf8(N.Name)) : OWJsonValue::Null());
			Nodes.Push(std::move(Node));
		}
		Out.Set("nodes", std::move(Nodes));
		OWJsonValue Edges = OWJsonValue::Array();
		for (const FOWGraphEdge& E : G.Edges)
		{
			OWJsonValue Edge = OWJsonValue::Object();
			Edge.Set("source", OWJsonValue::String(Utf8(E.Source)));
			Edge.Set("field", OWJsonValue::String(Utf8(E.Field)));
			Edge.Set("target", OWJsonValue::String(Utf8(E.Target)));
			Edge.Set("index", OWJsonValue::Number(static_cast<int64_t>(E.Index)));
			Edges.Push(std::move(Edge));
		}
		Out.Set("edges", std::move(Edges));
		return Out;
	}

	TArray<FString> LoIds(const OWJsonValue* List)
	{
		TArray<FString> Out;
		if (List && List->IsArray())
		{
			for (const OWJsonValue& V : List->Items())
			{
				if (V.IsString()) Out.Add(Str(V.Text()));
			}
		}
		return Out;
	}

	OWJsonValue LoIdsJson(const TArray<FString>& Ids)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& Id : Ids) Out.Push(OWJsonValue::String(Utf8(Id)));
		return Out;
	}

	std::string LoJoined(const std::vector<std::string>& Ids)
	{
		std::string Out;
		for (const std::string& Id : Ids) Out += Id + " ";
		return Out;
	}

	std::string LoJoined(const TArray<FString>& Ids)
	{
		std::string Out;
		for (const FString& Id : Ids) Out += Utf8(Id) + " ";
		return Out;
	}

	// The core's 2D answers, every point by its bits.
	std::string LoKey(const OnlyWorlds::OWLayoutByKind& A)
	{
		std::string Out = "error:" + A.error + ";";
		for (const OnlyWorlds::OWLayoutPoint& P : A.points) Out += P.id + "," + std::to_string(LoBits(P.x)) + "," + std::to_string(LoBits(P.y)) + ";";
		return Out;
	}

	std::string LoKey(const OnlyWorlds::OWLayoutAround& A)
	{
		std::string Out = "error:" + A.error + ";";
		for (const OnlyWorlds::OWLayoutAroundPoint& P : A.points)
			Out += P.id + "," + std::to_string(P.depth) + "," + std::to_string(LoBits(P.x)) + "," + std::to_string(LoBits(P.y)) + ";";
		return Out;
	}

	std::string LoKey(const OnlyWorlds::OWLayoutUnplaced& A) { return "error:" + A.error + ";" + LoJoined(A.unplaced); }

	std::string LoKey(const OnlyWorlds::OWLayoutByLinks3d& A)
	{
		std::string Out = "code:" + A.code + ";error:" + A.error + ";version:" + std::to_string(A.version) + ";";
		for (const OnlyWorlds::OWLayout3dPoint& P : A.points)
		{
			Out += P.id + "," + std::to_string(LoBits(P.x)) + "," + std::to_string(LoBits(P.y)) + "," + std::to_string(LoBits(P.z)) + ","
				+ P.how + "," + P.group.value_or("") + "," + P.part.value_or("") + "," + (P.held ? "1" : "0") + ";";
		}
		return Out + "centres:" + LoJoined(A.centres) + "unplaced:" + LoJoined(A.unplaced);
	}

	// The node's answers in the same keys, from the structs alone.
	std::string LoKey(bool bOk, const FString& Reason, const TArray<FOWLayoutPoint>& Points)
	{
		std::string Out = "error:" + (bOk ? std::string() : Utf8(Reason)) + ";";
		for (const FOWLayoutPoint& P : Points)
			Out += Utf8(P.Id) + "," + std::to_string(LoBits(P.Point.X)) + "," + std::to_string(LoBits(P.Point.Y)) + ";";
		return Out;
	}

	std::string LoKey(bool bOk, const FString& Reason, const TArray<FOWLayoutAroundPoint>& Points)
	{
		std::string Out = "error:" + (bOk ? std::string() : Utf8(Reason)) + ";";
		for (const FOWLayoutAroundPoint& P : Points)
			Out += Utf8(P.Id) + "," + std::to_string(P.Depth) + "," + std::to_string(LoBits(P.Point.X)) + "," + std::to_string(LoBits(P.Point.Y)) + ";";
		return Out;
	}

	std::string LoKey(bool bOk, const FString& Reason, const TArray<FString>& Unplaced)
	{
		return "error:" + (bOk ? std::string() : Utf8(Reason)) + ";" + LoJoined(Unplaced);
	}

	// A refusal by the page's word is the node's Reason alone; a graph it cannot read, the core's sentence.
	std::string LoKey(bool bOk, const FString& Reason, const FOWLayoutByLinks3d& L, const OnlyWorlds::OWLayoutByLinks3d& Want)
	{
		if (!bOk)
		{
			const std::string R = Utf8(Reason);
			return "code:" + (Want.code.empty() ? std::string() : R) + ";error:" + (Want.code.empty() ? R : Want.error) + ";version:"
				+ std::to_string(Want.version) + ";centres:unplaced:";
		}
		std::string Out = "code:;error:;version:" + std::to_string(L.Version) + ";";
		for (const FOWLayout3dPoint& P : L.Points)
		{
			Out += Utf8(P.Id) + "," + std::to_string(LoBits(P.Point.X)) + "," + std::to_string(LoBits(P.Point.Y)) + "," + std::to_string(LoBits(P.Point.Z))
				+ "," + Utf8(P.How) + "," + Utf8(P.Group) + "," + Utf8(P.Part) + "," + (P.bHeld ? "1" : "0") + ";";
		}
		return Out + "centres:" + LoJoined(L.Centres) + "unplaced:" + LoJoined(L.Unplaced);
	}

	struct FLoTally
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

		// One case: not carried when the core answers the struct's input otherwise; else the node must answer the core's.
		void Hold(FAutomationTestBase& Test, const FString& Name, const std::string& Want, const std::string& FromStruct, const std::string& Got)
		{
			if (FromStruct != Want)
			{
				NotCarried.Add(Name + TEXT(" (the struct cannot hold its input)"));
				return;
			}
			++Carried;
			if (!Test.TestTrue(Name + TEXT(": the core's answer to the bit; the node gave ") + Str(Got.substr(0, 400)) + TEXT(", the core ")
				+ Str(Want.substr(0, 400)), Got == Want))
				++Failed;
		}
	};

	bool LoReadFixture(FAutomationTestBase& Test, const TCHAR* Rel, OWJsonValue& Out)
	{
		const FString Path = OWTestPaths::InRepo(Rel);
		return Test.TestTrue(TEXT("the fixture at ") + Path, OWTestPaths::ReadJson(Path, Out));
	}

	int32 LoSteps(const OWJsonValue& C)
	{
		const OWJsonValue* S = C.Find("steps");
		return S && S->IsNumber() ? static_cast<int32>(S->AsDouble()) : 1;
	}

	// The held points as the node takes them (an earlier answer's rows: id and point), or false when a value is not
	// three numbers the struct can hold.
	bool LoHeldOf(const OWJsonValue* Held, TArray<FOWLayout3dPoint>& Rows)
	{
		Rows.Reset();
		if (!Held || Held->IsNull()) return true;
		if (!Held->IsObject()) return false;
		for (size_t I = 0; I < Held->Size(); ++I)
		{
			const OWJsonValue& V = Held->Items()[I];
			if (!V.IsArray() || V.Size() != 3) return false;
			std::optional<double> X = OnlyWorlds::OWMapSpace::NumberOf(&V.Items()[0]);
			std::optional<double> Y = OnlyWorlds::OWMapSpace::NumberOf(&V.Items()[1]);
			std::optional<double> Z = OnlyWorlds::OWMapSpace::NumberOf(&V.Items()[2]);
			if (!X || !Y || !Z) return false;
			FOWLayout3dPoint& Row = Rows.AddDefaulted_GetRef();
			Row.Id = Str(Held->Keys()[I]);
			Row.Point = FVector(*X, *Y, *Z);
		}
		return true;
	}

	// The held rows written back by this file: id to [x, y, z]; null for none.
	OWJsonValue LoHeldJson(const TArray<FOWLayout3dPoint>& Rows)
	{
		OWJsonValue Out = OWJsonValue::Object();
		for (const FOWLayout3dPoint& R : Rows)
		{
			OWJsonValue At = OWJsonValue::Array();
			At.Push(OWJsonValue::Number(R.Point.X));
			At.Push(OWJsonValue::Number(R.Point.Y));
			At.Push(OWJsonValue::Number(R.Point.Z));
			Out.Set(Utf8(R.Id), std::move(At));
		}
		return Out;
	}

	TArray<FString> LoTexts(const OWJsonValue* Rows)
	{
		TArray<FString> Out;
		if (Rows && Rows->IsArray())
		{
			for (const OWJsonValue& R : Rows->Items()) Out.Add(Str(LoBytes(R)));
		}
		return Out;
	}

	OWJsonValue LoBack(const TArray<FString>& Texts)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& T : Texts) Out.Push(LoParse(Utf8(T)));
		return Out;
	}

	// A small graph with names, a link both ways and an element linked to no other.
	FOWWorldGraph LoHandGraph()
	{
		return LoGraphOf(LoParse(R"({"nodes": [
			{"id": "c1", "type": "character", "name": "Ada"}, {"id": "c2", "type": "character", "name": "Bo"},
			{"id": "l1", "type": "location", "name": "Keep"}, {"id": "o1", "type": "object", "name": "Cup"},
			{"id": "s1", "type": "species", "name": "Lone"}],
			"edges": [{"source": "c1", "field": "friends", "target": "c2", "index": 0},
			{"source": "c2", "field": "friends", "target": "c1", "index": 0},
			{"source": "c1", "field": "location", "target": "l1", "index": 0},
			{"source": "o1", "field": "location", "target": "l1", "index": 0}]})"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLayoutFixtureTest, "OnlyWorlds.Layout.Fixture", OWTestFlags)

bool FOWLayoutFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	if (!LoReadFixture(*this, TEXT("codegen/schema-dist/profiles/layout.fixture.json"), Fixture)) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	const OWJsonValue* Around = Fixture.Find("around");
	const OWJsonValue* Unplaced = Fixture.Find("unplaced");
	if (!TestTrue(TEXT("the fixture holds cases, around and unplaced"), Cases && Cases->IsArray() && Around && Around->IsArray()
		&& Unplaced && Unplaced->IsArray()))
		return false;
	{
		FLoTally T;
		for (const OWJsonValue& C : Cases->Items())
		{
			const OWJsonValue Graph = C.Find("graph") ? *C.Find("graph") : OWJsonValue();
			const FOWWorldGraph G = LoGraphOf(Graph);
			TArray<FOWLayoutPoint> Points;
			FString Reason;
			const bool bOk = UOWLayout::LayoutByKind(G, Points, Reason);
			T.Hold(*this, Str(LoText(C.Find("name"))), LoKey(LoCore::LayoutByKind(Graph)), LoKey(LoCore::LayoutByKind(LoGraphJson(G))),
				LoKey(bOk, Reason, Points));
		}
		T.Close(*this, TEXT("by-kind"), Cases->Size());
	}
	{
		FLoTally T;
		for (const OWJsonValue& C : Around->Items())
		{
			const OWJsonValue Graph = C.Find("graph") ? *C.Find("graph") : OWJsonValue();
			const FOWWorldGraph G = LoGraphOf(Graph);
			const std::string Focus = LoText(C.Find("focus"));
			const int32 Steps = LoSteps(C);
			TArray<FOWLayoutAroundPoint> Points;
			FString Reason;
			const bool bOk = UOWLayout::LayoutAround(G, Str(Focus), Steps, Points, Reason);
			T.Hold(*this, Str(LoText(C.Find("name"))), LoKey(LoCore::LayoutAround(Graph, Focus, Steps)),
				LoKey(LoCore::LayoutAround(LoGraphJson(G), Focus, Steps)), LoKey(bOk, Reason, Points));
		}
		T.Close(*this, TEXT("around"), Around->Size());
	}
	{
		FLoTally T;
		for (const OWJsonValue& C : Unplaced->Items())
		{
			const FString Name = Str(LoText(C.Find("name")));
			const std::string Method = LoText(C.Find("method"));
			const OWJsonValue Graph = C.Find("graph") ? *C.Find("graph") : OWJsonValue();
			const OWJsonValue PlacedJson = C.Find("placed") ? *C.Find("placed") : OWJsonValue();
			const FOWWorldGraph G = LoGraphOf(Graph);
			const TArray<FString> Placed = LoIds(&PlacedJson);
			const std::string Focus = LoText(C.Find("focus"));
			const int32 Steps = LoSteps(C);
			TArray<FString> Ids;
			FString Reason;
			if (Method == "by-kind")
			{
				const bool bOk = UOWLayout::UnplacedByKind(G, Placed, Ids, Reason);
				T.Hold(*this, Name, LoKey(LoCore::UnplacedByKind(Graph, PlacedJson)), LoKey(LoCore::UnplacedByKind(LoGraphJson(G), LoIdsJson(Placed))),
					LoKey(bOk, Reason, Ids));
			}
			else if (Method == "around")
			{
				const bool bOk = UOWLayout::UnplacedAround(G, Str(Focus), Placed, Steps, Ids, Reason);
				T.Hold(*this, Name, LoKey(LoCore::UnplacedAround(Graph, Focus, PlacedJson, Steps)),
					LoKey(LoCore::UnplacedAround(LoGraphJson(G), Focus, LoIdsJson(Placed), Steps)), LoKey(bOk, Reason, Ids));
			}
			else
			{
				T.NotCarried.Add(Name + TEXT(" (a method this suite does not hold: ") + Str(Method) + TEXT(")"));
			}
		}
		T.Close(*this, TEXT("unplaced"), Unplaced->Size());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLayoutByLinks3dTest, "OnlyWorlds.Layout.ByLinks3d", OWTestFlags)

bool FOWLayoutByLinks3dTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	if (!LoReadFixture(*this, TEXT("codegen/schema-dist/profiles/layout-by-links-3d.fixture.json"), Fixture)) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	const OWJsonValue* Unplaced = Fixture.Find("unplaced");
	if (!TestTrue(TEXT("the fixture holds cases and unplaced"), Cases && Cases->IsArray() && Unplaced && Unplaced->IsArray())) return false;
	{
		FLoTally T;
		for (const OWJsonValue& C : Cases->Items())
		{
			const FString Name = Str(LoText(C.Find("name")));
			const OWJsonValue* Input = C.Find("input");
			if (!Input || !Input->IsObject())
			{
				T.NotCarried.Add(Name + TEXT(" (no input)"));
				continue;
			}
			// The case's own input, as the core's test hands it.
			OWJsonValue Graph = OWJsonValue::Object();
			Graph.Set("nodes", Input->Find("nodes") ? *Input->Find("nodes") : OWJsonValue());
			Graph.Set("edges", Input->Find("edges") ? *Input->Find("edges") : OWJsonValue());
			const OWJsonValue* Rows = Input->Find("rows");
			const OWJsonValue* HeldIn = Input->Find("held");
			const OWJsonValue* UnitIn = Input->Find("heldUnit");
			const OnlyWorlds::OWLayoutByLinks3d Want = LoCore::LayoutByLinks3d(Graph, Rows, HeldIn, UnitIn);
			// The input in the node's shape.
			const FOWWorldGraph G = LoGraphOf(Graph);
			const TArray<FString> Elements = LoTexts(Rows);
			TArray<FOWLayout3dPoint> Held;
			const bool bUnitHeld = !UnitIn || UnitIn->IsNull() || UnitIn->IsNumber();
			if (!LoHeldOf(HeldIn, Held) || !bUnitHeld || (Rows && !Rows->IsNull() && !Rows->IsArray()))
			{
				T.NotCarried.Add(Name + TEXT(" (held, heldUnit or rows the node's pins cannot hold)"));
				continue;
			}
			const bool bHasUnit = UnitIn && UnitIn->IsNumber();
			const double Unit = bHasUnit ? UnitIn->AsDouble() : 0.0;
			// The same, written back by this file and handed to the core.
			const OWJsonValue RowsBack = LoBack(Elements);
			const OWJsonValue HeldBack = LoHeldJson(Held);
			const OWJsonValue UnitBack = OWJsonValue::Number(Unit);
			const OnlyWorlds::OWLayoutByLinks3d FromStruct = LoCore::LayoutByLinks3d(LoGraphJson(G), Elements.Num() ? &RowsBack : nullptr,
				Held.Num() ? &HeldBack : nullptr, bHasUnit ? &UnitBack : nullptr);
			FOWLayoutByLinks3d L;
			FString Reason;
			const bool bOk = UOWLayout::LayoutByLinks3d(G, Elements, Held, bHasUnit, Unit, L, Reason);
			T.Hold(*this, Name, LoKey(Want), LoKey(FromStruct), LoKey(bOk, Reason, L, Want));
		}
		T.Close(*this, TEXT("by-links-3d"), Cases->Size());
	}
	{
		FLoTally T;
		for (const OWJsonValue& C : Unplaced->Items())
		{
			const OWJsonValue Graph = C.Find("graph") ? *C.Find("graph") : OWJsonValue();
			const OWJsonValue PlacedJson = C.Find("placed") ? *C.Find("placed") : OWJsonValue();
			const FOWWorldGraph G = LoGraphOf(Graph);
			const TArray<FString> Placed = LoIds(&PlacedJson);
			TArray<FString> Ids;
			FString Reason;
			const bool bOk = UOWLayout::UnplacedByLinks3d(G, Placed, Ids, Reason);
			T.Hold(*this, Str(LoText(C.Find("name"))), LoKey(LoCore::UnplacedByLinks3d(Graph, PlacedJson)),
				LoKey(LoCore::UnplacedByLinks3d(LoGraphJson(G), LoIdsJson(Placed))), LoKey(bOk, Reason, Ids));
		}
		T.Close(*this, TEXT("by-links-3d unplaced"), Unplaced->Size());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLayoutDoorTest, "OnlyWorlds.Layout.Door", OWTestFlags)

bool FOWLayoutDoorTest::RunTest(const FString& Parameters)
{
	const FOWWorldGraph G = LoHandGraph();
	const OWJsonValue GJson = LoGraphJson(G);
	FString Reason;
	// Names reach the core: around orders a ring by name, so a graph without them answers otherwise.
	TArray<FOWLayoutAroundPoint> Named;
	TestTrue(TEXT("LayoutAround on the hand graph"), UOWLayout::LayoutAround(G, TEXT("l1"), 2, Named, Reason));
	TestEqual(TEXT("around, the core's on the same graph with its names"), Str(LoKey(true, Reason, Named)), Str(LoKey(LoCore::LayoutAround(GJson, "l1", 2))));
	// Steps that is neither 1 nor 2, and a focus that is no node: the core's sentence.
	TestFalse(TEXT("steps 0"), UOWLayout::LayoutAround(G, TEXT("l1"), 0, Named, Reason));
	TestEqual(TEXT("steps 0, the core's sentence"), Reason, Str(LoCore::LayoutAround(GJson, "l1", 0).error));
	TestFalse(TEXT("a focus that is no node"), UOWLayout::LayoutAround(G, TEXT("L1"), 1, Named, Reason));
	TestEqual(TEXT("no focus, the core's sentence"), Reason, Str(LoCore::LayoutAround(GJson, "L1", 1).error));
	// A type that is not one of the 22.
	FOWWorldGraph Bad = G;
	Bad.Nodes[0].Type = TEXT("Character");
	TArray<FOWLayoutPoint> Points;
	TestFalse(TEXT("a type that is not a slug"), UOWLayout::LayoutByKind(Bad, Points, Reason));
	TestEqual(TEXT("its sentence, the core's"), Reason, Str(LoCore::LayoutByKind(LoGraphJson(Bad)).error));
	// by-links-3d: a held point that is not three finite numbers, by the page's word; a row that does not parse.
	FOWLayoutByLinks3d L;
	TArray<FOWLayout3dPoint> Held;
	Held.AddDefaulted_GetRef().Id = TEXT("c1");
	Held[0].Point = FVector(NAN, 0.0, 0.0);
	TestFalse(TEXT("a held point that is not finite"), UOWLayout::LayoutByLinks3d(G, {}, Held, false, 0.0, L, Reason));
	TestEqual(TEXT("by the page's word"), Reason, FString(TEXT("held-not-point")));
	TestFalse(TEXT("a heldUnit past an eighth of a rest"), UOWLayout::LayoutByLinks3d(G, {}, {}, true, 1000.0, L, Reason));
	TestEqual(TEXT("by the page's word"), Reason, FString(TEXT("held-unit")));
	TestFalse(TEXT("a row that does not parse"), UOWLayout::LayoutByLinks3d(G, { TEXT("{") }, {}, false, 0.0, L, Reason));
	TestTrue(TEXT("named by its place"), Reason.StartsWith(TEXT("elements[0]: ")));
	// An answer handed back as held stands where it stood.
	if (TestTrue(TEXT("by-links-3d on the hand graph"), UOWLayout::LayoutByLinks3d(G, {}, {}, false, 0.0, L, Reason)))
	{
		TestTrue(TEXT("the element linked to no other is unplaced"), L.Unplaced.Contains(TEXT("s1")));
		FOWLayoutByLinks3d Again;
		TestTrue(TEXT("handed back as held"), UOWLayout::LayoutByLinks3d(G, {}, L.Points, false, 0.0, Again, Reason));
		int32 Stood = 0;
		for (int32 I = 0; I < Again.Points.Num() && I < L.Points.Num(); ++I)
		{
			if (Again.Points[I].Id == L.Points[I].Id && LoBits(Again.Points[I].Point.X) == LoBits(L.Points[I].Point.X)
				&& LoBits(Again.Points[I].Point.Y) == LoBits(L.Points[I].Point.Y) && LoBits(Again.Points[I].Point.Z) == LoBits(L.Points[I].Point.Z))
				++Stood;
		}
		TestEqual(TEXT("every element stands where it stood"), Stood, L.Points.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLayoutReflectionTest, "OnlyWorlds.Layout.Reflection", OWTestFlags)

bool FOWLayoutReflectionTest::RunTest(const FString& Parameters)
{
	const FOWWorldGraph G = LoHandGraph();
	const TCHAR* GraphType = TEXT("FOWWorldGraph");
	const TCHAR* Texts = TEXT("TArray<FString>");
	const TArray<FString> Placed = { TEXT("c1"), TEXT("l1") };
	FString Reason;
	{
		FCall Call(*this, UOWLayout::StaticClass(), TEXT("LayoutByKind"));
		Call.SetAs(TEXT("Graph"), GraphType, G);
		Call.Invoke();
		TArray<FOWLayoutPoint> Direct;
		const bool bOk = UOWLayout::LayoutByKind(G, Direct, Reason);
		TestTrue(TEXT("LayoutByKind answers"), Call.ReturnBool() && bOk);
		TestEqual(TEXT("LayoutByKind through the graph's path, the C++ call's"),
			Str(LoKey(true, Reason, Call.OutAs<TArray<FOWLayoutPoint>>(TEXT("Points"), TEXT("TArray<FOWLayoutPoint>")))), Str(LoKey(true, Reason, Direct)));
	}
	{
		FCall Call(*this, UOWLayout::StaticClass(), TEXT("LayoutAround"));
		Call.SetAs(TEXT("Graph"), GraphType, G);
		Call.SetString(TEXT("Focus"), TEXT("c1"));
		Call.SetAs(TEXT("Steps"), TEXT("int32"), 2);
		Call.Invoke();
		TArray<FOWLayoutAroundPoint> Direct;
		const bool bOk = UOWLayout::LayoutAround(G, TEXT("c1"), 2, Direct, Reason);
		TestTrue(TEXT("LayoutAround answers"), Call.ReturnBool() && bOk);
		TestEqual(TEXT("LayoutAround through the graph's path, the C++ call's"),
			Str(LoKey(true, Reason, Call.OutAs<TArray<FOWLayoutAroundPoint>>(TEXT("Points"), TEXT("TArray<FOWLayoutAroundPoint>")))), Str(LoKey(true, Reason, Direct)));
	}
	{
		FCall Call(*this, UOWLayout::StaticClass(), TEXT("LayoutByLinks3d"));
		Call.SetAs(TEXT("Graph"), GraphType, G);
		Call.SetAs(TEXT("Elements"), Texts, TArray<FString>());
		Call.SetAs(TEXT("Held"), TEXT("TArray<FOWLayout3dPoint>"), TArray<FOWLayout3dPoint>());
		Call.SetBool(TEXT("bHasHeldUnit"), true);
		Call.SetAs(TEXT("HeldUnit"), TEXT("double"), 1.0);
		Call.Invoke();
		FOWLayoutByLinks3d Direct;
		const bool bOk = UOWLayout::LayoutByLinks3d(G, {}, {}, true, 1.0, Direct, Reason);
		TestTrue(TEXT("LayoutByLinks3d answers"), Call.ReturnBool() && bOk);
		const FOWLayoutByLinks3d Got = Call.OutAs<FOWLayoutByLinks3d>(TEXT("Layout"), TEXT("FOWLayoutByLinks3d"));
		const OnlyWorlds::OWLayoutByLinks3d None;
		TestEqual(TEXT("LayoutByLinks3d through the graph's path, the C++ call's"), Str(LoKey(true, Reason, Got, None)), Str(LoKey(true, Reason, Direct, None)));
	}
	for (const TCHAR* Name : { TEXT("UnplacedByKind"), TEXT("UnplacedAround"), TEXT("UnplacedByLinks3d") })
	{
		FCall Call(*this, UOWLayout::StaticClass(), Name);
		Call.SetAs(TEXT("Graph"), GraphType, G);
		Call.SetAs(TEXT("Placed"), Texts, Placed);
		TArray<FString> Direct;
		bool bOk = false;
		if (FString(Name) == TEXT("UnplacedAround"))
		{
			Call.SetString(TEXT("Focus"), TEXT("c1"));
			Call.SetAs(TEXT("Steps"), TEXT("int32"), 1);
			bOk = UOWLayout::UnplacedAround(G, TEXT("c1"), Placed, 1, Direct, Reason);
		}
		else if (FString(Name) == TEXT("UnplacedByKind")) bOk = UOWLayout::UnplacedByKind(G, Placed, Direct, Reason);
		else bOk = UOWLayout::UnplacedByLinks3d(G, Placed, Direct, Reason);
		Call.Invoke();
		TestTrue(FString(Name) + TEXT(" answers"), Call.ReturnBool() && bOk);
		TestEqual(FString(Name) + TEXT(" through the graph's path, the C++ call's"),
			Str(LoJoined(Call.OutAs<TArray<FString>>(TEXT("Unplaced"), Texts))), Str(LoJoined(Direct)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWLayoutSikeliaTest, "OnlyWorlds.Layout.Sikelia", OWTestFlags)

bool FOWLayoutSikeliaTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_SIKELIA"))) return true;
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	// The core's own graph of the world for the owner, read here on its own, laid out by the core.
	OWJsonValue Snapshot = OWJsonValue::Object();
	OWJsonValue Bodies = OWJsonValue::Array();
	for (const OnlyWorlds::OWElement& E : World->Native()->elements) Bodies.Push(UOWOpeners::ServedElement(E));
	Snapshot.Set("elements", std::move(Bodies));
	if (const OWJsonValue* Now = World->Native()->info.Find("time_current")) Snapshot.Set("time_current", *Now);
	const OnlyWorlds::OWWorldGraph CoreGraph = OnlyWorlds::OWGraph::WorldGraph(Snapshot, LoParse(R"({"rung": "owner"})"));
	const OnlyWorlds::OWLayoutByKind Want = LoCore::LayoutByKind(CoreGraph);
	FOWLensReader Owner;
	Owner.Rung = EOWLensRung::Owner;
	FOWWorldGraph G;
	FString Reason;
	if (!TestTrue(TEXT("Read, Sikelia's owner: ") + Reason, UOWGraph::Read(World, Owner, FOWGraphOptions(), G, Reason))) return false;
	TArray<FOWLayoutPoint> Points;
	const bool bOk = UOWLayout::LayoutByKind(G, Points, Reason);
	TestTrue(TEXT("Sikelia by kind, the core's to the bit"), bOk && LoKey(bOk, Reason, Points) == LoKey(Want));
	AddInfo(FString::Printf(TEXT("Sikelia by kind: %d points"), Points.Num()));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
