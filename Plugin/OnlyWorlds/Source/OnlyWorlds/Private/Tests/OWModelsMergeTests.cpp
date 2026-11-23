// A struct's JSON merged into its element as read, so a write keeps what a struct cannot hold
// (UOWElementJson::MergeElementJson, OWElementJsonLibrary.h). Six tests:
//
// - RoundTrip: every element of Sikelia and Moppetopia, its JSON as read (UOWWorld::GetElementJson), read through
//   UOWElements::Find<Type>, unchanged, written through Get<Type>Json and merged into the JSON as read, each called
//   as a Blueprint calls it (OWBlueprintCall.h): the merge's JSON equals the JSON as read, as a value and as text. The
//   elements and the extension keys kept are counted and logged, not asserted (the worlds move).
// - Changes: on one Character as read, a changed integer and link list land in place, a cleared string lands as "",
//   a cleared link and integer as null, a field the read lacks lands at the end, an unset the read lacks stays absent,
//   and every other key (extensions, stamps, type, world, a number's spelling, a null string) is kept in its place.
// - Ids: the read's id wins over the struct's empty one; the same id is fine; two ids are a refusal; a read without an
//   id takes the struct's; an id that is not a string is a refusal.
// - Refusals: text that is not JSON, or not an object, on either side, and empty text: false, the side named in
//   Reason, Merged "".
// - Upsert: the merge's text through UpsertElement on the fake transport: the body sent carries the extensions and none
//   of the six read-only keys (the core's Sanitize), and is the merge's JSON so cleaned; the door's JSON alone carries
//   no extension (the trap the merge closes). A known-of stub's merge keeps known_of, so the upsert is refused and
//   nothing is sent; a served-trimmed mark is kept for keel to refuse by name.
// - Function: MergeElementJson's pins: two string inputs, two string outputs, a bool answer, pure and static.
//
// Every helper is in the namespace OWModelsMergeTest, never at file scope (C4459, an error here: CppCompileWarnings.cs).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "OWBlueprintCall.h"
#include "OWClient.h"
#include "OWClientNodes.h"
#include "OWConvert.h"
#include "OWElementJsonLibrary.h"
#include "OWFakeTransport.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWModelsJson.h"
#include "OWTestPaths.h"
#include "OWTransport.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <memory>
#include <string>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace OWModelsMergeTest
{
	using OnlyWorlds::OWJsonValue;

	constexpr EAutomationTestFlags MergeFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// The six keys the core's Sanitize erases before a write is sent (OWClient::ReadOnlyFields).
	const char* const ReadOnlySix[] = { "world", "type", "created_at", "updated_at", "change_seq", "known_of" };

	OWJsonValue ParseText(const FString& Text, bool& bOk)
	{
		const std::string Utf8 = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Utf8));
		bOk = Parsed.ok;
		return Parsed.ok ? Parsed.value : OWJsonValue();
	}

	OWJsonValue ParseOrFail(FAutomationTestBase& Test, const FString& Text, const TCHAR* What)
	{
		bool bOk = false;
		OWJsonValue Out = ParseText(Text, bOk);
		if (!bOk || !Out.IsObject()) Test.AddError(FString::Printf(TEXT("%s is not a JSON object: %s"), What, *Text.Left(200)));
		return Out;
	}

	bool IsExtensionKey(const std::string& Key)
	{
		return Key.rfind("x_", 0) == 0 || Key.rfind("atlas_", 0) == 0 || Key.rfind("shadow_", 0) == 0;
	}

	// Equal as values with each number by its canonical spelling: what a body written compact (numbers as
	// JSON.stringify writes them) is to the value it was written from.
	bool SameOnTheWire(const OWJsonValue& A, const OWJsonValue& B)
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
			if (A.Size() != B.Size() || (A.IsObject() && A.Keys() != B.Keys())) return false;
			for (size_t I = 0; I < A.Size(); ++I)
			{
				if (!SameOnTheWire(A.Items()[I], B.Items()[I])) return false;
			}
			return true;
		}
		return false;
	}

	FString TypePascal(const FString& Slug) { return Slug.Left(1).ToUpper() + Slug.Mid(1); }

	// A door's answer, the return property of a pure call (its class asserted).
	FString ReturnText(FAutomationTestBase& Test, OWBlueprintTest::FCall& Call)
	{
		const FStrProperty* Ret = CastField<FStrProperty>(Call.Return());
		if (!Ret || !Call.GetFrame())
		{
			Test.AddError(FString::Printf(TEXT("%s does not return a string"), Call.GetName()));
			return FString();
		}
		return Ret->GetPropertyValue_InContainer(Call.GetFrame());
	}

	// A struct's door called as a Blueprint calls it: the struct copied into the door's parameter, its text answered.
	FString DoorText(FAutomationTestBase& Test, const FString& Pas, const UScriptStruct* Struct, const void* Data)
	{
		OWBlueprintTest::FCall Door(Test, UOWElements::StaticClass(), *(TEXT("Get") + Pas + TEXT("Json")));
		void* In = Door.StructSlot(*Pas, Struct);
		if (!In || !Data) return FString();
		Struct->CopyScriptStruct(In, Data);
		Door.Invoke();
		return ReturnText(Test, Door);
	}

	FString CharacterDoor(FAutomationTestBase& Test, const FOWCharacter& Character)
	{
		return DoorText(Test, TEXT("Character"), FOWCharacter::StaticStruct(), &Character);
	}

	// The Character a Find node would give for this body (OWFromJson, the reading Find<Type> runs).
	FOWCharacter CharacterOf(FAutomationTestBase& Test, const FString& BodyText)
	{
		FOWCharacter Out;
		OWFromJson(ParseOrFail(Test, BodyText, TEXT("the body")), Out);
		return Out;
	}

	struct FMergeAnswer
	{
		bool bOk = false;
		FString Merged;
		FString Reason;
	};

	// MergeElementJson called as a Blueprint calls it.
	FMergeAnswer MergeCall(FAutomationTestBase& Test, const FString& ElementJson, const FString& StructJson)
	{
		OWBlueprintTest::FCall Call(Test, UOWElementJson::StaticClass(), TEXT("MergeElementJson"));
		Call.SetString(TEXT("ElementJson"), ElementJson);
		Call.SetString(TEXT("StructJson"), StructJson);
		Call.Invoke();
		FMergeAnswer Answer;
		Answer.bOk = Call.ReturnBool();
		Answer.Merged = Call.Out<FStrProperty>(TEXT("Merged"));
		Answer.Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		return Answer;
	}

	// The index of a key in an object, or -1.
	int32 IndexOf(const OWJsonValue& Obj, const char* Key)
	{
		for (size_t I = 0; I < Obj.Keys().size(); ++I)
		{
			if (Obj.Keys()[I] == Key) return static_cast<int32>(I);
		}
		return -1;
	}

	FOWClientConfig WriterConfig()
	{
		FOWClientConfig Out;
		Out.ApiKey = TEXT("ow_w_test");
		Out.ApiPin = TEXT("1234");
		Out.BaseUrl = TEXT("http://keel.test/api/v2/");
		return Out;
	}

	OnlyWorlds::OWResponse Answer200(const char* Text)
	{
		OnlyWorlds::OWResponse Out;
		Out.status = 200;
		Out.body = Text;
		return Out;
	}

	// An UpsertElement of this text activated on a fake that answers inside Send; the fake, holding what was sent.
	std::shared_ptr<OWClientNodesTest::FFakeTransport> UpsertThrough(const FString& Type, const FString& Id, const FString& Text)
	{
		auto Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		Fake->Fallback = Answer200(R"({"id": "c1", "name": "Ana"})");
		UOWClientNode* Node = UOWClientWrite::UpsertElement(WriterConfig(), Type, Id, Text);
		Node->SetTransport(Fake);
		Node->Activate();
		return Fake;
	}

	OWJsonValue SentBody(FAutomationTestBase& Test, const OWClientNodesTest::FFakeTransport& Fake, const TCHAR* What)
	{
		if (Fake.Sent.size() != 1 || !Fake.Sent[0].body)
		{
			Test.AddError(FString::Printf(TEXT("%s: not one request with a body (%d sent)"), What, static_cast<int32>(Fake.Sent.size())));
			return OWJsonValue();
		}
		return ParseOrFail(Test, OWConvert::Str(*Fake.Sent[0].body), What);
	}

	// One Character as a folder or a read node gives it: the stamps, type and world, two extension keys (one with an
	// integer past 2^53), a null string, an integer spelled 180.0, and most of the schema's fields absent.
	const TCHAR* const AnaRead = TEXT(R"({
  "id": "c1",
  "type": "character",
  "world": "w1",
  "name": "Ana",
  "description": "A smith",
  "subtype": null,
  "height": 180.0,
  "level": 3,
  "friends": ["f1", "f2"],
  "location": "l1",
  "x_mine": {"a": 1, "big": 9007199254740993},
  "atlas_shape": [1.5, 2],
  "created_at": "2026-01-01T00:00:00Z",
  "updated_at": "2026-01-02T00:00:00Z",
  "change_seq": 7,
  "created_by": "u1"
})");
}

