// The generated way back: a struct into its element's JSON (OWToJson, OWModelsJson.h) and the Blueprint doors that
// give it as the text a write node takes (UOWElements::Get<Type>Json). Four tests:
//
// - RoundTrip: every element of Sikelia and Moppetopia read through UOWElements::Find<Type> and written back through
//   Get<Type>Json, both called as a Blueprint calls them (OWBlueprintCall.h), the text parsed and held to the element's
//   file on the schema's keys (schema.json, read here apart from the generator). A key the file holds as the struct
//   writes it is the same; the forms the struct cannot tell apart are named and counted (a key the file lacks, written
//   unset; a null string written ""; a null link list written []; an empty single link written null; an integer
//   spelled 4255.0 written 4255); anything else is a loss, and a loss fails. The keys a struct cannot carry (the
//   server's stamps, extension keys, anything the schema does not declare) are counted too: they are what an
//   UpsertElement of a struct's JSON clears.
// - Unset: OWToJson on default structs and on set ones, each kind's unset and set form, the keys never written.
// - Doors: the 22 doors are there, each a pure static Blueprint function of its type's struct answering a string.
// - Node: a struct made member by member as a Make node makes it, its door called through reflection, and the text
//   handed to UpsertElement, PatchElement and BulkWrite: the body each sends (the read nodes' fake transport) is the
//   door's JSON as a value, unchanged by the core's Sanitize.
//
// Every helper is in the namespace OWModelsWriteTest, never at file scope (C4459, an error here: CppCompileWarnings.cs).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "OWBlueprintCall.h"
#include "OWClientNodes.h"
#include "OWConvert.h"
#include "OWFakeTransport.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWModelsJson.h"
#include "OWTestPaths.h"
#include "OWTransport.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace OWModelsWriteTest
{
	using OnlyWorlds::OWJsonValue;

	constexpr EAutomationTestFlags WriteFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// How the struct writes one key (schema.json's kinds; a generic field is two keys).
	enum class EKeyKind : uint8 { Text, Int, Bool, Single, Multi, GenericType, GenericId };

	struct FSchemaKey
	{
		std::string Key;
		EKeyKind Kind = EKeyKind::Text;
	};

	// schema.json from the vendored dist, read by the core's strict reader; false (an error logged) when it is not there.
	bool ReadSchema(FAutomationTestBase& Test, OWJsonValue& Into)
	{
		const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/schema.json"));
		if (OWTestPaths::ReadJson(Path, Into) && Into.Find("types") && Into.Find("base")) return true;
		Test.AddError(TEXT("schema.json does not read at ") + Path);
		return false;
	}

	const char* TextOf(const OWJsonValue& Obj, const char* Field)
	{
		const OWJsonValue* V = Obj.Find(Field);
		return V && V->IsString() ? V->Text().c_str() : "";
	}

	// One type's keys in the generator's order: the base's (world left out: never written), then the type's own.
	std::vector<FSchemaKey> KeysOf(const OWJsonValue& Schema, const std::string& Slug)
	{
		std::vector<FSchemaKey> Keys;
		const OWJsonValue* TypeFields = Schema.Find("types") ? Schema.Find("types")->Find(Slug) : nullptr;
		if (!TypeFields) return Keys;
		std::vector<const OWJsonValue*> All;
		for (const OWJsonValue& F : Schema.Find("base")->Items()) All.push_back(&F);
		for (const OWJsonValue& F : TypeFields->Items()) All.push_back(&F);
		for (const OWJsonValue* F : All)
		{
			const std::string FieldName = TextOf(*F, "name");
			const std::string FieldKind = TextOf(*F, "kind");
			if (FieldName == "world") continue;
			if (FieldKind == "generic")
			{
				Keys.push_back({ TextOf(*F, "type_key"), EKeyKind::GenericType });
				Keys.push_back({ TextOf(*F, "id_key"), EKeyKind::GenericId });
				continue;
			}
			FSchemaKey One;
			One.Key = FieldName;
			One.Kind = FieldKind == "scalar_int" ? EKeyKind::Int : FieldKind == "scalar_bool" ? EKeyKind::Bool
				: FieldKind == "single" ? EKeyKind::Single : FieldKind == "multi" ? EKeyKind::Multi : EKeyKind::Text;
			Keys.push_back(One);
		}
		return Keys;
	}

	// The unset form the struct writes for a kind.
	bool IsUnsetForm(EKeyKind Kind, const OWJsonValue& V)
	{
		switch (Kind)
		{
		case EKeyKind::Text:
		case EKeyKind::GenericType: return V.IsString() && V.Text().empty();
		case EKeyKind::Int:
		case EKeyKind::Single:
		case EKeyKind::GenericId: return V.IsNull();
		case EKeyKind::Multi: return V.IsArray() && V.Size() == 0;
		case EKeyKind::Bool: return V.IsBool() && !V.AsBool();
		}
		return false;
	}

	const char* KindWord(const OWJsonValue* V)
	{
		if (!V) return "absent";
		switch (V->kind())
		{
		case OWJsonValue::Kind::Null: return "null";
		case OWJsonValue::Kind::Bool: return "a bool";
		case OWJsonValue::Kind::Number: return "a number";
		case OWJsonValue::Kind::String: return V->Text().empty() ? "\"\"" : "a string";
		case OWJsonValue::Kind::Array: return "an array";
		case OWJsonValue::Kind::Object: return "an object";
		}
		return "?";
	}

	// What became of one key: "" when the written value is the file's, else an exception's name (a form the struct
	// cannot tell apart), else a loss, which starts "lost: ".
	std::string Fate(const FSchemaKey& K, const OWJsonValue* InFile, const OWJsonValue* Written)
	{
		if (K.Key == "id" && InFile && InFile->IsString() && InFile->Text().empty() && !Written) return "an empty id, left out";
		if (!Written) return std::string("lost: ") + K.Key + " not written";
		if (InFile && *InFile == *Written) return "";
		if (!InFile) return IsUnsetForm(K.Kind, *Written) ? "absent in the file, written unset" : "lost: " + K.Key + " absent, written set";
		const bool bTextKind = K.Kind == EKeyKind::Text || K.Kind == EKeyKind::GenericType;
		const bool bIdKind = K.Kind == EKeyKind::Single || K.Kind == EKeyKind::GenericId;
		if (bTextKind && InFile->IsNull() && IsUnsetForm(K.Kind, *Written)) return "a null string, written \"\"";
		if (K.Kind == EKeyKind::Multi && InFile->IsNull() && IsUnsetForm(K.Kind, *Written)) return "a null link list, written []";
		if (bIdKind && InFile->IsString() && InFile->Text().empty() && Written->IsNull()) return "an empty link, written null";
		if (K.Kind == EKeyKind::Int && InFile->IsNumber() && Written->IsNumber() && InFile->AsDouble() == Written->AsDouble())
			return "an integer spelled otherwise, written as its digits";
		return "lost: " + K.Key + " was " + KindWord(InFile) + ", written " + KindWord(Written);
	}

	bool IsExtensionKey(const std::string& Key)
	{
		return Key.rfind("x_", 0) == 0 || Key.rfind("atlas_", 0) == 0 || Key.rfind("shadow_", 0) == 0;
	}

	// The door's answer, the return property of a pure call (its class asserted).
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

	OWJsonValue ParseText(const FString& Text, bool& bOk)
	{
		const std::string Utf8 = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Utf8));
		bOk = Parsed.ok;
		return Parsed.ok ? Parsed.value : OWJsonValue();
	}

	FString Line(const std::map<std::string, int32>& Counts)
	{
		FString Out;
		for (const auto& Row : Counts) Out += FString::Printf(TEXT("%s%s: %d"), Out.IsEmpty() ? TEXT("") : TEXT("; "), *OWConvert::Str(Row.first), Row.second);
		return Out.IsEmpty() ? FString(TEXT("none")) : Out;
	}

	FString TypePascal(const FString& Slug) { return Slug.Left(1).ToUpper() + Slug.Mid(1); }

	// A write node given the fake (which answers inside Send) and activated; the request it sent, or nullptr.
	const OnlyWorlds::OWRequest* SendThrough(UOWClientNode* Node, const std::shared_ptr<OWClientNodesTest::FFakeTransport>& Fake)
	{
		Node->SetTransport(Fake);
		Node->Activate();
		return Fake->Sent.size() == 1 ? &Fake->Sent[0] : nullptr;
	}

	FOWClientConfig WriterConfig()
	{
		FOWClientConfig Out;
		Out.ApiKey = TEXT("ow_w_test");
		Out.ApiPin = TEXT("1234");
		Out.BaseUrl = TEXT("http://keel.test/api/v2/");
		return Out;
	}

	OnlyWorlds::OWResponse Answer(int Code, const char* Text)
	{
		OnlyWorlds::OWResponse Out;
		Out.status = Code;
		Out.body = Text;
		return Out;
	}
}

