// The core from a game module: a world opened through UOWLibrary, then read in C++ through UOWWorld::Native() and
// the core's own readers. Type OnlyWorlds.NativeRead <world folder> in the editor's console.
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

#include "OWCalendar.h"
#include "OWConvert.h"
#include "OWLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <vector>

static void OWNativeRead(const TArray<FString>& Args)
{
	if (Args.Num() != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("OWNativeRead: give one world folder"));
		return;
	}
	FString Error;
	UOWWorld* Opened = UOWLibrary::OpenWorldFolder(Args[0], Error);
	OnlyWorlds::OWWorld* Core = Opened ? Opened->Native() : nullptr;
	if (!Core)
	{
		UE_LOG(LogTemp, Warning, TEXT("OWNativeRead: %s"), *Error);
		return;
	}

	std::vector<OnlyWorlds::OWJsonValue> Bodies;
	for (const OnlyWorlds::OWElement& Element : Core->elements) Bodies.push_back(Element.body);
	const std::vector<OnlyWorlds::OWElement*> Characters = Core->OfType("character");
	const std::vector<OnlyWorlds::OWWorldCalendar> Calendars = OnlyWorlds::OWCalendar::WorldCalendars(Bodies);

	UE_LOG(LogTemp, Display, TEXT("OWNativeRead: world %s: %d elements, %d characters (the first %s), %d calendars"),
		*OWConvert::Str(Core->Id()), static_cast<int32>(Core->elements.size()), static_cast<int32>(Characters.size()),
		Characters.empty() ? TEXT("none") : *OWConvert::Str(Characters[0]->Name()), static_cast<int32>(Calendars.size()));
}

static FAutoConsoleCommand OWNativeReadCommand(TEXT("OnlyWorlds.NativeRead"),
	TEXT("Opens a world folder and reads it in C++ through UOWWorld::Native() and the core's calendar reader."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&OWNativeRead));
