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
| `OWText` | the profiles' one comparison and one order, public: `TrimAndFold(text)` (Unicode's White_Space trimmed at both ends, then simple case folding with no NFC first: room.md's "trimmed and without regard to case", not OW-18's fold) and `CompareByCodePoint(a, b)` with `ByCodePoint` for `std::sort` and `std::map` (code point order, never UTF-16 units or a collation), the npm SDK's `trimAndFold` and `byCodePoint`. Names as the profiles order them: their folds by code point, ties by id. Every profile reader here matches and orders by these |
| `OWZip` | the archive's own central-directory walk, each entry inflated by zlib and held to both sizes it declares, a writer with fixed stamps |
| `OWWorldArchive` | reads an `.owworld` by every reader rule of section 4.7 (OW-1 to OW-22), refusing with one cause; packs a folder |
| `OWWorld` | a world folder read and written back: an unedited file keeps its bytes, an edited one is written in section 5's form, a new element takes section 3.2's filename |
| `OWAddress` | the address profile (schema-dist `profiles/address.md`): `Parse` reads an `ow://` link or a site URL into a reading (form, world, type, id, moment, lens, canonical) or refuses it by the profile's named reason; `Write` and `WriteJson` write an element's or a world's address, with the writer's own refusals. Held to the profile's 137-case fixture |
| `OWCalendar` | the calendar profile (`profiles/calendar.md`, version 2): `ReadValue` reads an `atlas_calendar` value (an object, or the same object as a strict JSON string) into an `OWCalendarConfig`, or refuses it (a tool then shows the bare step); `Breakdown` and `FormatStep` read a step aloud ("3 Frost, 12 AG"); `WorldCalendars` finds a world's calendars by the field, never by supertype. Numbers are read as JavaScript reads them (whole to 2^53 - 1, `1.0` is 1), as the profile's reference package does. Held to the profile's 76-case fixture |
| `OWMessage` | the message profile (`profiles/message.md`): over element bodies as JSON, `IsMessage` and `IsNote` (the pair, trimmed and folded), addressing (`IsAddressedTo`, `IsToEveryone`, `HasUnseenRecipients` with the server's trimmed mark and a keyless reader), `SenderStatus` (verified, mismatch, unverifiable, unseen, against the world's members list), `EditedBy`, the read mark (`ReadMarkOf`: the reader's own hand, the newest), `UnreadFor`, `ThreadOf`, and a Character's line (`NoteOf`), each ordered by `created_at` as the profile orders. Held to the profile's 88-case fixture |
| `OWLens` | the lens profile (`profiles/lens.md`): `Resolve(world, lens, reader, {as_of, clock})`, the profile's `resolveLens` over a world snapshot as JSON, as keel's reader resolves it: visibility first (the hidden mark, which keeps nothing from a keeper, the owner or a co-builder, nor from a contributor its own hidden creations in its `whole`; a guest's whole and known-of ids; a proposed element in no view), a hop both ways and on through a Relation, never through a room element, the reading aids, an umbrella entering each child once, the fence and the window on each element's own date, `changed_within`, `show`'s rows as keel serves that reader, notes, the owner's two counts, and the mail rule through `OWMessage`. Beside it `IsLens`, `IsTemplate`, `CheckKeys` (a whole float reads whole, so `7.0` is 7; a fraction is refused), `LensesInUse`, `DateOf` and the room, reading-aid and review marks. Held to the fixture's 114 shape, keys, uses, resolve and invariant cases, and `IsRoomElement` to the room profile's 32. `TalksOf(element)` (serial 135): each `{type, id}` a line in the room names in its `objects` or `constructs`, ids read as written, held to the room fixture's `talk_cases` |
| `OWGraph` | the lens profile's graph section: `WorldGraph(world, reader, options)`, the profile's `worldGraph` over the same snapshot and reader `OWLens::Resolve` reads: the elements the reader sees as nodes (`{id, type, name, family, icon}`, the family and icon `OWPresentation`'s, with `marks` and `known_of`), the schema's links and the two declared extension links as edges with the index the reader is served, and what is left out counted by reason (`edges_cut`, `cut`). Room, hidden, proposed and map-space elements and the reading aids (the charter, its guide, its domains) are out unless a flag brings them back (`rooms`, `hidden`, `proposed`, `map_space`, `reading_aids`), marked (`room`, `hidden`, `proposed`, `reading_aid`; map space carries none); `hidden` and `proposed` are the owner's, a public reader takes no flag, and a flag from the wrong rung is refused by name before anything is read. With a lens, the nodes are exactly its kept (marked as above: a reading aid a lens keeps carries `reading_aid`) and its cut rides along. Beside it `DegreeOf` and `OutDegreeOf` (each node's edges, a mirror pair two and a link to itself one; the distinct nodes its own links point at), every node a key in the graph's order. Held to the fixture's 54 graph cases and the profile's invariants over each, and its 5 degree cases |
| `OWLanding` | the landing profile (`profiles/landing.md`): `LandingOf(graph, options)`, the profile's `landingOf` over what `OWGraph::WorldGraph` answers for the visitor, so it holds exactly what that reader may see: `{mode, size, clusters, unclustered, gaps}`. The heads are the most-linked Characters (never a known-of node), ties by id in code point order, at most `heads` (5); a node within `hops` steps (1) of a head joins the nearest, a tie going to the higher-ranked head, each member `{id, hops, degree}`; what no head reaches is `unclustered`; the gaps are `orphan` and `near_orphan` (degree 0 and 1, by the page's degree: a link to itself once, each place in a list once), `no_place` (a Character whose `location` names no Location node) and `nobody_here` (a Location no Character's `location` names); `mode` is `gaps` under `size` (24) nodes and `graph` at it. Options typed (`OWLandingOptions`) or as JSON, an unknown key or a value that is not a whole number of 1 or more refused by name; a whole number too large to hold reads as the largest. Held to the fixture's 33 cases |
| `OWCharter` | the charter profile (`profiles/charter.md`): `CharterOf(world, reader)`, the profile's `charterOf` over the same snapshot and reader `OWLens::Resolve` reads: the newest marked Charter (`{id, name, rules, skill}`, read whole as keel reads it), the newest marked Guide (`{id, name}`) and the marked Domains (`{id, name, sentence, hubs}`, as the reader is served each) in the Guide's order, then by folded name and id; a public reader only on a world open to anyone. Beside it `DomainsOf`, `IsCharterElement`, `ReadingAidOf`, `FirstLine` and the profile's tables. Held to the fixture's 27 cases. `Served(answer)` reads the packet a server's front door serves (`reading_aids` in a `GET /me` or an agent's join answer) as served for that key: the Charter, the Guide, the Domains, then the strand (`strand`, an `OWWorldStrand`, six empty slots when the key sees none; absent from a server that sends none). `DesignPartsOf(world, reader)` (serial 135): each Object or Construct the reader is served whole whose subtype, trimmed and folded, is `in design` or `locked`, `{type, id, state}`, only in a world where `CharterOf` finds a Charter for that reader; held to the fixture's `design_cases` |
| `OWStrand` | the strand profile (`profiles/strand.md`): `StrandOf(world, reader)`, the profile's `strandOf` over the same snapshot and reader `OWLens::Resolve` reads, with the World's `x_strand` beside the elements: the story the mark names, one of its events and that event's place, the story's protagonist and where the hero is now (`{narrative, event, location, character, now, same}`, each slot `{id, name}` or null, `same` by id). Only the story's id is required (the event and the place may be unset). A slot holds only its own kind and is never filled from another; a story the reader only knows of fills its own slot alone; a public reader only on a world open to anyone. Held to the fixture's cases |
| `OWRecord` | the record profile (`profiles/record.md`): `RecordsOf(character, elements)`, the profile's `recordsOf` over the elements as served (a JSON list): every Event whose `x_record` mark reads and whose `characters` names the Character, not still proposed (as the review profile's `proposedMarkOf` reads it: an `x_review_proposed` object with a key, so an empty mark is settled), newest first by `played_at` as an instant to the millisecond, a tie by the Event's id in code point order, each with the tool, version, seed, outcome, the Character's seat and their stories (`{id, name, as}`, `as` `protagonist`, `narrator` or `both`, by id). Who took part is `characters` alone, never `seats`. `MarkOf(element)` reads the mark or none (only on an Event; a broken writer is no mark); `InstantOf` reads `played_at` by the message profile's grammar (`OWMessage::TimeKey`, by its parts, never a date parser). Held to the fixture's 40 cases |
| `OWReport` | the report profile (`profiles/report.md`, rule 9, serial 120): `ThreadsOf(narratives)`, the profile's `reportThreadsOf` over the Narratives as served (a list of bodies, in the order served): `{parents, groups, lints}`, its `ToJson()` the fixture's shape. A report is the pair `narrative` + `Report` and the room's mark, a board entry `narrative` + `Board` and the mark, both read by `OWLens::IsRoomElement` (`IsReport`, `IsBoardEntry`); only reports are grouped. Each report's parent, `parent_narrative` read as written and never trimmed: `none` (absent, null, no string, or empty once trimmed of White_Space), `self`, `missing` (no element given has that id), `report`, `board` or `other`; a report whose parent is a report or a board entry is in that parent's group, by `created_at` through `OWMessage::InCreatedOrder`; lints `self` and `chain`, never repaired, nothing walked further up. Of two elements with one id the first is read. Core only. Held to the fixture's 20 thread cases. `TalksOf(element)` (serial 135): the talks on an element a reader shows a report with, its `objects` and `constructs` read as the room profile reads a line's, held to the fixture's `talk_cases` |
| `OWMapSpace` | the map-space profile (`profiles/map-space.md`): `MapSpaceOf(elements)`, the profile's `mapSpace` over the elements as served (a JSON list, visibility already applied): `{maps, zones, pins, out}` in the Map's pixels, from its bottom left, z an elevation (never a height). Each Map's frame (`width` and `height`, measured from its own points with `extent: "points"` when not written; `depth`; `z_max`, so a z past `depth` shows; `scale` from `x_metres_per_pixel` when positive, else null; `location`, `parent_map`); each Zone's outline per Map from its Markers by whole `order`, a Marker without one after every ordered one, ties by id, closed by `shape` (`polygon` unless `line`); each Pin's point with its element as the wire's two keys, both or neither; what is left out counted once by reason (`no_point`, `no_markers`, `dangling`, `no_map`). Two rows of one type with one id read as the last one served, for every type; `type` and `shape` read case-exactly; a frame measured from placed points only. Whole numbers read by value (`2.0` is 2), only where they are held exactly: an integer literal within int64, a fraction or an exponent up to 2^53 - 1; ids by code point. Core only. Held to the fixture's 46 cases. `LayoutOf(map, maps, pins)` (serial 110, rules 8 to 10) is the profile's second, small reading of one Map, `{layout, ground, hand}`: the `x_layout` method when it is a non-empty string (`"hand"` a board), the ground Map's id when `settings.ground.map` names a served Map other than this one, and the ids of this Map's Pins a new lay-out keeps (every Pin on a board, those whose `x_hand` is the JSON `true` on any other layout map, none on a plain Map), by code point; the Pins are filtered by `map` inside, so a caller passes every Pin it holds. Held to the fixture's 32 `layout_cases` (17 at serial 110). `CarryOf(map, x, y, maps)` (serial 139, rules 13 and 14) is the third reading, `{parent_frame, lint, points, stop}`: a point on a child Map carried onto its parent by the child's `x_parent_frame` and on up the `parent_map` chain, one level at a time, in 64-bit floats in the rule's order (`x0 + ((x * (x1 - x0)) / width)`), with the Map's own lint and the word the walk stopped on; a carry size any whole double above 0, as npm reads it. Held to the fixture's 66 `frame_cases`, every number exactly, the fixture's decimals first held to the bits Python reads. The `down_cases` (`downOf`, serial 144) are not read yet |
| `OWNames` | the names profile (`profiles/names.md`): `TypeWord(world, type, supertype, many)`, the profile's `typeWord`: a world's own word for a type from `x_names` on the World (Character shown as *Employee*, a supertype's finer word first, `many` for a plural), or absent for the standard's name, printed as written. A display lookup only: the folder, the ids and every other reader keep the standard's names. Held to the fixture's 29 cases |
| `OWPresentation` | the presentation sidecar (schema-dist `presentation.json`), generated: the colour words in their order (`Order`), each type's colour word and icon (`FamilyOf`, `IconOf`) and the fields a known-of stub shows (`PublicOf`). A family word not in `colors.order` fails loudly: the generator refuses it, naming the word, and the table holds the rule as a `static_assert` |
| `OWOpeners` | the Open with menu's rows (the tools list's README, "Open with"), as the npm SDK's `openersFor` and the Python package's `openers_for` give them: `OpenersFor(tools, kind, address, surface)` for a world or an element, the site's page first, then each listed tool with a door for that kind in the list's order, each with its word ("Sign in to Atlas first", "Opens in your browser", ...) or none; for the public (the signed-out reader) a tool that needs an account is a row that says "Needs an account" and opens nothing (its href kept), and one that needs the device is not there; with the surface's `online` false, every row whose href is a web address (`http:` or `https:`, any case) says "Needs a connection", the site's included, after "You are here" and before every other word, its href kept. `OWTools::All()` is the OnlyWorlds tools list at the pinned commit (`codegen/tools-pin.json`), generated; `OpenersForJson` takes the list, the address and the surface as JSON. On an element's menu, pass the element as you were served it (`OWOpenersOptions{&body}`, a last argument; the JSON form's `{element}`): a tool whose element link cannot open it (a room element, a hidden or proposed one, a Map, Pin or Marker, a reading aid: the tools list's `element_skips`) is left out, each mark read as `OWGraph` reads it (`MarksOf`). A tool whose element door opens only some types (the tools list's `element_types`, `OWToolOpen::element_types`: Tactical Tangle opens Characters) is left off every other type's menu, world door and all, read from the address's type with no element needed; a world's menu reads no types. A refused address answers the address profile's `reason`; any other mistake an empty `reason` and a `message` naming the key. Held to the packages' shared 100-case fixture |
| `OWClient` | the first API client (keel's v2 API, onlyworlds.com). The read calls: `Health`, `GetWorld`, `Me` (its body is what `OWCharter::Served` reads), `Members`, `List` (`OWListOptions`: the typed options and the single-link filters), `ListAll` (the cursor walk, by `has_more`, refusing a page that hands back its cursor), `Get`, `Changes` and `ChangesHead` (the tip, conditional on its ETag). The element writes: `Create` (an id minted when the element has none, by `OWId`: a UUIDv7, or under `OWCreateOptions::idempotency_key` the UUIDv5 of the key, so a retry replays), `Upsert`, `Patch` and `Delete` (`OWWriteOptions::if_match`, the `change_seq` sent as `If-Match`), `EditLinks` (`OWLinkEdit`: add and remove, the one way to change a link array), `Bulk` (`OWBulkItem`s, `OWBulkOptions`, answering an `OWBulkResult` of `OWBulkSlot`s); each body a copy cleaned by `Sanitize` (the six read-only keys left out, a null string field sent as `""`, extension keys untouched), a known-of stub refused. Answers are the core's JSON values, an element as its body (what every reader here takes and the plugin's `OWFromJson` reads), through a handler called once (`OWReply`: the value, or an `OWApiError` of kind `Api`, `Transport`, `Refused` or `Answer`; a write that became a proposal is a 202 with `IsProposed`, a stale `If-Match` a 412 with `Current()`, a `fields` list naming fields the type lacks a 422 whose `FieldsRefusal()` lists every unknown name, the types that have each and the names allowed). The credential rules are the Unity SDK's: `API-Key` for a world key with its PIN only on `GET /me` among the reads (and on every request of a legacy key) and on every write, a Bearer for an account token, never a PIN, `world=` for an account token or no key, and what such a client may not send (every write) refused with nothing sent; every write carries `X-OW-Client` (`OWClientConfig::client`, `unreal-sdk` by default) and, on a client declared an agent (`OWClientConfig::agent`), `X-OW-Agent`. It sends through `OWTransport` (`OWTransport.h`), a seam with one call and a callback the transport calls once, on the thread the client is used from; the core has no HTTP of its own. Held to keel's OpenAPI document (`tests/data/keel-wire.json`, vendored from the Unity SDK): every path, method, query parameter, header and body key it sends, every key it reads |
| `OWJoin` | joining a world by invite link with no account: `ParseLink` (the `ow_j_` code out of a pasted link), `Preview` (what a code holds, unspent: an `OWJoinPreview` with a person link's note, the glimpse and a table's picks) and `Redeem` (a player's read key or an agent's seat, an `OWJoined` with the seat's front door and reading aids), both keyless POSTs through an `OWJoinConfig`'s transport; a person's link is `OWClient::AcceptJoin` on an account token (an `OWJoinedMembership`). keel's refusals have their helpers (`IsCharacterTaken`, `IsCharacterInvalid`, `IsWorldFull`, `IsAlreadyMember`, `IsPinRequired`); every bad code is the one `IsNotFound`. The Unity SDK's `OWJoin` |

An Open with menu in a game, which cannot see the browser's sign-in:

```cpp
using namespace OnlyWorlds;
OWOpenerSurface surface;
surface.audience = OWOpeners::Member;  // or OWOpeners::Public: an account tool's row says "Needs an account"
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
| `UOWOpeners` | the Open with menu on Blueprints, over the baked tools list: `OpenersFor(World, ElementId, Surface, Rows, Reason)` for an element of an opened world (`""` for the world itself) and `OpenersForAddress(Address, Surface, Rows, Reason)` for an address (an element's rows when it gives an id). A tool that opens only some types is left off every other type's menu through either call (Tactical Tangle is on a world's and a Character's, not a Location's). `FOWOpenerSurface`: `Audience` (`Public` by default, the signed-out reader: Atlas, Lantern and Tactical Tangle say "Needs an account"), `Tool`, `SignIn` (`Unknown`, `SignedIn`, `SignedOut`), `bBrowser` (false by default: a game), `bPhone`, `bOnline` (true by default; false offline, a web row then says "Needs a connection"). Each `FOWOpener` row: `Id`, `Name`, `Href`, `State` (`""` for none; a row that says "Needs a connection" or "Needs an account" keeps its `Href` and opens nothing: draw it disabled) |
| `UOWNames` | the names profile on Blueprints: `TypeWord(World, Type, Supertype, bMany, Word)`, the world's own word for a type from `x_names` on its World, or `false` for the standard's name (`Supertype` `""` for none) |
| `UOWMessages` | the message profile on Blueprints, over an opened world, each element asked by its id as the API serves it (a body that names no type takes its folder's): `GetMessages(World)` and `GetNotes(World)` (ids in `created_at` order), `IsMessage`, `IsNote`, `RecipientsOf`, `IsAddressedTo`, `IsToEveryone(World, MessageId, bKeyless)`, `HasUnseenRecipients(World, MessageId, bKeyless)` (an `EOWUnseenRecipients`: `Yes`, `No`, or `Unknown` for a keyless reader), `IsSentBy`, `ThreadOf(World, RootId)`; against the world's members list as JSON text (what `OWClient::Members` answers; `""` for no list) `SenderStatus(World, MessageId, MembersJson, Sender, Reason)` (an `FOWSender`: `Status`, one of `Verified`, `Mismatch`, `Unverifiable`, `Unseen`; `Claimed`; `WrittenBy` on a mismatch), `EditedBy(World, MessageId, MembersJson, Edited, Reason)` (an `FOWEdited`: `bEdited`, `By`), `ReadMarkOf(World, CharacterId, MembersJson, bKeyless, MarkId, Reason)`, `UnreadFor(World, CharacterId, ReadMarkId, MembersJson, Unread, Reason)` and `NoteOf(World, CharacterId, MembersJson, NoteId, Reason)`, an id `""` for none. A call that reads the members list or a read mark refuses as `false` and the reason (`no world`, `no element <id> in this world`, `members: ` and where the text does not parse); every other call reads an id the world does not hold as the profile's no |
| `UOWLens` | the lens profile on Blueprints, over an opened world read as a snapshot (every element as served, world.json's `time_current` as the world's now, a folder's hidden mark on each body): `GetLenses(World)`, `IsLens`, `IsTemplate`, `LensesFor(World, CharacterId, bMembershipChose, Membership)` (the membership's choice wins whole, else the Character's `x_lens_uses`), the marks `IsHidden`, `IsProposed`, `IsRoomElement`, `IsReadingAid`, `DateOf(World, ElementId, Date)`, and `Resolve(World, LensId, Reader, Options, Reading, Reason)` with `ResolveJson` for a lens body given as JSON text. `FOWLensReader`: `Rung` (`EOWLensRung`: `Owner`, `Contributor`, `Guest`, `Public`, the default), `Character`, a guest's `Whole` and `KnownOf`, `bOwnerCounts` (the owner reading as this reader). `FOWLensOptions`: `AsOf` with `bHasAsOf`, `Clock` (RFC 3339, `""` the machine's), `MembersJson`. `FOWLensReading`: `Kept`, `KnownOf` (the kept ids a guest holds only as stubs), `Rows` (each as JSON text, as `show` makes it), `Cut` (`FOWLensCut`: `Reason`, `Count`), `NotesIgnored`, `Mail` with `bHasMail` (`FOWLensMail`: `Unread`, `Read`, `Rows`, `Cut`, `TotalUnread`, `TotalRead`), `Unreadable`. A lens the reader is not served whole reads as nothing; a refusal is `false` and the reason (`no world`, `lens: ` or `members: ` and where the text does not parse, the core's sentence for a clock that does not read). The names are the Unity SDK's `OWLens`'s, the behaviour the core's |
| `UOWGraph` | the lens profile's graph on Blueprints: `Read(World, Reader, Options, Graph, Reason)`, the core's `WorldGraph` over the same snapshot and reader as `UOWLens`; `FOWGraphOptions`: `LensId` or `LensJson` (never both), `AsOf` with `bHasAsOf`, `Clock`, `MembersJson`, and the five flags `bRooms`, `bHidden`, `bProposed`, `bMapSpace`, `bReadingAids`. `FOWWorldGraph`: `Nodes` (`FOWGraphNode`: `Id`, `Type`, `Name` with `bHasName`, `Family`, `Icon`, `Marks`, `bKnownOf`), `Edges` (`FOWGraphEdge`: `Source`, `Field`, `Target`, `Index`), `EdgesCut`, `Cut`, `Unreadable`. A flag from the wrong rung is refused by its word (`hidden`, `proposed`, `rooms`, `map_space`, `reading_aids`, the first unmet) before anything is read. `DegreeOf(Graph)` and `OutDegreeOf(Graph)` (`FOWGraphDegree`: `Id`, `Degree`), every node in the graph's order. The names are the Unity SDK's `OWGraph`'s |
| `UOWCharter` | the charter profile on Blueprints: `CharterOf(World, Reader, bOpen, ReadingAids, Reason)` and `DomainsOf` over an opened world (`bOpen`: the world is open to anyone, the only way a `Public` reader reads anything; pass the World's `public_read`, from world.json in a folder or the world GET over the API, and nothing else), `ReadingAidsOf(AnswerJson, ReadingAids, Reason)` over a front door's packet as served (`ReadMe`'s `Json`, so a graph wires one into the other), `IsCharterElement` and `ReadingAidOf`. `FOWReadingAids`: `Charter` with `bHasCharter` (`FOWCharterLaw`: `Id`, `Name`, `RulesJson`, `Skill`), `Guide` with `bHasGuide` (`FOWCharterGuide`), `Domains` (`FOWCharterDomain`: `Id`, `Name`, `Sentence`, `Hubs` of `FOWCharterHub` `Type` and `Id`), and from a front door the strand as JSON text, `StrandJson` with `bHasStrand`. A refusal is `false` and the reason (`no world`, `answer: ` and where the text does not parse, `no reading aids in the answer`) |
| `FOWHttpTransport` | the core client's transport on Unreal's own HTTP (`FHttpModule`): `Config.transport = FOWHttpTransport::Make();`, and each `OnlyWorlds::OWClient` call answers once, on the game thread. The engine follows a redirect before the transport sees it (curl, `CURLOPT_FOLLOWLOCATION`), so a followed answer is refused as a failure: point the client at `https://www.onlyworlds.com/api/v2` (its default), which redirects no API call (`OnlyWorldsLive.Client.Redirect`) |
| `UOWClientRead`, `UOWClientList`, `UOWClientListAll`, `UOWClientChanges` | the API client's read calls on Blueprints, one async node each, with `OnSuccess` and `OnFailure` (one signature per node, its value pins and an `FOWApiError`): `ReadHealth`, `ReadWorld`, `ReadMe`, `ReadMembers`, `ReadElement(Config, Type, Id, Expand, Fields, ViewAs)` (each a body as JSON text, the form `UOWWorld::GetElementJson` gives), `ListElements(Config, Type, Options)` (`Elements`, each row's JSON text, `bHasMore`, `NextCursor`), `ListAllElements(Config, Type, Options)` (every page by `has_more`, or no half list), `ReadChanges(Config, Since, After)` and `ReadChangesHead(Config)` (an `FOWChangesPage`: `Cursor`, `Changes` of `FOWChange`, an upsert's element as JSON text, `Head`, `Epoch` with `bHasEpoch`). `ReadMembers` answers the roster as the JSON array `UOWMessages` takes as `MembersJson`, so a graph wires one into the other. `FOWClientConfig`: `ApiKey`, `ApiPin`, `World` (for an account token or no key), `BaseUrl` (`""` the default), `PageSize`, and for the writes `Client` (sent as `X-OW-Client`; `""` the core's `unreal-sdk`) and `bAgent` (`X-OW-Agent`); `FOWListOptions` is the core's `OWListOptions`, its filters a map. Each node is its own `OWClient` on `FOWHttpTransport`, so it answers later, on the game thread; the core's refusals (a config it will not build, a call the key may not send, a value that is not one path segment) answer at once, inside the activation, kind `Refused`, nothing sent. An `FOWApiError` carries `Kind` (`None` on `OnSuccess`, `Api`, `Transport`, `Refused`, `Answer`), `Status`, keel's `Type`, `Code`, `Message`, `Param`, `DocUrl`, `RawBody`, `RetryAfter` with `bHasRetryAfter`, `bRetryable`, `ResyncReason`, `Line`, and for a write `bIsProposed` with `Proposal` (a 202: the row as JSON text) and `Current` (a 412's element now, as JSON text). A node is kept (in the root set) from its activation until it answered, once; C++ may hand one another transport with `SetTransport` before it is activated. Never keep a key or a PIN in an asset |
| `UOWClientWrite`, `UOWClientBulk` | the API client's element writes on Blueprints, one async node each on the read nodes' base (`UOWClientNode`: the same config, keep, transport seam and `FOWApiError`), with `OnSuccess` and `OnFailure`: `CreateElement(Config, Type, ElementJson, IdempotencyKey)`, `UpsertElement(Config, Type, Id, ElementJson)`, `PatchElement(Config, Type, Id, PartialJson, bHasIfMatch, IfMatch)`, `DeleteElement(Config, Type, Id, bHasIfMatch, IfMatch)` and `EditElementLinks(Config, Type, Id, Field, Add, Remove, bHasIfMatch, IfMatch)` (`Json`, the element as keel wrote it as JSON text, `""` after a delete), and `BulkWrite(Config, Items, bAtomic, IdempotencyKey)` (`Items` of `FOWBulkItem`: `Type`, `ElementJson`; `Outcome`, an `FOWBulkResult`: `bErrors`, `Items` of `FOWBulkSlot` (`Status`, `Id`, `CreatedAt`, `UpdatedAt`, `bSucceeded`, `bIsProposed`, `Error`, `Proposal`), `bWasReplay`, `Failed` (the indices), `SucceededCount`, `bAllSucceeded`). The element goes in as JSON text and the core does the rest: the body cleaned by `Sanitize`, an id minted for an element without one (under the key, the keyed id, so a retry replays), the PIN, `X-OW-Client`, `X-OW-Agent`, `If-Match` (the `change_seq` as read, sent only with `bHasIfMatch`), `Idempotency-Key` (`""` for none), an empty link list not sent. A write that became a proposal (a 202 on an upsert, a patch or a link edit) is `OnFailure` with `Error.bIsProposed` and the row in `Error.Proposal`; a stale `If-Match` is `OnFailure`, status 412, with `Error.Current`; a bulk is `OnSuccess` whenever keel answers 200, its slots saying what landed. Refused at once, nothing sent: everything the core refuses (an account token or no key, a config it will not build, a value that is not one path segment, a negative `IfMatch`, an element that is not an object or is a known-of stub, an empty bulk, an item without a type) and, the node's own word, element text that is not JSON. A read key's write and a write key's without a PIN are sent, as the core sends them: keel answers those |
| `FOW<Type>`, `UOWElements`, `UOWElementJson` | one `USTRUCT(BlueprintType)` per element type (`FOWCharacter`, `FOWPin`, ...), and the library that reads them out of a world: `GetAll<Types>(World)`, `Find<Type>(World, Id, Out)`, and gives one back as the JSON text a write node takes: `Get<Type>Json(<Type>)`, pure (`GetCharacterJson(Character)`, ...; `CreateElement`, `UpsertElement`, `PatchElement`, a `BulkWrite` item). For C++, `OWFromJson(Body, Out)` and `OWToJson(Struct)` on the core's `OWJsonValue` (`OWModelsJson.h`). Generated from schema-dist's `schema.json`. Beside it, hand-written, `UOWElementJson::MergeElementJson(ElementJson, StructJson, Merged, Reason)`, pure: a door's JSON laid over the element's JSON as read (`UOWWorld::GetElementJson`, or a read node's `Json`), every other key kept in its place, so an `UpsertElement` of `Merged` writes the struct's fields and keeps the element's extensions (below) |

Members are PascalCase (`birth_date` -> `BirthDate`, `STR` -> `Str`). A string field is an `FString` (`""` for unset),
an integer two members, `bool bHas<Field>` and `int64 <Field>` (`false` and `0` for null or a missing key, so a set
`0` is told from an unset one: `bHasBirthDate`, `BirthDate`), a link the linked element's id (an `FString`, or a
`TArray<FString>` for a list), and the Pin's generic link two strings, `ElementType` and `ElementId`. The JSON key and
the schema's description are each member's tooltip. An integer is not a `TOptional<int64>`: 5.8's K2 schema gives a
`TOptional` member no pin on a Break or a Make node, while the pair has both (`OnlyWorlds.Graph.Pins`).

Written back (`OWToJson`, `Get<Type>Json`), a struct is every schema field, in the schema's order, each unset one as
keel serves it: a string `""` (schema-dist's `string-empty-is-unset`), an integer without its `bHas` `null`, a link
`null` for `""`, a link list `[]`, the Pin's `element_type` `""` and `element_id` `null`. `world` is never written,
nor `type` or the server's stamps, and the id is left out when it is `""` (a create then mints one). An integer is
written as its digits, so `4255.0` in a file comes back `4255`, and a number that is not a whole one (which reads as
unset) comes back `null`. A struct holds no extension key (`x_*`, `atlas_*`, `shadow_*`) and no field a newer schema
adds: `UpsertElement` (keel's PUT) writes an element whole, so a struct's JSON sent there clears the extensions keel
holds for it; `PatchElement` keeps them (it replaces only the keys sent, though a link list whole).

To upsert a struct read out of a world, merge it first: `MergeElementJson(ElementJson, StructJson, Merged, Reason)`
answers the element as read with each key of the struct's JSON laid over it in place, and every other key kept in its
place and order: the extensions, the keys a newer schema adds, `type`, `world` and the server's stamps (the write
nodes' `Sanitize` strips `world`, `type`, `created_at`, `updated_at`, `change_seq` and `known_of` before sending;
keel drops `created_by` and the other echoed stamps). A struct's unset (`""`, `null`, `[]`) lands over a set value (a
cleared field means it), but over an unset or absent one the read's form is kept, and an integer of the same value
keeps the read's spelling (`180.0`), so an unchanged struct merges back to the element as read (byte for byte when the
read is `GetElementJson`'s text: `Merged` is written in the same form). A key the read lacks and the struct sets is
added at the end. The id is the read's when the struct's is `""`; two different ids, an id that is not a string, or
text on either side that is not a JSON object are `false` with `Reason` (`element: `, `struct: `, `id: `) and `Merged`
`""`. A known-of stub merged stays a stub (`known_of` kept), so the write node refuses it; a body served with
`x_served_trimmed` keeps the mark, and keel refuses it by name rather than the hidden ids being dropped.

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
python codegen/generate_frame_bits.py --check         # tests/data's frame bits match the map-space fixture
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
  fixture's (`tests/data/`) 46 row cases (the core's rows for the baked list, the fixture's first row) and 6 address
  refusals; the names fixture's 29 cases placed on Moppetopia's World.
- `OnlyWorlds.Messages.World`, `.Fixture`, `.Beside`, `.Sikelia`: each `UOWMessages` function called the same way, its
  enums read by number and its structs member by member. A world built in memory (two bodies that name no type, read
  as their folder's), every answer and refusal written out; the message profile's 88 fixture cases and 76 of the 79
  readings beside it (`tests/data/message-beside-the-fixture.json`; the other three hold what no world holds: a list
  as a body, a number as an id, a list as a read mark), each case placed in a world of its own and asked by id; and
  every element of Sikelia against the core.
- `OnlyWorlds.Lens.World`, `.Fixture`, `.Reflection`, `OnlyWorlds.WorldGraph.World`, `.Fixture`, `.Reflection`,
  `.Sikelia`, `OnlyWorlds.Charter.World`, `.Fixture`, `.Reflection`: `UOWLens`, `UOWGraph` and `UOWCharter` on worlds
  built in memory (a body that names no type, a hidden, a proposed, a room and a reading-aid element, five lenses, two
  Charters and their Domains), every rung, flag and refusal; every case of the lens profile's fixture (its 6 shape, 25
  keys, 5 uses, 102 resolve and 4 invariant cases through `UOWLens`, its 54 graph and 5 degree cases through
  `UOWGraph`; the one node name stored as a number read as no text) and the charter profile's 27, each case's world
  placed in a world of its own (a world-level hidden list as `hidden: true` on each listed body, the one mark the
  profile reads both as); every function also called through reflection as a graph calls it, against the direct call;
  and Sikelia's whole graph for the owner against the core's.
- `OnlyWorlds.Graph.Pins`, `.Run`, `.Integers` (editor only): Blueprints built in code. `Pins` places a Break and a
  Make node on each of the plugin's 53 structs: every member gets a pin, the 70 integer pairs a bool and an Integer64
  on both. `Run` builds one function graph
  of real K2 nodes (`ParseAddress` -> Break -> Make -> `WriteAddress` and `OpenersForAddress`, `FormatStep`,
  `TypeWord`), compiles it with the Kismet compiler (no error, no warning), and calls it once per element of Sikelia
  and twice per element of Moppetopia, every answer the core's; three mutant graphs (a pin wired past the schema, two
  pins swapped, a node left out) are each caught, by the compiler or by the answers. `Integers` makes an
  `FOWCharacter` setting `bHasLevel` and `Level` and breaks them back, and breaks `FindCharacter`'s struct for every
  Sikelia character's birth date, as its file says.
