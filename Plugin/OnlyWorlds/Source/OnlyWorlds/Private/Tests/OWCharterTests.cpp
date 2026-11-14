// The charter profile through UOWCharter: a world built here in memory (two marked Charters, the newer leading; a
// Guide naming its Domains out of name order; a Domain that names no type, one hidden, one unmarked and so lore), read
// by every rung, open and closed; a front door's packet as GET /me serves it (keel's shape, the strand beside it, as
// tests/test_front_door.cpp reads it) and each refusal; then every case of the profile's fixture
// (codegen/schema-dist/profiles/charter.fixture.json), each case's world placed in a world of its own (its hidden list
// as hidden: true on each listed body, its open as bOpen), the packet compared whole; then every function called as a
// Blueprint calls it (OWBlueprintCall.h).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"

#include "OWCharterLibrary.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWBlueprintTest::FCall;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue ChParse(const std::string& Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	std::string ChText(const OWJsonValue* V) { return V && V->IsString() ? V->Text() : std::string(); }

	std::string ChCompact(const OWJsonValue& V)
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

	bool ChEqual(const OWJsonValue& A, const OWJsonValue& B)
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
				if (!ChEqual(A.Items()[I], B.Items()[I])) return false;
			}
			return true;
		case OWJsonValue::Kind::Object:
			if (A.Size() != B.Size()) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				const OWJsonValue* Other = B.Find(A.Keys()[I]);
				if (!Other || !ChEqual(A.Items()[I], *Other)) return false;
			}
			return true;
		}
		return false;
	}

	OWJsonValue ChStrings(const TArray<FString>& Ids)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& Id : Ids) Out.Push(OWJsonValue::String(Utf8(Id)));
		return Out;
	}

	OWJsonValue ChDomainsJson(const TArray<FOWCharterDomain>& Domains)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FOWCharterDomain& D : Domains)
		{
			OWJsonValue Domain = OWJsonValue::Object();
			Domain.Set("id", OWJsonValue::String(Utf8(D.Id)));
			Domain.Set("name", OWJsonValue::String(Utf8(D.Name)));
			Domain.Set("sentence", OWJsonValue::String(Utf8(D.Sentence)));
			OWJsonValue Hubs = OWJsonValue::Array();
			for (const FOWCharterHub& H : D.Hubs)
			{
				OWJsonValue Hub = OWJsonValue::Object();
				Hub.Set("type", OWJsonValue::String(Utf8(H.Type)));
				Hub.Set("id", OWJsonValue::String(Utf8(H.Id)));
				Hubs.Push(std::move(Hub));
			}
			Domain.Set("hubs", std::move(Hubs));
			Out.Push(std::move(Domain));
		}
		return Out;
	}

	// The packet read back to the profile's shape: {charter, guide, domains}, an absent aid null, the rules through
	// their JSON text.
	OWJsonValue ChAidsJson(const FOWReadingAids& A)
	{
		OWJsonValue Out = OWJsonValue::Object();
		if (A.bHasCharter)
		{
			OWJsonValue Law = OWJsonValue::Object();
			Law.Set("id", OWJsonValue::String(Utf8(A.Charter.Id)));
			Law.Set("name", OWJsonValue::String(Utf8(A.Charter.Name)));
			Law.Set("rules", ChParse(Utf8(A.Charter.RulesJson)));
			Law.Set("skill", ChStrings(A.Charter.Skill));
			Out.Set("charter", std::move(Law));
		}
		else
		{
			Out.Set("charter", OWJsonValue::Null());
		}
		if (A.bHasGuide)
		{
			OWJsonValue Guide = OWJsonValue::Object();
			Guide.Set("id", OWJsonValue::String(Utf8(A.Guide.Id)));
			Guide.Set("name", OWJsonValue::String(Utf8(A.Guide.Name)));
			Out.Set("guide", std::move(Guide));
		}
		else
		{
			Out.Set("guide", OWJsonValue::Null());
		}
		Out.Set("domains", ChDomainsJson(A.Domains));
		return Out;
	}

	FString ChDomainIds(const TArray<FOWCharterDomain>& Domains)
	{
		TArray<FString> Ids;
		for (const FOWCharterDomain& D : Domains) Ids.Add(D.Id);
		return FString::Join(Ids, TEXT(" "));
	}

	// -- worlds made here ---------------------------------------------------------------------------------------------

	UOWWorld* ChMakeWorld(std::vector<std::pair<std::string, OWJsonValue>> Bodies)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f000"));
		for (std::pair<std::string, OWJsonValue>& B : Bodies) Native->Add(B.first, std::move(B.second));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}

	std::string ChFolderOf(const OWJsonValue& Body)
	{
		const OWJsonValue* T = Body.IsObject() ? Body.Find("type") : nullptr;
		return T && T->IsString() ? T->Text() : std::string();
	}

	UOWWorld* ChPlace(const OWJsonValue& Fixture, FString& Why)
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
			if (Hidden.count(ChText(B.Find("id")))) B.Set("hidden", OWJsonValue::Bool(true));
			Bodies.emplace_back(ChFolderOf(B), std::move(B));
		}
		return ChMakeWorld(std::move(Bodies));
	}

	bool ChReaderOf(const OWJsonValue& R, FOWLensReader& Out, FString& Why)
	{
		if (!R.IsObject())
		{
			Why = TEXT("a reader that is not an object");
			return false;
		}
		const std::string Rung = ChText(R.Find("rung"));
		const OWJsonValue* As = R.Find("as");
		if (As && !As->IsNull())
		{
			if (Rung != "owner" || !As->IsObject() || (As->Find("as") && !As->Find("as")->IsNull()))
			{
				Why = TEXT("an as the struct cannot hold");
				return false;
			}
			if (!ChReaderOf(*As, Out, Why)) return false;
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
		Out.Character = Str(ChText(C));
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

	OWJsonValue ChReaderByName(const OWJsonValue& World, const std::string& Name)
	{
		const OWJsonValue* Readers = World.Find("readers");
		const OWJsonValue* R = Readers ? Readers->Find(Name) : nullptr;
		if (!R) return OWJsonValue::Null();
		OWJsonValue Out = *R;
		if (const OWJsonValue* As = R->Find("as"); As && As->IsString()) Out.Set("as", ChReaderByName(World, As->Text()));
		return Out;
	}

	const TCHAR* const ChLedger = TEXT("019a7f00-0000-7000-8000-0000000000b1");

	// g1 and g2 both marked, g2 the newer (its supertype trimmed and folded); u1 names d2, d1 and the lore d3; d2 names
	// no type (its folder's: construct); d4 hidden; the Ledger and the Harbour the hubs. The Charter's description names
	// the Ledger twice and an Object the world does not hold.
	UOWWorld* ChHandWorld()
	{
		const char* const Bodies[][2] = {
			{ "law", R"({"id": "g1", "type": "law", "supertype": "Charter", "name": "Old rules", "x_charter_of": "w", "created_at": "2026-10-01T09:00:00Z", "description": "Old."})" },
			{ "law", R"({"id": "g2", "type": "law", "supertype": " charter ", "name": "House rules", "x_charter_of": "w", "created_at": "2026-10-02T09:00:00Z", "description": "Read ow://object/019a7f00-0000-7000-8000-0000000000b1 and 019a7f00-0000-7000-8000-0000000000b9 first, then 019a7f00-0000-7000-8000-0000000000b1 again.", "x_checks_rules": [{"rule": "every Character has a name"}]})" },
			{ "construct", R"({"id": "u1", "type": "construct", "supertype": "Guide", "name": "Start here", "x_charter_of": "w", "constructs": ["d2", "d1", "d3"]})" },
			{ "construct", R"({"id": "d1", "type": "construct", "supertype": "Domain", "name": "the Archive", "x_charter_of": "w", "description": "  Ledgers and letters.\nMore below.", "objects": ["019a7f00-0000-7000-8000-0000000000b1"], "locations": ["l1"]})" },
			{ "construct", R"({"id": "d2", "name": "Docks", "supertype": "Domain", "x_charter_of": "w", "description": "Where the ships come in.", "locations": ["l1"]})" },
			{ "construct", R"({"id": "d3", "type": "construct", "supertype": "Domain", "name": "Lore", "description": "No mark."})" },
			{ "construct", R"({"id": "d4", "type": "construct", "supertype": "Domain", "name": "Attic", "x_charter_of": "w", "description": "Hidden.", "hidden": true})" },
			{ "object", R"({"id": "019a7f00-0000-7000-8000-0000000000b1", "type": "object", "name": "Ledger"})" },
			{ "location", R"({"id": "l1", "type": "location", "name": "Harbour"})" },
		};
		std::vector<std::pair<std::string, OWJsonValue>> B;
		for (const auto& Row : Bodies) B.emplace_back(Row[0], ChParse(Row[1]));
		return ChMakeWorld(std::move(B));
	}

	FOWLensReader ChReader(EOWLensRung Rung)
	{
		FOWLensReader R;
		R.Rung = Rung;
		return R;
	}

	// A GET /me answer in keel's shape (tests/test_front_door.cpp's), its packet carrying `Strand` (nullptr: no key).
	FString ChMe(const char* Strand)
	{
		std::string Aids = R"({"charter": {"id": "law-1", "name": "House rules", "rules": [{"rule": "no-orphans"}], "skill": ["obj-1"]},
			"guide": {"id": "con-g", "name": "Start here"},
			"domains": [{"id": "con-d1", "name": "The Harbour", "sentence": "Where the ships come in.", "hubs": [{"type": "location", "id": "loc-1"}]}])";
		if (Strand) Aids += std::string(R"(, "strand": )") + Strand;
		Aids += "}";
		return Str(R"({"world": {"id": "w1", "name": "W"}, "kind": "owner", "front_door": {"charter": "law-1"}, "reading_aids": )" + Aids + "}");
	}

	const char* const ChStrand = R"({"narrative": {"id": "n-1", "name": "The Crossing"}, "event": null,
		"location": {"id": "loc-1", "name": "The River"}, "character": {"id": "c-1", "name": "The Judge"},
		"now": {"id": "loc-1", "name": "The River"}, "same": true})";

	// -- the calls, each as a graph makes it ------------------------------------------------------------------------

	void ChMakeReader(FAutomationTestBase& Test, void* Data, const FOWLensReader& R)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* S = FOWLensReader::StaticStruct();
		SetEnumMember(Test, S, Data, TEXT("Rung"), StaticEnum<EOWLensRung>(), static_cast<int64>(R.Rung));
		SetMember<FStrProperty>(Test, S, Data, TEXT("Character"), R.Character);
		for (const TCHAR* Name : { TEXT("Whole"), TEXT("KnownOf") })
		{
			const TArray<FString>& Values = FString(Name) == TEXT("Whole") ? R.Whole : R.KnownOf;
			const FArrayProperty* P = CastField<FArrayProperty>(S->FindPropertyByName(Name));
			if (!P || !CastField<FStrProperty>(P->Inner))
			{
				Test.AddError(FString::Printf(TEXT("FOWLensReader has no TArray<FString> member %s"), Name));
				continue;
			}
			FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Data));
			Array.EmptyAndAddValues(Values.Num());
			for (int32 I = 0; I < Values.Num(); ++I) *reinterpret_cast<FString*>(Array.GetRawPtr(I)) = Values[I];
		}
		SetBoolMember(Test, S, Data, TEXT("bOwnerCounts"), R.bOwnerCounts);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCharterWorldTest, "OnlyWorlds.Charter.World", OWTestFlags)

