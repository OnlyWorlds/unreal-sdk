// The generated way in from the API: an element's JSON text, as a read node answers it, into its struct
// (UOWElements::<Type>FromJson), so a Blueprint on the onlyworlds.com road breaks the same struct the folder road gives.
// Four tests, each node called as a Blueprint calls it (OWBlueprintCall.h), never the C++ function directly:
//
// - Doors: the 22 nodes are there, each pure and static: the text in, its type's struct and a reason out, a bool back.
// - World: every element of Sikelia and Moppetopia, its text (UOWWorld::GetElementJson, the form the read nodes answer)
//   through <Type>FromJson, is the struct Find<Type> gives for it, member for member (UScriptStruct::CompareScriptStruct).
// - Refused: text that is not JSON, and JSON that is not an object, answer false with the reason and an empty struct.
// - OnlyWorldsLive.Models.CharacterFromJson: one Moppetopia Character read over its public key (0000000001), its body
//   as the node's text, broken by CharacterFromJson into the served id and name. Reads production, so it sits outside
//   the OnlyWorlds prefix the suite runs by default: run it by name (Automation RunTests OnlyWorldsLive).
//
// Every helper is in the namespace OWModelsFromJsonTest, never at file scope (C4459, an error here).

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#include "OWBlueprintCall.h"
#include "OWClient.h"
#include "OWConvert.h"
#include "OWHttpTransport.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWTestPaths.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <memory>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWModelsFromJsonTest
{
	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* const Suffix = TEXT("FromJson");

	FString TypePascal(const FString& Slug) { return Slug.Left(1).ToUpper() + Slug.Mid(1); }

	// The struct a node's out parameter named Pas carries, or nullptr.
	const UScriptStruct* StructOf(const UFunction* Func, const FString& Pas)
	{
		const FStructProperty* P = Func ? CastField<FStructProperty>(Func->FindPropertyByName(*Pas)) : nullptr;
		return P ? P->Struct : nullptr;
	}

	// One <Pas>FromJson call as a graph makes it: whether it answered true, the struct copied out, the reason.
	struct FFromJson
	{
		bool bOk = false;
		FString Reason;
		TSharedPtr<FStructOnScope> Data;
	};

	FFromJson Call(FAutomationTestBase& Test, const FString& Pas, const FString& Json)
	{
		FFromJson Got;
		const FString Name = Pas + Suffix;
		OWBlueprintTest::FCall Node(Test, UOWElements::StaticClass(), *Name);
		const UScriptStruct* S = StructOf(UOWElements::StaticClass()->FindFunctionByName(*Name), Pas);
		if (!S) return Got;
		Node.SetString(TEXT("Json"), Json);
		Node.Invoke();
		Got.bOk = Node.ReturnBool();
		Got.Reason = Node.Out<FStrProperty>(TEXT("Reason"));
		if (const void* Out = Node.OutStruct(*Pas, S))
		{
			Got.Data = MakeShared<FStructOnScope>(S);
			S->CopyScriptStruct(Got.Data->GetStructMemory(), Out);
		}
		return Got;
	}
}

namespace OWFJ = OWModelsFromJsonTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsFromJsonDoorsTest, "OnlyWorlds.Models.FromJson.Doors", OWFJ::Flags)

