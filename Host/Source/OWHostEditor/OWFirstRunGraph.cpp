#include "OWFirstRunGraph.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/CriticalSection.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "UObject/Package.h"

#include "OWLibrary.h"
#include "OWModels.h"

namespace
{
	// The engine's own For Each Loop: a macro of its standard macro library, as the editor's action menu offers it.
	const TCHAR* const OWStandardMacros = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros");

	UK2Node_CallFunction* PlaceCall(UEdGraph& Graph, UClass* Owner, const TCHAR* FunctionName)
	{
		const UFunction* Function = Owner->FindFunctionByName(FunctionName);
		if (!Function) return nullptr;
		FGraphNodeCreator<UK2Node_CallFunction> Creator(Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->SetFromFunction(Function);
		Creator.Finalize();
		return Node;
	}

	// A pin by its name, spaces ignored ("LoopBody" finds the macro's "LoopBody" or "Loop Body").
	UEdGraphPin* PinOf(UEdGraphNode* Node, const TCHAR* Name, EEdGraphPinDirection Direction)
	{
		if (!Node) return nullptr;
		const FString Wanted = FString(Name).Replace(TEXT(" "), TEXT(""));
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && Pin->PinName.ToString().Replace(TEXT(" "), TEXT("")).Equals(Wanted, ESearchCase::IgnoreCase))
				return Pin;
		}
		return nullptr;
	}

	// The node as the editor shows it: its title, enabled or a ghost, and each visible pin's display name and type.
	FString Describe(const UEdGraphNode* Node)
	{
		const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
		TArray<FString> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->bHidden) continue;
			Pins.Add(FString::Printf(TEXT("%s %s: %s%s%s"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
				*K2->GetPinDisplayName(Pin).ToString(), *UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString(),
				Pin->LinkedTo.Num() > 0 ? TEXT(" (linked)") : TEXT(""),
				Pin->LinkedTo.Num() == 0 && !Pin->DefaultValue.IsEmpty() ? *FString::Printf(TEXT(" = \"%s\""), *Pin->DefaultValue) : TEXT("")));
		}
		return FString::Printf(TEXT("\"%s\" [%s]%s: %s"), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"), TEXT(" / ")),
			*Node->GetClass()->GetName(), Node->IsAutomaticallyPlacedGhostNode() ? TEXT(" (a ghost: disabled, never called)") : TEXT(""),
			*FString::Join(Pins, TEXT("; ")));
	}

	// The log lines a run is read by, heard from any thread while it runs.
	struct FHeard : public FOutputDevice
	{
		FCriticalSection Lock;
		TArray<FString> Printed, Opened;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			static const FName PrintCategory(TEXT("LogBlueprintUserMessages"));
			static const FName PluginCategory(TEXT("LogOnlyWorlds"));
			FScopeLock Held(&Lock);
			if (Category == PrintCategory) Printed.Add(V);
			else if (Category == PluginCategory && FCString::Strstr(V, TEXT("OpenWorldFolder"))) Opened.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	};
}

