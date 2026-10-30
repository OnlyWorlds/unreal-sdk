#include "OWBoot.h"

#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "OWAddressLibrary.h"
#include "OWCalendarLibrary.h"
#include "OWLibrary.h"
#include "OWModels.h"
#include "OWOpenersLibrary.h"
#include "OWWorldObject.h"

DEFINE_LOG_CATEGORY_STATIC(LogOWBoot, Log, All);

namespace
{
	// The world to open, from -OWWorld=<path> (quoted when it holds a space).
	bool WorldArg(FString& Path)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("-OWWorld="), Path, /*bShouldStopOnSeparator*/ false) && !Path.IsEmpty();
	}

	// Every call here is a static UFUNCTION of the plugin's libraries, the same entry a Blueprint node calls.
	UOWWorld* Open(const FString& Path, FString& Refusal)
	{
		if (Path.EndsWith(TEXT(".owworld"), ESearchCase::IgnoreCase) || FPaths::FileExists(Path))
		{
			FOWArchiveVerdict Verdict;
			UOWWorld* World = UOWLibrary::OpenWorldFile(Path, Verdict);
			if (!World) Refusal = FString::Printf(TEXT("refused: reason=%s message=%s"), *Verdict.Reason, *Verdict.Message);
			return World;
		}
		FString Error;
		UOWWorld* World = UOWLibrary::OpenWorldFolder(Path, Error);
		if (!World) Refusal = FString::Printf(TEXT("refused: %s"), *Error);
		return World;
	}

	void LogRows(const TCHAR* Label, const TArray<FOWOpener>& Rows)
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: open with (%s): %d rows"), Label, Rows.Num());
		for (const FOWOpener& Row : Rows)
		{
			UE_LOG(LogOWBoot, Display, TEXT("OWBoot:   %s | %s | %s"), *Row.Name, *Row.Href,
				Row.State.IsEmpty() ? TEXT("(opens now)") : *Row.State);
		}
	}

	void Quit(uint8 Code)
	{
		// A non-forced exit leaves the game's loop cleanly and returns 0 whatever code it is given; only a forced exit
		// (the log flushed, then the process ended with the code) carries a failure out.
		FPlatformMisc::RequestExitWithStatus(Code != 0, Code, TEXT("OWBoot"));
	}
}

bool UOWBootSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Path;
	return WorldArg(Path);
}

void UOWBootSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UOWBootSubsystem::Run));
}

void UOWBootSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(Tick);
	Super::Deinitialize();
}

bool UOWBootSubsystem::Run(float DeltaTime)
{
	FString Path;
	WorldArg(Path);
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OnlyWorlds"));
	UE_LOG(LogOWBoot, Display, TEXT("OWBoot: plugin %s %s at %s"), Plugin.IsValid() ? *Plugin->GetName() : TEXT("(none)"),
		Plugin.IsValid() ? *Plugin->GetDescriptor().VersionName : TEXT(""),
		Plugin.IsValid() ? *FPaths::ConvertRelativePathToFull(Plugin->GetDescriptorFileName()) : TEXT(""));
	UE_LOG(LogOWBoot, Display, TEXT("OWBoot: opening %s"), *Path);

	FString Refusal;
	UOWWorld* World = Open(Path, Refusal);
	if (!World)
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: %s"), *Refusal);
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: done, exit 2"));
		Quit(2);
		return false;
	}
	bool bHeld = true;

	// The elements by type, in type order.
	const TArray<FString> Ids = World->GetElementIds(TEXT(""));
	TMap<FString, int32> ByType;
	for (const FString& Id : Ids) ++ByType.FindOrAdd(World->GetElementType(Id));
	ByType.KeySort(TLess<FString>());
	UE_LOG(LogOWBoot, Display, TEXT("OWBoot: world %s \"%s\": %d elements, %d types"), *World->GetId(),
		*World->GetWorldName(), World->GetElementCount(), ByType.Num());
	for (const TPair<FString, int32>& Pair : ByType)
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot:   %s %d"), *Pair.Key, Pair.Value);
	}
	for (const FString& Error : World->GetErrors()) UE_LOG(LogOWBoot, Display, TEXT("OWBoot: read error: %s"), *Error);
	if (Ids.Num() == 0)
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: the world holds no element"));
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: done, exit 3"));
		Quit(3);
		return false;
	}

	// One element's address, written and read back.
	const FString Id = Ids[0];
	FOWAddress Address;
	Address.Form = TEXT("text");
	Address.World = World->GetId();
	Address.Type = World->GetElementType(Id);
	Address.Id = Id;
	FString Text, Reason;
	if (!UOWAddresses::WriteAddress(Address, Text, Reason))
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: address of %s refused by the writer: %s"), *Id, *Reason);
		bHeld = false;
	}
	else
	{
		FOWAddress Back;
		const bool bRead = UOWAddresses::ParseAddress(Text, Back, Reason);
		const bool bSame = bRead && Back.Type == Address.Type && Back.Id == Id.ToLower() && Back.World == Address.World.ToLower();
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: address of %s \"%s\": %s -> read back type=%s id=%s canonical=%s (%s)"),
			*Id, *World->GetElementName(Id), *Text, *Back.Type, *Back.Id, Back.bCanonical ? TEXT("true") : TEXT("false"),
			bSame ? TEXT("held") : *FString::Printf(TEXT("did not hold: %s"), *Reason));
		bHeld &= bSame && Back.bCanonical;
	}

	// One step by the world's calendar: the first character's birth date that is set, else step 0.
	const TArray<FOWWorldCalendar> Calendars = UOWCalendars::GetWorldCalendars(World);
	if (Calendars.Num() == 0)
	{
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: calendar: none in this world"));
	}
	else
	{
		int64 Step = 0;
		FString From = TEXT("step 0");
		for (const FOWCharacter& C : UOWElements::GetAllCharacters(World))
		{
			if (!C.bHasBirthDate) continue;
			Step = C.BirthDate;
			From = FString::Printf(TEXT("%s's birth_date"), *C.Name);
			break;
		}
		UE_LOG(LogOWBoot, Display, TEXT("OWBoot: calendar \"%s\" (%d in the world): %s, step %lld, reads \"%s\""),
			*Calendars[0].Name, Calendars.Num(), *From, Step, *UOWCalendars::FormatStep(World, Calendars[0].Id, Step));
	}

	// The same element's Open with rows, online and offline, for a member in a game (no browser: a tool that needs an
	// account says so; the public's menu is the site's row alone).
	FOWOpenerSurface Surface;
	Surface.Audience = EOWAudience::Member;
	TArray<FOWOpener> Online, Offline;
	const bool bOnline = UOWOpeners::OpenersFor(World, Id, Surface, Online, Reason);
	if (!bOnline) UE_LOG(LogOWBoot, Display, TEXT("OWBoot: open with (online) refused: %s"), *Reason);
	LogRows(TEXT("online"), Online);
	Surface.bOnline = false;
	const bool bOffline = UOWOpeners::OpenersFor(World, Id, Surface, Offline, Reason);
	if (!bOffline) UE_LOG(LogOWBoot, Display, TEXT("OWBoot: open with (offline) refused: %s"), *Reason);
	LogRows(TEXT("offline"), Offline);
	bHeld &= bOnline && bOffline && Online.Num() > 0 && Online.Num() == Offline.Num();

	const uint8 Code = bHeld ? 0 : 3;
	UE_LOG(LogOWBoot, Display, TEXT("OWBoot: done, exit %d"), Code);
	Quit(Code);
	return false;
}
