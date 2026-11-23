// The generated structs' integers on real worlds: every element of Sikelia and Moppetopia read through
// UOWElements::Find<Type> as a Blueprint calls it (OWBlueprintCall.h), and every integer member held to the element's
// file: a missing key or null is unset, a number is set to its value (0 included). The members are also written out,
// one line each (`id  member  value`, `-` for unset), to Saved/OWTests/integers-<world>.txt, so two builds' readings
// can be compared byte for byte.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "UObject/PropertyOptional.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <charconv>
#include <cmath>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWConvert::Str;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// The generator's member name for a JSON key (generate_models.py's pascal): `birth_date` -> `BirthDate`, `STR` -> `Str`.
	FString Pascal(const std::string& Key)
	{
		const FString K = Str(Key);
		if (K == K.ToUpper() && K != K.ToLower()) return K.Left(1) + K.Mid(1).ToLower();
		TArray<FString> Parts;
		K.ParseIntoArray(Parts, TEXT("_"), false);
		FString Out;
		for (const FString& P : Parts) Out += P.Left(1).ToUpper() + P.Mid(1);
		return Out;
	}

	// What the file says of an integer member, read apart from the plugin's reader. Counts by what the file holds.
	struct FFileCounts
	{
		int32 Absent = 0, Null = 0, Zero = 0, Other = 0, Spelled = 0, NotAnInteger = 0;
	};

	TOptional<int64> FromFile(const OWJsonValue& Body, const FString& Member, FFileCounts& Counts)
	{
		const OWJsonValue* V = nullptr;
		for (size_t I = 0; I < Body.Size(); ++I)
		{
			if (Pascal(Body.Keys()[I]) == Member) V = &Body.Items()[I];
		}
		if (!V) { ++Counts.Absent; return {}; }
		if (V->IsNull()) { ++Counts.Null; return {}; }
		if (!V->IsNumber()) { ++Counts.NotAnInteger; return {}; }
		const std::string& T = V->Text();
		int64_t N = 0;
		const std::from_chars_result R = std::from_chars(T.data(), T.data() + T.size(), N);
		if (R.ec == std::errc() && R.ptr == T.data() + T.size())
		{
			++(N == 0 ? Counts.Zero : Counts.Other);
			return static_cast<int64>(N);
		}
		const double D = V->AsDouble();
		if (std::isfinite(D) && D == std::floor(D)) { ++Counts.Spelled; return static_cast<int64>(D); }
		++Counts.NotAnInteger;
		return {};
	}

	// An integer member of a struct as it stands, in either shape the generator has had: a TOptional<int64>, or an
	// int64 beside its bool bHas<Member>. Unset when the member is not one.
	bool IntegerMember(const UScriptStruct* Struct, const void* Data, const FProperty* P, TOptional<int64>& Out)
	{
		if (const FOptionalProperty* O = CastField<FOptionalProperty>(P))
		{
			if (!CastField<FInt64Property>(O->GetValueProperty())) return false;
			Out = *O->ContainerPtrToValuePtr<TOptional<int64>>(Data);
			return true;
		}
		const FInt64Property* I = CastField<FInt64Property>(P);
		const FBoolProperty* Has = I ? CastField<FBoolProperty>(Struct->FindPropertyByName(*(TEXT("bHas") + P->GetName()))) : nullptr;
		if (!Has) return false;
		Out = Has->GetPropertyValue_InContainer(Data) ? TOptional<int64>(*I->ContainerPtrToValuePtr<int64>(Data)) : TOptional<int64>();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWIntegersTest, "OnlyWorlds.World.Integers", OWTestFlags)

bool FOWIntegersTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("Sikelia"), TEXT("Moppetopia") })
	{
		const bool bSikelia = FCString::Strcmp(Name, TEXT("Sikelia")) == 0;
		if (!OWTestPaths::WorldGiven(*this, bSikelia ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA"))) continue;
		UOWWorld* World = OWTestPaths::OpenWorld(*this, bSikelia ? OWTestPaths::Sikelia() : OWTestPaths::Moppetopia(),
			bSikelia ? TEXT("OW_SIKELIA") : TEXT("OW_MOPPETOPIA"));
		if (!World) return false;

		FString Dump;
		FFileCounts Counts;
		int32 Elements = 0, Members = 0, Same = 0;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements)
		{
			const FString Type = Str(E.Type()), Id = Str(E.Id());
			const FString Pas = Type.Left(1).ToUpper() + Type.Mid(1);
			OWBlueprintTest::FCall Call(*this, UOWElements::StaticClass(), *(TEXT("Find") + Pas));
			Call.SetWorld(World);
			Call.SetString(TEXT("Id"), Id);
			Call.Invoke();
			const FStructProperty* Out = CastField<FStructProperty>(Call.Property(*Pas));
			if (!Call.ReturnBool() || !Out)
			{
				AddError(FString::Printf(TEXT("%s: Find%s did not find %s"), Name, *Pas, *Id));
				continue;
			}
			++Elements;
			const UScriptStruct* Struct = Out->Struct;
			const void* Data = Call.OutStruct(*Pas, Struct);
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				TOptional<int64> Got;
				if (!IntegerMember(Struct, Data, *It, Got)) continue;
				++Members;
				const TOptional<int64> Want = FromFile(E.body, It->GetName(), Counts);
				if (Got == Want) ++Same;
				else if (Members - Same <= 3) AddError(FString::Printf(TEXT("%s %s %s: read %s, the file %s"), Name, *Id, *It->GetName(),
					Got ? *FString::Printf(TEXT("%lld"), *Got) : TEXT("unset"), Want ? *FString::Printf(TEXT("%lld"), *Want) : TEXT("unset")));
				Dump += FString::Printf(TEXT("%s\t%s\t%s\n"), *Id, *It->GetName(), Got ? *FString::Printf(TEXT("%lld"), *Got) : TEXT("-"));
			}
		}
		TestTrue(FString::Printf(TEXT("%s: elements read"), Name), Elements > 0);
		TestTrue(FString::Printf(TEXT("%s: integer members read"), Name), Members > 0);
		TestEqual(FString::Printf(TEXT("%s: every integer member as its file says"), Name), Same, Members);
		const FString Path = OWTestPaths::Scratch(FString::Printf(TEXT("integers-%s.txt"), Name));
		TestTrue(TEXT("the reading written out to ") + Path, FFileHelper::SaveStringToFile(Dump, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
		AddInfo(FString::Printf(TEXT("INTEGERS %s: %d elements, %d integer members; the files: %d absent, %d null, %d zero, %d other "
			"integers, %d integral spelled as a float, %d not an integer"), Name, Elements, Members, Counts.Absent, Counts.Null,
			Counts.Zero, Counts.Other, Counts.Spelled, Counts.NotAnInteger));
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