OWFirstRunGraph::FMade OWFirstRunGraph::Make(UObject* Outer, FName Name, const FString& Path)
{
	FMade Made;
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Outer, Name, BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	UEdGraph* Graph = BP ? FBlueprintEditorUtils::FindEventGraph(BP) : nullptr;
	if (!Graph)
	{
		Made.Why = TEXT("the Actor Blueprint has no event graph");
		return Made;
	}

	// Event BeginPlay: the one a new Actor Blueprint's event graph already holds (placed by the engine as a ghost
	// until a pin of it is linked), as a person finds it; placed new only when the engine placed none.
	// (AActor::ReceiveBeginPlay is protected, so its name is written out.)
	const FName BeginPlay(TEXT("ReceiveBeginPlay"));
	UK2Node_Event* Event = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Event* E = Cast<UK2Node_Event>(Node);
		if (E && E->EventReference.GetMemberName() == BeginPlay) Event = E;
	}
	if (!Event)
	{
		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		Event = Creator.CreateNode(false);
		Event->EventReference.SetExternalMember(BeginPlay, AActor::StaticClass());
		Event->bOverrideFunction = true;
		Creator.Finalize();
		Made.Nodes.Add(TEXT("(the engine placed no Event BeginPlay; one placed by hand)"));
	}

	UK2Node_CallFunction* Open = PlaceCall(*Graph, UOWLibrary::StaticClass(), TEXT("OpenWorldFolder"));
	UK2Node_CallFunction* All = PlaceCall(*Graph, UOWElements::StaticClass(), TEXT("GetAllCharacters"));
	UK2Node_CallFunction* Print = PlaceCall(*Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));

	UBlueprint* Macros = LoadObject<UBlueprint>(nullptr, OWStandardMacros);
	UEdGraph* ForEach = nullptr;
	if (Macros)
	{
		for (UEdGraph* G : Macros->MacroGraphs)
		{
			if (G && G->GetFName() == TEXT("ForEachLoop")) ForEach = G;
		}
	}
	UK2Node_MacroInstance* Loop = nullptr;
	if (ForEach)
	{
		FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
		Loop = Creator.CreateNode(false);
		Loop->SetMacroGraph(ForEach);
		Creator.Finalize();
	}

	UK2Node_BreakStruct* Break = nullptr;
	{
		FGraphNodeCreator<UK2Node_BreakStruct> Creator(*Graph);
		Break = Creator.CreateNode(false);
		Break->StructType = FOWCharacter::StaticStruct();
		Creator.Finalize();
	}
	if (!Open || !All || !Print || !Loop)
	{
		Made.Why = FString::Printf(TEXT("a node could not be placed:%s%s%s%s"), Open ? TEXT("") : TEXT(" OpenWorldFolder"),
			All ? TEXT("") : TEXT(" GetAllCharacters"), Print ? TEXT("") : TEXT(" PrintString"),
			Loop ? TEXT("") : TEXT(" ForEachLoop (StandardMacros)"));
		return Made;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	TArray<FString> RefusedLinks;
	auto Join = [&](UEdGraphPin* From, UEdGraphPin* To, const TCHAR* What)
	{
		++Made.Links;
		if (!From || !To || !Schema->TryCreateConnection(From, To))
		{
			++Made.Refused;
			RefusedLinks.Add(FString::Printf(TEXT("%s%s"), What, !From ? TEXT(" (no source pin)") : !To ? TEXT(" (no target pin)") : TEXT("")));
		}
	};

	// Step 2: from Event BeginPlay, Open World Folder, its Path typed.
	Join(Event->FindPin(UEdGraphSchema_K2::PN_Then), Open->GetExecPin(), TEXT("BeginPlay.Then -> OpenWorldFolder.Exec"));
	UEdGraphPin* PathPin = Open->FindPin(TEXT("Path"));
	if (PathPin) Schema->TrySetDefaultValue(*PathPin, Path);
	// Step 3: from its Return Value, Get All Characters.
	Join(Open->FindPin(UEdGraphSchema_K2::PN_ReturnValue), All->FindPin(TEXT("World")), TEXT("OpenWorldFolder.ReturnValue -> GetAllCharacters.World"));
	// Step 4: a For Each Loop on that array, entered from Open World Folder (the README wires no exec into it; a loop
	// with none is never entered), a Break OWCharacter on its Array Element, from Loop Body a Print String of Name.
	Join(Open->GetThenPin(), PinOf(Loop, TEXT("Exec"), EGPD_Input), TEXT("OpenWorldFolder.Then -> ForEachLoop.Exec"));
	Join(All->FindPin(UEdGraphSchema_K2::PN_ReturnValue), PinOf(Loop, TEXT("Array"), EGPD_Input), TEXT("GetAllCharacters.ReturnValue -> ForEachLoop.Array"));
	UEdGraphPin* BreakIn = nullptr;
	for (UEdGraphPin* Pin : Break->Pins)
	{
		if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct) BreakIn = Pin;
	}
	Join(PinOf(Loop, TEXT("Array Element"), EGPD_Output), BreakIn, TEXT("ForEachLoop.ArrayElement -> BreakOWCharacter"));
	Join(PinOf(Loop, TEXT("LoopBody"), EGPD_Output), Print->GetExecPin(), TEXT("ForEachLoop.LoopBody -> PrintString.Exec"));
	Join(Break->FindPin(GET_MEMBER_NAME_CHECKED(FOWCharacter, Name), EGPD_Output), Print->FindPin(TEXT("InString")),
		TEXT("BreakOWCharacter.Name -> PrintString.InString"));

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		if (Message->GetSeverity() < EMessageSeverity::Info) Made.Nodes.Add(TEXT("compiler: ") + Message->ToText().ToString());
	}
	Made.CompileErrors = Results.NumErrors;
	Made.CompileWarnings = Results.NumWarnings;
	for (const UEdGraphNode* Node : Graph->Nodes) Made.Nodes.Add(Describe(Node));
	Made.Blueprint = BP;

	const bool bCompiled = BP->Status == BS_UpToDate && BP->GeneratedClass != nullptr;
	if (Made.Refused > 0) Made.Why = TEXT("refused: ") + FString::Join(RefusedLinks, TEXT(", "));
	else if (!PathPin) Made.Why = TEXT("Open World Folder has no Path pin");
	else if (Results.NumErrors > 0 || !bCompiled) Made.Why = FString::Printf(TEXT("compiled with %d errors, %s"), Results.NumErrors,
		bCompiled ? TEXT("up to date") : TEXT("not up to date"));
	return Made;
}

OWFirstRunGraph::FPlayed OWFirstRunGraph::Play(UClass* ActorClass)
{
	FPlayed Played;
	if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass())) return Played;
	FHeard Heard;
	GLog->AddOutputDevice(&Heard);

	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("OWFirstRunWorld"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	// Placed in the level before play, as an actor dragged into it is.
	AActor* Actor = World->SpawnActor<AActor>(ActorClass);
	// The level's start: what a game state's HandleBeginPlay calls once the match begins.
	World->GetWorldSettings()->NotifyBeginPlay();
	Played.bBegunPlay = Actor && Actor->HasActorBegunPlay();

	GLog->Flush();
	GLog->RemoveOutputDevice(&Heard);
	{
		FScopeLock Held(&Heard.Lock);
		Played.Printed = Heard.Printed;
		Played.Opened = Heard.Opened;
	}
	// Play ended before the world goes, as a level's end ends it (else CleanupWorld warns of a missing EndPlay).
	World->EndPlay(EEndPlayReason::EndPlayInEditor);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return Played;
}

FString OWFirstRunGraph::Text(const FString& Printed)
{
	if (Printed.StartsWith(TEXT("[")))
	{
		const int32 Close = Printed.Find(TEXT("] "));
		if (Close != INDEX_NONE) return Printed.Mid(Close + 2);
	}
	return Printed;
}
