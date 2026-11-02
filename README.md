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
| `OWAddress` | the address profile (schema-dist `profiles/address.md`): `Parse` reads an `ow://` link or a site URL into a reading (form, world, type, id, moment, lens, canonical) or refuses it by the profile's named reason; `Write` and `WriteJson` write an element's or a world's address, with the writer's own refusals. Held to the profile's 137-case fixture |
| `OWCalendar` | the calendar profile (`profiles/calendar.md`, version 2): `ReadValue` reads an `atlas_calendar` value (an object, or the same object as a strict JSON string) into an `OWCalendarConfig`, or refuses it (a tool then shows the bare step); `Breakdown` and `FormatStep` read a step aloud ("3 Frost, 12 AG"); `WorldCalendars` finds a world's calendars by the field, never by supertype. Numbers are read as JavaScript reads them (whole to 2^53 - 1, `1.0` is 1), as the profile's reference package does. Held to the profile's 76-case fixture |
| `OWMessage` | the message profile (`profiles/message.md`): over element bodies as JSON, `IsMessage` and `IsNote` (the pair, trimmed and folded), addressing (`IsAddressedTo`, `IsToEveryone`, `HasUnseenRecipients` with the server's trimmed mark and a keyless reader), `SenderStatus` (verified, mismatch, unverifiable, unseen, against the world's members list), `EditedBy`, the read mark (`ReadMarkOf`: the reader's own hand, the newest), `UnreadFor`, `ThreadOf`, and a Character's line (`NoteOf`), each ordered by `created_at` as the profile orders. Held to the profile's 87-case fixture |
| `OWLens` | the lens profile (`profiles/lens.md`): `Resolve(world, lens, reader, {as_of, clock})`, the profile's `resolveLens` over a world snapshot as JSON, as keel's reader resolves it: visibility first (the hidden mark, a guest's whole and known-of ids, a proposed element in no view), a hop both ways and on through a Relation, never through a room element, the reading aids, an umbrella entering each child once, the fence and the window on each element's own date, `changed_within`, `show`'s rows as keel serves that reader, notes, the owner's two counts, and the mail rule through `OWMessage`. Beside it `IsLens`, `IsTemplate`, `CheckKeys` (a whole float reads whole, so `7.0` is 7; a fraction is refused), `LensesInUse`, `DateOf` and the room, reading-aid and review marks. Held to the fixture's 114 shape, keys, uses, resolve and invariant cases, and `IsRoomElement` to the room profile's 32 |
| `OWGraph` | the lens profile's graph section: `WorldGraph(world, reader, options)`, the profile's `worldGraph` over the same snapshot and reader `OWLens::Resolve` reads: the elements the reader sees as nodes (`{id, type, name, family, icon}`, the family and icon `OWPresentation`'s, with `marks` and `known_of`), the schema's links and the two declared extension links as edges with the index the reader is served, and what is left out counted by reason (`edges_cut`, `cut`). Room, hidden, proposed and map-space elements and the reading aids (the charter, its guide, its domains) are out unless a flag brings them back (`rooms`, `hidden`, `proposed`, `map_space`, `reading_aids`), marked (`room`, `hidden`, `proposed`, `reading_aid`; map space carries none); `hidden` and `proposed` are the owner's, a public reader takes no flag, and a flag from the wrong rung is refused by name before anything is read. With a lens, the nodes are exactly its kept (marked as above: a reading aid a lens keeps carries `reading_aid`) and its cut rides along. Beside it `DegreeOf` and `OutDegreeOf` (each node's edges, a mirror pair two and a link to itself one; the distinct nodes its own links point at), every node a key in the graph's order. Held to the fixture's 54 graph cases and the profile's invariants over each, and its 5 degree cases |
| `OWLanding` | the landing profile (`profiles/landing.md`): `LandingOf(graph, options)`, the profile's `landingOf` over what `OWGraph::WorldGraph` answers for the visitor, so it holds exactly what that reader may see: `{mode, size, clusters, unclustered, gaps}`. The heads are the most-linked Characters (never a known-of node), ties by id in code point order, at most `heads` (5); a node within `hops` steps (1) of a head joins the nearest, a tie going to the higher-ranked head, each member `{id, hops, degree}`; what no head reaches is `unclustered`; the gaps are `orphan` and `near_orphan` (degree 0 and 1, by the page's degree: a link to itself once, each place in a list once), `no_place` (a Character whose `location` names no Location node) and `nobody_here` (a Location no Character's `location` names); `mode` is `gaps` under `size` (24) nodes and `graph` at it. Options typed (`OWLandingOptions`) or as JSON, an unknown key or a value that is not a whole number of 1 or more refused by name; a whole number too large to hold reads as the largest. Held to the fixture's 33 cases |
| `OWCharter` | the charter profile (`profiles/charter.md`): `CharterOf(world, reader)`, the profile's `charterOf` over the same snapshot and reader `OWLens::Resolve` reads: the newest marked Charter (`{id, name, rules, skill}`, read whole as keel reads it), the newest marked Guide (`{id, name}`) and the marked Domains (`{id, name, sentence, hubs}`, as the reader is served each) in the Guide's order, then by folded name and id; a public reader only on a world open to anyone. Beside it `DomainsOf`, `IsCharterElement`, `ReadingAidOf`, `FirstLine` and the profile's tables. Held to the fixture's 27 cases. `Served(answer)` reads the packet a server's front door serves (`reading_aids` in a `GET /me` or an agent's join answer) as served for that key: the Charter, the Guide, the Domains, then the strand (`strand`, an `OWWorldStrand`, six empty slots when the key sees none; absent from a server that sends none) |
| `OWStrand` | the strand profile (`profiles/strand.md`): `StrandOf(world, reader)`, the profile's `strandOf` over the same snapshot and reader `OWLens::Resolve` reads, with the World's `x_strand` beside the elements: the story the mark names, one of its events and that event's place, the story's protagonist and where the hero is now (`{narrative, event, location, character, now, same}`, each slot `{id, name}` or null, `same` by id). Only the story's id is required (the event and the place may be unset). A slot holds only its own kind and is never filled from another; a story the reader only knows of fills its own slot alone; a public reader only on a world open to anyone. Held to the fixture's cases |
| `OWRecord` | the record profile (`profiles/record.md`): `RecordsOf(character, elements)`, the profile's `recordsOf` over the elements as served (a JSON list): every Event whose `x_record` mark reads and whose `characters` names the Character, not still proposed (as the review profile's `proposedMarkOf` reads it: an `x_review_proposed` object with a key, so an empty mark is settled), newest first by `played_at` as an instant to the millisecond, a tie by the Event's id in code point order, each with the tool, version, seed, outcome, the Character's seat and their stories (`{id, name, as}`, `as` `protagonist`, `narrator` or `both`, by id). Who took part is `characters` alone, never `seats`. `MarkOf(element)` reads the mark or none (only on an Event; a broken writer is no mark); `InstantOf` checks `played_at` by its parts on the Gregorian calendar, never a date parser. Held to the fixture's 40 cases |
| `OWMapSpace` | the map-space profile (`profiles/map-space.md`): `MapSpaceOf(elements)`, the profile's `mapSpace` over the elements as served (a JSON list, visibility already applied): `{maps, zones, pins, out}` in the Map's pixels, from its bottom left, z an elevation (never a height). Each Map's frame (`width` and `height`, measured from its own points with `extent: "points"` when not written; `depth`; `z_max`, so a z past `depth` shows; `scale` from `x_metres_per_pixel` when positive, else null; `location`, `parent_map`); each Zone's outline per Map from its Markers by whole `order`, a Marker without one after every ordered one, ties by id, closed by `shape` (`polygon` unless `line`); each Pin's point with its element as the wire's two keys, both or neither; what is left out counted once by reason (`no_point`, `no_markers`, `dangling`, `no_map`). Whole numbers read by value (`2.0` is 2), ids by code point. Core only. Held to the fixture's 24 cases |
| `OWNames` | the names profile (`profiles/names.md`): `TypeWord(world, type, supertype, many)`, the profile's `typeWord`: a world's own word for a type from `x_names` on the World (Character shown as *Employee*, a supertype's finer word first, `many` for a plural), or absent for the standard's name, printed as written. A display lookup only: the folder, the ids and every other reader keep the standard's names. Held to the fixture's 29 cases |
| `OWPresentation` | the presentation sidecar (schema-dist `presentation.json`), generated: the colour words in their order (`Order`), each type's colour word and icon (`FamilyOf`, `IconOf`) and the fields a known-of stub shows (`PublicOf`). A family word not in `colors.order` fails loudly: the generator refuses it, naming the word, and the table holds the rule as a `static_assert` |
| `OWOpeners` | the Open with menu's rows (the tools list's README, "Open with"), as the npm SDK's `openersFor` and the Python package's `openers_for` give them: `OpenersFor(tools, kind, address, surface)` for a world or an element, the site's page first, then each listed tool with a door for that kind in the list's order, each with its word ("Sign in to Atlas first", "Opens in your browser", ...) or none; with the surface's `online` false, every row whose href is a web address (`http:` or `https:`, any case) says "Needs a connection", the site's included, after "You are here" and before every other word, its href kept. `OWTools::All()` is the OnlyWorlds tools list at the pinned commit (`codegen/tools-pin.json`), generated; `OpenersForJson` takes the list, the address and the surface as JSON. On an element's menu, pass the element as you were served it (`OWOpenersOptions{&body}`, a last argument; the JSON form's `{element}`): a tool whose element link cannot open it (a room element, a hidden or proposed one, a Map, Pin or Marker, a reading aid: the tools list's `element_skips`) is left out, each mark read as `OWGraph` reads it (`MarksOf`). A refused address answers the address profile's `reason`; any other mistake an empty `reason` and a `message` naming the key. Held to the packages' shared 90-case fixture |
| `OWClient` | the first API client (keel's v2 API, onlyworlds.com), the read calls: `Health`, `GetWorld`, `Me` (its body is what `OWCharter::Served` reads), `Members`, `List` (`OWListOptions`: the typed options and the single-link filters), `ListAll` (the cursor walk, by `has_more`, refusing a page that hands back its cursor), `Get`, `Changes` and `ChangesHead` (the tip, conditional on its ETag). Answers are the core's JSON values, an element as its body (what every reader here takes and the plugin's `OWFromJson` reads), through a handler called once (`OWReply`: the value, or an `OWApiError` of kind `Api`, `Transport`, `Refused` or `Answer`). The credential rules are the Unity SDK's: `API-Key` for a world key with its PIN only on `GET /me` among the reads (and on every request of a legacy key), a Bearer for an account token, never a PIN, `world=` for an account token or no key, and what such a client may not send refused with nothing sent. It sends through `OWTransport` (`OWTransport.h`), a seam with one call and a callback the transport calls once, on the thread the client is used from; the core has no HTTP of its own. Held to keel's OpenAPI document (`tests/data/keel-wire.json`, vendored from the Unity SDK): every path, method, query parameter and header it sends, every key it reads |

An Open with menu in a game, which cannot see the browser's sign-in:

```cpp
using namespace OnlyWorlds;
OWOpenerSurface surface;
surface.audience = OWOpeners::Member;  // or OWOpeners::Public: only the rows that need nothing first
surface.browser = false;               // a game: a tool that needs an account says "Opens in your browser"
OWOpenersResult menu = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, OWAddress::Parse(link), surface);
for (const OWOpener& row : menu.rows) AddItem(row.name, row.href, row.state.value_or(""));
```

## The plugin

| | |
|---|---|
| `UOWLibrary` | `OpenWorldFolder`, `OpenWorldFile` (with the reader's verdict: accepted, reason, message), `CheckWorldFile`, `PackWorldFolder`. A relative path is taken from the project's folder |
| `UOWWorld` | one world: its id and name, element ids by type, an element's type, name and JSON, `SetStringField` and `SetIntField`, `WriteFolder`, `PackToFile`; `Native()` hands C++ the core's `OnlyWorlds::OWWorld` |
| `UOWCalendars` | the calendar profile on Blueprints: `GetWorldCalendars(World)` (each calendar's element id and name, found by the field), `FormatStep(World, CalendarId, Step)` (a step in words, "Year 1 BT"; the bare step when there is no such calendar or its value is refused, never an error) and `BreakdownStep(World, CalendarId, Step, Breakdown)` (an `FOWStepBreakdown`: year, era, month, day, the day outside the months, weekday; a part the calendar lacks is 0 or `""`). `CalendarId` `""` reads the world's only calendar when it holds exactly one. A step is an `int64` (Blueprint's Integer64) |
| `UOWAddresses` | the address profile on Blueprints: `ParseAddress(Text, Address, Reason)` reads an `ow://` link or a site URL into an `FOWAddress` (form, world, type, id, lens, moment, and a reading's `bWorldIsSlug` and `bCanonical`), `WriteAddress(Address, Text, Reason)` writes one. A part is given when its string is not empty or its `bHas` flag is set, so an empty string can refuse as that part would; the moment is an `int64` given by `bHasMoment` alone, past 32 bits `bad_moment`. A refusal is `false` and the profile's reason |
| `UOWOpeners` | the Open with menu on Blueprints, over the baked tools list: `OpenersFor(World, ElementId, Surface, Rows, Reason)` for an element of an opened world (`""` for the world itself) and `OpenersForAddress(Address, Surface, Rows, Reason)` for an address (an element's rows when it gives an id). `FOWOpenerSurface`: `Audience` (`Public` by default), `Tool`, `SignIn` (`Unknown`, `SignedIn`, `SignedOut`), `bBrowser` (false by default: a game), `bPhone`, `bOnline` (true by default; false offline, a web row then says "Needs a connection"). Each `FOWOpener` row: `Id`, `Name`, `Href`, `State` (`""` for none) |
| `UOWNames` | the names profile on Blueprints: `TypeWord(World, Type, Supertype, bMany, Word)`, the world's own word for a type from `x_names` on its World, or `false` for the standard's name (`Supertype` `""` for none) |
| `FOWHttpTransport` | the core client's transport on Unreal's own HTTP (`FHttpModule`): `Config.transport = FOWHttpTransport::Make();`, and each `OnlyWorlds::OWClient` call answers once, on the game thread. The engine follows a redirect before the transport sees it (curl, `CURLOPT_FOLLOWLOCATION`), so a followed answer is refused as a failure: point the client at `https://www.onlyworlds.com/api/v2` (its default), which redirects no API call (`OnlyWorldsLive.Client.Redirect`) |
| `FOW<Type>`, `UOWElements` | one `USTRUCT(BlueprintType)` per element type (`FOWCharacter`, `FOWPin`, ...), and the library that reads them out of a world: `GetAll<Types>(World)`, `Find<Type>(World, Id, Out)`. Generated from schema-dist's `schema.json` |

Members are PascalCase (`birth_date` -> `BirthDate`, `STR` -> `Str`). A string field is an `FString` (`""` for unset),
an integer two members, `bool bHas<Field>` and `int64 <Field>` (`false` and `0` for null or a missing key, so a set
`0` is told from an unset one: `bHasBirthDate`, `BirthDate`), a link the linked element's id (an `FString`, or a
`TArray<FString>` for a list), and the Pin's generic link two strings, `ElementType` and `ElementId`. The JSON key and
the schema's description are each member's tooltip. An integer is not a `TOptional<int64>`: 5.8's K2 schema gives a
`TOptional` member no pin on a Break or a Make node, while the pair has both (`OnlyWorlds.Graph.Pins`).

## Build and test

The core alone (Windows, MSVC; no Unreal process):

```
build.cmd                  # build\ow_tests.exe, linking the engine's zlib 1.3 (UE_ROOT, default UE_5.8), /MD
build\ow_tests.exe         # every test; exits 0 only when none failed (--filter=<text>, --require-all)
build.cmd twin             # build\twin_run.exe: tools/twin_run.cpp, the lens and graph readers over a JSON-lines file of
                           # random worlds, answered in the npm SDK's twin-runner shape (twin_run <cases> <out>)
python codegen/generate.py --check                    # the generated Unicode tables match the vendored files
python codegen/generate_models.py --check             # the generated structs match schema.json
python codegen/generate_tools.py --check              # the tools list's pin holds, OWTools.generated.cpp is current
python codegen/generate_presentation.py --check       # the generated sidecar matches presentation.json
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
  `OnlyWorlds.World.Integers`: every element of both worlds read through `UOWElements::Find<Type>` as a Blueprint
  calls it, every integer pair held to its file (null unset, `0` set to 0), the readings written out to
  `Host/Saved/OWTests/integers-<world>.txt`.
- `OnlyWorlds.Calendar.GetWorldCalendars`, `.FormatStep`, `.BreakdownStep`: each `UOWCalendars` function called as a
  Blueprint calls it (found by name, its parameters marshalled by property, `ProcessEvent` on the class default
  object, the answer and the struct's members read back by property), on Moppetopia's calendar (Sikelia holds none).
  `OnlyWorlds.Calendar.Fixture`: every case of the calendar profile's fixture (`codegen/schema-dist/profiles/`) placed
  on that calendar's element and read back the same way: the words and the breakdown of each step, every refusal.
- `OnlyWorlds.Address.Fixture`, `.World`, `.Parts`, `OnlyWorlds.Openers.World`, `.Address`, `OnlyWorlds.Names.World`,
  `.Fixture`: each `UOWAddresses`, `UOWOpeners` and `UOWNames` function called the same way, its structs set and read
  member by member and the rows read out of the out array. The address fixture's 101 parse cases and 33 of its 36
  format cases (a moment that is not a whole number cannot be set on an `int64` pin); every element of Sikelia written
  in the three spellings and read back, and its Open with rows and type words as the core gives them; the openers
  fixture's (`tests/data/`) 35 row cases (the core's rows for the baked list, the fixture's first row) and 6 address
  refusals; the names fixture's 29 cases placed on Moppetopia's World.
- `OnlyWorlds.Graph.Pins`, `.Run`, `.Integers` (editor only): Blueprints built in code. `Pins` places a Break and a
  Make node on each of the plugin's 28 structs: every member gets a pin, the 70 integer pairs a bool and an Integer64
  on both. `Run` builds one function graph
  of real K2 nodes (`ParseAddress` -> Break -> Make -> `WriteAddress` and `OpenersForAddress`, `FormatStep`,
  `TypeWord`), compiles it with the Kismet compiler (no error, no warning), and calls it once per element of Sikelia
  and twice per element of Moppetopia, every answer the core's; three mutant graphs (a pin wired past the schema, two
  pins swapped, a node left out) are each caught, by the compiler or by the answers. `Integers` makes an
  `FOWCharacter` setting `bHasLevel` and `Level` and breaks them back, and breaks `FindCharacter`'s struct for every
  Sikelia character's birth date, as its file says.
- `OnlyWorlds.Frame.MoppetopiaRootMap` needs a renderer (`-RenderOffscreen` instead of `-nullrhi`; a `-nullrhi`
  run leaves it out): the world's root Map as a plane carrying its image, the Pins as markers, captured from above to
  `Host/Saved/OWFrames/moppetopia-root-map.png`.

The core's runner and the world tests read two real worlds from this machine when present (`OW_SIKELIA`,
`OW_MOPPETOPIA`). In the core's runner a missing world is a named skip; in the automation tests it fails.

A packaged game: the host's game target, built, cooked and staged by the engine's own BuildCookRun (each step its own
call; together `-build -cook -stage -pak`), staged to `Host/Saved/StagedBuilds/Windows/` (about 930 MB, the PDB
included). The plugin's module is linked into `OWHost.exe`; its `.uplugin` is in the pak at
`RemappedPlugins/OnlyWorlds/`, since the plugin lives outside the project folder:

```
"%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%CD%\Host\OWHost.uproject" ^
    -platform=Win64 -clientconfig=Development -target=OWHost -build -cook -stage -pak -nop4 -utf8output -unattended
Host\Saved\StagedBuilds\Windows\OWHost.exe -OWWorld=<a world folder or an .owworld file> ^
    -nullrhi -unattended -nosplash -nosound -stdout -FullStdOutLogOutput
```

Given `-OWWorld`, the game (`Host/Source/OWHost/OWBoot.cpp`, a game-instance subsystem made only when the switch is
there) opens the world through `UOWLibrary` on its first tick and logs, each line starting `OWBoot:`, the plugin it
found, the element count by type, one element's address written by `UOWAddresses::WriteAddress` and read back by
`ParseAddress`, one step in the world's calendar (the first character's set birth date) when it holds one, that
element's Open with rows for a member in a game online and offline, and `done, exit N`. It then quits by itself: 0
when every reading held, 2 for a refused or missing world (the reader's reason logged), 3 for a reading that did not
hold. The log is also in the staged folder's `OWHost/Saved/Logs/OWHost.log`.

`codegen/schema-dist/` is vendored verbatim; never edit it, nor `codegen/tools/tools.json` (the tools list at the
commit `codegen/tools-pin.json` names). Under `Plugin/OnlyWorlds/`, `Core/Private/OWUnicodeData.generated.cpp` is
written by `codegen/generate.py`, `Core/Private/OWTools.generated.cpp` by `codegen/generate_tools.py`,
`Core/Private/OWPresentation.generated.cpp` by `codegen/generate_presentation.py`, and
`Source/OnlyWorlds/Public/OWModels.h`, `OWModelsJson.h` and `Private/OWModels.cpp` by `codegen/generate_models.py`;
regenerate, never edit.

## Not built yet

- A runtime actor that lays a Map out in a level: the frame is built by a test, in the editor, with a material made
  in code. A packaged game would need a material asset (the plugin ships no content yet).
- Writing a struct back into an element (edits go through `UOWWorld`'s field setters for now).
- A Blueprint graph saved as an asset, or one built in the editor's UI: the graphs that run are built in code by the
  tests; the calendar's `GetWorldCalendars` and `BreakdownStep` and the openers' `OpenersFor` are held through
  reflection only, not in a graph.
- The Open with menu over a tools list other than the baked one (`OWOpeners::OpenersForJson` is in the core only).
- The element's skips through an address on Blueprints: `UOWOpeners::OpenersForAddress` has no element to read, so
  its rows are every row; `UOWOpeners::OpenersFor` on an opened world reads the element and leaves the skipped rows out.
- The lens, its graph and the charter on Blueprints: `OWLens`, `OWGraph` and `OWCharter` are in the core only.
- The API client on Blueprints: `OWClient` and `FOWHttpTransport` are C++ only, with no async Blueprint node yet,
  and the client reads only (no writes).
- The message profile on Blueprints: the core reads it, and the client's `Members` now gives the members list that
  proves a sender, but no Blueprint call joins them yet.
- Any platform other than Win64, and any engine other than 5.8.