bool FOWModelsFromJsonDoorsTest::RunTest(const FString& Parameters)
{
	int32 Doors = 0;
	for (TFieldIterator<UFunction> It(UOWElements::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const FString Name = It->GetName();
		if (!Name.EndsWith(OWFJ::Suffix)) continue;
		const FString Pas = Name.LeftChop(FCString::Strlen(OWFJ::Suffix));
		// FCall asserts the flags: native, static, public, Blueprint callable and pure.
		OWBlueprintTest::FCall Node(*this, UOWElements::StaticClass(), *Name);
		const FStrProperty* Json = CastField<FStrProperty>(Node.Property(TEXT("Json")));
		TestTrue(Name + TEXT(": the text in, named Json"), Json && !OWBlueprintTest::IsOutputPin(Json));
		const FStructProperty* Out = CastField<FStructProperty>(Node.Property(*Pas));
		TestTrue(Name + TEXT(": one FOW") + Pas + TEXT(" out, named ") + Pas,
			Out && Out->Struct && Out->Struct->GetName() == TEXT("OW") + Pas && OWBlueprintTest::IsOutputPin(Out));
		const FStrProperty* Reason = CastField<FStrProperty>(Node.Property(TEXT("Reason")));
		TestTrue(Name + TEXT(": the reason out"), Reason && OWBlueprintTest::IsOutputPin(Reason));
		TestNotNull(Name + TEXT(": answers a bool"), CastField<FBoolProperty>(Node.Return()));
		int32 Params = 0;
		for (TFieldIterator<FProperty> P(*It); P && P->HasAnyPropertyFlags(CPF_Parm); ++P) ++Params;
		TestEqual(Name + TEXT(": the text, the struct, the reason and the answer, nothing else"), Params, 4);
		++Doors;
	}
	TestEqual(TEXT("a node per type"), Doors, 22);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsFromJsonWorldTest, "OnlyWorlds.Models.FromJson.World", OWFJ::Flags)

bool FOWModelsFromJsonWorldTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Env : {TEXT("OW_SIKELIA"), TEXT("OW_MOPPETOPIA")})
	{
		if (!OWTestPaths::WorldGiven(*this, Env)) continue;
		const bool bSikelia = FCString::Strcmp(Env, TEXT("OW_SIKELIA")) == 0;
		UOWWorld* World = OWTestPaths::OpenWorld(*this, bSikelia ? OWTestPaths::Sikelia() : OWTestPaths::Moppetopia(), Env);
		if (!World) return false;
		TStrongObjectPtr<UOWWorld> Keep(World);

		int32 Elements = 0, Same = 0;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements)
		{
			const FString Id = OWConvert::Str(E.Id());
			const FString Pas = OWFJ::TypePascal(OWConvert::Str(E.Type()));
			const FString FindName = TEXT("Find") + Pas;
			const UScriptStruct* S = OWFJ::StructOf(UOWElements::StaticClass()->FindFunctionByName(*FindName), Pas);
			if (!S)
			{
				AddError(FString::Printf(TEXT("%s: no Find%s with a struct out"), *Id, *Pas));
				continue;
			}
			++Elements;
			OWBlueprintTest::FCall Find(*this, UOWElements::StaticClass(), *FindName);
			Find.SetWorld(World);
			Find.SetString(TEXT("Id"), Id);
			Find.Invoke();
			const void* FromWorld = Find.ReturnBool() ? Find.OutStruct(*Pas, S) : nullptr;
			const OWFJ::FFromJson FromText = OWFJ::Call(*this, Pas, World->GetElementJson(Id));
			if (!FromWorld || !FromText.bOk || !FromText.Data.IsValid())
			{
				AddError(FString::Printf(TEXT("%s %s: not read (%s)"), *Pas, *Id, *FromText.Reason));
				continue;
			}
			if (S->CompareScriptStruct(FromWorld, FromText.Data->GetStructMemory(), PPF_None)) ++Same;
			else AddError(FString::Printf(TEXT("%s %s: the struct from its text is not the struct Find gives"), *Pas, *Id));
		}
		AddInfo(FString::Printf(TEXT("%s: %d elements, %d the same struct both ways"), Env, Elements, Same));
		TestTrue(FString(Env) + TEXT(": elements read"), Elements > 0);
		TestEqual(FString(Env) + TEXT(": every one the same"), Same, Elements);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsFromJsonRefusedTest, "OnlyWorlds.Models.FromJson.Refused", OWFJ::Flags)

