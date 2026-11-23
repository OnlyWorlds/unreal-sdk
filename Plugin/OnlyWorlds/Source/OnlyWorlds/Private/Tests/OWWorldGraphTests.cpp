// The lens profile's graph section through UOWGraph (OnlyWorlds.WorldGraph.*; OWGraphTests.cpp is the editor's
// Blueprint graphs): a world built here in memory, unlensed and lensed, every flag and refusal, the two counts; then
// every graph and degree case of the profile's fixture (codegen/schema-dist/profiles/lens.fixture.json), each case's own
// world placed in a world (a world-level hidden list as hidden: true on each listed body, the one mark the profile
// reads both as; its members list as JSON text), the want compared whole; then Read, DegreeOf and OutDegreeOf called as
// a Blueprint calls them (OWBlueprintCall.h); then Sikelia's whole graph for the owner against the core's.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"

#include "OWConvert.h"
#include "OWGraph.h"
#include "OWGraphLibrary.h"
#include "OWJson.h"
#include "OWLensLibrary.h"
#include "OWOpenersLibrary.h"
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

	OWJsonValue WgParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string WgText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	std::string WgCompact(const OWJsonValue& V)
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

	// Equal as JSON values: an object's keys in any order, a number by its value, as the core's test.
	bool WgEqual(const OWJsonValue& A, const OWJsonValue& B)
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
				if (!WgEqual(A.Items()[I], B.Items()[I])) return false;
			}
			return true;
		case OWJsonValue::Kind::Object:
			if (A.Size() != B.Size()) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				const OWJsonValue* Other = B.Find(A.Keys()[I]);
				if (!Other || !WgEqual(A.Items()[I], *Other)) return false;
			}
			return true;
		}
		return false;
	}

	OWJsonValue WgCounts(const TArray<FOWLensCut>& Cut)
	{
		OWJsonValue Out = OWJsonValue::Object();
		for (const FOWLensCut& C : Cut) Out.Set(Utf8(C.Reason), OWJsonValue::Number(static_cast<int64_t>(C.Count)));
		return Out;
	}

	// The graph read back to the fixture's shape, as the core's ToJson writes it: a node's marks only when it has one,
	// known_of only when true, a name that is no text null.
	OWJsonValue WgGraphJson(const FOWWorldGraph& G)
	{
		OWJsonValue Out = OWJsonValue::Object();
		OWJsonValue Nodes = OWJsonValue::Array();
		for (const FOWGraphNode& N : G.Nodes)
		{
			OWJsonValue Node = OWJsonValue::Object();
			Node.Set("id", OWJsonValue::String(Utf8(N.Id)));
			Node.Set("type", OWJsonValue::String(Utf8(N.Type)));
			Node.Set("name", N.bHasName ? OWJsonValue::String(Utf8(N.Name)) : OWJsonValue::Null());
			Node.Set("family", OWJsonValue::String(Utf8(N.Family)));
			Node.Set("icon", OWJsonValue::String(Utf8(N.Icon)));
			if (N.Marks.Num())
			{
				OWJsonValue Marks = OWJsonValue::Array();
				for (const FString& M : N.Marks) Marks.Push(OWJsonValue::String(Utf8(M)));
				Node.Set("marks", std::move(Marks));
			}
			if (N.bKnownOf) Node.Set("known_of", OWJsonValue::Bool(true));
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
		Out.Set("edges_cut", WgCounts(G.EdgesCut));
		Out.Set("cut", WgCounts(G.Cut));
		Out.Set("unreadable", G.Unreadable.IsEmpty() ? OWJsonValue::Null() : OWJsonValue::String(Utf8(G.Unreadable)));
		return Out;
	}

	FString WgNodeIds(const FOWWorldGraph& G)
	{
		TArray<FString> Ids;
		for (const FOWGraphNode& N : G.Nodes)
		{
			FString Id = N.Id;
			if (N.Marks.Num()) Id += TEXT("[") + FString::Join(N.Marks, TEXT(",")) + TEXT("]");
			if (N.bKnownOf) Id += TEXT("(known of)");
			Ids.Add(Id);
		}
		return FString::Join(Ids, TEXT(" "));
	}

	FString WgEdges(const FOWWorldGraph& G)
	{
		TArray<FString> Rows;
		for (const FOWGraphEdge& E : G.Edges) Rows.Add(FString::Printf(TEXT("%s.%s->%s@%lld"), *E.Source, *E.Field, *E.Target, E.Index));
		return FString::Join(Rows, TEXT(" "));
	}

	FString WgCutText(const TArray<FOWLensCut>& Cut) { return Str(WgCompact(WgCounts(Cut))); }

	// The core's counts in the same shape.
	OWJsonValue WgCoreCounts(const std::vector<std::pair<std::string, int64_t>>& Cut)
	{
		OWJsonValue Out = OWJsonValue::Object();
		for (const std::pair<std::string, int64_t>& C : Cut) Out.Set(C.first, OWJsonValue::Number(C.second));
		return Out;
	}

	FString WgDegrees(const TArray<FOWGraphDegree>& Rows)
	{
		TArray<FString> Parts;
		for (const FOWGraphDegree& D : Rows) Parts.Add(FString::Printf(TEXT("%s=%lld"), *D.Id, D.Degree));
		return FString::Join(Parts, TEXT(" "));
	}

	// -- worlds made here ---------------------------------------------------------------------------------------------

	UOWWorld* WgMakeWorld(std::vector<std::pair<std::string, OWJsonValue>> Bodies)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f000"));
		for (std::pair<std::string, OWJsonValue>& B : Bodies) Native->Add(B.first, std::move(B.second));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}

	std::string WgFolderOf(const OWJsonValue& Body)
	{
		const OWJsonValue* T = Body.IsObject() ? Body.Find("type") : nullptr;
		return T && T->IsString() ? T->Text() : std::string();
	}

	// A case's world placed in a world, each id of its hidden list as hidden: true on the body; nullptr and why when an
	// element is not an object.
	UOWWorld* WgPlace(const OWJsonValue& Fixture, FString& Why)
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
			if (Hidden.count(WgText(B.Find("id")))) B.Set("hidden", OWJsonValue::Bool(true));
			Bodies.emplace_back(WgFolderOf(B), std::move(B));
		}
		return WgMakeWorld(std::move(Bodies));
	}

	bool WgReaderOf(const OWJsonValue& R, FOWLensReader& Out, FString& Why)
	{
		if (!R.IsObject())
		{
			Why = TEXT("a reader that is not an object");
			return false;
		}
		const std::string Rung = WgText(R.Find("rung"));
		const OWJsonValue* As = R.Find("as");
		if (As && !As->IsNull())
		{
			if (Rung != "owner" || !As->IsObject() || (As->Find("as") && !As->Find("as")->IsNull()))
			{
				Why = TEXT("an as the struct cannot hold");
				return false;
			}
			if (!WgReaderOf(*As, Out, Why)) return false;
			Out.bOwnerCounts = true;
			return true;
		}
		if (Rung == "owner") Out.Rung = EOWLensRung::Owner;
		else if (Rung == "co-builder") Out.Rung = EOWLensRung::CoBuilder;
		else if (Rung == "contributor") Out.Rung = EOWLensRung::Contributor;
		else if (Rung == "guest") Out.Rung = EOWLensRung::Guest;
		else if (Rung == "public") Out.Rung = EOWLensRung::Public;
		else
		{
			Why = TEXT("a rung outside the five");
			return false;
		}
		const OWJsonValue* C = R.Find("character");
		if (C && !C->IsNull() && !C->IsString())
		{
			Why = TEXT("a character that is not a string");
			return false;
		}
		Out.Character = Str(WgText(C));
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

	// The lens profile's hand world (OWLensTests.cpp's): c2 names no type, c3 hidden, o1 proposed, n1 a room element,
	// g1 a reading aid; k1 a lens on Ada, k2 a template, k3 unreadable, k4 no lens, k5 names on Keep.
	UOWWorld* WgHandWorld()
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
		for (const auto& Row : Bodies) B.emplace_back(Row[0], WgParse(Row[1]));
		return WgMakeWorld(std::move(B));
	}

	FOWLensReader WgReader(EOWLensRung Rung)
	{
		FOWLensReader R;
		R.Rung = Rung;
		return R;
	}

	FOWGraphOptions WgEvery()
	{
		FOWGraphOptions O;
		O.bRooms = O.bHidden = O.bProposed = O.bMapSpace = O.bReadingAids = true;
		return O;
	}

	// -- the calls, each as a graph makes it ------------------------------------------------------------------------

	void WgSetStringsMember(FAutomationTestBase& Test, const UScriptStruct* Struct, void* Data, const TCHAR* Name,
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

	void WgMakeReader(FAutomationTestBase& Test, void* Data, const FOWLensReader& R)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* S = FOWLensReader::StaticStruct();
		SetEnumMember(Test, S, Data, TEXT("Rung"), StaticEnum<EOWLensRung>(), static_cast<int64>(R.Rung));
		SetMember<FStrProperty>(Test, S, Data, TEXT("Character"), R.Character);
		WgSetStringsMember(Test, S, Data, TEXT("Whole"), R.Whole);
		WgSetStringsMember(Test, S, Data, TEXT("KnownOf"), R.KnownOf);
		SetBoolMember(Test, S, Data, TEXT("bOwnerCounts"), R.bOwnerCounts);
	}

	void WgMakeOptions(FAutomationTestBase& Test, void* Data, const FOWGraphOptions& O)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* S = FOWGraphOptions::StaticStruct();
		SetMember<FStrProperty>(Test, S, Data, TEXT("LensId"), O.LensId);
		SetMember<FStrProperty>(Test, S, Data, TEXT("LensJson"), O.LensJson);
		SetBoolMember(Test, S, Data, TEXT("bHasAsOf"), O.bHasAsOf);
		SetMember<FInt64Property>(Test, S, Data, TEXT("AsOf"), O.AsOf);
		SetMember<FStrProperty>(Test, S, Data, TEXT("Clock"), O.Clock);
		SetMember<FStrProperty>(Test, S, Data, TEXT("MembersJson"), O.MembersJson);
		SetBoolMember(Test, S, Data, TEXT("bRooms"), O.bRooms);
		SetBoolMember(Test, S, Data, TEXT("bHidden"), O.bHidden);
		SetBoolMember(Test, S, Data, TEXT("bProposed"), O.bProposed);
		SetBoolMember(Test, S, Data, TEXT("bMapSpace"), O.bMapSpace);
		SetBoolMember(Test, S, Data, TEXT("bReadingAids"), O.bReadingAids);
	}

	// A returned array of FOWGraphDegree, each row read by its properties.
	TArray<FOWGraphDegree> WgCallDegrees(FAutomationTestBase& Test, FCall& Call)
	{
		TArray<FOWGraphDegree> Out;
		const FArrayProperty* P = CastField<FArrayProperty>(Call.Return());
		const FStructProperty* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
		if (!Inner || Inner->Struct != FOWGraphDegree::StaticStruct() || !Call.GetFrame())
		{
			Test.AddError(FString::Printf(TEXT("%s does not return a TArray<FOWGraphDegree>"), Call.GetName()));
			return Out;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		for (int32 I = 0; I < Array.Num(); ++I)
		{
			const void* Row = Array.GetRawPtr(I);
			FOWGraphDegree& D = Out.AddDefaulted_GetRef();
			D.Id = OWBlueprintTest::Member<FStrProperty>(Test, Inner->Struct, Row, TEXT("Id"));
			D.Degree = OWBlueprintTest::Member<FInt64Property>(Test, Inner->Struct, Row, TEXT("Degree"));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldGraphWorldTest, "OnlyWorlds.WorldGraph.World", OWTestFlags)

bool FOWWorldGraphWorldTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = WgHandWorld();
	auto Read = [&](const FString& What, const FOWLensReader& Reader, const FOWGraphOptions& Options, FOWWorldGraph& G)
	{
		FString Reason;
		const bool bOk = UOWGraph::Read(World, Reader, Options, G, Reason);
		TestTrue(What + TEXT(": answered, got ") + Reason, bOk && Reason.IsEmpty());
	};
	auto Refused = [&](const FString& What, const UOWWorld* In, const FOWLensReader& Reader, const FOWGraphOptions& Options)
	{
		FOWWorldGraph G;
		FString Reason;
		const bool bOk = UOWGraph::Read(In, Reader, Options, G, Reason);
		TestTrue(What + TEXT(": refused, an empty graph"), !bOk && G.Nodes.Num() == 0 && G.Edges.Num() == 0);
		return Reason;
	};
	const FOWGraphOptions NoFlags = FOWGraphOptions();
	const FString OwnersEdges = TEXT("c1.friends->c2@0 c1.location->l1@0 e1.characters->c1@0 e1.characters->c2@1 k1.characters->c1@0 "
		"k2.characters->c1@0 k5.locations->l1@0");

	// The owner, no flag: every kind out by default counted by reason; the edge to Cass cut as hidden.
	{
		FOWWorldGraph G;
		Read(TEXT("the owner"), WgReader(EOWLensRung::Owner), NoFlags, G);
		TestEqual(TEXT("the owner's nodes"), WgNodeIds(G), FString(TEXT("c1 c2 e1 k1 k2 k3 k4 k5 l1")));
		TestEqual(TEXT("the owner's edges"), WgEdges(G), OwnersEdges);
		TestEqual(TEXT("its edges cut"), WgCutText(G.EdgesCut), FString(TEXT("{\"hidden\": 1}")));
		TestEqual(TEXT("its cut"), WgCutText(G.Cut), FString(TEXT("{\"room\": 1,\"hidden\": 1,\"proposed\": 1,\"reading_aid\": 1}")));
		TestTrue(TEXT("readable"), G.Unreadable.IsEmpty());
		if (G.Nodes.Num() == 9)
		{
			const FOWGraphNode& Ada = G.Nodes[0];
			TestTrue(TEXT("Ada: her name, blue, person"), Ada.bHasName && Ada.Name == TEXT("Ada") && Ada.Family == TEXT("blue")
				&& Ada.Icon == TEXT("person") && Ada.Marks.Num() == 0 && !Ada.bKnownOf);
			TestEqual(TEXT("Bo's type, his folder's"), G.Nodes[1].Type, FString(TEXT("character")));
			TestTrue(TEXT("the Siege: amber, saved_search"), G.Nodes[2].Family == TEXT("amber") && G.Nodes[2].Icon == TEXT("saved_search"));
			TestTrue(TEXT("the Keep: green, castle"), G.Nodes[8].Family == TEXT("green") && G.Nodes[8].Icon == TEXT("castle"));
		}
		// The two counts: a mirror is none here; Ada's five edges, the Keep's two in.
		TestEqual(TEXT("DegreeOf the owner's graph"), WgDegrees(UOWGraph::DegreeOf(G)), FString(TEXT("c1=5 c2=2 e1=2 k1=1 k2=1 k3=0 k4=0 k5=1 l1=2")));
		TestEqual(TEXT("OutDegreeOf the owner's graph"), WgDegrees(UOWGraph::OutDegreeOf(G)), FString(TEXT("c1=2 c2=0 e1=2 k1=1 k2=1 k3=0 k4=0 k5=1 l1=0")));
	}
	{
		FOWWorldGraph G;
		Read(TEXT("the owner, every flag"), WgReader(EOWLensRung::Owner), WgEvery(), G);
		TestEqual(TEXT("every element, each brought back marked"), WgNodeIds(G),
			FString(TEXT("c1 c2 c3[hidden] e1 g1[reading_aid] k1 k2 k3 k4 k5 l1 n1[room] o1[proposed]")));
		TestEqual(TEXT("eleven edges"), G.Edges.Num(), 11);
		TestTrue(TEXT("nothing cut"), G.EdgesCut.Num() == 0 && G.Cut.Num() == 0);
		TestEqual(TEXT("DegreeOf"), WgDegrees(UOWGraph::DegreeOf(G)), FString(TEXT("c1=8 c2=2 c3=2 e1=2 g1=0 k1=1 k2=1 k3=0 k4=0 k5=1 l1=3 n1=1 o1=1")));
		TestEqual(TEXT("OutDegreeOf"), WgDegrees(UOWGraph::OutDegreeOf(G)), FString(TEXT("c1=3 c2=0 c3=1 e1=2 g1=0 k1=1 k2=1 k3=0 k4=0 k5=1 l1=0 n1=1 o1=1")));
	}
	{
		// A contributor sees no Cass and counts neither her nor her edge: the owner's reasons are never carried.
		FOWWorldGraph G;
		Read(TEXT("a contributor"), WgReader(EOWLensRung::Contributor), NoFlags, G);
		TestEqual(TEXT("a contributor's nodes"), WgNodeIds(G), FString(TEXT("c1 c2 e1 k1 k2 k3 k4 k5 l1")));
		TestEqual(TEXT("its edges"), WgEdges(G), OwnersEdges);
		TestTrue(TEXT("no edge cut it may count"), G.EdgesCut.Num() == 0);
		TestEqual(TEXT("its cut"), WgCutText(G.Cut), FString(TEXT("{\"room\": 1,\"reading_aid\": 1}")));
	}
	{
		// A guest: Bo and the Keep as stubs, no edge out of either; Cass hidden, not even a stub.
		FOWLensReader Guest = WgReader(EOWLensRung::Guest);
		Guest.Whole = { TEXT("c1"), TEXT("k1"), TEXT("e1") };
		Guest.KnownOf = { TEXT("c2"), TEXT("c3"), TEXT("l1") };
		FOWWorldGraph G;
		Read(TEXT("a guest"), Guest, NoFlags, G);
		TestEqual(TEXT("a guest's nodes"), WgNodeIds(G), FString(TEXT("c1 c2(known of) e1 k1 l1(known of)")));
		TestEqual(TEXT("its edges"), WgEdges(G),
			FString(TEXT("c1.friends->c2@0 c1.location->l1@0 e1.characters->c1@0 e1.characters->c2@1 k1.characters->c1@0")));
	}
	{
		// With a lens the nodes are its kept, Cass marked hidden for the owner, and its cut rides along.
		FOWGraphOptions Lens;
		Lens.LensId = TEXT("k1");
		FOWWorldGraph G;
		Read(TEXT("the owner through k1"), WgReader(EOWLensRung::Owner), Lens, G);
		TestEqual(TEXT("k1's kept"), WgNodeIds(G), FString(TEXT("c1 c2 c3[hidden] e1 k2 l1")));
		TestEqual(TEXT("its edges"), WgEdges(G), FString(TEXT("c1.friends->c2@0 c1.friends->c3@1 c1.location->l1@0 c3.friends->c1@0 "
			"e1.characters->c1@0 e1.characters->c2@1 k2.characters->c1@0")));
		TestEqual(TEXT("the lens's cut"), WgCutText(G.Cut), FString(TEXT("{\"types\": 1}")));
		Read(TEXT("a contributor through k1"), WgReader(EOWLensRung::Contributor), Lens, G);
		TestEqual(TEXT("k1's kept for a contributor"), WgNodeIds(G), FString(TEXT("c1 c2 e1 k2 l1")));
		// Every flag beside a lens: no node added.
		FOWGraphOptions Every = WgEvery();
		Every.LensId = TEXT("k1");
		Read(TEXT("the owner through k1, every flag"), WgReader(EOWLensRung::Owner), Every, G);
		TestEqual(TEXT("a flag never widens a lens"), WgNodeIds(G), FString(TEXT("c1 c2 c3[hidden] e1 k2 l1")));
		Lens.LensId = TEXT("k3");
		Read(TEXT("the owner through k3"), WgReader(EOWLensRung::Owner), Lens, G);
		TestTrue(TEXT("unreadable by depth, an empty graph, got ") + G.Unreadable,
			G.Unreadable.Equals(TEXT("depth"), ESearchCase::CaseSensitive) && G.Nodes.Num() == 0);
		FOWGraphOptions Body;
		Body.LensJson = TEXT("{\"type\": \"construct\", \"supertype\": \"Lens\", \"events\": [\"e1\"], \"x_lens\": {\"depth\": 0}}");
		Read(TEXT("the owner through a lens body"), WgReader(EOWLensRung::Owner), Body, G);
		TestEqual(TEXT("the body's kept"), WgNodeIds(G), FString(TEXT("e1")));
		TestEqual(TEXT("the Siege's two links outside the lens"), WgCutText(G.EdgesCut), FString(TEXT("{\"outside_lens\": 2}")));
	}

	// Refused: a flag from the wrong rung by its word, the first unmet; a caller's mistake by its sentence.
	{
		FOWGraphOptions Hidden;
		Hidden.bHidden = true;
		TestEqual(TEXT("hidden from a contributor"), Refused(TEXT("hidden from a contributor"), World, WgReader(EOWLensRung::Contributor), Hidden),
			FString(TEXT("hidden")));
		FOWLensReader AsContributor = WgReader(EOWLensRung::Contributor);
		AsContributor.bOwnerCounts = true;
		TestEqual(TEXT("hidden from the owner as a contributor"), Refused(TEXT("hidden, the owner as a contributor"), World, AsContributor, Hidden),
			FString(TEXT("hidden")));
		FOWGraphOptions Rooms;
		Rooms.bRooms = true;
		TestEqual(TEXT("rooms from the public"), Refused(TEXT("rooms from the public"), World, WgReader(EOWLensRung::Public), Rooms), FString(TEXT("rooms")));
		TestEqual(TEXT("every flag from the public: hidden first"), Refused(TEXT("every flag from the public"), World, WgReader(EOWLensRung::Public), WgEvery()),
			FString(TEXT("hidden")));
		FOWGraphOptions Aids;
		Aids.bReadingAids = true;
		TestEqual(TEXT("reading_aids from the public"), Refused(TEXT("reading_aids from the public"), World, WgReader(EOWLensRung::Public), Aids),
			FString(TEXT("reading_aids")));
		FOWWorldGraph G;
		Read(TEXT("reading_aids from a contributor: taken"), WgReader(EOWLensRung::Contributor), Aids, G);
		TestTrue(TEXT("the Charter back, marked"), WgNodeIds(G).Contains(TEXT("g1[reading_aid]")));

		TestEqual(TEXT("no world"), Refused(TEXT("no world"), nullptr, WgReader(EOWLensRung::Owner), NoFlags), FString(TEXT("no world")));
		FOWGraphOptions Both;
		Both.LensId = TEXT("k1");
		Both.LensJson = TEXT("{}");
		TestEqual(TEXT("a lens id and a body"), Refused(TEXT("a lens id and a body"), World, WgReader(EOWLensRung::Owner), Both),
			FString(TEXT("lens: an id or a body, not both")));
		FOWGraphOptions BadBody;
		BadBody.LensJson = TEXT("{");
		TestTrue(TEXT("a body that does not parse"), Refused(TEXT("a body that does not parse"), World, WgReader(EOWLensRung::Owner), BadBody).StartsWith(TEXT("lens: ")));
		FOWGraphOptions BadMembers;
		BadMembers.MembersJson = TEXT("x");
		TestTrue(TEXT("members that do not parse"), Refused(TEXT("members that do not parse"), World, WgReader(EOWLensRung::Owner), BadMembers).StartsWith(TEXT("members: ")));
		FOWGraphOptions BadClock;
		BadClock.LensId = TEXT("k1");
		BadClock.Clock = TEXT("soon");
		TestEqual(TEXT("a clock that does not read"), Refused(TEXT("a clock that does not read"), World, WgReader(EOWLensRung::Owner), BadClock),
			FString(TEXT("world_graph: clock is an RFC 3339 date-time; got soon")));
	}
	TestEqual(TEXT("DegreeOf an empty graph"), UOWGraph::DegreeOf(FOWWorldGraph()).Num(), 0);
	TestEqual(TEXT("OutDegreeOf an empty graph"), UOWGraph::OutDegreeOf(FOWWorldGraph()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldGraphFixtureTest, "OnlyWorlds.WorldGraph.Fixture", OWTestFlags)

bool FOWWorldGraphFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/lens.fixture.json"));
	if (!TestTrue(TEXT("the lens fixture at ") + Path, OWTestPaths::ReadJson(Path, Fixture))) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;

	int32 Graphs = 0, Degrees = 0, Failed = 0, NamesNotText = 0;
	TArray<FString> Left;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const std::string Kind = WgText(Case.Find("kind"));
		if (Kind != "graph" && Kind != "degree") continue;
		const FString Name = Str(Kind) + TEXT("/") + Str(WgText(Case.Find("name")));
		bool bHeld = true;
		auto Check = [&](bool bCond, const FString& What)
		{
			if (!TestTrue(Name + TEXT(": ") + What, bCond)) bHeld = false;
		};
		auto Same = [&](const OWJsonValue& Got, const OWJsonValue* Want, const FString& What)
		{
			Check(Want && WgEqual(Got, *Want), What + TEXT(": gave ") + Str(WgCompact(Got)) + TEXT(", the case says ")
				+ (Want ? Str(WgCompact(*Want)) : FString(TEXT("nothing"))));
		};
		if (Kind == "graph")
		{
			const OWJsonValue* CaseWorld = Case.Find("world");
			FString Why;
			const UOWWorld* World = CaseWorld ? WgPlace(*CaseWorld, Why) : nullptr;
			FOWLensReader Reader;
			if (!World || !WgReaderOf(Case.Find("reader") ? *Case.Find("reader") : OWJsonValue(), Reader, Why))
			{
				Left.Add(Name + TEXT(" (") + Why + TEXT(")"));
				continue;
			}
			FOWGraphOptions Options;
			if (const OWJsonValue* M = CaseWorld->Find("members"); M && !M->IsNull()) Options.MembersJson = Str(OnlyWorlds::OWJson::Stringify(*M, true));
			if (const OWJsonValue* O = Case.Find("options"); O && O->IsObject())
			{
				if (const OWJsonValue* L = O->Find("lens"))
				{
					if (L->IsString()) Options.LensId = Str(L->Text());
					else Options.LensJson = Str(OnlyWorlds::OWJson::Stringify(*L, true));
				}
				if (const OWJsonValue* A = O->Find("as_of"); A && A->IsNumber())
				{
					Options.bHasAsOf = true;
					Options.AsOf = static_cast<int64>(A->AsDouble());
				}
				Options.Clock = Str(WgText(O->Find("clock")));
				auto Flag = [&](const char* Key)
				{
					const OWJsonValue* V = O->Find(Key);
					return V && V->IsBool() && V->AsBool();
				};
				Options.bRooms = Flag("rooms");
				Options.bHidden = Flag("hidden");
				Options.bProposed = Flag("proposed");
				Options.bMapSpace = Flag("map_space");
				Options.bReadingAids = Flag("reading_aids");
			}
			FOWWorldGraph G;
			FString Reason;
			OWJsonValue Got;
			if (UOWGraph::Read(World, Reader, Options, G, Reason)) Got = WgGraphJson(G);
			else
			{
				// A refusal by a flag's word is the fixture's refused; anything else is a mistake the case never makes.
				Got = OWJsonValue::Object();
				const bool bFlag = Reason == TEXT("hidden") || Reason == TEXT("proposed") || Reason == TEXT("rooms")
					|| Reason == TEXT("map_space") || Reason == TEXT("reading_aids");
				Got.Set(bFlag ? "refused" : "error", OWJsonValue::String(Utf8(Reason)));
			}
			// A name stored as a value that is not a text is no text on the node (bHasName false): the want read so.
			OWJsonValue Want = Case.Find("want") ? *Case.Find("want") : OWJsonValue();
			if (OWJsonValue* Nodes = Want.Find("nodes"); Nodes && Nodes->IsArray())
			{
				for (OWJsonValue& Node : Nodes->Items())
				{
					const OWJsonValue* NodeName = Node.Find("name");
					if (NodeName && !NodeName->IsString() && !NodeName->IsNull())
					{
						Node.Set("name", OWJsonValue::Null());
						++NamesNotText;
					}
				}
			}
			Same(Got, &Want, TEXT("the graph"));
			++Graphs;
		}
		else
		{
			// The case's graph made as a Blueprint holds one (ids, types, edges), the two counts in node order.
			const OWJsonValue* CaseGraph = Case.Find("graph");
			FOWWorldGraph G;
			if (CaseGraph)
			{
				if (const OWJsonValue* Nodes = CaseGraph->Find("nodes"))
				{
					for (const OWJsonValue& N : Nodes->Items())
					{
						FOWGraphNode& Node = G.Nodes.AddDefaulted_GetRef();
						Node.Id = Str(WgText(N.Find("id")));
						Node.Type = Str(WgText(N.Find("type")));
					}
				}
				if (const OWJsonValue* Edges = CaseGraph->Find("edges"))
				{
					for (const OWJsonValue& E : Edges->Items())
					{
						FOWGraphEdge& Edge = G.Edges.AddDefaulted_GetRef();
						Edge.Source = Str(WgText(E.Find("source")));
						Edge.Field = Str(WgText(E.Find("field")));
						Edge.Target = Str(WgText(E.Find("target")));
						const OWJsonValue* At = E.Find("index");
						Edge.Index = At && At->IsNumber() ? static_cast<int64>(At->AsDouble()) : 0;
					}
				}
			}
			const OWJsonValue* Want = Case.Find("want");
			auto Counted = [&](const TArray<FOWGraphDegree>& Rows, const char* Key)
			{
				OWJsonValue Out = OWJsonValue::Object();
				for (const FOWGraphDegree& D : Rows) Out.Set(Utf8(D.Id), OWJsonValue::Number(static_cast<int64_t>(D.Degree)));
				const OWJsonValue* W = Want ? Want->Find(Key) : nullptr;
				Same(Out, W, Str(Key));
				Check(W && W->IsObject() && Out.Keys() == W->Keys(), Str(Key) + TEXT(": the keys in the graph's node order"));
			};
			Counted(UOWGraph::DegreeOf(G), "degree");
			Counted(UOWGraph::OutDegreeOf(G), "out_degree");
			++Degrees;
		}
		if (!bHeld) ++Failed;
	}
	// The fixture as vendored at serial 145: 66 graph and 5 degree cases.
	TestEqual(TEXT("the 66 graph cases carried"), Graphs, 66);
	TestEqual(TEXT("the 5 degree cases carried"), Degrees, 5);
	TestTrue(TEXT("none left out: ") + FString::Join(Left, TEXT("; ")), Left.Num() == 0);
	TestEqual(TEXT("cases failed"), Failed, 0);
	// One node's name is stored as a number ("a node's name is the stored value, null when absent"): no text on the node.
	TestEqual(TEXT("the one name stored as a number, read as no text"), NamesNotText, 1);
	AddInfo(FString::Printf(TEXT("graph fixture: %d graph and %d degree cases through UOWGraph, %d failed"), Graphs, Degrees, Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldGraphReflectionTest, "OnlyWorlds.WorldGraph.Reflection", OWTestFlags)

bool FOWWorldGraphReflectionTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = WgHandWorld();
	auto CallRead = [&](const FString& What, const FOWLensReader& Reader, const FOWGraphOptions& Options)
	{
		FCall Call(*this, UOWGraph::StaticClass(), TEXT("Read"));
		Call.SetWorld(World);
		if (void* R = Call.StructSlot(TEXT("Reader"), FOWLensReader::StaticStruct())) WgMakeReader(*this, R, Reader);
		if (void* O = Call.StructSlot(TEXT("Options"), FOWGraphOptions::StaticStruct())) WgMakeOptions(*this, O, Options);
		Call.Invoke();
		FOWWorldGraph Direct;
		FString DirectReason;
		const bool bDirect = UOWGraph::Read(World, Reader, Options, Direct, DirectReason);
		TestTrue(What + TEXT(": the bool"), Call.ReturnBool() == bDirect);
		TestEqual(What + TEXT(": the reason"), Call.Out<FStrProperty>(TEXT("Reason")), DirectReason);
		const UScriptStruct* S = FOWWorldGraph::StaticStruct();
		const void* Data = Call.OutStruct(TEXT("Graph"), S);
		if (!Data) return Direct;
		// The nodes' ids by property, as a Break node and a For Each read them.
		const FArrayProperty* Nodes = CastField<FArrayProperty>(S->FindPropertyByName(TEXT("Nodes")));
		const FStructProperty* Inner = Nodes ? CastField<FStructProperty>(Nodes->Inner) : nullptr;
		if (TestTrue(What + TEXT(": Nodes, a TArray<FOWGraphNode>"), Inner && Inner->Struct == FOWGraphNode::StaticStruct()))
		{
			FScriptArrayHelper Array(Nodes, Nodes->ContainerPtrToValuePtr<void>(Data));
			TArray<FString> Ids;
			for (int32 I = 0; I < Array.Num(); ++I) Ids.Add(OWBlueprintTest::Member<FStrProperty>(*this, Inner->Struct, Array.GetRawPtr(I), TEXT("Id")));
			TArray<FString> Want;
			for (const FOWGraphNode& N : Direct.Nodes) Want.Add(N.Id);
			TestEqual(What + TEXT(": the node ids by property"), FString::Join(Ids, TEXT(" ")), FString::Join(Want, TEXT(" ")));
		}
		TestTrue(What + TEXT(": the graph whole, as the direct call's"), S->CompareScriptStruct(Data, &Direct, PPF_None));
		return Direct;
	};
	FOWGraphOptions Lens;
	Lens.LensId = TEXT("k1");
	FOWLensReader Guest = WgReader(EOWLensRung::Guest);
	Guest.Whole = { TEXT("c1"), TEXT("k1"), TEXT("e1") };
	Guest.KnownOf = { TEXT("c2"), TEXT("l1") };
	FOWLensReader AsContributor = WgReader(EOWLensRung::Contributor);
	AsContributor.bOwnerCounts = true;
	FOWGraphOptions Hidden;
	Hidden.bHidden = true;
	const FOWWorldGraph Owner = CallRead(TEXT("Read, the owner, every flag"), WgReader(EOWLensRung::Owner), WgEvery());
	CallRead(TEXT("Read, a guest"), Guest, FOWGraphOptions());
	CallRead(TEXT("Read, the owner as a contributor through k1"), AsContributor, Lens);
	CallRead(TEXT("Read, hidden from a contributor: refused"), WgReader(EOWLensRung::Contributor), Hidden);

	// The two counts, the graph handed in as a pin hands it (the struct copied whole into the frame).
	for (const TCHAR* Name : { TEXT("DegreeOf"), TEXT("OutDegreeOf") })
	{
		FCall Call(*this, UOWGraph::StaticClass(), Name);
		if (void* G = Call.StructSlot(TEXT("Graph"), FOWWorldGraph::StaticStruct())) FOWWorldGraph::StaticStruct()->CopyScriptStruct(G, &Owner);
		Call.Invoke();
		const TArray<FOWGraphDegree> Direct = FString(Name) == TEXT("DegreeOf") ? UOWGraph::DegreeOf(Owner) : UOWGraph::OutDegreeOf(Owner);
		TestEqual(FString(Name) + TEXT(" through the graph's path"), WgDegrees(WgCallDegrees(*this, Call)), WgDegrees(Direct));
		TestEqual(FString(Name) + TEXT(": every node"), Direct.Num(), 13);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWWorldGraphSikeliaTest, "OnlyWorlds.WorldGraph.Sikelia", OWTestFlags)

bool FOWWorldGraphSikeliaTest::RunTest(const FString& Parameters)
{
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_SIKELIA"))) return true;
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	// The core over the world as served, read here on its own: every element's body, with its folder's type when it
	// names none; world.json's time_current.
	OWJsonValue Snapshot = OWJsonValue::Object();
	OWJsonValue Bodies = OWJsonValue::Array();
	for (const OnlyWorlds::OWElement& E : World->Native()->elements) Bodies.Push(UOWOpeners::ServedElement(E));
	Snapshot.Set("elements", std::move(Bodies));
	if (const OWJsonValue* Now = World->Native()->info.Find("time_current")) Snapshot.Set("time_current", *Now);
	const OnlyWorlds::OWWorldGraph CoreGraph = OnlyWorlds::OWGraph::WorldGraph(Snapshot, WgParse(R"({"rung": "owner"})"));
	TestTrue(TEXT("the core reads Sikelia: ") + Str(CoreGraph.error), CoreGraph.error.empty() && CoreGraph.refused.empty());

	FOWWorldGraph G;
	FString Reason;
	if (!TestTrue(TEXT("Read, Sikelia's owner: ") + Reason, UOWGraph::Read(World, WgReader(EOWLensRung::Owner), FOWGraphOptions(), G, Reason))) return false;
	TestEqual(TEXT("as many nodes as the core's"), G.Nodes.Num(), static_cast<int32>(CoreGraph.nodes.size()));
	TestEqual(TEXT("as many edges as the core's"), G.Edges.Num(), static_cast<int32>(CoreGraph.edges.size()));
	int32 NodesSame = 0, EdgesSame = 0;
	for (int32 I = 0; I < G.Nodes.Num() && I < static_cast<int32>(CoreGraph.nodes.size()); ++I)
	{
		const OnlyWorlds::OWGraphNode& C = CoreGraph.nodes[I];
		const FOWGraphNode& N = G.Nodes[I];
		const std::string CoreName = C.name.IsString() ? C.name.Text() : std::string();
		if (Utf8(N.Id) == C.id && Utf8(N.Type) == C.type && Utf8(N.Name) == CoreName && N.bHasName == C.name.IsString()
			&& Utf8(N.Family) == C.family && Utf8(N.Icon) == C.icon && N.Marks.Num() == static_cast<int32>(C.marks.size()))
			++NodesSame;
	}
	for (int32 I = 0; I < G.Edges.Num() && I < static_cast<int32>(CoreGraph.edges.size()); ++I)
	{
		const OnlyWorlds::OWGraphEdge& C = CoreGraph.edges[I];
		const FOWGraphEdge& E = G.Edges[I];
		if (Utf8(E.Source) == C.source && Utf8(E.Field) == C.field && Utf8(E.Target) == C.target && E.Index == C.index) ++EdgesSame;
	}
	TestEqual(TEXT("every node, the core's, in its order"), NodesSame, G.Nodes.Num());
	TestEqual(TEXT("every edge, the core's, in its order"), EdgesSame, G.Edges.Num());
	TestTrue(TEXT("the cut and the edges cut, the core's"), WgCompact(WgCounts(G.Cut)) == WgCompact(WgCoreCounts(CoreGraph.cut)) && WgCompact(WgCounts(G.EdgesCut)) == WgCompact(WgCoreCounts(CoreGraph.edges_cut)));
	AddInfo(FString::Printf(TEXT("Sikelia's graph for the owner: %d nodes, %d edges"), G.Nodes.Num(), G.Edges.Num()));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
