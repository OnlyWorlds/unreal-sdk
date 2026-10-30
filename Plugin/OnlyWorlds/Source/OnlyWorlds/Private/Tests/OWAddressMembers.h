#pragma once
// FOWAddress member by member, by its properties, as a Break node reads it and a Make node sets it: shared by the
// address and the openers tests.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWAddressLibrary.h"
#include "OWBlueprintCall.h"

namespace OWAddressMembers
{
	using OWBlueprintTest::BoolMember;
	using OWBlueprintTest::Member;

	// FOWAddress read member by member, by its properties.
	inline FOWAddress BreakAddress(FAutomationTestBase& Test, const void* Data)
	{
		const UScriptStruct* S = FOWAddress::StaticStruct();
		FOWAddress A;
		A.Form = Member<FStrProperty>(Test, S, Data, TEXT("Form"));
		A.World = Member<FStrProperty>(Test, S, Data, TEXT("World"));
		A.bHasWorld = BoolMember(Test, S, Data, TEXT("bHasWorld"));
		A.Type = Member<FStrProperty>(Test, S, Data, TEXT("Type"));
		A.bHasType = BoolMember(Test, S, Data, TEXT("bHasType"));
		A.Id = Member<FStrProperty>(Test, S, Data, TEXT("Id"));
		A.bHasId = BoolMember(Test, S, Data, TEXT("bHasId"));
		A.Lens = Member<FStrProperty>(Test, S, Data, TEXT("Lens"));
		A.bHasLens = BoolMember(Test, S, Data, TEXT("bHasLens"));
		A.Moment = Member<FInt64Property>(Test, S, Data, TEXT("Moment"));
		A.bHasMoment = BoolMember(Test, S, Data, TEXT("bHasMoment"));
		A.bWorldIsSlug = BoolMember(Test, S, Data, TEXT("bWorldIsSlug"));
		A.bCanonical = BoolMember(Test, S, Data, TEXT("bCanonical"));
		return A;
	}

	// FOWAddress set member by member, by its properties.
	inline void MakeAddress(FAutomationTestBase& Test, void* Data, const FOWAddress& A)
	{
		using OWBlueprintTest::SetBoolMember;
		using OWBlueprintTest::SetMember;
		const UScriptStruct* S = FOWAddress::StaticStruct();
		SetMember<FStrProperty>(Test, S, Data, TEXT("Form"), A.Form);
		SetMember<FStrProperty>(Test, S, Data, TEXT("World"), A.World);
		SetBoolMember(Test, S, Data, TEXT("bHasWorld"), A.bHasWorld);
		SetMember<FStrProperty>(Test, S, Data, TEXT("Type"), A.Type);
		SetBoolMember(Test, S, Data, TEXT("bHasType"), A.bHasType);
		SetMember<FStrProperty>(Test, S, Data, TEXT("Id"), A.Id);
		SetBoolMember(Test, S, Data, TEXT("bHasId"), A.bHasId);
		SetMember<FStrProperty>(Test, S, Data, TEXT("Lens"), A.Lens);
		SetBoolMember(Test, S, Data, TEXT("bHasLens"), A.bHasLens);
		SetMember<FInt64Property>(Test, S, Data, TEXT("Moment"), A.Moment);
		SetBoolMember(Test, S, Data, TEXT("bHasMoment"), A.bHasMoment);
		SetBoolMember(Test, S, Data, TEXT("bWorldIsSlug"), A.bWorldIsSlug);
		SetBoolMember(Test, S, Data, TEXT("bCanonical"), A.bCanonical);
	}
}
