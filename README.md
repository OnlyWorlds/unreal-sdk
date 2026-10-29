# OnlyWorlds for Unreal

An Unreal Engine 5.8 code plugin for [OnlyWorlds](https://onlyworlds.com) world folders and `.owworld` world files:
open a world, read its elements as Blueprint structs, write it back with every untouched file byte-equal.

**The plugin is the folder `Plugin/OnlyWorlds/`, whole and on its own**: copy it into a project's
`Plugins/OnlyWorlds/`. The rest of the repository builds and tests it: `build.cmd` and `tests/` (the core alone),
`codegen/` (the generators and the vendored schema-dist), `Host/` (a project that builds the plugin and runs its
automation tests).

The plugin is two layers:

- **`Plugin/OnlyWorlds/Core/`, the engine-independent core**: plain C++, the standard library and zlib only, no
  Unreal header. It does all the work, and builds and tests on its own with `build.cmd` (no Unreal process).
- **`Plugin/OnlyWorlds/Source/OnlyWorlds/`, the Runtime module**: compiles the core in place (its `Build.cs` adds
  `Core/` as a module directory, with no precompiled header and no unity build, so no Unreal header enters the core's
  files) and puts UObject at the edges.

## The core

| | |
|---|---|
| `OWJson` | strict JSON (RFC 8259) whose value keeps key order and each number's source text; the folder spec's section 5 writer (`JSON.stringify(value, null, 2)`'s bytes) |
| `OWUnicode` | NFC, the OW-18 fold (NFC, then CaseFolding's C and S rows), strict UTF-8, Python's `strip()` white space, the slug's NFKD: Unicode 15.1.0, generated from Unicode's own files |
| `OWZip` | the archive's own central-directory walk, each entry inflated by zlib and held to both sizes it declares, a writer with fixed stamps |
| `OWWorldArchive` | reads an `.owworld` by every reader rule of section 4.7 (OW-1 to OW-22), refusing with one cause; packs a folder |
| `OWWorld` | a world folder read and written back: an unedited file keeps its bytes, an edited one is written in section 5's form, a new element takes section 3.2's filename |
| `OWAddress` | the address profile (schema-dist `profiles/address.md`): `Parse` reads an `ow://` link or a site URL into a reading (form, world, type, id, moment, lens, canonical) or refuses it by the profile's named reason; `Write` and `WriteJson` write an element's or a world's address, with the writer's own refusals. Held to the profile's 133-case fixture |
| `OWCalendar` | the calendar profile (`profiles/calendar.md`, version 2): `ReadValue` reads an `atlas_calendar` value (an object, or the same object as a strict JSON string) into an `OWCalendarConfig`, or refuses it (a tool then shows the bare step); `Breakdown` and `FormatStep` read a step aloud ("3 Frost, 12 AG"); `WorldCalendars` finds a world's calendars by the field, never by supertype. Numbers are read as JavaScript reads them (whole to 2^53 - 1, `1.0` is 1), as the profile's reference package does. Held to the profile's 64-case fixture |
| `OWMessage` | the message profile (`profiles/message.md`): over element bodies as JSON, `IsMessage` and `IsNote` (the pair, trimmed and folded), addressing (`IsAddressedTo`, `IsToEveryone`, `HasUnseenRecipients` with the server's trimmed mark and a keyless reader), `SenderStatus` (verified, mismatch, unverifiable, unseen, against the world's members list), `EditedBy`, the read mark (`ReadMarkOf`: the reader's own hand, the newest), `UnreadFor`, `ThreadOf`, and a Character's line (`NoteOf`), each ordered by `created_at` as the profile orders. Held to the profile's 85-case fixture |

## The plugin

| | |
|---|---|
| `UOWLibrary` | `OpenWorldFolder`, `OpenWorldFile` (with the reader's verdict: accepted, reason, message), `CheckWorldFile`, `PackWorldFolder`. A relative path is taken from the project's folder |
| `UOWWorld` | one world: its id and name, element ids by type, an element's type, name and JSON, `SetStringField` and `SetIntField`, `WriteFolder`, `PackToFile`; `Native()` hands C++ the core's `OnlyWorlds::OWWorld` |
| `UOWCalendars` | the calendar profile on Blueprints: `GetWorldCalendars(World)` (each calendar's element id and name, found by the field), `FormatStep(World, CalendarId, Step)` (a step in words, "Year 1 BT"; the bare step when there is no such calendar or its value is refused, never an error) and `BreakdownStep(World, CalendarId, Step, Breakdown)` (an `FOWStepBreakdown`: year, era, month, day, the day outside the months, weekday; a part the calendar lacks is 0 or `""`). `CalendarId` `""` reads the world's only calendar when it holds exactly one. A step is an `int64` (Blueprint's Integer64) |
| `FOW<Type>`, `UOWElements` | one `USTRUCT(BlueprintType)` per element type (`FOWCharacter`, `FOWPin`, ...), and the library that reads them out of a world: `GetAll<Types>(World)`, `Find<Type>(World, Id, Out)`. Generated from schema-dist's `schema.json` |

Members are PascalCase (`birth_date` -> `BirthDate`, `STR` -> `Str`). A string field is an `FString` (`""` for unset),
an integer a `TOptional<int64>` (unset for null), a link the linked element's id (an `FString`, or a `TArray<FString>`
for a list), and the Pin's generic link two strings, `ElementType` and `ElementId`. The JSON key and the schema's
description are each member's tooltip.

## Build and test

The core alone (Windows, MSVC; no Unreal process):

```
build.cmd                  # build\ow_tests.exe, linking the engine's zlib 1.3 (UE_ROOT, default UE_5.8), /MD
build\ow_tests.exe         # every test; exits 0 only when none failed (--filter=<text>, --require-all)
python codegen/generate.py --check                    # the generated Unicode tables match the vendored files
python codegen/generate_models.py --check             # the generated structs match schema.json
python tests/check_writer.py build/out/pass-good-repacked.owworld   # the reference checker on what pack wrote
```

The plugin, through the host project in `Host/` (Unreal 5.8, Visual Studio 2022 with the C++ workload and the .NET
Framework SDK 4.6.2 or later). The host finds the plugin by `"AdditionalPluginDirectories": ["../Plugin"]` (Unreal
looks for plugins in the subfolders of the folder named), and builds it into `Host/Binaries` and
`Host/Intermediate/External`:

```
"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" OWHostEditor Win64 Development -Project="%CD%\Host\OWHost.uproject"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Host\OWHost.uproject" ^
    -ExecCmds="Automation RunTests OnlyWorlds; Quit" -TestExit="Automation Test Queue Empty" ^
    -nullrhi -unattended -nosplash -nop4          # exit 0 when every test passed, 255 when one failed
```

The automation tests (`OnlyWorlds.*`):

- `OnlyWorlds.Archive.Conformance.<fixture>`: each of the `.owworld` conformance set's generated fixtures, read
  through `UOWLibrary`, its reader verdict and reason asserted (`codegen/schema-dist/fixtures/owworld/verdicts.json`,
  found two folders above the plugin; `OW_FIXTURES` for a plugin copied elsewhere). `OnlyWorlds.Archive.Set` checks
  the set itself.
- `OnlyWorlds.World.WriteBack.<world>`: a real world folder opened, written unedited to `Host/Saved/OWTests/`, and
  compared file by file with the folder read. `OnlyWorlds.World.Models`: the structs against their files, and one edit.
- `OnlyWorlds.Calendar.GetWorldCalendars`, `.FormatStep`, `.BreakdownStep`: each `UOWCalendars` function called as a
  Blueprint calls it (found by name, its parameters marshalled by property, `ProcessEvent` on the class default
  object, the answer and the struct's members read back by property), on Moppetopia's calendar (Sikelia holds none).
  `OnlyWorlds.Calendar.Fixture`: every case of the calendar profile's fixture (`codegen/schema-dist/profiles/`) placed
  on that calendar's element and read back the same way: the words and the breakdown of each step, every refusal.
- `OnlyWorlds.Frame.MoppetopiaRootMap` needs a renderer (`-RenderOffscreen` instead of `-nullrhi`; a `-nullrhi`
  run leaves it out): the world's root Map as a plane carrying its image, the Pins as markers, captured from above to
  `Host/Saved/OWFrames/moppetopia-root-map.png`.

The core's runner and the world tests read two real worlds from this machine when present (`OW_SIKELIA`,
`OW_MOPPETOPIA`). In the core's runner a missing world is a named skip; in the automation tests it fails.

`codegen/schema-dist/` is vendored verbatim; never edit it. Under `Plugin/OnlyWorlds/`,
`Core/Private/OWUnicodeData.generated.cpp` is written by `codegen/generate.py`, and
`Source/OnlyWorlds/Public/OWModels.h`, `OWModelsJson.h` and `Private/OWModels.cpp` by `codegen/generate_models.py`;
regenerate, never edit.

## Not built yet

- A runtime actor that lays a Map out in a level: the frame is built by a test, in the editor, with a material made
  in code. A packaged game would need a material asset (the plugin ships no content yet).
- Writing a struct back into an element (edits go through `UOWWorld`'s field setters for now).
- Blueprint graphs: the structs and functions compile as Blueprint types, and the calendar's functions are called
  through reflection as a graph calls them; no graph has been run against any of them.
- The message profile on Blueprints: the core reads it, but with no API client there is no members list to prove a
  sender.
- Any platform other than Win64, and any engine other than 5.8.