- `OnlyWorlds.Client.Nodes.Requests`, `.Options`, `.Answers`, `.Errors`, `.Refusals`, `.Once`, `.Latent`,
  `.Reflection`, `.Pins` (editor only): the client's async nodes through a fake transport that records each request
  and answers from a script, never the network. Each of the nine nodes on seven credentials (a write, a read and a legacy key, an account token
  with a world and without, no key with a world and without: 40 requests as the core sends them, method, URL and headers,
  and 23 refusals before sending); every list, get and feed option; each answer's shape; keel's envelope, a
  `Retry-After`, a resync's reason, no answer and a body that is not the answer on `OnFailure`; one answer from a transport
  that answers twice; a walk held a frame a page, the node kept through a garbage collection in flight and collected after;
  `ReadMembers` made and activated through reflection, its text read by `UOWMessages::SenderStatus` and `EditedBy`; and the
  pins a graph's `UK2Node_AsyncAction` gets for each factory.
- `OnlyWorlds.Client.WriteNodes.Requests`, `.Bodies`, `.Answers`, `.Errors`, `.Refusals`, `.Once`, `.Reflection`,
  `.Pins` (editor only): the write nodes through the same fake transport. Each of the six on eight credentials (a
  write key with a PIN and without, a read key, a legacy key: 24 requests, method, URL, headers and body as the core
  sends them; an account token and no key, each with a world and without: 24 refusals before sending); the bodies
  and options (a keyed id and a minted UUIDv7, `If-Match` given and not, the client's name and the agent mark, an
  empty link list, a keyed atomic bulk); the answers (an element's numbers as served, a delete's `""`, a bulk's
  created, failed and proposed slots with `bWasReplay`); keel's envelope, a 412's `Current`, a 202 proposal on the
  three writes that may become one, a 202 without a row, no answer; 21 refusals with nothing sent, the node's own
  for text that is not JSON among them; one answer from a transport that answers twice; `CreateElement` made and
  activated through reflection; and each factory's pins.