namespace OWMW = OWModelsWriteTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsWriteRoundTripTest, "OnlyWorlds.Models.Write.RoundTrip", OWMW::WriteFlags)

bool FOWModelsWriteRoundTripTest::RunTest(const FString& Parameters)
{
	OnlyWorlds::OWJsonValue Schema;
	if (!OWMW::ReadSchema(*this, Schema)) return false;
	for (const TCHAR* Name : { TEXT("Sikelia"), TEXT("Moppetopia") })
	{
		const bool bSikelia = FCString::Strcmp(Name, TEXT("Sikelia")) == 0;
		UOWWorld* World = OWTestPaths::OpenWorld(*this, bSikelia ? OWTestPaths::Sikelia() : OWTestPaths::Moppetopia(),
			bSikelia ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA"));
		if (!World) return false;
		TStrongObjectPtr<UOWWorld> Keep(World);

		int32 Elements = 0, Same = 0, Lost = 0, Unparsed = 0, Extra = 0;
		std::map<std::string, int32> Exceptions, Cannot, Losses;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements)
		{
			const FString Slug = OWConvert::Str(E.Type()), Id = OWConvert::Str(E.Id());
			const FString Pas = OWMW::TypePascal(Slug);
			const std::vector<OWMW::FSchemaKey> Keys = OWMW::KeysOf(Schema, E.Type());
			if (Keys.empty())
			{
				AddError(FString::Printf(TEXT("%s %s: no type %s in schema.json"), Name, *Id, *Slug));
				continue;
			}
			OWBlueprintTest::FCall FindCall(*this, UOWElements::StaticClass(), *(TEXT("Find") + Pas));
			FindCall.SetWorld(World);
			FindCall.SetString(TEXT("Id"), Id);
			FindCall.Invoke();
			const FStructProperty* Found = CastField<FStructProperty>(FindCall.Property(*Pas));
			if (!FindCall.ReturnBool() || !Found)
			{
				AddError(FString::Printf(TEXT("%s: Find%s did not find %s"), Name, *Pas, *Id));
				continue;
			}
			const void* Data = FindCall.OutStruct(*Pas, Found->Struct);
			OWBlueprintTest::FCall Door(*this, UOWElements::StaticClass(), *(TEXT("Get") + Pas + TEXT("Json")));
			void* In = Door.StructSlot(*Pas, Found->Struct);
			if (!Data || !In) continue;
			Found->Struct->CopyScriptStruct(In, Data);
			Door.Invoke();
			bool bParsed = false;
			const OnlyWorlds::OWJsonValue Written = OWMW::ParseText(OWMW::ReturnText(*this, Door), bParsed);
			if (!bParsed || !Written.IsObject())
			{
				if (++Unparsed <= 3) AddError(FString::Printf(TEXT("%s %s: Get%sJson's text is not a JSON object"), Name, *Id, *Pas));
				continue;
			}
			++Elements;

			// Every schema key, the file's against the written.
			std::vector<std::string> Declared;
			for (const OWMW::FSchemaKey& K : Keys)
			{
				Declared.push_back(K.Key);
				const std::string What = OWMW::Fate(K, E.body.Find(K.Key), Written.Find(K.Key));
				if (What.empty()) ++Same;
				else if (What.rfind("lost: ", 0) == 0)
				{
					++Lost;
					++Losses[What];
					if (Lost <= 5) AddError(FString::Printf(TEXT("%s %s: %s"), Name, *Id, *OWConvert::Str(What)));
				}
				else ++Exceptions[What];
			}
			// Nothing written outside the schema's keys (world, type and the stamps never).
			for (const std::string& Key : Written.Keys())
			{
				if (std::find(Declared.begin(), Declared.end(), Key) != Declared.end()) continue;
				if (++Extra <= 3) AddError(FString::Printf(TEXT("%s %s: wrote %s, not a schema key"), Name, *Id, *OWConvert::Str(Key)));
			}
			// What the file holds and a struct cannot carry.
			for (const std::string& Key : E.body.Keys())
			{
				if (std::find(Declared.begin(), Declared.end(), Key) != Declared.end()) continue;
				++Cannot[Key == "world" ? std::string("world (never written)") : OWMW::IsExtensionKey(Key) ? std::string("extension keys") : Key];
			}
		}
		TestTrue(FString::Printf(TEXT("%s: elements written back"), Name), Elements > 0);
		TestEqual(FString::Printf(TEXT("%s: every element's text a JSON object"), Name), Unparsed, 0);
		TestEqual(FString::Printf(TEXT("%s: nothing lost on the schema's keys"), Name), Lost, 0);
		TestEqual(FString::Printf(TEXT("%s: nothing written outside the schema's keys"), Name), Extra, 0);
		AddInfo(FString::Printf(TEXT("ROUNDTRIP %s: %d elements, %d schema keys as the file holds them; exceptions: %s; lost: %s"),
			Name, Elements, Same, *OWMW::Line(Exceptions), *OWMW::Line(Losses)));
		AddInfo(FString::Printf(TEXT("ROUNDTRIP %s: keys a struct cannot carry (an upsert of its JSON clears them): %s"), Name,
			*OWMW::Line(Cannot)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsWriteUnsetTest, "OnlyWorlds.Models.Write.Unset", OWMW::WriteFlags)

bool FOWModelsWriteUnsetTest::RunTest(const FString& Parameters)
{
	using OnlyWorlds::OWJsonValue;
	OWJsonValue Schema;
	if (!OWMW::ReadSchema(*this, Schema)) return false;
	const std::vector<OWMW::FSchemaKey> CharacterKeys = OWMW::KeysOf(Schema, "character");

	// A default struct: every key in its unset form, the empty id and world left out.
	const OWJsonValue Blank = OWToJson(FOWCharacter());
	TestTrue(TEXT("a default Character is an object"), Blank.IsObject());
	TestFalse(TEXT("an empty id is left out"), Blank.Has("id"));
	TestFalse(TEXT("world is never written"), Blank.Has("world"));
	TestFalse(TEXT("type is never written"), Blank.Has("type"));
	TestFalse(TEXT("change_seq is never written"), Blank.Has("change_seq"));
	TestEqual(TEXT("every schema key but id and world"), static_cast<int32>(Blank.Size()), static_cast<int32>(CharacterKeys.size()) - 1);
	TestTrue(TEXT("name: \"\""), Blank.Find("name") && Blank.Find("name")->IsString() && Blank.Find("name")->Text().empty());
	TestTrue(TEXT("background (a string): \"\""), Blank.Find("background") && Blank.Find("background")->IsString() && Blank.Find("background")->Text().empty());
	TestTrue(TEXT("level (an integer, bHasLevel false): null"), Blank.Find("level") && Blank.Find("level")->IsNull());
	TestTrue(TEXT("location (a single link): null"), Blank.Find("location") && Blank.Find("location")->IsNull());
	TestTrue(TEXT("species (a link list): []"), Blank.Find("species") && Blank.Find("species")->IsArray() && Blank.Find("species")->Size() == 0);
	int32 Unset = 0;
	for (const OWMW::FSchemaKey& K : CharacterKeys)
	{
		const OWJsonValue* V = Blank.Find(K.Key);
		Unset += V && OWMW::IsUnsetForm(K.Kind, *V);
	}
	TestEqual(TEXT("every key written is in its kind's unset form"), Unset, static_cast<int32>(CharacterKeys.size()) - 1);
	if (Blank.Size() >= 2)
	{
		TestEqual(TEXT("the schema's order: name first when there is no id"), OWConvert::Str(Blank.Keys()[0]), FString(TEXT("name")));
	}

	// A set struct: each kind's set form, the bool of an integer deciding over its value.
	FOWCharacter Ana;
	Ana.Id = TEXT("c1");
	Ana.Name = TEXT("Ana");
	Ana.World = TEXT("w1");
	Ana.bHasLevel = true;
	Ana.Level = 0;
	Ana.bHasHeight = false;
	Ana.Height = 7;
	Ana.bHasWeight = true;
	Ana.Weight = 9007199254740993LL;
	Ana.bHasStr = true;
	Ana.Str = -5;
	Ana.Location = TEXT("l1");
	Ana.Species = { TEXT("s1"), TEXT("s2") };
	const OWJsonValue Set = OWToJson(Ana);
	TestEqual(TEXT("the id first"), Set.Size() > 0 ? OWConvert::Str(Set.Keys()[0]) : FString(), FString(TEXT("id")));
	TestTrue(TEXT("id: \"c1\""), Set.Find("id") && Set.Find("id")->IsString() && Set.Find("id")->Text() == "c1");
	TestFalse(TEXT("world is never written, set or not"), Set.Has("world"));
	TestTrue(TEXT("level 0 with bHasLevel: 0, not null"), Set.Find("level") && Set.Find("level")->IsNumber() && Set.Find("level")->Text() == "0");
	TestTrue(TEXT("height 7 without bHasHeight: null"), Set.Find("height") && Set.Find("height")->IsNull());
	TestTrue(TEXT("weight past 2^53: every digit"), Set.Find("weight") && Set.Find("weight")->Text() == "9007199254740993");
	TestTrue(TEXT("STR keeps its key's spelling: -5"), Set.Find("STR") && Set.Find("STR")->Text() == "-5" && !Set.Has("str"));
	TestTrue(TEXT("location: \"l1\""), Set.Find("location") && Set.Find("location")->IsString() && Set.Find("location")->Text() == "l1");
	const OWJsonValue* Species = Set.Find("species");
	TestTrue(TEXT("species: [\"s1\", \"s2\"]"), Species && Species->IsArray() && Species->Size() == 2 && Species->Items()[0].Text() == "s1"
		&& Species->Items()[1].Text() == "s2");

	// The generic pair: ("", null) unset, as keel serves it; both strings set.
	const OWJsonValue BlankPin = OWToJson(FOWPin());
	TestTrue(TEXT("a pin's element_type unset: \"\""), BlankPin.Find("element_type") && BlankPin.Find("element_type")->IsString()
		&& BlankPin.Find("element_type")->Text().empty());
	TestTrue(TEXT("a pin's element_id unset: null"), BlankPin.Find("element_id") && BlankPin.Find("element_id")->IsNull());
	FOWPin Placed;
	Placed.ElementType = TEXT("character");
	Placed.ElementId = TEXT("c9");
	const OWJsonValue PlacedPin = OWToJson(Placed);
	TestTrue(TEXT("a pin's element_type set"), PlacedPin.Find("element_type") && PlacedPin.Find("element_type")->Text() == "character");
	TestTrue(TEXT("a pin's element_id set"), PlacedPin.Find("element_id") && PlacedPin.Find("element_id")->IsString()
		&& PlacedPin.Find("element_id")->Text() == "c9");

	// Into a struct and back: the schema's keys kept, an extension key and the stamps not (what a struct cannot carry).
	bool bOk = false;
	const OWJsonValue Body = OWMW::ParseText(TEXT("{\"id\": \"c1\", \"type\": \"character\", \"name\": \"Ana\", \"level\": 4255.0, "
		"\"friends\": [\"f1\"], \"location\": null, \"change_seq\": 9, \"x_mine\": {\"a\": 1}}"), bOk);
	TestTrue(TEXT("the body parses"), bOk);
	FOWCharacter ReadBack;
	OWFromJson(Body, ReadBack);
	const OWJsonValue Back = OWToJson(ReadBack);
	TestTrue(TEXT("back: name"), Back.Find("name") && Back.Find("name")->Text() == "Ana");
	TestTrue(TEXT("back: 4255.0 as 4255"), Back.Find("level") && Back.Find("level")->Text() == "4255");
	TestTrue(TEXT("back: friends"), Back.Find("friends") && Back.Find("friends")->Size() == 1);
	TestTrue(TEXT("back: location null"), Back.Find("location") && Back.Find("location")->IsNull());
	TestFalse(TEXT("back: no x_mine (a struct carries no extension)"), Back.Has("x_mine"));
	TestFalse(TEXT("back: no change_seq"), Back.Has("change_seq"));
	TestFalse(TEXT("back: no type"), Back.Has("type"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsWriteDoorsTest, "OnlyWorlds.Models.Write.Doors", OWMW::WriteFlags)

bool FOWModelsWriteDoorsTest::RunTest(const FString& Parameters)
{
	OnlyWorlds::OWJsonValue Schema;
	if (!OWMW::ReadSchema(*this, Schema)) return false;
	const OnlyWorlds::OWJsonValue& Types = *Schema.Find("types");
	int32 Doors = 0;
	for (const std::string& SlugUtf8 : Types.Keys())
	{
		const FString Pas = OWMW::TypePascal(OWConvert::Str(SlugUtf8));
		const FString DoorName = TEXT("Get") + Pas + TEXT("Json");
		// FCall asserts the flags: native, static, public, Blueprint callable and pure.
		OWBlueprintTest::FCall Door(*this, UOWElements::StaticClass(), *DoorName);
		const FStructProperty* Param = CastField<FStructProperty>(Door.Property(*Pas));
		const bool bParam = Param && Param->Struct && Param->Struct->GetName() == TEXT("OW") + Pas && !OWBlueprintTest::IsOutputPin(Param);
		TestTrue(DoorName + TEXT(": one FOW") + Pas + TEXT(" input named ") + Pas, bParam);
		TestNotNull(DoorName + TEXT(": answers a string"), CastField<FStrProperty>(Door.Return()));
		int32 Params = 0;
		if (const UFunction* Func = UOWElements::StaticClass()->FindFunctionByName(*DoorName))
		{
			for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) ++Params;
		}
		TestEqual(DoorName + TEXT(": the struct in and the text out, nothing else"), Params, 2);
		Doors += bParam;
	}
	TestEqual(TEXT("a door per type"), Doors, 22);
	TestEqual(TEXT("schema.json's types"), static_cast<int32>(Types.Size()), 22);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsWriteNodeTest, "OnlyWorlds.Models.Write.Node", OWMW::WriteFlags)

bool FOWModelsWriteNodeTest::RunTest(const FString& Parameters)
{
	using OnlyWorlds::OWJsonValue;
	// The struct as a Make node makes it, member by member, into the door's own parameter.
	OWBlueprintTest::FCall Door(*this, UOWElements::StaticClass(), TEXT("GetCharacterJson"));
	void* Made = Door.StructSlot(TEXT("Character"), FOWCharacter::StaticStruct());
	if (!Made) return false;
	const UScriptStruct* S = FOWCharacter::StaticStruct();
	OWBlueprintTest::SetMember<FStrProperty>(*this, S, Made, TEXT("Id"), TEXT("c1"));
	OWBlueprintTest::SetMember<FStrProperty>(*this, S, Made, TEXT("Name"), TEXT("Ana"));
	OWBlueprintTest::SetBoolMember(*this, S, Made, TEXT("bHasLevel"), true);
	OWBlueprintTest::SetMember<FInt64Property>(*this, S, Made, TEXT("Level"), 4);
	OWBlueprintTest::SetMember<FStrProperty>(*this, S, Made, TEXT("Location"), TEXT("l1"));
	Door.Invoke();
	const FString Text = OWMW::ReturnText(*this, Door);

	FOWCharacter Same;
	Same.Id = TEXT("c1");
	Same.Name = TEXT("Ana");
	Same.bHasLevel = true;
	Same.Level = 4;
	Same.Location = TEXT("l1");
	const OWJsonValue Want = OWToJson(Same);
	bool bOk = false;
	const OWJsonValue Got = OWMW::ParseText(Text, bOk);
	TestTrue(TEXT("the door's text parses"), bOk);
	TestTrue(TEXT("the door's text is OWToJson of the same struct"), Got == Want);

	// The text into each write node that takes an element: the body sent is the door's JSON as a value.
	struct FWrite
	{
		const TCHAR* Label;
		UOWClientNode* (*Make)(const FString&);
	};
	const FWrite Writes[] = {
		{ TEXT("UpsertElement"), [](const FString& J) -> UOWClientNode* { return UOWClientWrite::UpsertElement(OWMW::WriterConfig(), TEXT("character"), TEXT("c1"), J); } },
		{ TEXT("PatchElement"), [](const FString& J) -> UOWClientNode* { return UOWClientWrite::PatchElement(OWMW::WriterConfig(), TEXT("character"), TEXT("c1"), J, false, 0); } },
		{ TEXT("CreateElement"), [](const FString& J) -> UOWClientNode* { return UOWClientWrite::CreateElement(OWMW::WriterConfig(), TEXT("character"), J, TEXT("")); } },
	};
	for (const FWrite& W : Writes)
	{
		auto Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		Fake->Fallback = OWMW::Answer(200, R"({"id": "c1", "name": "Ana"})");
		const OnlyWorlds::OWRequest* Sent = OWMW::SendThrough(W.Make(Text), Fake);
		if (!TestNotNull(FString(W.Label) + TEXT(": one request sent"), Sent)) continue;
		bool bBody = false;
		const OWJsonValue Body = Sent->body ? OWMW::ParseText(OWConvert::Str(*Sent->body), bBody) : OWJsonValue();
		TestTrue(FString(W.Label) + TEXT(": the body is the door's JSON, as a value"), bBody && Body == Want);
	}
	{
		FOWBulkItem Item;
		Item.Type = TEXT("character");
		Item.ElementJson = Text;
		auto Fake = std::make_shared<OWClientNodesTest::FFakeTransport>();
		Fake->Fallback = OWMW::Answer(200, R"({"errors": false, "items": [{"status": 200, "id": "c1"}]})");
		const OnlyWorlds::OWRequest* Sent = OWMW::SendThrough(UOWClientBulk::BulkWrite(OWMW::WriterConfig(), { Item }, false, TEXT("")), Fake);
		bool bBody = false;
		const OWJsonValue Body = Sent && Sent->body ? OWMW::ParseText(OWConvert::Str(*Sent->body), bBody) : OWJsonValue();
		const OWJsonValue* Items = bBody ? Body.Find("items") : nullptr;
		const OWJsonValue* Element = Items && Items->IsArray() && Items->Size() == 1 ? Items->Items()[0].Find("element") : nullptr;
		TestTrue(TEXT("BulkWrite: the item's element is the door's JSON, as a value"), Element && *Element == Want);
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