bool FOWCharterWorldTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = ChHandWorld();

	// The pair and the mark: the lore Domain is a Domain's pair without the mark.
	for (const TCHAR* Id : { TEXT("g1"), TEXT("g2"), TEXT("u1"), TEXT("d1"), TEXT("d2"), TEXT("d4") })
		TestTrue(FString::Printf(TEXT("IsCharterElement %s"), Id), UOWCharter::IsCharterElement(World, Id));
	for (const TCHAR* Id : { TEXT("d3"), ChLedger, TEXT("l1"), TEXT("nope"), TEXT("") })
		TestFalse(FString::Printf(TEXT("IsCharterElement %s"), Id), UOWCharter::IsCharterElement(World, Id));
	{
		FString Aid;
		TestTrue(TEXT("ReadingAidOf g2: charter"), UOWCharter::ReadingAidOf(World, TEXT("g2"), Aid) && Aid == TEXT("charter"));
		TestTrue(TEXT("ReadingAidOf u1: guide"), UOWCharter::ReadingAidOf(World, TEXT("u1"), Aid) && Aid == TEXT("guide"));
		TestTrue(TEXT("ReadingAidOf d2: domain, its type its folder's"), UOWCharter::ReadingAidOf(World, TEXT("d2"), Aid) && Aid == TEXT("domain"));
		TestTrue(TEXT("ReadingAidOf d3: domain, the mark ignored"), UOWCharter::ReadingAidOf(World, TEXT("d3"), Aid) && Aid == TEXT("domain"));
		TestTrue(TEXT("ReadingAidOf the Ledger: none"), !UOWCharter::ReadingAidOf(World, ChLedger, Aid) && Aid.IsEmpty());
		TestFalse(TEXT("ReadingAidOf nope"), UOWCharter::ReadingAidOf(World, TEXT("nope"), Aid));
	}

	auto Read = [&](const FString& What, const FOWLensReader& Reader, bool bOpen, FOWReadingAids& A)
	{
		FString Reason;
		const bool bOk = UOWCharter::CharterOf(World, Reader, bOpen, A, Reason);
		TestTrue(What + TEXT(": answered, got ") + Reason, bOk && Reason.IsEmpty());
	};
	{
		// The owner: the newer Charter whole, its skill the Ledger once (the Object the world lacks left out); the Guide;
		// the Domains in the Guide's order (the lore skipped), then the hidden Attic, which the owner sees.
		FOWReadingAids A;
		Read(TEXT("the owner"), ChReader(EOWLensRung::Owner), false, A);
		TestTrue(TEXT("the Charter: g2, House rules"), A.bHasCharter && A.Charter.Id == TEXT("g2") && A.Charter.Name == TEXT("House rules"));
		TestTrue(TEXT("its skill: the Ledger once"), A.Charter.Skill.Num() == 1 && A.Charter.Skill[0].Equals(ChLedger, ESearchCase::CaseSensitive));
		TestTrue(TEXT("its rules as stored"), ChEqual(ChParse(Utf8(A.Charter.RulesJson)), ChParse(R"([{"rule": "every Character has a name"}])")));
		TestTrue(TEXT("the Guide: u1"), A.bHasGuide && A.Guide.Id == TEXT("u1") && A.Guide.Name == TEXT("Start here"));
		TestEqual(TEXT("the Domains"), ChDomainIds(A.Domains), FString(TEXT("d2 d1 d4")));
		if (A.Domains.Num() == 3)
		{
			const FOWCharterDomain& Archive = A.Domains[1];
			TestEqual(TEXT("the Archive's sentence: its first line, trimmed"), Archive.Sentence, FString(TEXT("Ledgers and letters.")));
			TestTrue(TEXT("the Archive's hubs: the Ledger, the Harbour"), Archive.Hubs.Num() == 2 && Archive.Hubs[0].Type == TEXT("object")
				&& Archive.Hubs[0].Id.Equals(ChLedger, ESearchCase::CaseSensitive) && Archive.Hubs[1].Type == TEXT("location") && Archive.Hubs[1].Id == TEXT("l1"));
			TestEqual(TEXT("the Attic has no hub"), A.Domains[2].Hubs.Num(), 0);
		}
		TestTrue(TEXT("CharterOf reads no strand"), !A.bHasStrand && A.StrandJson.IsEmpty());
	}
	{
		FOWReadingAids A;
		Read(TEXT("a contributor"), ChReader(EOWLensRung::Contributor), false, A);
		TestEqual(TEXT("a contributor's Domains: the Attic hidden"), ChDomainIds(A.Domains), FString(TEXT("d2 d1")));
		TestTrue(TEXT("the same Charter"), A.bHasCharter && A.Charter.Id == TEXT("g2"));
		Read(TEXT("the public, a closed world"), ChReader(EOWLensRung::Public), false, A);
		TestTrue(TEXT("nothing for the public of a closed world"), !A.bHasCharter && !A.bHasGuide && A.Domains.Num() == 0);
		Read(TEXT("the public, an open world"), ChReader(EOWLensRung::Public), true, A);
		TestTrue(TEXT("the public of an open world reads the Charter"), A.bHasCharter && A.Charter.Id == TEXT("g2"));
		TestEqual(TEXT("and the Domains it sees"), ChDomainIds(A.Domains), FString(TEXT("d2 d1")));
	}
	{
		// A guest with the Charter, the Guide and the Ledger whole, told of the Archive and the Attic: the Archive as a
		// stub (no hubs), the Attic hidden and so not there.
		FOWLensReader Guest = ChReader(EOWLensRung::Guest);
		Guest.Whole = { TEXT("g2"), TEXT("u1"), ChLedger };
		Guest.KnownOf = { TEXT("d1"), TEXT("d4") };
		FOWReadingAids A;
		Read(TEXT("a guest"), Guest, false, A);
		TestEqual(TEXT("a guest's Domains"), ChDomainIds(A.Domains), FString(TEXT("d1")));
		TestTrue(TEXT("the Archive a stub: its name, no hubs"), A.Domains.Num() == 1 && A.Domains[0].Name == TEXT("the Archive") && A.Domains[0].Hubs.Num() == 0);
	}
	{
		TArray<FOWCharterDomain> Domains;
		FString Reason;
		TestTrue(TEXT("DomainsOf the owner"), UOWCharter::DomainsOf(World, ChReader(EOWLensRung::Owner), false, Domains, Reason));
		TestEqual(TEXT("its Domains, CharterOf's"), ChDomainIds(Domains), FString(TEXT("d2 d1 d4")));
		FOWReadingAids A;
		TestTrue(TEXT("CharterOf, no world: refused"), !UOWCharter::CharterOf(nullptr, ChReader(EOWLensRung::Owner), false, A, Reason) && Reason == TEXT("no world"));
		TestTrue(TEXT("DomainsOf, no world: refused"), !UOWCharter::DomainsOf(nullptr, ChReader(EOWLensRung::Owner), false, Domains, Reason)
			&& Reason == TEXT("no world") && Domains.Num() == 0);
	}

	// A front door's packet, read as served.
	{
		FOWReadingAids A;
		FString Reason;
		TestTrue(TEXT("ReadingAidsOf GET /me with a strand"), UOWCharter::ReadingAidsOf(ChMe(ChStrand), A, Reason));
		TestTrue(TEXT("the Charter as served"), A.bHasCharter && A.Charter.Id == TEXT("law-1") && A.Charter.Skill.Num() == 1 && A.Charter.Skill[0] == TEXT("obj-1")
			&& ChEqual(ChParse(Utf8(A.Charter.RulesJson)), ChParse(R"([{"rule": "no-orphans"}])")));
		TestTrue(TEXT("the Guide as served"), A.bHasGuide && A.Guide.Id == TEXT("con-g"));
		TestTrue(TEXT("the Domain as served"), A.Domains.Num() == 1 && A.Domains[0].Sentence == TEXT("Where the ships come in.")
			&& A.Domains[0].Hubs.Num() == 1 && A.Domains[0].Hubs[0].Id == TEXT("loc-1"));
		TestTrue(TEXT("the strand as served"), A.bHasStrand && ChEqual(ChParse(Utf8(A.StrandJson)), ChParse(ChStrand)));
		TestTrue(TEXT("ReadingAidsOf a packet from before the strand"), UOWCharter::ReadingAidsOf(ChMe(nullptr), A, Reason) && A.bHasCharter && !A.bHasStrand
			&& A.StrandJson.IsEmpty());
		TestTrue(TEXT("ReadingAidsOf a Charter served with rules null"), UOWCharter::ReadingAidsOf(TEXT("{\"reading_aids\": {\"charter\": {\"id\": \"law-1\", "
			"\"name\": \"House rules\", \"rules\": null, \"skill\": []}, \"guide\": null, \"domains\": []}}"), A, Reason)
			&& A.bHasCharter && ChEqual(ChParse(Utf8(A.Charter.RulesJson)), OWJsonValue::Array()) && !A.bHasGuide);
		TestTrue(TEXT("ReadingAidsOf a player's join answer: no packet, got ") + Reason,
			!UOWCharter::ReadingAidsOf(TEXT("{\"kind\": \"player\"}"), A, Reason) && Reason == TEXT("no reading aids in the answer") && !A.bHasCharter);
		TestTrue(TEXT("ReadingAidsOf text that does not parse, got ") + Reason, !UOWCharter::ReadingAidsOf(TEXT("{"), A, Reason) && Reason.StartsWith(TEXT("answer: ")));
		TestTrue(TEXT("ReadingAidsOf \"\": no packet"), !UOWCharter::ReadingAidsOf(TEXT(""), A, Reason) && Reason == TEXT("no reading aids in the answer"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCharterFixtureTest, "OnlyWorlds.Charter.Fixture", OWTestFlags)

bool FOWCharterFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/charter.fixture.json"));
	if (!TestTrue(TEXT("the charter fixture at ") + Path, OWTestPaths::ReadJson(Path, Fixture))) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	const OWJsonValue* Worlds = Fixture.Find("worlds");
	if (!TestTrue(TEXT("the fixture holds cases and worlds"), Cases && Cases->IsArray() && Worlds && Worlds->IsObject())) return false;
	int32 Elements = 0, Packets = 0, Failed = 0;
	TArray<FString> Left;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const std::string Kind = ChText(Case.Find("kind"));
		const FString Name = Str(Kind) + TEXT("/") + Str(ChText(Case.Find("name")));
		bool bHeld = true;
		auto Same = [&](const OWJsonValue& Got, const OWJsonValue* Want, const FString& What)
		{
			if (!TestTrue(Name + TEXT(": ") + What + TEXT(": gave ") + Str(ChCompact(Got)) + TEXT(", the case says ")
				+ (Want ? Str(ChCompact(*Want)) : FString(TEXT("nothing"))), Want && ChEqual(Got, *Want))) bHeld = false;
		};
		if (Kind == "element")
		{
			OWJsonValue Body = Case.Find("element") ? *Case.Find("element") : OWJsonValue();
			if (!Body.IsObject())
			{
				Left.Add(Name + TEXT(" (its element is not an object)"));
				continue;
			}
			if (!Body.Find("id")) Body.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f001"));
			const FString Id = Str(ChText(Body.Find("id")));
			std::vector<std::pair<std::string, OWJsonValue>> Bodies;
			Bodies.emplace_back(ChFolderOf(Body), std::move(Body));
			const UOWWorld* World = ChMakeWorld(std::move(Bodies));
			Same(OWJsonValue::Bool(UOWCharter::IsCharterElement(World, Id)), Case.Find("is_charter_element"), TEXT("is_charter_element"));
			++Elements;
		}
		else if (Kind == "charter")
		{
			// A case's world: one the fixture names, or one of its own.
			const OWJsonValue* W = Case.Find("world");
			const OWJsonValue* CaseWorld = W && W->IsObject() ? W : (W && W->IsString() ? Worlds->Find(W->Text()) : nullptr);
			FString Why;
			const UOWWorld* World = CaseWorld ? ChPlace(*CaseWorld, Why) : nullptr;
			FOWLensReader Reader;
			if (!World || !ChReaderOf(ChReaderByName(*CaseWorld, ChText(Case.Find("reader"))), Reader, Why))
			{
				Left.Add(Name + TEXT(" (") + Why + TEXT(")"));
				continue;
			}
			const OWJsonValue* Open = CaseWorld->Find("open");
			const bool bOpen = Open && Open->IsBool() && Open->AsBool();
			FOWReadingAids A;
			FString Reason;
			if (!TestTrue(Name + TEXT(": answered, got ") + Reason, UOWCharter::CharterOf(World, Reader, bOpen, A, Reason))) bHeld = false;
			const OWJsonValue* Want = Case.Find("want");
			Same(ChAidsJson(A), Want, TEXT("the packet"));
			TArray<FOWCharterDomain> Domains;
			UOWCharter::DomainsOf(World, Reader, bOpen, Domains, Reason);
			Same(ChDomainsJson(Domains), Want ? Want->Find("domains") : nullptr, TEXT("domainsOf"));
			++Packets;
		}
		else
		{
			Left.Add(Name + TEXT(" (a kind this test does not know)"));
			continue;
		}
		if (!bHeld) ++Failed;
	}
	// The fixture as vendored at dist.126: 6 element cases and 35 charter cases.
	TestEqual(TEXT("the 6 element cases carried"), Elements, 6);
	TestEqual(TEXT("the 35 charter cases carried"), Packets, 35);
	TestTrue(TEXT("none left out: ") + FString::Join(Left, TEXT("; ")), Left.Num() == 0);
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("charter fixture: %d cases through UOWCharter, %d failed"), Elements + Packets, Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWCharterReflectionTest, "OnlyWorlds.Charter.Reflection", OWTestFlags)