- `OnlyWorlds.Models.Write.RoundTrip`, `.Unset`, `.Doors`, `.Node`: the way back. `RoundTrip` reads every element
  of both worlds through `Find<Type>` and writes it through `Get<Type>Json`, both called as a Blueprint calls them,
  and holds the text to the file on `schema.json`'s keys: a key as the file holds it is the same, a form a struct
  cannot tell apart is counted by name (a key the file lacks, written unset; a null string written `""`; a null link
  list `[]`; an empty link `null`; an integer spelled `4255.0` written `4255`), any other difference fails, and so
  does a key written outside the schema; the keys a struct cannot carry (the stamps, extension keys) are counted.
  `Unset`: each kind's unset and set form through `OWToJson` (a set `0`, a value past 2^53, a `bHas` false over a
  value, `STR`'s spelling, the Pin's pair, an extension key not carried). `Doors`: the 22 doors, each pure and static,
  its type's struct in and a string out. `Node`: a struct made member by member, its door called through reflection,
  and the text handed to `UpsertElement`, `PatchElement`, `CreateElement` and `BulkWrite`: each body sent (the fake
  transport) is the door's JSON as a value.
- `OnlyWorlds.Models.Merge.RoundTrip`, `.Changes`, `.Ids`, `.Refusals`, `.Upsert`, `.Function`: the merge.
  `RoundTrip`: every element of both worlds as read (`GetElementJson`), through `Find<Type>`, unchanged, its door and
  `MergeElementJson`, each called as a Blueprint calls it: the merge is the element as read, as a value and as text
  (the extension keys kept are counted). `Changes`: on one Character, changed fields land in place, a cleared string
  `""`, a cleared link and integer `null`, a field set where the read had none at the end, an unset where it had none
  stays absent, and the extensions, stamps, a null string and `180.0` are kept. `Ids` and `Refusals`: the id's rules
  and each refusal's side. `Upsert`: the merge's text through `UpsertElement` (the fake transport) sends the extensions
  and none of the six read-only keys, the door's text alone sends no extension, and a known-of stub's merge is refused
  with nothing sent. `Function`: its pins.
