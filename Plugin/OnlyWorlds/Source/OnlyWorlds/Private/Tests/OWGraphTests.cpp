// Blueprint graphs made of real K2 nodes, in the editor: a transient Blueprint built in code (as the engine's own tests
// build one, BlueprintGraph's TypePromotionTests.cpp:110-124), its nodes placed by FGraphNodeCreator, wired by the K2
// schema, compiled by the Kismet compiler (FKismetEditorUtilities::CompileBlueprint) and its function run. Three tests:
// which struct members a Break and a Make node give a pin (every one, the integers' flag and value included), a graph
// calling every door (the address, the calendar, the Open with rows, the names) on real worlds, its answers the
// core's, and a graph making and breaking a character's integers. Editor only: the whole file is under WITH_EDITOR, and the module links UnrealEd and BlueprintGraph only in an
// editor target (OnlyWorlds.Build.cs).

#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MakeStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/PropertyOptional.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include "OWAddress.h"
#include "OWAddressLibrary.h"
#include "OWCalendar.h"
#include "OWCalendarLibrary.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWModels.h"
#include "OWNames.h"
#include "OWNamesLibrary.h"
#include "OWOpeners.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const UEdGraphSchema_K2* K2() { return GetDefault<UEdGraphSchema_K2>(); }

	// A Blueprint of UObject in the transient package, as TypePromotionTests' MakeTestableBP makes one of AActor.
	UBlueprint* NewBlueprint(const TCHAR* Name)
	{
		const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(Name));
		return FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Unique, BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	}

	// A node placed as the editor places one: created, given its struct or function, then finalized (a GUID,
	// PostPlacedNewNode, AllocateDefaultPins: EdGraph.h's FGraphNodeCreator).
	template <typename NodeT>
	NodeT* PlaceStructNode(UEdGraph& Graph, UScriptStruct* Struct)
	{
		FGraphNodeCreator<NodeT> Creator(Graph);
		NodeT* Node = Creator.CreateNode(false);
		Node->StructType = Struct;
		Creator.Finalize();
		return Node;
	}

	UK2Node_CallFunction* PlaceCall(UEdGraph& Graph, UClass* Library, const TCHAR* Function)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->SetFromFunction(Library->FindFunctionByName(Function));
		Creator.Finalize();
		return Node;
	}

	FString PinTypeText(const UEdGraphPin* Pin)
	{
		if (!Pin) return TEXT("-");
		FString S = Pin->PinType.PinCategory.ToString();
		if (!Pin->PinType.PinSubCategory.IsNone()) S += TEXT("/") + Pin->PinType.PinSubCategory.ToString();
		if (const UObject* Sub = Pin->PinType.PinSubCategoryObject.Get()) S += TEXT(":") + Sub->GetName();
		if (Pin->PinType.ContainerType == EPinContainerType::Array) S = TEXT("array of ") + S;
		return S;
	}

	UEdGraphPin* PinOf(UEdGraphNode* Node, const FName Name, EEdGraphPinDirection Direction)
	{
		return Node ? Node->FindPin(Name, Direction) : nullptr;
	}

	FString PropertyText(const FProperty* P)
	{
		if (const FOptionalProperty* O = CastField<FOptionalProperty>(P))
			return FString::Printf(TEXT("TOptional<%s>"), *O->GetValueProperty()->GetCPPType());
		return P->GetCPPType();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWGraphPinsTest, "OnlyWorlds.Graph.Pins", OWTestFlags)

