# OnlyWorlds SDK for Unreal

Your [OnlyWorlds](https://www.onlyworlds.com) world, its characters, locations and laws, in your Unreal game: as
Blueprint structs and nodes, and as plain C++.

An Unreal Engine code plugin. It reads and writes worlds through the OnlyWorlds API, and reads and writes them on disk
as world folders and `.owworld` files, with a struct for each of the 22 element types. A world read from disk and
written back unedited is byte-equal.

## Install

**Unreal Engine 5.8 on Windows (Win64)**, with Visual Studio 2022 and its C++ workload. Other engine versions and
platforms are untested.

1. Copy the `Plugin/OnlyWorlds/` folder of this repository into your project as `Plugins/OnlyWorlds/`.
2. Open the project. Unreal builds the plugin from source, so the project needs a C++ toolchain; a Blueprint-only
   project needs one C++ class first (**Tools → New C++ Class**).
3. Check that **OnlyWorlds** is enabled under **Edit → Plugins**.
4. To call it from C++, add `"OnlyWorlds"` to your module's dependencies in its `.Build.cs`
   (`PrivateDependencyModuleNames`).

## First run

This reads Moppetopia, a public sample world, with its demo key. No account needed.

```cpp
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
```

Type `OnlyWorlds.FirstRun` in the editor's console (the `` ` `` key, or the command line under the Output Log). The
Output Log shows `LogTemp: Display: Moppetopia`, then `LogTemp: Display: Admiral Fluffington`. In this repository the
same file is `Host/Source/OWHost/OWFirstRun.cpp`.

On Blueprints the same read is the **Read World** node: give it a config whose `ApiKey` is `0000000001`, and
`OnSuccess` hands you the world as JSON text. Every API call has its node, with `OnSuccess` and `OnFailure`.

## Your own world

Create a world at [onlyworlds.com](https://www.onlyworlds.com); its keys are on the world's page. A new key is shown
once, so copy it then.

- **`ow_r_`** reads, with no PIN. This is the key to ship in a game.
- **`ow_w_`** reads and writes. Writes also send a secret as the PIN (`ApiPin` on Blueprints, `api_pin` in C++). For
  code, use an [agent seat](https://onlyworlds.github.io/docs/development/agents): it has its own key and secret
  (`ow_s_…`), works in one world, and the world's owner can remove it at any time. The account PIN works too, but it
  opens every world on the account.

The write nodes (`CreateElement`, `UpsertElement`, `PatchElement`, `DeleteElement`, `EditElementLinks`, `BulkWrite`)
take an element as JSON text; `Get<Type>Json` turns a struct (`FOWCharacter`, …) into that text. Keep write keys out
of games you ship: a key in an asset, a config file or a Blueprint default is cooked into the package, where anyone
with the build can read it, so read write credentials at run time.

**A player link** (`https://www.onlyworlds.com/join#ow_j_…`, made by a world's owner for one Character) needs no
account: `OnlyWorlds::OWJoin::ParseLink` and `Redeem` turn it into that player's read key (in C++ only, for now).

**A world on disk.** `UOWLibrary::OpenWorldFolder` reads a world folder, `OpenWorldFile` an `.owworld` file (one zip
that holds a folder), and `GetAll<Types>` (`GetAllCharacters`, …) gives its elements as structs. This repository
carries a small example world, Hyperion, at `codegen/schema-dist/example/hyperion`.

## What's in it

| Part | Where | What it does |
|---|---|---|
| **Core** | `Plugin/OnlyWorlds/Core/` | Plain C++17 with zlib and no Unreal header: the API client (`OWClient`, `OWJoin`), world folders and `.owworld` files (`OWWorld`, `OWWorldArchive`, `OWJson`), and the standard's readers (`OWLens`, `OWGraph`, `OWLanding`, `OWMapSpace`, `OWCharter`, `OWStrand`, `OWRecord`, `OWReport`, `OWMessage`, `OWNames`, `OWCalendar`, `OWAddress`, `OWOpeners`). It does all the work and is tested on its own. |
| **Blueprints** | `Plugin/OnlyWorlds/Source/OnlyWorlds/` | The Runtime module: the core compiled in place, with UObject at the edges. `UOWLibrary` and `UOWWorld` (open, read, edit, write back, pack), one `USTRUCT` per element type with `UOWElements` to find them, async nodes for every API read and write, and Blueprint libraries over the readers. `FOWHttpTransport` is the client's transport on Unreal's HTTP. |

Every call, with what it answers and what it refuses, and what is not built yet:
[docs/reference.md](docs/reference.md).

## How it follows the standard

The 22 structs are generated from one pinned release of the
[schema distribution](https://github.com/OnlyWorlds/schema-dist) (`codegen/PIN`), vendored unchanged in
`codegen/schema-dist/`. Each reader answers every case of its profile's shared fixture, the same cases the other
OnlyWorlds SDKs answer; the `.owworld` reader gives the conformance set's verdict on every one of its files; and every
request the client makes is checked against the API's own document (`tests/data/keel-wire.json`; keel is the server
behind onlyworlds.com). The reference explains the standard's words used in this repository (keel, serial, rung).

## Working on the SDK

The core builds and tests without Unreal:

```
build.cmd                  # build\ow_tests.exe with MSVC, linking zlib 1.3 (the engine's under UE_ROOT, or ZLIB and ZLIB_LIB)
build\ow_tests.exe         # the suite: exits 0 only when no test failed (--filter=<text>)
python codegen/generate.py --check                  # the schema pin holds, the generated Unicode tables are current
python codegen/generate_models.py --check           # the generated structs match schema.json
python codegen/generate_tools.py --check            # the tools list's pin holds, OWTools.generated.cpp is current
python codegen/generate_presentation.py --check     # the generated sidecar matches presentation.json
python codegen/generate_frame_bits.py --check       # tests/data's frame bits match the map-space fixture
```

The plugin builds, and its automation tests (`OnlyWorlds.*`) run, through the host project in `Host/`, which finds the
plugin in `Plugin/`; the commands are in the reference. The tests need no network and no key: the client's tests run
against a fake transport. A few tests read two large real worlds when `OW_SIKELIA` and `OW_MOPPETOPIA` name their
folders, and one reads Unicode's `NormalizationTest.txt` (15.1.0) when `OW_NORMALIZATION_TEST` names it; without them
the core's runner skips those tests and the automation tests warn and do not run them, never passing them. The
plugin's tests are compiled only inside this repository, so a copy of the plugin in your project lists none.

## OnlyWorlds

An open standard for world data: 22 element types, linked by id, usable by any tool. The schema lives at
[OnlyWorlds/OnlyWorlds](https://github.com/OnlyWorlds/OnlyWorlds). Other clients: the
[TypeScript SDK](https://github.com/OnlyWorlds/sdk), the [Python SDK](https://github.com/OnlyWorlds/python-sdk) and
the [Unity SDK](https://github.com/OnlyWorlds/unity-sdk).

## Licence and contributions

The code is MIT ([LICENSE](LICENSE)). Issues are welcome: a bug, a wrong word, a thing you could not do in Unreal.
Pull requests are read; a small fix is usually taken as is, and a change to what the plugin does is better opened as
an issue first, since it may already be built or ruled out. A pull request runs two checks by itself: the words of its
commit messages, and the core's test suite on Windows; the plugin's own tests need Unreal and are run by the
maintainers. A reply can take a few days. A security problem goes through
[private vulnerability reporting](https://github.com/OnlyWorlds/unreal-sdk/security/advisories/new), not an issue
([SECURITY.md](SECURITY.md)). What we ask at the door, and what you can expect:
[the code of conduct](https://github.com/OnlyWorlds/OnlyWorlds/blob/main/CODE_OF_CONDUCT.md).

Maintained by Quillon, an AI agent working for Titus. Issues and pull requests are read and answered here.
