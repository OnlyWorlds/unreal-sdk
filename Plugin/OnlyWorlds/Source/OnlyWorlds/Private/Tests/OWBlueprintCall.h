#pragma once
// One call of a Blueprint library function as a graph makes it, for the automation tests: the UFUNCTION found by name
// on its library's class, its flags asserted (what puts it in a graph's menu), a parameter frame allocated from the
// UFunction itself, each parameter set through its property (whose class is asserted), ProcessEvent on the class
// default object (the Kismet VM's path to a static library function), and the answer read back by property, a
// struct's members and an array's rows too, as Make and Break nodes read them. Never the C++ function directly.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "OWWorldObject.h"

namespace OWBlueprintTest
{
	// What a graph's call node makes an output pin of: an out parameter that is not a reference (a const& is passed
	// by reference, an input), UEdGraphSchema_K2's own test (EdGraphSchema_K2.cpp:1022 in 5.8).
	inline bool IsOutputPin(const FProperty* P)
	{
		return P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ReferenceParm);
	}

	class FCall
	{
	public:
		FCall(FAutomationTestBase& InTest, UClass* InLibrary, const TCHAR* InName)
			: Test(InTest), Library(InLibrary), Name(InName), Func(InLibrary->FindFunctionByName(InName))
		{
			if (!Func)
			{
				Test.AddError(FString::Printf(TEXT("no UFUNCTION %s on %s"), Name, *Library->GetName()));
				return;
			}
			const EFunctionFlags Need = FUNC_Native | FUNC_Static | FUNC_Public | FUNC_BlueprintCallable | FUNC_BlueprintPure;
			if (!Func->HasAllFunctionFlags(Need))
			{
				Test.AddError(FString::Printf(TEXT("%s is not a static, public, pure Blueprint function"), Name));
			}
			Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Func->ParmsSize, 1), Func->GetMinAlignment()));
			Func->InitializeStruct(Frame);
		}

		~FCall()
		{
			if (Frame)
			{
				Func->DestroyStruct(Frame);
				FMemory::Free(Frame);
			}
		}

		FCall(const FCall&) = delete;
		FCall& operator=(const FCall&) = delete;

		// The parameter's slot in the frame, as its property's own type, or nullptr (an error logged).
		template <typename PropT>
		typename PropT::TCppType* Slot(const TCHAR* Param)
		{
			if (!Frame) return nullptr;
			PropT* P = CastField<PropT>(Func->FindPropertyByName(Param));
			if (!P)
			{
				Test.AddError(FString::Printf(TEXT("%s has no %s parameter %s"), Name, *PropT::StaticClass()->GetName(), Param));
				return nullptr;
			}
			return P->template ContainerPtrToValuePtr<typename PropT::TCppType>(Frame);
		}

		void SetWorld(const UOWWorld* World)
		{
			if (!Frame) return;
			FObjectProperty* P = CastField<FObjectProperty>(Func->FindPropertyByName(TEXT("World")));
			if (!P || P->PropertyClass != UOWWorld::StaticClass())
			{
				Test.AddError(FString::Printf(TEXT("%s has no UOWWorld parameter World"), Name));
				return;
			}
			P->SetObjectPropertyValue_InContainer(Frame, const_cast<UOWWorld*>(World));
		}

		void SetString(const TCHAR* Param, const FString& Value)
		{
			if (FString* S = Slot<FStrProperty>(Param)) *S = Value;
		}

		void SetInt64(const TCHAR* Param, int64 Value)
		{
			if (int64* V = Slot<FInt64Property>(Param)) *V = Value;
		}

		void SetBool(const TCHAR* Param, bool Value)
		{
			if (!Frame) return;
			FBoolProperty* P = CastField<FBoolProperty>(Func->FindPropertyByName(Param));
			if (!P)
			{
				Test.AddError(FString::Printf(TEXT("%s has no bool parameter %s"), Name, Param));
				return;
			}
			P->SetPropertyValue_InContainer(Frame, Value);
		}

		// A struct parameter's slot, for its members to be set one by one (as a Make node does), or nullptr.
		void* StructSlot(const TCHAR* Param, const UScriptStruct* Expected)
		{
			if (!Frame) return nullptr;
			FStructProperty* P = CastField<FStructProperty>(Func->FindPropertyByName(Param));
			if (!P || P->Struct != Expected || IsOutputPin(P))
			{
				Test.AddError(FString::Printf(TEXT("%s has no %s parameter %s"), Name, *Expected->GetName(), Param));
				return nullptr;
			}
			return P->ContainerPtrToValuePtr<void>(Frame);
		}

		void Invoke()
		{
			if (Frame) Library->GetDefaultObject()->ProcessEvent(Func, Frame);
		}

		FProperty* Property(const TCHAR* Param) const { return Func ? Func->FindPropertyByName(Param) : nullptr; }
		FProperty* Return() const { return Func ? Func->GetReturnProperty() : nullptr; }
		uint8* GetFrame() const { return Frame; }
		const TCHAR* GetName() const { return Name; }

		// The bool return, or false (an error logged).
		bool ReturnBool()
		{
			const FBoolProperty* Ret = CastField<FBoolProperty>(Return());
			if (!Ret || !Frame)
			{
				Test.AddError(FString::Printf(TEXT("%s does not return a bool"), Name));
				return false;
			}
			return Ret->GetPropertyValue_InContainer(Frame);
		}

		// An out parameter's value, as its property's own type (its class and its out flag asserted).
		template <typename PropT>
		typename PropT::TCppType Out(const TCHAR* Param)
		{
			const PropT* P = CastField<PropT>(Property(Param));
			if (!P || !Frame || !IsOutputPin(P))
			{
				Test.AddError(FString::Printf(TEXT("%s has no %s out parameter %s"), Name, *PropT::StaticClass()->GetName(), Param));
				return typename PropT::TCppType();
			}
			return *P->template ContainerPtrToValuePtr<typename PropT::TCppType>(Frame);
		}

		// An out struct parameter's data and its struct (asserted), or nullptr.
		const void* OutStruct(const TCHAR* Param, const UScriptStruct* Expected)
		{
			const FStructProperty* P = CastField<FStructProperty>(Property(Param));
			if (!P || !Frame || P->Struct != Expected || !IsOutputPin(P))
			{
				Test.AddError(FString::Printf(TEXT("%s has no %s out parameter %s"), Name, *Expected->GetName(), Param));
				return nullptr;
			}
			return P->ContainerPtrToValuePtr<void>(Frame);
		}

	private:
		FAutomationTestBase& Test;
		UClass* Library;
		const TCHAR* Name;
		UFunction* Func;
		uint8* Frame = nullptr;
	};

	// A struct's member read by its property, as a Break node reads it.
	template <typename PropT>
	typename PropT::TCppType Member(FAutomationTestBase& Test, const UScriptStruct* Struct, const void* Data, const TCHAR* Name)
	{
		const PropT* P = CastField<PropT>(Struct->FindPropertyByName(Name));
		if (!P || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no %s member %s"), *Struct->GetName(), *PropT::StaticClass()->GetName(), Name));
			return typename PropT::TCppType();
		}
		return *P->template ContainerPtrToValuePtr<typename PropT::TCppType>(Data);
	}

	inline bool BoolMember(FAutomationTestBase& Test, const UScriptStruct* Struct, const void* Data, const TCHAR* Name)
	{
		const FBoolProperty* P = CastField<FBoolProperty>(Struct->FindPropertyByName(Name));
		if (!P || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no bool member %s"), *Struct->GetName(), Name));
			return false;
		}
		return P->GetPropertyValue_InContainer(Data);
	}

	// A struct's member set by its property, as a Make node sets it.
	template <typename PropT>
	void SetMember(FAutomationTestBase& Test, const UScriptStruct* Struct, void* Data, const TCHAR* Name,
		const typename PropT::TCppType& Value)
	{
		const PropT* P = CastField<PropT>(Struct->FindPropertyByName(Name));
		if (!P || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no %s member %s"), *Struct->GetName(), *PropT::StaticClass()->GetName(), Name));
			return;
		}
		*P->template ContainerPtrToValuePtr<typename PropT::TCppType>(Data) = Value;
	}

	inline void SetBoolMember(FAutomationTestBase& Test, const UScriptStruct* Struct, void* Data, const TCHAR* Name, bool Value)
	{
		const FBoolProperty* P = CastField<FBoolProperty>(Struct->FindPropertyByName(Name));
		if (!P || !Data)
		{
			Test.AddError(FString::Printf(TEXT("%s has no bool member %s"), *Struct->GetName(), Name));
			return;
		}
		P->SetPropertyValue_InContainer(Data, Value);
	}

	// An enum member set by its property: a UENUM (BlueprintType) of this enum, set by the value's own number.
	inline void SetEnumMember(FAutomationTestBase& Test, const UScriptStruct* Struct, void* Data, const TCHAR* Name,
		const UEnum* Enum, int64 Value)
	{
		const FEnumProperty* P = CastField<FEnumProperty>(Struct->FindPropertyByName(Name));
		if (!P || !Data || P->GetEnum() != Enum)
		{
			Test.AddError(FString::Printf(TEXT("%s has no %s member %s"), *Struct->GetName(), *Enum->GetName(), Name));
			return;
		}
		P->GetUnderlyingProperty()->SetIntPropertyValue(P->ContainerPtrToValuePtr<void>(Data), Value);
	}
}