bool FOWCharterReflectionTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = ChHandWorld();
	FOWLensReader Guest = ChReader(EOWLensRung::Guest);
	Guest.Whole = { TEXT("g2"), TEXT("u1"), ChLedger };
	Guest.KnownOf = { TEXT("d1") };
	const FOWLensReader Readers[] = { ChReader(EOWLensRung::Owner), ChReader(EOWLensRung::Public), Guest };

	for (const FOWLensReader& Reader : Readers)
	{
		for (const bool bOpen : { false, true })
		{
			const FString What = FString::Printf(TEXT("rung %d, open %d"), static_cast<int32>(Reader.Rung), bOpen ? 1 : 0);
			FOWReadingAids Direct;
			FString DirectReason;
			UOWCharter::CharterOf(World, Reader, bOpen, Direct, DirectReason);
			{
				FCall Call(*this, UOWCharter::StaticClass(), TEXT("CharterOf"));
				Call.SetWorld(World);
				if (void* R = Call.StructSlot(TEXT("Reader"), FOWLensReader::StaticStruct())) ChMakeReader(*this, R, Reader);
				Call.SetBool(TEXT("bOpen"), bOpen);
				Call.Invoke();
				TestTrue(What + TEXT(": CharterOf answered"), Call.ReturnBool());
				const UScriptStruct* S = FOWReadingAids::StaticStruct();
				const void* Data = Call.OutStruct(TEXT("ReadingAids"), S);
				if (Data)
				{
					TestTrue(What + TEXT(": bHasCharter by property"), OWBlueprintTest::BoolMember(*this, S, Data, TEXT("bHasCharter")) == Direct.bHasCharter);
					TestTrue(What + TEXT(": the packet whole, as the direct call's"), S->CompareScriptStruct(Data, &Direct, PPF_None));
				}
			}
			{
				FCall Call(*this, UOWCharter::StaticClass(), TEXT("DomainsOf"));
				Call.SetWorld(World);
				if (void* R = Call.StructSlot(TEXT("Reader"), FOWLensReader::StaticStruct())) ChMakeReader(*this, R, Reader);
				Call.SetBool(TEXT("bOpen"), bOpen);
				Call.Invoke();
				const FArrayProperty* P = CastField<FArrayProperty>(Call.Property(TEXT("Domains")));
				const FStructProperty* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
				if (TestTrue(What + TEXT(": DomainsOf's Domains, an out TArray<FOWCharterDomain>"),
					Inner && Inner->Struct == FOWCharterDomain::StaticStruct() && OWBlueprintTest::IsOutputPin(P) && Call.GetFrame()))
				{
					FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Call.GetFrame()));
					TArray<FString> Ids;
					for (int32 I = 0; I < Array.Num(); ++I) Ids.Add(OWBlueprintTest::Member<FStrProperty>(*this, Inner->Struct, Array.GetRawPtr(I), TEXT("Id")));
					TestEqual(What + TEXT(": the Domains by property"), FString::Join(Ids, TEXT(" ")), ChDomainIds(Direct.Domains));
				}
			}
		}
	}
	{
		FCall Call(*this, UOWCharter::StaticClass(), TEXT("ReadingAidsOf"));
		Call.SetString(TEXT("AnswerJson"), ChMe(ChStrand));
		Call.Invoke();
		FOWReadingAids Direct;
		FString Reason;
		UOWCharter::ReadingAidsOf(ChMe(ChStrand), Direct, Reason);
		const void* Data = Call.OutStruct(TEXT("ReadingAids"), FOWReadingAids::StaticStruct());
		TestTrue(TEXT("ReadingAidsOf through the graph's path"), Call.ReturnBool() && Data
			&& FOWReadingAids::StaticStruct()->CompareScriptStruct(Data, &Direct, PPF_None)
			&& OWBlueprintTest::BoolMember(*this, FOWReadingAids::StaticStruct(), Data, TEXT("bHasStrand")));
	}
	{
		FCall Call(*this, UOWCharter::StaticClass(), TEXT("IsCharterElement"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), TEXT("d2"));
		Call.Invoke();
		TestTrue(TEXT("IsCharterElement d2 through the graph's path"), Call.ReturnBool());
	}
	{
		FCall Call(*this, UOWCharter::StaticClass(), TEXT("ReadingAidOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), TEXT("d3"));
		Call.Invoke();
		TestTrue(TEXT("ReadingAidOf d3 through the graph's path: domain"), Call.ReturnBool() && Call.Out<FStrProperty>(TEXT("Aid")) == TEXT("domain"));
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