- `OnlyWorlds.Frame.MoppetopiaRootMap` needs a renderer (`-RenderOffscreen` instead of `-nullrhi`; a `-nullrhi`
  run leaves it out): the world's root Map as a plane carrying its image, the Pins as markers, captured from above to
  `Host/Saved/OWFrames/moppetopia-root-map.png`. The scene is the plugin's (`Public/OWFrame.h`, the one the packaged
  game's `-OWFrame` shoots); the test makes its two materials in code.
- `OnlyWorlds.Frame.RootMapSkipsLayoutMaps`: `OWFrame::RootMap` on worlds made in memory (no renderer): a layout map
  (the map-space profile's rule 8, `x_layout` an object with a non-empty string `method`) read last is not the root, a
  world whose only Map is one has none, and seven `x_layout` values that are no mark leave the Map a candidate.

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

A Blueprint graph saved as an asset, cooked and run by the game: the host's editor-only module
(`Host/Source/OWHostEditor/`, `"Type": "Editor"`, never in a game build) has a commandlet that writes two graphs into
`Host/Content/OW/`, built as `OWGraphTests.cpp` builds its graphs but in real packages, compiled by the Kismet compiler
and saved. `BP_OWBootGraph`'s one function, `ReadWorld(Path, Id) -> ElementCount, ElementName, Error`, opens the world
folder through `UOWLibrary::OpenWorldFolder` and reads the opened world's `GetElementCount` and `GetElementName(Id)`;
`BP_OWBootGraphWrong` wires `GetWorldName` into `ElementName` instead, a graph that compiles and answers wrong. The
folder is generated, never committed (`.gitignore`), and `Host/Config/DefaultGame.ini` cooks it although no map
references it (`DirectoriesToAlwaysCook`, `/Game/OW`). Run the commandlet after the editor target is built and before
BuildCookRun:

```
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Host\OWHost.uproject" -run=OWMakeGraph ^
    -unattended -nosplash -nop4               # exit 0 when both graphs are compiled and saved
```

Given `-OWGraph` beside `-OWWorld` (bare: `BP_OWBootGraph`; `-OWGraph=<a soft class path>`: that class, such as
`/Game/OW/BP_OWBootGraphWrong.BP_OWBootGraphWrong_C`), the game, after its other readings, loads the class, calls
`ReadWorld` on an instance with the world's path and the first element's id, and logs the graph's answer beside the
C++ reading on an `OWBoot:` line: exit 3 when they differ, 4 when the class or its `ReadWorld` is not there (not
cooked, or the path names none). The graph reads a world folder only: given an `.owworld` file it answers the folder
reader's refusal, and the run exits 3. Without `-OWGraph`, nothing changes.

Run in a Development package after the commandlet: on Moppetopia's folder the saved graph answers 310 elements and
"Splashworth's Glimpse", the same as the C++ path (exit 0); the wrong graph answers the world's name, "Moppetopia"
(exit 3); a class not in the pak, a class without `ReadWorld` and an `.owworld` file exit 4, 4 and 3. From Git Bash,
set `MSYS_NO_PATHCONV=1`, or it rewrites `/Game/...` into a Windows path and every class reads as missing. An empty
`-OWGraph=` takes the next switch as its value (exit 4, the message naming that switch).

A world drawn by the game: `-OWFrame=<a .png path>` beside `-OWWorld` (a relative path from the staged `OWHost/`)
draws the world's root Map after the other readings, the frame test's scene (`OWFrame.h`: a plane carrying the map's
image, one red sphere per Pin with an x and a y, a capture straight above, 1800 x 1200), and writes it there. A game
cannot make a material, so the host's editor module writes one, `/Game/OW/M_OWFrame` (unlit, emissive the texture
parameter `MapImage` times the colour `Tint`), into `Host/Content/OW/`, cooked with the graphs; the engine's plane and
sphere, loaded by path, are cooked by `DefaultGame.ini`'s `/Engine/BasicShapes` line. Run the commandlet after the
editor target is built and before BuildCookRun, and the game with a renderer:

```
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\Host\OWHost.uproject" -run=OWMakeFrame ^
    -unattended -nosplash -nop4               # exit 0 when the material is saved
Host\Saved\StagedBuilds\Windows\OWHost.exe -OWWorld=<a world folder or an .owworld file> -OWFrame=<a .png path> ^
    -RenderOffscreen -unattended -nosplash -nosound -stdout -FullStdOutLogOutput
```

The map's image is the first `.png` under `media/<map id>/base-image/` among the world's files, from a folder or an
`.owworld` file alike; a map without one is a plain sand-coloured plane. A cooked game makes a mesh's scene proxy only
once its pipeline states have precached, so the frame waits, tick by tick, until every mesh has one (60 s at most),
then shoots. It logs the map, the image, the wait, `wrote <path> (1800 x 1200, N bytes)`, and how many markers it finds
again in its own pixels where their pins put them: exit 0 when every one is there and the plane holds more than 200
colours (2 without an image), 3 when not (the frame still written). Exit 5, and no file written, when it cannot draw:
`-OWFrame` bare, empty or not a `.png` path, no renderer (`-nullrhi`), no root Map (a Map with no `parent_map` that is not a layout map) or one
without a width and a height, the material or the meshes not in the pak, pixels that cannot be read, a write that
fails. A refused world (2) and a missing graph (4) end the run before the frame. Without `-OWFrame`, nothing changes.

