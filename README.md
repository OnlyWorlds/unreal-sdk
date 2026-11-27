# OnlyWorlds SDK for Unreal

[OnlyWorlds](https://www.onlyworlds.com) is an open standard for world data: a world's characters, places, events
and lore as elements of 22 types, linked by id, that any tool can read. This plugin brings such a world into your
Unreal game: written outside the engine, then read at run time over the onlyworlds.com API, or shipped with the game
as a folder.

It has two roads, and they give different things:

- **A world on disk** (a world folder, or one `.owworld` file): `OpenWorldFolder` or `OpenWorldFile`, then
  `GetAll<Types>` (`GetAllCharacters`, …), gives every element as a Blueprint struct (`FOWCharacter`, …), with no
  server. This is the road for a Blueprint graph.
- **The onlyworlds.com API**: the client's Blueprint nodes (**Read Element**, **List Elements**, the writes) answer each
  element as JSON text, and `<Type>FromJson` (`CharacterFromJson`, …) turns that text into the same struct a world on
  disk gives; in C++, `OWFromJson` does.

An Unreal Engine code plugin, with a struct for each of the 22 element types. It writes worlds back too, over the API
and on disk; a world read from disk and written back unedited is byte-equal.

## Install

**Unreal Engine 5.8 on Windows (Win64)**, with Visual Studio 2022 (17.14 or later, Epic's floor for 5.8) and its
**Game development with C++** workload. Other engine versions and platforms are untested.

It is a source plugin: Unreal compiles it with your project, so the project must be a C++ project. A Blueprint-only
project becomes one with its first C++ class (**Tools → New C++ Class**).

1. Put the plugin in the `Plugins` folder of your project's directory (beside the `.uproject`; make it if there is
   none), one of two ways:
   - **The release zip**: `onlyworlds-unreal-0.5.0.zip`, from this repository's
     [Releases](https://github.com/OnlyWorlds/unreal-sdk/releases) page. It holds one folder, `OnlyWorlds/`: unpack
     it into `Plugins/`, so the plugin is at `Plugins/OnlyWorlds/OnlyWorlds.uplugin`.
   - **A clone** of this repository: copy its `Plugin/OnlyWorlds/` folder into your project as `Plugins/OnlyWorlds/`.
2. Open the project. Unreal builds the plugin from source with the project, so the first open takes a while.
3. Check that **OnlyWorlds** is enabled under **Edit → Plugins**.
4. To call it from C++, add `"OnlyWorlds"` to your module's dependencies in its `.Build.cs`
   (`PrivateDependencyModuleNames`).

**Versions.** 0.1.0 is the first numbered release. Before 1.0, a minor version may change a call's name, and the
[CHANGELOG](CHANGELOG.md) says so when it does.

## First run

One for each road. Neither needs an account.

### In C++, over the API

This reads Moppetopia, a public sample world, with its demo key.

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

The client is the plugin's core, plain C++17 with no Unreal header, so its types are the standard library's
(`std::make_shared`, snake_case members) and `OWConvert` turns its text into an `FString`. The first callback holds
`Client`, so the client is still there for the second call. Your module links against the core's classes and
functions directly: the plugin's module exports them (`OW_API`).

On Blueprints the same read is the **Read World** node: give it a config whose `ApiKey` is `0000000001`, and
`OnSuccess` hands you the world as JSON text, not a struct. The API's reads and element writes each have a node like
it, with `OnSuccess` and `OnFailure`. An element read that way becomes its struct with `<Type>FromJson`: wire **Read
Element**'s `Json` into **Character From Json**, then break the `FOWCharacter` as you would one from a folder.

### On Blueprints, from a world on disk

These nodes, wired as below, are built in code and run (placed in a game world, not by play-in-editor) by this
repository's test `OnlyWorlds.FirstRun.Blueprint` beside the plugin's own; placing them by hand in the editor's graph
view has not been done.

1. Copy the example world `codegen/schema-dist/example/hyperion` of this repository into your project as
   `Worlds/hyperion` (beside the `.uproject`).
2. In the Level Blueprint's event graph, or in an Actor Blueprint's that you then place in the level, from **Event
   BeginPlay** place **Open World Folder** (category OnlyWorlds) and type `Worlds/hyperion` in its **Path**. A
   relative path is taken from the project's folder.
3. From its **Return Value** (the opened world), place **Get All Characters**: an array of `OWCharacter` structs.
4. From Open World Folder's exec output, place a **For Each Loop** on that array, a **Break OWCharacter** on its
   **Array Element**, and from **Loop Body** a **Print String** whose **In String** is the struct's **Name**.
5. Play in the editor. The screen and the Output Log show the world's three Characters: Mr. Steinway, Rachmaninov and
   The Consul (in the log, `LogBlueprintUserMessages: [<the actor>] Mr. Steinway`).

When **Return Value** is empty, **Error** says why (`no world.json in Worlds/hyperion`). In this graph Error is wired
to nothing, so nothing prints; the Output Log carries the reason as `LogOnlyWorlds: OpenWorldFolder: no world.json in
Worlds/hyperion`.

## Your own world

Create a world at [onlyworlds.com](https://www.onlyworlds.com); its keys are on the world's page. A new key is shown
once, so copy it then.

- **`ow_r_`** reads, with no PIN. This is the key to ship in a game.
- **`ow_w_`** reads and writes. Writes also send your PIN (`ApiPin` on Blueprints, `api_pin` in C++).

The write nodes (`CreateElement`, `UpsertElement`, `PatchElement`, `DeleteElement`, `EditElementLinks`, `BulkWrite`)
take an element as JSON text; `Get<Type>Json` turns a struct (`FOWCharacter`, …) into that text. Keep write keys out
of games you ship: a key in an asset, a config file or a Blueprint default is cooked into the package, where anyone
with the build can read it, so read write credentials at run time.

An AI agent that writes to your world takes a seat of its own instead, an
[agent seat](https://onlyworlds.github.io/docs/development/agents): its own key and secret (`ow_s_…`), for one world,
which the world's owner can remove at any time.

**A world on disk.** `UOWLibrary::OpenWorldFolder` reads a world folder, `OpenWorldFile` an `.owworld` file (one zip
that holds a folder), and `GetAll<Types>` (`GetAllCharacters`, …) gives its elements as structs. `PackWorldFolder` and
`UOWWorld::PackToFile` make an `.owworld` file. By default a pack is a copy: every element file travels whole, hidden
ones included, as you would back up or move your own world. With `bAsShare` (in C++, `include_hidden` false) it is a
share, a file to hand to someone else: an element marked `hidden` is left out, and so is every file under `elements/`
that the reader could not read as one element; the result names each, hidden apart from unread (`hidden_left_out`,
`unread_left_out`). Either way, who wrote each element does not travel: the five author fields (`created_by`,
`changed_by`, `changed_via`, `changed_client`, `doors`) are left out of every element file that is one JSON object,
since a `.owworld` is a file handed on (in C++, `include_author_fields` keeps them). This repository
carries a small example world, Hyperion, at `codegen/schema-dist/example/hyperion`.

**A player link** (`https://www.onlyworlds.com/join#ow_j_…`, made by a world's owner for one Character) needs no
account: `OnlyWorlds::OWJoin::ParseLink` and `Redeem` turn it into that player's read key (in C++ only, for now).

## What's in it

| Part | Where | What it does |
|---|---|---|
| **Core** | `Plugin/OnlyWorlds/Core/` | Plain C++17 with zlib and no Unreal header: the API client (`OWClient`, `OWJoin`), world folders and `.owworld` files (`OWWorld`, `OWWorldArchive`, `OWJson`), and readers over a world it holds, with no request of their own (`OWLens`, `OWGraph`, `OWLanding`, `OWMapSpace`, `OWCharter`, `OWStrand`, `OWRecord`, `OWReport`, `OWMessage`, `OWNames`, `OWCalendar`, `OWAddress`, `OWNameFinder`, `OWOpeners`: what one reader may see of a world, the world as nodes and edges, its maps as points and outlines, its calendar, in-world messages, element names found in prose and more). It does all the work and is tested on its own. |
| **Blueprints** | `Plugin/OnlyWorlds/Source/OnlyWorlds/` | The Runtime module: the core compiled in place, with UObject at the edges. `UOWLibrary` and `UOWWorld` (open, read, edit, write back, pack), one `USTRUCT` per element type with `UOWElements` to find them, async nodes for the API's reads and element writes, and Blueprint libraries over most of the readers. `FOWHttpTransport` is the client's transport on Unreal's HTTP. |

Every call, with what it answers and what it refuses, and what is not built yet:
[docs/reference.md](docs/reference.md).

## How it follows the standard

The 22 structs are generated from one pinned release of the
[schema distribution](https://github.com/OnlyWorlds/schema-dist), the standard's published form (`codegen/PIN`),
vendored unchanged in `codegen/schema-dist/`. Each reader gives the same answers as the other OnlyWorlds SDKs on a
shared set of test cases; the `.owworld` reader accepts and refuses the standard's own test files as they expect; and
every request the client makes is checked against the API's own description of itself (`tests/data/keel-wire.json`).
The reference opens with the few words of the standard it uses.

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
python tools/release_zip.py v0.5.0                  # the release zip, from a tag or a commit, into dist/
python tools/check_exports.py                       # every core declaration a game module links against carries OW_API
```

The plugin builds, and its automation tests (`OnlyWorlds.*`) run, through the host project in `Host/`, which finds the
plugin in `Plugin/`; the commands are in the reference. The tests need no network and no key: the client's tests run
against a fake transport. A few tests read two large real worlds when `OW_SIKELIA` and `OW_MOPPETOPIA` name their
folders, and one reads Unicode's `NormalizationTest.txt` (15.1.0) when `OW_NORMALIZATION_TEST` names it; without them
the core's runner skips those tests and the automation tests warn and do not run them, never passing them. The
core's runner reads the vendored `.owworld` conformance set, or a draft copy when `OW_OWWORLD_FIXTURES` names a folder
holding its `verdicts.json`. The
plugin's tests are compiled only inside this repository, so a copy of the plugin in your project lists none.

## OnlyWorlds

The schema lives at [OnlyWorlds/OnlyWorlds](https://github.com/OnlyWorlds/OnlyWorlds). Other clients: the
[TypeScript SDK](https://github.com/OnlyWorlds/sdk), the [Python SDK](https://github.com/OnlyWorlds/python-sdk), the
[Unity SDK](https://github.com/OnlyWorlds/unity-sdk) and the [Godot SDK](https://github.com/OnlyWorlds/godot-sdk).

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