bool FOWGraphPinsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UBlueprint> BP(NewBlueprint(TEXT("OWPinsProbe")));
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP.Get()) : nullptr;
	if (!TestNotNull(TEXT("a Blueprint with an event graph"), Graph)) return false;

	// Every struct of the plugin's module: the 22 generated ones and the doors' own.
	TArray<UScriptStruct*> Structs;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetOutermost()->GetName() == TEXT("/Script/OnlyWorlds")) Structs.Add(*It);
	}
	Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName() < B.GetName(); });
	// The 22 generated, and FOWArchiveVerdict, FOWWorldCalendar, FOWStepBreakdown, FOWAddress, FOWOpenerSurface, FOWOpener,
	// FOWSender, FOWEdited.
	TestEqual(TEXT("the plugin's structs"), Structs.Num(), 22 + 8);

	int32 Optionals = 0, OptionalBreakPins = 0, OptionalMakePins = 0, Members = 0, Pairs = 0, PairsWithPins = 0;
	TArray<FString> OptionalMembers;
	for (UScriptStruct* Struct : Structs)
	{
		UK2Node_BreakStruct* Break = PlaceStructNode<UK2Node_BreakStruct>(*Graph, Struct);
		UK2Node_MakeStruct* Make = PlaceStructNode<UK2Node_MakeStruct>(*Graph, Struct);
		int32 BreakPins = 0, MakePins = 0, StructMembers = 0;
		TArray<FString> NoBreak, Rows;
		const bool bDetail = Struct == FOWCharacter::StaticStruct() || Struct == FOWAddress::StaticStruct() || Struct == FOWOpener::StaticStruct() || Struct == FOWOpenerSurface::StaticStruct()
			|| Struct == FOWStepBreakdown::StaticStruct() || Struct == FOWWorldCalendar::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* P = *It;
			++Members;
			++StructMembers;
			const UEdGraphPin* Out = PinOf(Break, P->GetFName(), EGPD_Output);
			const UEdGraphPin* In = PinOf(Make, P->GetFName(), EGPD_Input);
			BreakPins += Out != nullptr;
			MakePins += In != nullptr;
			FEdGraphPinType Converted;
			const bool bConvertible = K2()->ConvertPropertyToPinType(P, Converted);
			const bool bOptional = CastField<FOptionalProperty>(P) != nullptr;
			if (bOptional)
			{
				++Optionals;
				OptionalBreakPins += Out != nullptr;
				OptionalMakePins += In != nullptr;
				OptionalMembers.Add(Struct->GetName() + TEXT(".") + P->GetName());
				// The schema's own answer for the property, beside the node's.
				TestEqual(Struct->GetName() + TEXT(".") + P->GetName() + TEXT(": a Break pin exactly when the schema converts it"),
					Out != nullptr, bConvertible && P->HasAnyPropertyFlags(CPF_BlueprintVisible));
			}
			else if (P->HasAnyPropertyFlags(CPF_BlueprintVisible))
			{
				// Every other visible member gets a Break pin, and a Make pin unless it is read-only.
				TestNotNull(Struct->GetName() + TEXT(".") + P->GetName() + TEXT(": a Break pin"), Out);
				if (!P->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
					TestNotNull(Struct->GetName() + TEXT(".") + P->GetName() + TEXT(": a Make pin"), In);
				else TestNull(Struct->GetName() + TEXT(".") + P->GetName() + TEXT(": read-only, no Make pin"), In);
			}
			// A generated integer: an int64 beside its bool bHas<Member> (gaming/DECISIONS.md 14), each with both pins. A
			// generated struct is one UOWElements finds (FOWAddress's Moment has the same shape, and is the door's own).
			const bool bGenerated = UOWElements::StaticClass()->FindFunctionByName(*(TEXT("Find") + Struct->GetName().Mid(2))) != nullptr;
			const FBoolProperty* Flag = bGenerated && CastField<FInt64Property>(P)
				? CastField<FBoolProperty>(Struct->FindPropertyByName(*(TEXT("bHas") + P->GetName()))) : nullptr;
			if (Flag)
			{
				++Pairs;
				const UEdGraphPin* FlagOut = PinOf(Break, Flag->GetFName(), EGPD_Output);
				const UEdGraphPin* FlagIn = PinOf(Make, Flag->GetFName(), EGPD_Input);
				const FName Int64 = UEdGraphSchema_K2::PC_Int64, Bool = UEdGraphSchema_K2::PC_Boolean;
				PairsWithPins += Out && In && FlagOut && FlagIn && Out->PinType.PinCategory == Int64 && In->PinType.PinCategory == Int64
					&& FlagOut->PinType.PinCategory == Bool && FlagIn->PinType.PinCategory == Bool;
			}
			if (!Out) NoBreak.Add(P->GetName());
			if (bDetail)
				Rows.Add(FString::Printf(TEXT("  %s | %s | break %s | make %s | schema %s"), *P->GetName(), *PropertyText(P),
					*PinTypeText(Out), *PinTypeText(In), bConvertible ? *K2()->TypeToText(Converted).ToString() : TEXT("no pin type")));
		}
		AddInfo(FString::Printf(TEXT("PINS %s: %d members, Break %d pins, Make %d pins; no Break pin: %s"), *Struct->GetName(),
			StructMembers, BreakPins, MakePins, NoBreak.Num() ? *FString::Join(NoBreak, TEXT(", ")) : TEXT("none")));
		for (const FString& Row : Rows) AddInfo(TEXT("PIN ") + Struct->GetName() + Row);
	}
	AddInfo(FString::Printf(TEXT("PINS TOptional: %d members across the plugin's structs, %d with a Break pin, %d with a Make pin: %s"),
		Optionals, OptionalBreakPins, OptionalMakePins, *FString::Join(OptionalMembers, TEXT(", "))));
	AddInfo(FString::Printf(TEXT("PINS integers: %d bHas<Field> + int64 <Field> pairs, %d with a bool and an Integer64 pin on both nodes"),
		Pairs, PairsWithPins));
	// In 5.8 a TOptional member gets no pin on a Break or a Make (the K2 schema converts no FOptionalProperty, 70
	// members measured at unreal-sdk 94d56a6), so the generator writes each integer as a flag and a value
	// (gaming/DECISIONS.md 14): the 70 are pairs now, every one with both pins, and no TOptional is left.
	TestEqual(TEXT("the generated structs' TOptional members"), Optionals, 0);
	TestEqual(TEXT("the generated structs' integers as bHas<Field> + int64 <Field>"), Pairs, 70);
	TestEqual(TEXT("the integer pairs with a bool and an Integer64 pin on a Break and on a Make"), PairsWithPins, 70);

	// The doors' own types, by the category a pin carries.
	UK2Node_MakeStruct* Address = PlaceStructNode<UK2Node_MakeStruct>(*Graph, FOWAddress::StaticStruct());
	const UEdGraphPin* Moment = PinOf(Address, TEXT("Moment"), EGPD_Input);
	TestTrue(TEXT("FOWAddress.Moment: an Integer64 pin"), Moment && Moment->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64);
	UK2Node_MakeStruct* Surface = PlaceStructNode<UK2Node_MakeStruct>(*Graph, FOWOpenerSurface::StaticStruct());
	for (const TPair<const TCHAR*, UEnum*>& E : { TPair<const TCHAR*, UEnum*>(TEXT("Audience"), StaticEnum<EOWAudience>()),
		TPair<const TCHAR*, UEnum*>(TEXT("SignIn"), StaticEnum<EOWSignIn>()) })
	{
		const UEdGraphPin* Pin = PinOf(Surface, E.Key, EGPD_Input);
		TestTrue(FString::Printf(TEXT("FOWOpenerSurface.%s: a byte pin of its enum (%s)"), E.Key, *PinTypeText(Pin)),
			Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject == E.Value);
	}
	UK2Node_BreakStruct* Breakdown = PlaceStructNode<UK2Node_BreakStruct>(*Graph, FOWStepBreakdown::StaticStruct());
	for (const TCHAR* Name : { TEXT("Step"), TEXT("AbsYear"), TEXT("Year"), TEXT("Day") })
	{
		const UEdGraphPin* Pin = PinOf(Breakdown, Name, EGPD_Output);
		TestTrue(FString::Printf(TEXT("FOWStepBreakdown.%s: an Integer64 pin"), Name), Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64);
	}
	return true;
}

namespace
{
	// The graph's mutants: each must be caught, by the compiler or by the answers.
	enum class EGraphMutant
	{
		None,
		StepIntoCalendarId,  // an Integer64 wired into a String pin past the schema: the compiler's to refuse
		TypeAndIdSwapped,    // the Break's Type into the Make's Id and back: compiles, the address is wrong
		NoTypeWord,          // the TypeWord node left out: compiles, the word is never asked
	};

	struct FBuiltGraph
	{
		TStrongObjectPtr<UBlueprint> Blueprint;
		int32 Errors = 0, Warnings = 0, Links = 0, Refused = 0;
		TArray<FString> Messages;
		bool bCompiled = false;
	};