bool FOWModelsFromJsonRefusedTest::RunTest(const FString& Parameters)
{
	const UScriptStruct* S = FOWCharacter::StaticStruct();
	FOWCharacter Empty;
	struct FCase { const TCHAR* Text; const TCHAR* Reason; };
	for (const FCase& C : {FCase{TEXT("{\"name\": "), TEXT("not JSON: ")}, FCase{TEXT("[]"), TEXT("not a JSON object")},
			 FCase{TEXT("\"Ama\""), TEXT("not a JSON object")}, FCase{TEXT(""), TEXT("not JSON: ")}})
	{
		const OWFJ::FFromJson Got = OWFJ::Call(*this, TEXT("Character"), C.Text);
		TestFalse(FString::Printf(TEXT("%s: refused"), C.Text), Got.bOk);
		TestTrue(FString::Printf(TEXT("%s: the reason starts '%s' (it is '%s')"), C.Text, C.Reason, *Got.Reason),
			Got.Reason.StartsWith(C.Reason));
		TestTrue(FString::Printf(TEXT("%s: the struct empty"), C.Text),
			Got.Data.IsValid() && S->CompareScriptStruct(Got.Data->GetStructMemory(), &Empty, PPF_None));
	}
	// A text with keys the struct has no member for (an extension key, world) and a key of the wrong kind still reads.
	const OWFJ::FFromJson Got = OWFJ::Call(*this, TEXT("Character"),
		TEXT("{\"id\": \"c1\", \"name\": \"Ama\", \"world\": \"w1\", \"x_note\": 1, \"birth_date\": \"spring\"}"));
	TestTrue(TEXT("extension keys and a mistyped key: read"), Got.bOk);
	if (Got.Data.IsValid())
	{
		TestEqual(TEXT("its id"), OWBlueprintTest::Member<FStrProperty>(*this, S, Got.Data->GetStructMemory(), TEXT("Id")), FString(TEXT("c1")));
		TestEqual(TEXT("its name"), OWBlueprintTest::Member<FStrProperty>(*this, S, Got.Data->GetStructMemory(), TEXT("Name")), FString(TEXT("Ama")));
		TestFalse(TEXT("a birth date that is not a number stays unset"),
			OWBlueprintTest::BoolMember(*this, S, Got.Data->GetStructMemory(), TEXT("bHasBirthDate")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWModelsFromJsonLiveTest, "OnlyWorldsLive.Models.CharacterFromJson", OWFJ::Flags)

bool FOWModelsFromJsonLiveTest::RunTest(const FString& Parameters)
{
	OnlyWorlds::OWClientConfig Config;
	Config.api_key = "0000000001";
	Config.transport = FOWHttpTransport::Make();
	auto Client = std::make_shared<OnlyWorlds::OWClient>(std::move(Config));

	struct FGot
	{
		int32 Calls = 0;
		OnlyWorlds::OWReply<OnlyWorlds::OWPage> Reply;
	};
	auto A = std::make_shared<FGot>();
	OnlyWorlds::OWListOptions Options;
	Options.limit = 1;
	Client->List("character", Options, [A](OnlyWorlds::OWReply<OnlyWorlds::OWPage> R)
	{
		++A->Calls;
		A->Reply = std::move(R);
	});

	const double Start = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, A, Client, Start]()
	{
		if (A->Calls == 0 && FPlatformTime::Seconds() - Start < 40.0) return false;
		TestEqual(TEXT("answered once"), A->Calls, 1);
		if (!A->Reply.Ok())
		{
			AddError(UTF8_TO_TCHAR(A->Reply.error->ToString().c_str()));
			return true;
		}
		if (!TestEqual(TEXT("one Character on the page"), static_cast<int32>(A->Reply.value.data.size()), 1)) return true;
		const OnlyWorlds::OWJsonValue& Body = A->Reply.value.data[0];
		const OnlyWorlds::OWJsonValue* Id = Body.Find("id");
		const OnlyWorlds::OWJsonValue* Name = Body.Find("name");
		// The text a read node answers: the body stringified with its number texts kept (OWClientNodes.cpp, JsonText).
		const FString Text = OWConvert::Str(OnlyWorlds::OWJson::Stringify(Body, /*keep_number_text=*/true));
		const OWModelsFromJsonTest::FFromJson Got = OWModelsFromJsonTest::Call(*this, TEXT("Character"), Text);
		TestTrue(TEXT("read"), Got.bOk);
		if (!Got.Data.IsValid()) return true;
		const UScriptStruct* S = FOWCharacter::StaticStruct();
		const FString GotId = OWBlueprintTest::Member<FStrProperty>(*this, S, Got.Data->GetStructMemory(), TEXT("Id"));
		const FString GotName = OWBlueprintTest::Member<FStrProperty>(*this, S, Got.Data->GetStructMemory(), TEXT("Name"));
		TestEqual(TEXT("the served id"), GotId, Id && Id->IsString() ? OWConvert::Str(Id->Text()) : FString());
		TestEqual(TEXT("the served name"), GotName, Name && Name->IsString() ? OWConvert::Str(Name->Text()) : FString());
		TestFalse(TEXT("a name"), GotName.IsEmpty());
		AddInfo(FString::Printf(TEXT("Moppetopia's Character %s, %s, broken from the API's text"), *GotName, *GotId));
		return true;
	}));
	return true;
}

#endif