`-clientconfig=Shipping` builds, cooks and stages the same way (`OWHost-Win64-Shipping.exe`, about 600 MB staged with
its PDB), and the plugin runs in it: exit 0 on Moppetopia's folder, 2 on a world that is not there. A Shipping game
writes no `OWBoot:` lines and no log file (the host's target keeps the engine's default, no logging in Shipping), so
there the exit code is the reading.

Copied the way the opening says (`Plugin/OnlyWorlds/`'s `Core/`, `Source/` and `.uplugin` into a fresh copy of the
host's `Plugins/OnlyWorlds/`, with no `AdditionalPluginDirectories`), the plugin builds from nothing, packages with the
same BuildCookRun, and the game finds it at `OWHost/Plugins/OnlyWorlds/` in the staged folder: Moppetopia's folder read
whole (310 elements, 22 types, exit 0), a missing world refused (exit 2). Its automation tests need the repository
around it: they read the conformance set at `../../codegen/schema-dist/` from the plugin (`OWTestPaths.h`).

`codegen/schema-dist/` is vendored verbatim; never edit it, nor `codegen/tools/tools.json` (the tools list at the
commit `codegen/tools-pin.json` names). Under `Plugin/OnlyWorlds/`, `Core/Private/OWUnicodeData.generated.cpp` is
written by `codegen/generate.py`, `Core/Private/OWTools.generated.cpp` by `codegen/generate_tools.py`,
`Core/Private/OWPresentation.generated.cpp` by `codegen/generate_presentation.py`, and
`Source/OnlyWorlds/Public/OWModels.h`, `OWModelsJson.h` and `Private/OWModels.cpp` by `codegen/generate_models.py`;
regenerate, never edit.

## Not built yet

- Joining by invite link on Blueprints: `OWJoin::ParseLink`, `Preview`, `Redeem` and `OWClient::AcceptJoin` are in
  the core only. The account's own link calls (making, listing and revoking a world's links) are not built at all.

