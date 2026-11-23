// A first run: Moppetopia, a public sample world, read with its demo key. Put this file in any C++ module that
// depends on "OnlyWorlds", then type OnlyWorlds.FirstRun in the editor's console (or call OWFirstRun() yourself).
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

#include "OWClient.h"
#include "OWConvert.h"
#include "OWHttpTransport.h"

#include <memory>

static FString NameOf(const OnlyWorlds::OWJsonValue& Body)
{
	const OnlyWorlds::OWJsonValue* Name = Body.Find("name");
	return Name ? OWConvert::Str(Name->Text()) : FString();
}

void OWFirstRun()
{
	OnlyWorlds::OWClientConfig Config;
	Config.api_key = "0000000001";  // demo key: Moppetopia, read-only
	Config.transport = FOWHttpTransport::Make();
	auto Client = std::make_shared<OnlyWorlds::OWClient>(std::move(Config));

	// Each answer comes later, on the game thread.
	Client->GetWorld([Client](OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> World)
	{
		if (!World.Ok())
		{
			// No connection, or the server said why.
			UE_LOG(LogTemp, Warning, TEXT("%s"), *OWConvert::Str(World.error->ToString()));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("%s"), *NameOf(World.value));

		OnlyWorlds::OWListOptions Options;
		Options.name_contains = "fluffington";
		Client->List("character", Options, [](OnlyWorlds::OWReply<OnlyWorlds::OWPage> Found)
		{
			if (Found.Ok() && !Found.value.data.empty())
			{
				UE_LOG(LogTemp, Display, TEXT("%s"), *NameOf(Found.value.data[0]));
			}
		});
	});
}

static FAutoConsoleCommand OWFirstRunCommand(TEXT("OnlyWorlds.FirstRun"),
	TEXT("Reads Moppetopia, a public OnlyWorlds world, and logs its name and one of its characters."),
	FConsoleCommandDelegate::CreateStatic(&OWFirstRun));