	FEdGraphPinType PinType(FName Category, UObject* Sub = nullptr, bool bArray = false)
	{
		FEdGraphPinType T;
		T.PinCategory = Category;
		T.PinSubCategoryObject = Sub;
		if (bArray) T.ContainerType = EPinContainerType::Array;
		return T;
	}

	// Run(World, Link, CalendarId, Step, Type, Supertype) -> bParsed, ParseReason, bWritten, Written, WriteReason,
	// Words, bOpeners, Rows, OpenersReason, bWord, Word: ParseAddress -> Break -> Make -> WriteAddress and
	// OpenersForAddress (with a Make of the surface: a member, in a game), FormatStep, TypeWord (bMany true).
	FBuiltGraph BuildRunGraph(EGraphMutant Mutant)
	{
		FBuiltGraph Out;
		UBlueprint* BP = NewBlueprint(TEXT("OWDoorsGraph"));
		Out.Blueprint.Reset(BP);
		UEdGraph* G = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("Run"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, G, /*bIsUserCreated=*/true, nullptr);
		TArray<UK2Node_FunctionEntry*> Entries;
		G->GetNodesOfClass(Entries);
		UK2Node_FunctionEntry* Entry = Entries.Num() == 1 ? Entries[0] : nullptr;
		if (!Entry) return Out;
		UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);

		const FName S = UEdGraphSchema_K2::PC_String, B = UEdGraphSchema_K2::PC_Boolean;
		UEdGraphPin* InWorld = Entry->CreateUserDefinedPin(TEXT("World"), PinType(UEdGraphSchema_K2::PC_Object, UOWWorld::StaticClass()), EGPD_Output);
		UEdGraphPin* InLink = Entry->CreateUserDefinedPin(TEXT("Link"), PinType(S), EGPD_Output);
		UEdGraphPin* InCalendar = Entry->CreateUserDefinedPin(TEXT("CalendarId"), PinType(S), EGPD_Output);
		UEdGraphPin* InStep = Entry->CreateUserDefinedPin(TEXT("Step"), PinType(UEdGraphSchema_K2::PC_Int64), EGPD_Output);
		UEdGraphPin* InType = Entry->CreateUserDefinedPin(TEXT("Type"), PinType(S), EGPD_Output);
		UEdGraphPin* InSupertype = Entry->CreateUserDefinedPin(TEXT("Supertype"), PinType(S), EGPD_Output);
		auto Output = [Result](const TCHAR* Name, FEdGraphPinType Type) { return Result->CreateUserDefinedPin(Name, Type, EGPD_Input); };
		UEdGraphPin* OutParsed = Output(TEXT("bParsed"), PinType(B));
		UEdGraphPin* OutParseReason = Output(TEXT("ParseReason"), PinType(S));
		UEdGraphPin* OutWritten = Output(TEXT("bWritten"), PinType(B));
		UEdGraphPin* OutText = Output(TEXT("Written"), PinType(S));
		UEdGraphPin* OutWriteReason = Output(TEXT("WriteReason"), PinType(S));
		UEdGraphPin* OutWords = Output(TEXT("Words"), PinType(S));
		UEdGraphPin* OutOpeners = Output(TEXT("bOpeners"), PinType(B));
		UEdGraphPin* OutRows = Output(TEXT("Rows"), PinType(UEdGraphSchema_K2::PC_Struct, FOWOpener::StaticStruct(), true));
		UEdGraphPin* OutOpenersReason = Output(TEXT("OpenersReason"), PinType(S));
		UEdGraphPin* OutHasWord = Output(TEXT("bWord"), PinType(B));
		UEdGraphPin* OutWord = Output(TEXT("Word"), PinType(S));

		auto Link = [&Out](UEdGraphPin* A, UEdGraphPin* Bp)
		{
			++Out.Links;
			if (!A || !Bp || !K2()->TryCreateConnection(A, Bp)) ++Out.Refused;
		};
		const FName Ret = UEdGraphSchema_K2::PN_ReturnValue;
		Link(Entry->GetThenPin(), Result->GetExecPin());

		UK2Node_CallFunction* Parse = PlaceCall(*G, UOWAddresses::StaticClass(), TEXT("ParseAddress"));
		Link(InLink, Parse->FindPin(TEXT("Text")));
		Link(Parse->FindPin(Ret), OutParsed);
		Link(Parse->FindPin(TEXT("Reason")), OutParseReason);
		UK2Node_BreakStruct* Break = PlaceStructNode<UK2Node_BreakStruct>(*G, FOWAddress::StaticStruct());
		UK2Node_MakeStruct* Make = PlaceStructNode<UK2Node_MakeStruct>(*G, FOWAddress::StaticStruct());
		Link(Parse->FindPin(TEXT("Address")), Break->FindPin(FOWAddress::StaticStruct()->GetFName(), EGPD_Input));
		// Every member through, by name, as a person wires a Break into a Make.
		for (UEdGraphPin* From : Break->Pins)
		{
			if (From->Direction != EGPD_Output) continue;
			FName To = From->PinName;
			if (Mutant == EGraphMutant::TypeAndIdSwapped && (To == TEXT("Type") || To == TEXT("Id")))
				To = To == TEXT("Type") ? FName(TEXT("Id")) : FName(TEXT("Type"));
			Link(From, Make->FindPin(To, EGPD_Input));
		}
		UEdGraphPin* Address = Make->FindPin(FOWAddress::StaticStruct()->GetFName(), EGPD_Output);
		UK2Node_CallFunction* Write = PlaceCall(*G, UOWAddresses::StaticClass(), TEXT("WriteAddress"));
		Link(Address, Write->FindPin(TEXT("Address")));
		Link(Write->FindPin(Ret), OutWritten);
		Link(Write->FindPin(TEXT("Text")), OutText);
		Link(Write->FindPin(TEXT("Reason")), OutWriteReason);

		UK2Node_CallFunction* Format = PlaceCall(*G, UOWCalendars::StaticClass(), TEXT("FormatStep"));
		Link(InWorld, Format->FindPin(TEXT("World")));
		if (Mutant == EGraphMutant::StepIntoCalendarId)
		{
			// Past the schema, which would refuse it: MakeLinkTo joins any two pins.
			InStep->MakeLinkTo(Format->FindPin(TEXT("CalendarId")));
		}
		else
		{
			Link(InCalendar, Format->FindPin(TEXT("CalendarId")));
		}
		Link(InStep, Format->FindPin(TEXT("Step")));
		Link(Format->FindPin(Ret), OutWords);