namespace OWMM = OWModelsMergeTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeRoundTripTest, "OnlyWorlds.Models.Merge.RoundTrip", OWMM::MergeFlags)

bool FOWModelsMergeRoundTripTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* WorldName : { TEXT("Sikelia"), TEXT("Moppetopia") })
	{
		const bool bSikelia = FCString::Strcmp(WorldName, TEXT("Sikelia")) == 0;
		if (!OWTestPaths::WorldGiven(*this, bSikelia ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA"))) continue;
		UOWWorld* World = OWTestPaths::OpenWorld(*this, bSikelia ? OWTestPaths::Sikelia() : OWTestPaths::Moppetopia(),
			bSikelia ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA"));
		if (!World) return false;
		TStrongObjectPtr<UOWWorld> Keep(World);

		int32 Elements = 0, Equal = 0, SameText = 0, Refused = 0, Differ = 0, Extensions = 0, WithExtensions = 0;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements)
		{
			const FString Slug = OWConvert::Str(E.Type()), Id = OWConvert::Str(E.Id());
			const FString Pas = OWMM::TypePascal(Slug);
			const FString AsRead = World->GetElementJson(Id);

			OWBlueprintTest::FCall FindCall(*this, UOWElements::StaticClass(), *(TEXT("Find") + Pas));
			FindCall.SetWorld(World);
			FindCall.SetString(TEXT("Id"), Id);
			FindCall.Invoke();
			const FStructProperty* Found = CastField<FStructProperty>(FindCall.Property(*Pas));
			if (!FindCall.ReturnBool() || !Found)
			{
				AddError(FString::Printf(TEXT("%s: Find%s did not find %s"), WorldName, *Pas, *Id));
				continue;
			}
			const FString Written = OWMM::DoorText(*this, Pas, Found->Struct, FindCall.OutStruct(*Pas, Found->Struct));
			const OWMM::FMergeAnswer Answer = OWMM::MergeCall(*this, AsRead, Written);
			++Elements;
			if (!Answer.bOk)
			{
				if (++Refused <= 3) AddError(FString::Printf(TEXT("%s %s: the merge refused: %s"), WorldName, *Id, *Answer.Reason));
				continue;
			}
			bool bMergedOk = false, bReadOk = false;
			const OnlyWorlds::OWJsonValue Merged = OWMM::ParseText(Answer.Merged, bMergedOk);
			const OnlyWorlds::OWJsonValue Expected = OWMM::ParseText(AsRead, bReadOk);
			if (bMergedOk && bReadOk && Merged == Expected) ++Equal;
			else if (++Differ <= 5) AddError(FString::Printf(TEXT("%s %s: the merge of an unchanged struct is not the element as read"), WorldName, *Id));
			SameText += Answer.Merged == AsRead;
			int32 Here = 0;
			for (const std::string& Key : Merged.Keys()) Here += OWMM::IsExtensionKey(Key);
			Extensions += Here;
			WithExtensions += Here > 0;
		}
		TestTrue(FString::Printf(TEXT("%s: elements merged"), WorldName), Elements > 0);
		TestEqual(FString::Printf(TEXT("%s: no merge refused"), WorldName), Refused, 0);
		TestEqual(FString::Printf(TEXT("%s: every unchanged merge is the element as read, as a value"), WorldName), Equal, Elements);
		TestEqual(FString::Printf(TEXT("%s: and as text"), WorldName), SameText, Elements);
		AddInfo(FString::Printf(TEXT("MERGE %s: %d elements, %d equal as read; extension keys kept: %d on %d elements"),
			WorldName, Elements, Equal, Extensions, WithExtensions));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeChangesTest, "OnlyWorlds.Models.Merge.Changes", OWMM::MergeFlags)

bool FOWModelsMergeChangesTest::RunTest(const FString& Parameters)
{
	using OnlyWorlds::OWJsonValue;
	const OWJsonValue AsRead = OWMM::ParseOrFail(*this, OWMM::AnaRead, TEXT("Ana as read"));

	// Unchanged: the element as read, a null string, 180.0 and the absent fields as they were.
	FOWCharacter Ana = OWMM::CharacterOf(*this, OWMM::AnaRead);
	const OWMM::FMergeAnswer Same = OWMM::MergeCall(*this, OWMM::AnaRead, OWMM::CharacterDoor(*this, Ana));
	TestTrue(TEXT("unchanged: merged"), Same.bOk);
	TestTrue(TEXT("unchanged: the element as read, as a value"), OWMM::ParseOrFail(*this, Same.Merged, TEXT("unchanged")) == AsRead);
	TestTrue(TEXT("unchanged: no Reason"), Same.Reason.IsEmpty());

	// Changed as a graph changes a struct.
	Ana.Level = 4;
	Ana.Friends = { TEXT("f2"), TEXT("f3") };
	Ana.Description = TEXT("");
	Ana.Location = TEXT("");
	Ana.bHasHeight = false;
	Ana.Background = TEXT("Born at sea");
	const OWMM::FMergeAnswer Changed = OWMM::MergeCall(*this, OWMM::AnaRead, OWMM::CharacterDoor(*this, Ana));
	TestTrue(TEXT("changed: merged"), Changed.bOk);
	const OWJsonValue M = OWMM::ParseOrFail(*this, Changed.Merged, TEXT("changed"));

	TestTrue(TEXT("level 4 lands"), M.Find("level") && M.Find("level")->IsNumber() && M.Find("level")->Text() == "4");
	const OWJsonValue* Friends = M.Find("friends");
	TestTrue(TEXT("friends [f2, f3] lands whole"), Friends && Friends->IsArray() && Friends->Size() == 2
		&& Friends->Items()[0].Text() == "f2" && Friends->Items()[1].Text() == "f3");
	TestTrue(TEXT("an emptied description lands as \"\""), M.Find("description") && M.Find("description")->IsString()
		&& M.Find("description")->Text().empty());
	TestTrue(TEXT("a cleared location lands as null"), M.Find("location") && M.Find("location")->IsNull());
	TestTrue(TEXT("a cleared height lands as null"), M.Find("height") && M.Find("height")->IsNull());
	TestTrue(TEXT("name unchanged"), M.Find("name") && M.Find("name")->Text() == "Ana");

	// Every key as read kept in its place; the one field the read lacked and the struct set, at the end.
	TestEqual(TEXT("the read's keys and one more"), static_cast<int32>(M.Size()), static_cast<int32>(AsRead.Size()) + 1);
	bool bOrder = M.Size() == AsRead.Size() + 1;
	for (size_t I = 0; bOrder && I < AsRead.Size(); ++I) bOrder = M.Keys()[I] == AsRead.Keys()[I];
	TestTrue(TEXT("every key as read, in the read's order"), bOrder);
	TestTrue(TEXT("background, absent as read and set, at the end"), M.Size() > 0 && M.Keys().back() == "background"
		&& M.Items().back().Text() == "Born at sea");
	TestFalse(TEXT("species, absent as read and unset, stays absent"), M.Has("species"));
	TestFalse(TEXT("charisma, absent as read and unset, stays absent"), M.Has("charisma"));

	// What a struct cannot carry, kept as read.
	for (const char* Key : { "x_mine", "atlas_shape", "type", "world", "created_at", "updated_at", "change_seq", "created_by", "subtype" })
	{
		TestTrue(FString::Printf(TEXT("%s kept as read"), *OWConvert::Str(Key)), M.Find(Key) && AsRead.Find(Key) && *M.Find(Key) == *AsRead.Find(Key));
	}
	const OWJsonValue* Mine = M.Find("x_mine");
	TestTrue(TEXT("x_mine's integer past 2^53 keeps every digit"), Mine && Mine->Find("big") && Mine->Find("big")->Text() == "9007199254740993");
	TestTrue(TEXT("subtype's null kept (an unset over an unset)"), M.Find("subtype") && M.Find("subtype")->IsNull());

	// An integer spelled otherwise and unchanged keeps the read's spelling; the same integer set again too.
	FOWCharacter Tall = OWMM::CharacterOf(*this, OWMM::AnaRead);
	Tall.bHasHeight = true;
	Tall.Height = 180;
	const OWMM::FMergeAnswer Spelled = OWMM::MergeCall(*this, OWMM::AnaRead, OWMM::CharacterDoor(*this, Tall));
	const OWJsonValue S = OWMM::ParseOrFail(*this, Spelled.Merged, TEXT("spelled"));
	TestTrue(TEXT("height 180 over 180.0 keeps 180.0"), S.Find("height") && S.Find("height")->Text() == "180.0");
	Tall.Height = 181;
	const OWJsonValue Taller = OWMM::ParseOrFail(*this, OWMM::MergeCall(*this, OWMM::AnaRead, OWMM::CharacterDoor(*this, Tall)).Merged, TEXT("taller"));
	TestTrue(TEXT("height 181 over 180.0 lands as 181"), Taller.Find("height") && Taller.Find("height")->Text() == "181");
	TestEqual(TEXT("height in its place"), OWMM::IndexOf(Taller, "height"), OWMM::IndexOf(AsRead, "height"));

	// A string set over the read's null lands.
	FOWCharacter Sub = OWMM::CharacterOf(*this, OWMM::AnaRead);
	Sub.Subtype = TEXT("Smith");
	const OWJsonValue Subbed = OWMM::ParseOrFail(*this, OWMM::MergeCall(*this, OWMM::AnaRead, OWMM::CharacterDoor(*this, Sub)).Merged, TEXT("subtype"));
	TestTrue(TEXT("subtype set over null lands"), Subbed.Find("subtype") && Subbed.Find("subtype")->IsString() && Subbed.Find("subtype")->Text() == "Smith");
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeIdsTest, "OnlyWorlds.Models.Merge.Ids", OWMM::MergeFlags)

bool FOWModelsMergeIdsTest::RunTest(const FString& Parameters)
{
	using OnlyWorlds::OWJsonValue;
	const FString Ana = TEXT(R"({"id": "c1", "name": "Ana", "x_k": 1})");

	FOWCharacter NoId;
	NoId.Name = TEXT("Ana");
	const FString NoIdText = OWMM::CharacterDoor(*this, NoId);
	TestFalse(TEXT("the door leaves an empty id out"), OWMM::ParseOrFail(*this, NoIdText, TEXT("no id")).Has("id"));
	const OWMM::FMergeAnswer Kept = OWMM::MergeCall(*this, Ana, NoIdText);
	const OWJsonValue K = OWMM::ParseOrFail(*this, Kept.Merged, TEXT("kept"));
	TestTrue(TEXT("the struct's empty id: merged"), Kept.bOk);
	TestTrue(TEXT("the struct's empty id: the read's id kept, first"), K.Size() > 0 && K.Keys()[0] == "id" && K.Items()[0].Text() == "c1");
	TestTrue(TEXT("the struct's empty id: x_k kept"), K.Has("x_k"));

	const OWMM::FMergeAnswer NullId = OWMM::MergeCall(*this, Ana, TEXT(R"({"id": null, "name": "Ana"})"));
	TestTrue(TEXT("an id null: the read's kept"), NullId.bOk && OWMM::ParseOrFail(*this, NullId.Merged, TEXT("null id")).Find("id")
		&& OWMM::ParseOrFail(*this, NullId.Merged, TEXT("null id")).Find("id")->Text() == "c1");

	FOWCharacter SameId = NoId;
	SameId.Id = TEXT("c1");
	const OWMM::FMergeAnswer Both = OWMM::MergeCall(*this, Ana, OWMM::CharacterDoor(*this, SameId));
	TestTrue(TEXT("the same id: merged"), Both.bOk);
	TestTrue(TEXT("the same id: one id, c1"), OWMM::ParseOrFail(*this, Both.Merged, TEXT("same id")).Find("id")
		&& OWMM::ParseOrFail(*this, Both.Merged, TEXT("same id")).Find("id")->Text() == "c1");

	FOWCharacter Other = NoId;
	Other.Id = TEXT("c2");
	const OWMM::FMergeAnswer Two = OWMM::MergeCall(*this, Ana, OWMM::CharacterDoor(*this, Other));
	TestFalse(TEXT("two ids: refused"), Two.bOk);
	TestTrue(TEXT("two ids: Merged \"\""), Two.Merged.IsEmpty());
	TestTrue(TEXT("two ids: Reason names both"), Two.Reason.StartsWith(TEXT("id: ")) && Two.Reason.Contains(TEXT("c1")) && Two.Reason.Contains(TEXT("c2")));

	const OWMM::FMergeAnswer Fresh = OWMM::MergeCall(*this, TEXT(R"({"name": "Ana", "x_k": 1})"), OWMM::CharacterDoor(*this, Other));
	const OWJsonValue N = OWMM::ParseOrFail(*this, Fresh.Merged, TEXT("new id"));
	TestTrue(TEXT("a read without an id: merged"), Fresh.bOk);
	TestTrue(TEXT("a read without an id: the struct's id"), N.Find("id") && N.Find("id")->Text() == "c2");
	TestTrue(TEXT("a read without an id: x_k kept"), N.Has("x_k"));

	const OWMM::FMergeAnswer Numbered = OWMM::MergeCall(*this, TEXT(R"({"id": 5, "name": "Ana"})"), NoIdText);
	TestFalse(TEXT("a read id that is not a string: refused"), Numbered.bOk);
	TestTrue(TEXT("a read id that is not a string: Reason"), Numbered.Reason.StartsWith(TEXT("element: ")));
	const OWMM::FMergeAnswer StructNumbered = OWMM::MergeCall(*this, Ana, TEXT(R"({"id": 5, "name": "Ana"})"));
	TestFalse(TEXT("a struct id that is not a string: refused"), StructNumbered.bOk);
	TestTrue(TEXT("a struct id that is not a string: Reason"), StructNumbered.Reason.StartsWith(TEXT("struct: ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeRefusalsTest, "OnlyWorlds.Models.Merge.Refusals", OWMM::MergeFlags)

bool FOWModelsMergeRefusalsTest::RunTest(const FString& Parameters)
{
	const FString Good = TEXT(R"({"id": "c1", "name": "Ana"})");
	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* ElementJson;
		const TCHAR* StructJson;
		const TCHAR* Prefix;
	};
	const FCase Cases[] = {
		{ TEXT("element not JSON"), TEXT("not json"), nullptr, TEXT("element: ") },
		{ TEXT("element empty"), TEXT(""), nullptr, TEXT("element: ") },
		{ TEXT("element an array"), TEXT("[1, 2]"), nullptr, TEXT("element: not a JSON object") },
		{ TEXT("element a string"), TEXT("\"Ana\""), nullptr, TEXT("element: not a JSON object") },
		{ TEXT("struct not JSON"), nullptr, TEXT("{\"name\": "), TEXT("struct: ") },
		{ TEXT("struct empty"), nullptr, TEXT(""), TEXT("struct: ") },
		{ TEXT("struct null"), nullptr, TEXT("null"), TEXT("struct: not a JSON object") },
		{ TEXT("struct an array"), nullptr, TEXT("[]"), TEXT("struct: not a JSON object") },
	};
	for (const FCase& C : Cases)
	{
		const OWMM::FMergeAnswer Answer = OWMM::MergeCall(*this, C.ElementJson ? FString(C.ElementJson) : Good,
			C.StructJson ? FString(C.StructJson) : Good);
		TestFalse(FString(C.Label) + TEXT(": refused"), Answer.bOk);
		TestTrue(FString(C.Label) + TEXT(": Merged \"\""), Answer.Merged.IsEmpty());
		TestTrue(FString(C.Label) + TEXT(": Reason starts ") + C.Prefix + TEXT(" (was: ") + Answer.Reason + TEXT(")"),
			Answer.Reason.StartsWith(C.Prefix));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeUpsertTest, "OnlyWorlds.Models.Merge.Upsert", OWMM::MergeFlags)

bool FOWModelsMergeUpsertTest::RunTest(const FString& Parameters)
{
	using OnlyWorlds::OWJsonValue;
	const OWJsonValue AsRead = OWMM::ParseOrFail(*this, OWMM::AnaRead, TEXT("Ana as read"));
	FOWCharacter Ana = OWMM::CharacterOf(*this, OWMM::AnaRead);
	Ana.Level = 4;
	const FString DoorJson = OWMM::CharacterDoor(*this, Ana);
	const OWMM::FMergeAnswer Answer = OWMM::MergeCall(*this, OWMM::AnaRead, DoorJson);
	TestTrue(TEXT("merged"), Answer.bOk);
	const OWJsonValue Merged = OWMM::ParseOrFail(*this, Answer.Merged, TEXT("merged"));

	// The merge's text through UpsertElement: the extensions go, the six read-only keys do not.
	auto Fake = OWMM::UpsertThrough(TEXT("character"), TEXT("c1"), Answer.Merged);
	const OWJsonValue Body = OWMM::SentBody(*this, *Fake, TEXT("the merge's upsert"));
	TestTrue(TEXT("a PUT"), Fake->Sent.size() == 1 && Fake->Sent[0].method == "PUT");
	TestTrue(TEXT("x_mine sent as read"), Body.Find("x_mine") && OWMM::SameOnTheWire(*Body.Find("x_mine"), *AsRead.Find("x_mine")));
	TestTrue(TEXT("x_mine's integer past 2^53 sent with every digit"), Body.Find("x_mine") && Body.Find("x_mine")->Find("big")
		&& Body.Find("x_mine")->Find("big")->Text() == "9007199254740993");
	TestTrue(TEXT("atlas_shape sent as read"), Body.Find("atlas_shape") && OWMM::SameOnTheWire(*Body.Find("atlas_shape"), *AsRead.Find("atlas_shape")));
	TestTrue(TEXT("level 4 sent"), Body.Find("level") && Body.Find("level")->Text() == "4");
	for (const char* Key : OWMM::ReadOnlySix)
	{
		TestFalse(FString::Printf(TEXT("%s not sent"), *OWConvert::Str(Key)), Body.Has(Key));
	}
	TestTrue(TEXT("the body is the merge's JSON as the core's Sanitize cleans it"),
		OWMM::SameOnTheWire(Body, OnlyWorlds::OWClient::Sanitize(Merged, "character")));

	// The door's JSON alone: no extension sent (an Upsert of it clears them at keel).
	auto Bare = OWMM::UpsertThrough(TEXT("character"), TEXT("c1"), DoorJson);
	const OWJsonValue BareBody = OWMM::SentBody(*this, *Bare, TEXT("the door's upsert"));
	TestFalse(TEXT("the door alone: no x_mine"), BareBody.Has("x_mine"));
	TestFalse(TEXT("the door alone: no atlas_shape"), BareBody.Has("atlas_shape"));

	// A known-of stub as read: the merge keeps known_of, and the core refuses the upsert, nothing sent.
	const FString Stub = TEXT(R"({"id": "c1", "type": "character", "name": "Ana", "known_of": true})");
	const OWMM::FMergeAnswer StubMerge = OWMM::MergeCall(*this, Stub, OWMM::CharacterDoor(*this, OWMM::CharacterOf(*this, Stub)));
	TestTrue(TEXT("a stub: merged"), StubMerge.bOk);
	const OWJsonValue StubMerged = OWMM::ParseOrFail(*this, StubMerge.Merged, TEXT("the stub's merge"));
	TestTrue(TEXT("a stub: known_of kept"), StubMerged.Find("known_of") && StubMerged.Find("known_of")->IsBool() && StubMerged.Find("known_of")->AsBool());
	auto StubFake = OWMM::UpsertThrough(TEXT("character"), TEXT("c1"), StubMerge.Merged);
	TestEqual(TEXT("a stub's upsert: refused, nothing sent"), static_cast<int32>(StubFake->Sent.size()), 0);

	// A served-trimmed mark as read: kept, so keel refuses the write by name (served_trimmed_not_writable) rather
	// than the hidden ids being dropped.
	const FString Trimmed = TEXT(R"({"id": "n1", "type": "narrative", "name": "Note", "characters": [], "x_served_trimmed": ["characters"]})");
	FOWNarrative Note;
	OWFromJson(OWMM::ParseOrFail(*this, Trimmed, TEXT("the trimmed body")), Note);
	const FString NoteJson = OWMM::DoorText(*this, TEXT("Narrative"), FOWNarrative::StaticStruct(), &Note);
	const OWMM::FMergeAnswer TrimMerge = OWMM::MergeCall(*this, Trimmed, NoteJson);
	TestTrue(TEXT("trimmed: merged"), TrimMerge.bOk);
	TestTrue(TEXT("trimmed: x_served_trimmed kept"), OWMM::ParseOrFail(*this, TrimMerge.Merged, TEXT("the trimmed merge")).Has("x_served_trimmed"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsMergeFunctionTest, "OnlyWorlds.Models.Merge.Function", OWMM::MergeFlags)

bool FOWModelsMergeFunctionTest::RunTest(const FString& Parameters)
{
	// FCall asserts the flags: native, static, public, Blueprint callable and pure.
	OWBlueprintTest::FCall Call(*this, UOWElementJson::StaticClass(), TEXT("MergeElementJson"));
	for (const TCHAR* In : { TEXT("ElementJson"), TEXT("StructJson") })
	{
		const FStrProperty* P = CastField<FStrProperty>(Call.Property(In));
		TestTrue(FString(In) + TEXT(": a string input"), P && !OWBlueprintTest::IsOutputPin(P));
	}
	for (const TCHAR* Out : { TEXT("Merged"), TEXT("Reason") })
	{
		const FStrProperty* P = CastField<FStrProperty>(Call.Property(Out));
		TestTrue(FString(Out) + TEXT(": a string output"), P && OWBlueprintTest::IsOutputPin(P));
	}
	TestNotNull(TEXT("answers a bool"), CastField<FBoolProperty>(Call.Return()));
	int32 Params = 0;
	if (const UFunction* Func = UOWElementJson::StaticClass()->FindFunctionByName(TEXT("MergeElementJson")))
	{
		for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) ++Params;
	}
	TestEqual(TEXT("two in, two out and the answer"), Params, 5);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