- A Map laid out in a level on Blueprints, or as an actor a level keeps: `OWFrame` (C++) lays one out in a world of
  its own and shoots it once, with materials its caller gives (the plugin ships no content; the host cooks its own).
  Child maps, Markers and Zones are not drawn.
- Writing a struct back into an opened world's element (edits go through `UOWWorld`'s field setters for now; a
  struct goes out over the API through `Get<Type>Json`).
- A Blueprint graph built in the editor's UI: the graphs that run are built in code, by the tests and by the host's
  `-run=OWMakeGraph` (whose saved graph the packaged game runs, `OpenWorldFolder`, `GetElementCount` and
  `GetElementName` alone); the calendar's `GetWorldCalendars` and `BreakdownStep`, the openers' `OpenersFor` and every `UOWMessages`,
  `UOWLens`, `UOWGraph` and `UOWCharter` call are held through reflection only, not in a graph. The client's nodes are placed on a graph for their pins but
  never compiled or run in one: one is activated through reflection, the rest from C++.
- The Open with menu over a tools list other than the baked one (`OWOpeners::OpenersForJson` is in the core only).
- The element's skips through an address on Blueprints: `UOWOpeners::OpenersForAddress` has no element to read, so
  no row is left out for a mark (a row is still left out for the address's type, `element_types`);
  `UOWOpeners::OpenersFor` on an opened world reads the element and leaves the skipped rows out.
- What the lens, the graph and the charter keep in the core only: `OWLens::CheckKeys`, `FieldsOf`, `PublicFieldsOf`
  and the profile's tables (`UOWLens::Resolve` names a refused key as `Unreadable`); a lens's recipe (`x_lens`) and a
  row as typed values (a row is JSON text); the graph's neighbours of one id (the core has no such call: walk
  `Edges`); `OWCharter::FirstLine`. A world-level hidden list (a snapshot's `hidden`, which a folder does not keep: the
  hidden mark is read from each body) and the charter's `open` read from world.json by the door itself (`bOpen` is the caller's: the World's `public_read`).
- The strand on Blueprints: `OWStrand::StrandOf` is in the core only; a front door's strand rides on
  `FOWReadingAids.StrandJson` as JSON text.
- A patch of only the fields a graph changed: `Get<Type>Json` gives every schema field, merged or not.
  No client node, read or write, can be cancelled or given its own timeout (the transport's 30 seconds hold).
- An element read over the API as its `FOW<Type>` struct: the nodes answer each element as JSON text, and no Blueprint
  call turns that text into a struct or places it in a world (`OWFromJson` does it in C++). `UOWMessages` reads an
  opened world only: a message served by the API and not in a world has no Blueprint call.
- The conditional tip read on Blueprints: each node is its own client, so `ReadChangesHead` holds no ETag between
  reads and every poll is a full one (`bUnchanged` is never set); `ListAllElements` gives no progress per page.
- Any platform other than Win64, and any engine other than 5.8.