		UK2Node_MakeStruct* Surface = PlaceStructNode<UK2Node_MakeStruct>(*G, FOWOpenerSurface::StaticStruct());
		if (UEdGraphPin* Audience = Surface->FindPin(TEXT("Audience"), EGPD_Input)) K2()->TrySetDefaultValue(*Audience, TEXT("Member"));
		UK2Node_CallFunction* Openers = PlaceCall(*G, UOWOpeners::StaticClass(), TEXT("OpenersForAddress"));
		Link(Address, Openers->FindPin(TEXT("Address")));
		Link(Surface->FindPin(FOWOpenerSurface::StaticStruct()->GetFName(), EGPD_Output), Openers->FindPin(TEXT("Surface")));
		Link(Openers->FindPin(Ret), OutOpeners);
		Link(Openers->FindPin(TEXT("Rows")), OutRows);
		Link(Openers->FindPin(TEXT("Reason")), OutOpenersReason);

		if (Mutant != EGraphMutant::NoTypeWord)
		{
			UK2Node_CallFunction* Names = PlaceCall(*G, UOWNames::StaticClass(), TEXT("TypeWord"));
			Link(InWorld, Names->FindPin(TEXT("World")));
			Link(InType, Names->FindPin(TEXT("Type")));
			Link(InSupertype, Names->FindPin(TEXT("Supertype")));
			if (UEdGraphPin* Many = Names->FindPin(TEXT("bMany"))) K2()->TrySetDefaultValue(*Many, TEXT("true"));
			Link(Names->FindPin(Ret), OutHasWord);
			Link(Names->FindPin(TEXT("Word")), OutWord);
		}

		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		Out.Errors = Results.NumErrors;
		Out.Warnings = Results.NumWarnings;
		for (const TSharedRef<FTokenizedMessage>& M : Results.Messages)
		{
			if (M->GetSeverity() < EMessageSeverity::Info)
				Out.Messages.Add(M->ToText().ToString());
		}
		Out.bCompiled = BP->Status == BS_UpToDate && BP->GeneratedClass != nullptr;
		return Out;
	}

	struct FRow
	{
		FString Id, Name, Href, State;
		bool operator==(const FRow& O) const { return Id == O.Id && Name == O.Name && Href == O.Href && State == O.State; }
	};

	struct FAnswer
	{
		bool bParsed = false, bWritten = false, bOpeners = false, bWord = false;
		FString ParseReason, Written, WriteReason, Words, OpenersReason, Word;
		TArray<FRow> Rows;
		bool operator==(const FAnswer& O) const
		{
			return bParsed == O.bParsed && bWritten == O.bWritten && bOpeners == O.bOpeners && bWord == O.bWord
				&& ParseReason == O.ParseReason && Written == O.Written && WriteReason == O.WriteReason && Words == O.Words
				&& OpenersReason == O.OpenersReason && Word == O.Word && Rows == O.Rows;
		}
		FString Show() const
		{
			return FString::Printf(TEXT("parsed %d (%s), written %d \"%s\" (%s), words \"%s\", openers %d (%d rows%s%s) (%s), word %d \"%s\""),
				bParsed, *ParseReason, bWritten, *Written, *WriteReason, *Words, bOpeners, Rows.Num(), Rows.Num() ? TEXT(", the last ") : TEXT(""),
				Rows.Num() ? *(Rows.Last().Href + TEXT(" ") + Rows.Last().State) : TEXT(""), *OpenersReason, bWord, *Word);
		}
	};

	struct FInputs
	{
		const UOWWorld* World = nullptr;
		FString Link, CalendarId, Type, Supertype;
		int64 Step = 0;
		std::optional<OnlyWorlds::OWCalendarConfig> Calendar;  // what the core reads CalendarId as, for the expected words
	};

	// The calendar an id names among the world's (a refused value is none), or for "" the only one: the profile's
	// reading, from the core's WorldCalendars, apart from the door's own lookup.
	std::optional<OnlyWorlds::OWCalendarConfig> CalendarFor(const UOWWorld* World, const FString& CalendarId)
	{
		std::vector<OWJsonValue> Bodies;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements) Bodies.push_back(E.body);
		const std::vector<OnlyWorlds::OWWorldCalendar> All = OnlyWorlds::OWCalendar::WorldCalendars(Bodies);
		if (CalendarId.IsEmpty()) return All.size() == 1 ? std::optional<OnlyWorlds::OWCalendarConfig>(All[0].calendar) : std::nullopt;
		for (const OnlyWorlds::OWWorldCalendar& C : All)
			if (C.id == Utf8(CalendarId)) return C.calendar;
		return std::nullopt;
	}

	// The compiled function called on an instance of the generated class, its parameters set and read by property.
	FAnswer RunGraph(FAutomationTestBase& Test, UObject* Instance, const FInputs& In)
	{
		FAnswer A;
		UFunction* Run = Instance->GetClass()->FindFunctionByName(TEXT("Run"));
		if (!Run)
		{
			Test.AddError(TEXT("the generated class has no Run"));
			return A;
		}
		// A compiled Blueprint function's struct holds its locals after its parameters: the frame is ParmsSize, and
		// only the parameters are initialized and destroyed in it (as UObject::CallFunctionByNameWithArguments builds
		// one, ScriptCore.cpp:1432-1442); ProcessEvent makes the locals' frame itself.
		uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Run->ParmsSize, 1), Run->GetMinAlignment()));
		FMemory::Memzero(Frame, Run->ParmsSize);
		for (TFieldIterator<FProperty> It(Run); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->InitializeValue_InContainer(Frame);
		auto Prop = [&](const TCHAR* Name) { return Run->FindPropertyByName(Name); };
		auto SetStr = [&](const TCHAR* Name, const FString& V) {
			if (FStrProperty* P = CastField<FStrProperty>(Prop(Name))) P->SetPropertyValue_InContainer(Frame, V);
			else Test.AddError(FString::Printf(TEXT("Run has no string parameter %s"), Name));
		};
		if (FObjectProperty* P = CastField<FObjectProperty>(Prop(TEXT("World")))) P->SetObjectPropertyValue_InContainer(Frame, const_cast<UOWWorld*>(In.World));
		else Test.AddError(TEXT("Run has no object parameter World"));
		SetStr(TEXT("Link"), In.Link);
		SetStr(TEXT("CalendarId"), In.CalendarId);
		SetStr(TEXT("Type"), In.Type);
		SetStr(TEXT("Supertype"), In.Supertype);
		if (FInt64Property* P = CastField<FInt64Property>(Prop(TEXT("Step")))) P->SetPropertyValue_InContainer(Frame, In.Step);
		else Test.AddError(TEXT("Run has no Integer64 parameter Step"));

		Instance->ProcessEvent(Run, Frame);

		auto GetBool = [&](const TCHAR* Name) {
			const FBoolProperty* P = CastField<FBoolProperty>(Prop(Name));
			if (!P) Test.AddError(FString::Printf(TEXT("Run has no bool output %s"), Name));
			return P && P->GetPropertyValue_InContainer(Frame);
		};
		auto GetStr = [&](const TCHAR* Name) {
			const FStrProperty* P = CastField<FStrProperty>(Prop(Name));
			if (!P) Test.AddError(FString::Printf(TEXT("Run has no string output %s"), Name));
			return P ? P->GetPropertyValue_InContainer(Frame) : FString();
		};
		A.bParsed = GetBool(TEXT("bParsed"));
		A.ParseReason = GetStr(TEXT("ParseReason"));
		A.bWritten = GetBool(TEXT("bWritten"));
		A.Written = GetStr(TEXT("Written"));
		A.WriteReason = GetStr(TEXT("WriteReason"));
		A.Words = GetStr(TEXT("Words"));
		A.bOpeners = GetBool(TEXT("bOpeners"));
		A.OpenersReason = GetStr(TEXT("OpenersReason"));
		A.bWord = GetBool(TEXT("bWord"));
		A.Word = GetStr(TEXT("Word"));
		const FArrayProperty* Rows = CastField<FArrayProperty>(Prop(TEXT("Rows")));
		const FStructProperty* Inner = Rows ? CastField<FStructProperty>(Rows->Inner) : nullptr;
		if (Inner && Inner->Struct == FOWOpener::StaticStruct())
		{
			FScriptArrayHelper Array(Rows, Rows->ContainerPtrToValuePtr<void>(Frame));
			for (int32 I = 0; I < Array.Num(); ++I)
			{
				const FOWOpener* R = reinterpret_cast<const FOWOpener*>(Array.GetRawPtr(I));
				A.Rows.Add({ R->Id, R->Name, R->Href, R->State });
			}
		}
		else
		{
			Test.AddError(TEXT("Run has no TArray<FOWOpener> output Rows"));
		}
		for (TFieldIterator<FProperty> It(Run); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->DestroyValue_InContainer(Frame);
		FMemory::Free(Frame);
		return A;
	}

	// What the core answers for the same inputs: the graph's own chain, step by step.
	FAnswer CoreAnswer(const FInputs& In)
	{
		FAnswer A;
		const OnlyWorlds::OWAddressReading R = OnlyWorlds::OWAddress::Parse(Utf8(In.Link));
		A.bParsed = R.ok;
		A.ParseReason = Str(R.reason);
		// A refused reading leaves an empty address, which the Make carries on: no form, no part.
		OnlyWorlds::OWAddressParts P;
		if (R.ok)
		{
			P.form = R.form;
			P.world = R.world;
			P.type = R.type;
			P.id = R.id;
			P.lens = R.lens;
			P.moment = R.moment;
		}
		const OnlyWorlds::OWAddressWritten W = OnlyWorlds::OWAddress::Write(P);
		A.bWritten = W.ok;
		A.Written = Str(W.address);
		A.WriteReason = Str(W.reason);

		const OnlyWorlds::OWWorld* Native = In.World ? In.World->Native() : nullptr;
		A.Words = Str(OnlyWorlds::OWCalendar::FormatStep(In.Calendar, static_cast<int64_t>(In.Step)));

		OnlyWorlds::OWOpenerSurface Surface;
		Surface.audience = OnlyWorlds::OWOpeners::Member;
		Surface.browser = false;
		const OnlyWorlds::OWOpenersResult O = OnlyWorlds::OWOpeners::OpenersFor(OnlyWorlds::OWTools::All(),
			P.id ? OnlyWorlds::OWOpeners::Element : OnlyWorlds::OWOpeners::World, P, Surface);
		A.bOpeners = O.ok;
		A.OpenersReason = Str(O.reason.empty() ? O.message : O.reason);
		for (const OnlyWorlds::OWOpener& Row : O.rows) A.Rows.Add({ Str(Row.id), Str(Row.name), Str(Row.href), Row.state ? Str(*Row.state) : FString() });

		const std::string Super = Utf8(In.Supertype);
		const std::optional<std::string> Word = Native ? OnlyWorlds::OWNames::TypeWord(Native->info, Utf8(In.Type),
			Super.empty() ? std::nullopt : std::optional<std::string_view>(Super), true) : std::nullopt;
		A.bWord = Word.has_value();
		A.Word = Word ? Str(*Word) : FString();
		return A;
	}

	// Every element of the world as one call each: its address in one of the three spellings (a moment on some, a
	// lens on others), its step, its type and supertype.
	TArray<FInputs> ElementInputs(const UOWWorld* World, const FString& CalendarId)
	{
		TArray<FInputs> Out;
		const FString Id = World->GetId();
		const std::optional<OnlyWorlds::OWCalendarConfig> Calendar = CalendarFor(World, CalendarId);
		int32 I = 0;
		for (const OnlyWorlds::OWElement& E : World->Native()->elements)
		{
			FInputs In;
			In.World = World;
			const FString Type = Str(E.Type()), Element = Str(E.Id());
			switch (I % 3)
			{
			case 0: In.Link = FString::Printf(TEXT("https://www.onlyworlds.com/w/%s/%s/%s"), *Id, *Type, *Element); break;
			case 1: In.Link = FString::Printf(TEXT("ow://%s/%s/%s"), *Id, *Type, *Element); break;
			default: In.Link = FString::Printf(TEXT("ow://%s/%s"), *Type, *Element); break;
			}
			if (I % 5 == 0) In.Link += FString::Printf(TEXT("@%d"), I % 2 ? I : -I);
			if (I % 7 == 0) In.Link += TEXT("?lens=019a2b7e-4c1d-7f20-9b3e-1a2c3d4e5f60");
			In.CalendarId = CalendarId;
			In.Calendar = Calendar;
			In.Step = I % 11 == 0 ? 5000000000LL + I : static_cast<int64>(I) - 300;
			In.Type = Type;
			const OWJsonValue* Super = E.body.Find("supertype");
			In.Supertype = Super && Super->IsString() ? Str(Super->Text()) : FString();
			Out.Add(MoveTemp(In));
			++I;
		}
		return Out;
	}

	// Runs every input through the graph and the core; returns how many answers differ (the first few logged).
	int32 Mismatches(FAutomationTestBase& Test, UObject* Instance, const TArray<FInputs>& Inputs, const TCHAR* Label, bool bLog)
	{
		int32 Differ = 0;
		for (const FInputs& In : Inputs)
		{
			const FAnswer Got = RunGraph(Test, Instance, In);
			const FAnswer Want = CoreAnswer(In);
			if (Got == Want) continue;
			if (bLog && ++Differ <= 3) Test.AddError(FString::Printf(TEXT("%s, %s: the graph %s; the core %s"), Label, *In.Link, *Got.Show(), *Want.Show()));
			else if (!bLog) ++Differ;
		}
		return Differ;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWGraphRunTest, "OnlyWorlds.Graph.Run", OWTestFlags)

bool FOWGraphRunTest::RunTest(const FString& Parameters)
{
	UOWWorld* Sikelia = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	UOWWorld* Moppetopia = OWTestPaths::OpenWorld(*this, OWTestPaths::Moppetopia(), TEXT("OW_MOPPETOPIA"));
	if (!Sikelia || !Moppetopia) return false;
	TStrongObjectPtr<UOWWorld> KeepSikelia(Sikelia), KeepMoppetopia(Moppetopia);
	// Sikelia carries no x_names: a map on its World, in memory, so the word is a word.
	OnlyWorlds::OWJsonParseResult Map = OnlyWorlds::OWJson::Parse(std::string_view(R"({"version": 1,
		"types": {"character": {"one": "Employee", "many": "Employees"}, "location": {"one": "Site"}},
		"supertypes": {"character": {"noble": {"one": "Lord", "many": "Lords"}}}})"));
	Sikelia->Native()->SetInfo(OnlyWorlds::OWNames::Field, Map.value);

	const FBuiltGraph Graph = BuildRunGraph(EGraphMutant::None);
	TestEqual(TEXT("every link the schema accepts"), Graph.Refused, 0);
	TestEqual(TEXT("the compile's errors"), Graph.Errors, 0);
	TestEqual(TEXT("the compile's warnings"), Graph.Warnings, 0);
	for (const FString& M : Graph.Messages) AddError(TEXT("compiler: ") + M);
	if (!TestTrue(TEXT("compiled, up to date, a generated class"), Graph.bCompiled)) return false;
	AddInfo(FString::Printf(TEXT("GRAPH Run: %d nodes, %d links, compiled with %d errors and %d warnings"),
		Graph.Blueprint->FunctionGraphs[0]->Nodes.Num(), Graph.Links, Graph.Errors, Graph.Warnings));
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
	TStrongObjectPtr<UObject> KeepInstance(Instance);

	// Sikelia, every element; Moppetopia, every element read in the Treaty Reckoning, by its id and by "".
	const TArray<FInputs> OnSikelia = ElementInputs(Sikelia, FString());
	const TCHAR* const Treaty = TEXT("01a11ff9-e0aa-7e43-9b13-1a1320cf9270");
	TArray<FInputs> OnMoppetopia = ElementInputs(Moppetopia, Treaty);
	TArray<FInputs> OnlyCalendar = ElementInputs(Moppetopia, FString());
	OnMoppetopia.Append(OnlyCalendar);
	TestEqual(TEXT("Sikelia: every element's answers as the core's"), Mismatches(*this, Instance, OnSikelia, TEXT("Sikelia"), true), 0);
	TestEqual(TEXT("Moppetopia: every element's answers as the core's"), Mismatches(*this, Instance, OnMoppetopia, TEXT("Moppetopia"), true), 0);

	// The answers are not empty ones: a word, a calendar's words, four rows, and the refusals carried as values.
	FInputs Ana = OnSikelia[0];
	for (const FInputs& In : OnSikelia) if (In.Type == TEXT("character") && In.Link.StartsWith(TEXT("https"))) { Ana = In; break; }
	const FAnswer A = RunGraph(*this, Instance, Ana);
	TestTrue(TEXT("a character: read, written back, four rows, a word (") + A.Show() + TEXT(")"), A.bParsed && A.bWritten
		&& A.Written == Ana.Link.ToLower() && A.bOpeners && A.Rows.Num() == 4 && A.Rows[1].State == TEXT("Opens in your browser")
		&& A.bWord && (A.Word == TEXT("Employees") || A.Word == TEXT("Lords")));
	FInputs Dated = OnMoppetopia[0];
	Dated.Step = 249;
	TestEqual(TEXT("Moppetopia: step 249 in the Treaty Reckoning"), RunGraph(*this, Instance, Dated).Words, FString(TEXT("Year 1 BT")));
	FInputs Refused = Ana;
	Refused.Link = TEXT("ow://dragon/019a1f3c-2b4e-7d10-8c3a-6f2e9b0d4a71");
	const FAnswer R = RunGraph(*this, Instance, Refused);
	TestTrue(TEXT("a refused link: values, never an error (") + R.Show() + TEXT(")"), !R.bParsed && R.ParseReason == TEXT("unknown_type")
		&& !R.bWritten && R.WriteReason == TEXT("bad_form") && !R.bOpeners && R.OpenersReason == TEXT("no_world") && R.Rows.Num() == 0);
	AddInfo(FString::Printf(TEXT("GRAPH Run: %d calls on Sikelia, %d on Moppetopia, each answer the core's"), OnSikelia.Num(), OnMoppetopia.Num()));

	// The graph's mutants, each caught: by the compiler, or by the answers.
	const FBuiltGraph Wrong = BuildRunGraph(EGraphMutant::StepIntoCalendarId);
	TestTrue(FString::Printf(TEXT("an Integer64 wired into a String pin: the compiler refuses it (%d errors)"), Wrong.Errors),
		Wrong.Errors > 0 && !Wrong.bCompiled);
	AddInfo(FString::Printf(TEXT("GRAPH mutant 1: %d errors, %d warnings: %s"), Wrong.Errors, Wrong.Warnings, *FString::Join(Wrong.Messages, TEXT(" | "))));
	// The other two on every tenth element of Sikelia (each FormatStep call walks the world's elements once).
	TArray<FInputs> Sample;
	for (int32 I = 0; I < OnSikelia.Num(); I += 10) Sample.Add(OnSikelia[I]);
	for (EGraphMutant M : { EGraphMutant::TypeAndIdSwapped, EGraphMutant::NoTypeWord })
	{
		const FBuiltGraph Mutant = BuildRunGraph(M);
		const bool bClean = Mutant.bCompiled && Mutant.Errors == 0 && Mutant.Refused == 0;
		TestTrue(FString::Printf(TEXT("mutant %d compiles (%s)"), static_cast<int32>(M), *FString::Join(Mutant.Messages, TEXT(" | "))), bClean);
		if (!bClean) continue;
		TStrongObjectPtr<UObject> MutantInstance(NewObject<UObject>(GetTransientPackage(), Mutant.Blueprint->GeneratedClass));
		const int32 Differ = Mismatches(*this, MutantInstance.Get(), Sample, TEXT("mutant"), false);
		TestTrue(FString::Printf(TEXT("mutant %d: its answers are not the core's (%d of %d differ)"), static_cast<int32>(M), Differ,
			Sample.Num()), Differ > 0);
		AddInfo(FString::Printf(TEXT("GRAPH mutant %d: %d of %d answers differ from the core's"), static_cast<int32>(M), Differ, Sample.Num()));
	}
	return true;
}

namespace
{
	// Pair(World, Id, bHas, Level) -> bMadeHas, MadeLevel, bFound, bHasBirthDate, BirthDate: a Make of FOWCharacter
	// setting bHasLevel and Level, a Break reading them back; and FindCharacter's struct broken for its birth date.
	FBuiltGraph BuildIntegersGraph()
	{
		FBuiltGraph Out;
		UBlueprint* BP = NewBlueprint(TEXT("OWIntegersGraph"));
		Out.Blueprint.Reset(BP);
		UEdGraph* G = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("Pair"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, G, /*bIsUserCreated=*/true, nullptr);
		TArray<UK2Node_FunctionEntry*> Entries;
		G->GetNodesOfClass(Entries);
		UK2Node_FunctionEntry* Entry = Entries.Num() == 1 ? Entries[0] : nullptr;
		if (!Entry) return Out;
		UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
		const FName B = UEdGraphSchema_K2::PC_Boolean, I = UEdGraphSchema_K2::PC_Int64;
		UEdGraphPin* InWorld = Entry->CreateUserDefinedPin(TEXT("World"), PinType(UEdGraphSchema_K2::PC_Object, UOWWorld::StaticClass()), EGPD_Output);
		UEdGraphPin* InId = Entry->CreateUserDefinedPin(TEXT("Id"), PinType(UEdGraphSchema_K2::PC_String), EGPD_Output);
		UEdGraphPin* InHas = Entry->CreateUserDefinedPin(TEXT("bHas"), PinType(B), EGPD_Output);
		UEdGraphPin* InLevel = Entry->CreateUserDefinedPin(TEXT("Level"), PinType(I), EGPD_Output);
		UEdGraphPin* OutMadeHas = Result->CreateUserDefinedPin(TEXT("bMadeHas"), PinType(B), EGPD_Input);
		UEdGraphPin* OutMadeLevel = Result->CreateUserDefinedPin(TEXT("MadeLevel"), PinType(I), EGPD_Input);
		UEdGraphPin* OutFound = Result->CreateUserDefinedPin(TEXT("bFound"), PinType(B), EGPD_Input);
		UEdGraphPin* OutHasBirth = Result->CreateUserDefinedPin(TEXT("bHasBirthDate"), PinType(B), EGPD_Input);
		UEdGraphPin* OutBirth = Result->CreateUserDefinedPin(TEXT("BirthDate"), PinType(I), EGPD_Input);
		auto Link = [&Out](UEdGraphPin* A, UEdGraphPin* Bp)
		{
			++Out.Links;
			if (!A || !Bp || !K2()->TryCreateConnection(A, Bp)) ++Out.Refused;
		};
		Link(Entry->GetThenPin(), Result->GetExecPin());

		UScriptStruct* Character = FOWCharacter::StaticStruct();
		UK2Node_MakeStruct* Make = PlaceStructNode<UK2Node_MakeStruct>(*G, Character);
		UK2Node_BreakStruct* Break = PlaceStructNode<UK2Node_BreakStruct>(*G, Character);
		Link(InHas, Make->FindPin(TEXT("bHasLevel"), EGPD_Input));
		Link(InLevel, Make->FindPin(TEXT("Level"), EGPD_Input));
		Link(Make->FindPin(Character->GetFName(), EGPD_Output), Break->FindPin(Character->GetFName(), EGPD_Input));
		Link(Break->FindPin(TEXT("bHasLevel"), EGPD_Output), OutMadeHas);
		Link(Break->FindPin(TEXT("Level"), EGPD_Output), OutMadeLevel);

		UK2Node_CallFunction* Find = PlaceCall(*G, UOWElements::StaticClass(), TEXT("FindCharacter"));
		Link(InWorld, Find->FindPin(TEXT("World")));
		Link(InId, Find->FindPin(TEXT("Id")));
		Link(Find->FindPin(UEdGraphSchema_K2::PN_ReturnValue), OutFound);
		UK2Node_BreakStruct* Found = PlaceStructNode<UK2Node_BreakStruct>(*G, Character);
		Link(Find->FindPin(TEXT("Character")), Found->FindPin(Character->GetFName(), EGPD_Input));
		Link(Found->FindPin(TEXT("bHasBirthDate"), EGPD_Output), OutHasBirth);
		Link(Found->FindPin(TEXT("BirthDate"), EGPD_Output), OutBirth);

		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		Out.Errors = Results.NumErrors;
		Out.Warnings = Results.NumWarnings;
		for (const TSharedRef<FTokenizedMessage>& M : Results.Messages)
		{
			if (M->GetSeverity() < EMessageSeverity::Info) Out.Messages.Add(M->ToText().ToString());
		}
		Out.bCompiled = BP->Status == BS_UpToDate && BP->GeneratedClass != nullptr;
		return Out;
	}

	struct FPairAnswer
	{
		bool bMadeHas = false, bFound = false, bHasBirthDate = false;
		int64 MadeLevel = 0, BirthDate = 0;
	};

	FPairAnswer RunPair(FAutomationTestBase& Test, UObject* Instance, const UOWWorld* World, const FString& Id, bool bHas, int64 Level)
	{
		FPairAnswer A;
		UFunction* Fn = Instance->GetClass()->FindFunctionByName(TEXT("Pair"));
		if (!Fn)
		{
			Test.AddError(TEXT("the generated class has no Pair"));
			return A;
		}
		// Parameters only (ScriptCore.cpp:1432-1442), as RunGraph does.
		uint8* Frame = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Fn->ParmsSize, 1), Fn->GetMinAlignment()));
		FMemory::Memzero(Frame, Fn->ParmsSize);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->InitializeValue_InContainer(Frame);
		CastField<FObjectProperty>(Fn->FindPropertyByName(TEXT("World")))->SetObjectPropertyValue_InContainer(Frame, const_cast<UOWWorld*>(World));
		CastField<FStrProperty>(Fn->FindPropertyByName(TEXT("Id")))->SetPropertyValue_InContainer(Frame, Id);
		CastField<FBoolProperty>(Fn->FindPropertyByName(TEXT("bHas")))->SetPropertyValue_InContainer(Frame, bHas);
		CastField<FInt64Property>(Fn->FindPropertyByName(TEXT("Level")))->SetPropertyValue_InContainer(Frame, Level);
		Instance->ProcessEvent(Fn, Frame);
		A.bMadeHas = CastField<FBoolProperty>(Fn->FindPropertyByName(TEXT("bMadeHas")))->GetPropertyValue_InContainer(Frame);
		A.MadeLevel = CastField<FInt64Property>(Fn->FindPropertyByName(TEXT("MadeLevel")))->GetPropertyValue_InContainer(Frame);
		A.bFound = CastField<FBoolProperty>(Fn->FindPropertyByName(TEXT("bFound")))->GetPropertyValue_InContainer(Frame);
		A.bHasBirthDate = CastField<FBoolProperty>(Fn->FindPropertyByName(TEXT("bHasBirthDate")))->GetPropertyValue_InContainer(Frame);
		A.BirthDate = CastField<FInt64Property>(Fn->FindPropertyByName(TEXT("BirthDate")))->GetPropertyValue_InContainer(Frame);
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->DestroyValue_InContainer(Frame);
		FMemory::Free(Frame);
		return A;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWGraphIntegersTest, "OnlyWorlds.Graph.Integers", OWTestFlags)

bool FOWGraphIntegersTest::RunTest(const FString& Parameters)
{
	UOWWorld* Sikelia = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!Sikelia) return false;
	TStrongObjectPtr<UOWWorld> KeepSikelia(Sikelia);

	const FBuiltGraph Graph = BuildIntegersGraph();
	TestEqual(TEXT("every link the schema accepts (the pairs have pins)"), Graph.Refused, 0);
	TestEqual(TEXT("the compile's errors"), Graph.Errors, 0);
	TestEqual(TEXT("the compile's warnings"), Graph.Warnings, 0);
	for (const FString& M : Graph.Messages) AddError(TEXT("compiler: ") + M);
	if (!TestTrue(TEXT("compiled, up to date, a generated class"), Graph.bCompiled)) return false;
	TStrongObjectPtr<UObject> Instance(NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass));

	// The Make sets both members and the Break reads both back, whatever they hold (0 set is not unset).
	const struct { bool bHas; int64 Level; } Made[] = { { true, 7 }, { false, 0 }, { true, 0 }, { true, -5000000000LL }, { false, 12 } };
	for (const auto& M : Made)
	{
		const FPairAnswer A = RunPair(*this, Instance.Get(), Sikelia, FString(), M.bHas, M.Level);
		TestTrue(FString::Printf(TEXT("Make (bHasLevel %d, Level %lld) -> Break (%d, %lld)"), M.bHas, M.Level, A.bMadeHas, A.MadeLevel),
			A.bMadeHas == M.bHas && A.MadeLevel == M.Level);
		TestFalse(TEXT("no character with id \"\""), A.bFound);
	}

	// Every character of Sikelia found through the graph, its birth date broken out: the file's (null or missing is unset).
	int32 Characters = 0, Same = 0, Dated = 0;
	for (const OnlyWorlds::OWElement& E : Sikelia->Native()->elements)
	{
		if (E.Type() != "character") continue;
		++Characters;
		const OWJsonValue* V = E.body.Find("birth_date");
		const bool bWant = V && V->IsNumber();
		const int64 Want = bWant ? static_cast<int64>(std::stoll(V->Text())) : 0;
		Dated += bWant;
		const FPairAnswer A = RunPair(*this, Instance.Get(), Sikelia, Str(E.Id()), false, 0);
		if (A.bFound && A.bHasBirthDate == bWant && A.BirthDate == Want) ++Same;
		else if (Characters - Same <= 3) AddError(FString::Printf(TEXT("%s: found %d, birth date %d %lld; the file %d %lld"), *Str(E.Id()),
			A.bFound, A.bHasBirthDate, A.BirthDate, bWant, Want));
	}
	TestTrue(TEXT("Sikelia has characters with a birth date"), Dated > 0);
	TestEqual(TEXT("every character's birth date through the graph, as its file says"), Same, Characters);
	AddInfo(FString::Printf(TEXT("GRAPH Integers: %d links, compiled with %d errors and %d warnings; %d characters (%d with a birth "
		"date) through FindCharacter and a Break"), Graph.Links, Graph.Errors, Graph.Warnings, Characters, Dated));
	return true;
}

#endif  // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
