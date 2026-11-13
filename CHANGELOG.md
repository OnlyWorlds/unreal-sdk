# Changelog

Notable changes to OnlyWorlds for Unreal. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Nothing is released yet; the history before this file is in the git log.

## [Unreleased]

### Added

- `OWReport::ThreadsOf(narratives)` in the core (`OWReport.h`), the report profile's rule 9 (schema-dist serial
  120): the reports among the Narratives, as served and in that order, grouped by `parent_narrative` as written.
  `OWReportThreads` holds `parents` (each report's parent: `none`, `report`, `board`, `missing`, `other` or `self`),
  `groups` (`{parent, kind, reports}`, a report's thread or a board entry's talks, the reports in the message
  profile's `created_at` order through `OWMessage::InCreatedOrder`) and `lints` (`chain`, `self`); `ToJson()` is the
  fixture's shape. A report and a board entry are the room profile's pairs with the mark, read by
  `OWLens::IsRoomElement` (`OWReport::IsReport`, `IsBoardEntry`). A parent is never trimmed before the lookup (only
  an empty or White_Space-only one reads as none), a parent that is no string reads as none, and of two elements with
  one id the first is read; the npm SDK 4.159.0 and the Python package 0.120.0 read these the same. Held to the
  profile's fixture (`profiles/report.fixture.json`, 20 thread cases, each its own test) and to the packages' corner
  vectors. Core only: no Blueprint face yet.
- `OWMapSpace::LayoutOf(map, maps, pins)`, the map-space profile's layout reading (rules 8 to 10, serial 110): one
  Map's `OWMapLayout`, whose `ToJson()` is `{layout, ground, hand}`. `layout` is the Map's `x_layout.method` when
  `x_layout` is an object whose `method` is a non-empty string (`"hand"` a board, `OWMapSpace::Board`; a method no page
  names answers itself), else none. `ground`, only on a layout map, is the id in `x_layout.settings.ground.map` when it
  names a Map in `maps` other than this one, else none; its size is not returned. `hand` is the ids, by code point, of
  this Map's Pins a new lay-out keeps: every Pin on a board, the Pins whose `x_hand` is the JSON `true` on any other
  layout map (`1`, `1.0` and `"true"` are not), none on a plain Map. `pins` may hold every Pin the caller has: the
  reader keeps those whose `map` names this Map, reads rows as `MapSpaceOf` does (a row without a non-empty string id
  is no element, a Map row without one no layout map; the last row served for an id) and counts a Pin whose element
  it cannot see. A caller's mistake (a `map` that is not an object, `maps` or `pins` not a list) is named in `error`
  and reads nothing. Parity with the TypeScript SDK 4.142.0's `layoutOf` and the Python package 0.110.0's
  `layout_of`: every one of the fixture's 21 `layout_cases`, whole answers in key order, and the two packages' vectors
  beyond the fixture. `MapSpaceOf` is unchanged. Core only: there is no Blueprint door.
- `OWTools` repinned at the tools list's `aa00969` (tools.json sha256 `2140d2b9…`): one new row, the OnlyWorlds mapper
  (`mapper`, listed, client `mapper`), with a world door and an element door that need an account, open a public
  world with none (`opens_public`), skip an element in map space (`element_skips: ["map_space"]`) and open every type
  (`element_types: null`). So the Open with menu offers it, last, on a world's menu and on every element's, a Map, a
  Pin or a Marker held as served excepted (a Zone is content); the public's row is live, as Atlas's is. Seven rows
  baked where there were six; every menu of a world or an element outside map space has one row more.
- `OWTools` repinned at the tools list's `53a05e4` (tools.json sha256 `198f62a2…`), the list the TypeScript SDK's and
  the Python package's pins name: one new row, Windrose (`windrose`, listed), with no `open`, so the Open with menu
  offers it nowhere. No other row's door changed (Little Lens's `element_skips` is only re-wrapped in the list). Six
  rows baked where there were five.
- `OWTools` repinned at the tools list's `0a7539c` (tools.json sha256 `f1fac743…`), the list the TypeScript SDK
  4.158.1 bakes and the Python package 0.120.0 pins: one new row, Subs (`subs`, listed), whose only door is an
  element's, for a Character (`element_types: ["character"]`, needs an account), so the Open with menu offers it
  beside a Character and on no world. No other row changed.
- `OWTools` repinned at the tools list's `9452529` (tools.json sha256 `8905defb…`), the list the TypeScript SDK
  4.133.1 bakes: Atlas's row carries `opens_public: true` (its `needs` stays `account`), so a world's public page
  offers Atlas live. The generator bakes the key only on a row that carries it.
- A tools-list row can say it opens a public world with no account: `OWToolOpen::opens_public`, read from the row's
  `open.opens_public`. On a world's public page a row with `needs: account` and `opens_public: true` is live instead
  of "Needs an account"; anywhere else it reads as its `needs` says. Absent reads as false. A value that is not true or
  false, or true beside a `needs` other than `account`, is refused by name. The same rule as the TypeScript SDK
  4.133.0; the shared openers fixture at 110 cases.
- A world drawn by the packaged game. The plugin's frame scene moves out of the frame test into the runtime,
  `OWFrame` (`Public/OWFrame.h`, C++): `RootMap` (the Map with no `parent_map`, its Pins with an x and a y),
  `MapImage` (the first `.png` under `media/<map id>/base-image/` among the world's files, a folder's or an
  `.owworld` file's, as a transient texture), `NewFrameWorld` and `EndFrameWorld`, `LayOut` (the plane and one sphere
  per pin, with the caller's materials), `PlaceCamera`, `MeshesWaiting`, `Shoot` and `SavePng`.
  `OnlyWorlds.Frame.MoppetopiaRootMap` builds its frame through them and asserts what it asserted before. The host's
  game takes `-OWFrame=<a .png path>` beside `-OWWorld`: after its readings it lays the root Map out with a material
  cooked into the pak, waits for its meshes' scene proxies (a cooked game makes one only once the pipeline states have
  precached), shoots, writes the PNG, and finds each marker again in its pixels (exit 3 when one is not there). A new
  exit 5, with nothing written, when it cannot draw: no PNG path, no renderer (`-nullrhi`), no root Map with a size,
  the material or the engine's meshes not in the pak, a write that fails. The material, `/Game/OW/M_OWFrame` (unlit,
  `MapImage` times `Tint`), is written by a new commandlet in the host's editor module (`-run=OWMakeFrame`);
  `Host/Config/DefaultGame.ini` also cooks `/Engine/BasicShapes`, which the frame loads by path.
- A struct's JSON merged into its element as read, so an upsert keeps what a struct cannot hold (schema-dist's
  `extension-passthrough`: extensions merged back on write, never dropped). `UOWElementJson::MergeElementJson(
  ElementJson, StructJson, Merged, Reason)`, pure, hand-written, one function for every type
  (`Public/OWElementJsonLibrary.h`; the merge itself `OWModelWrite::Merge`): the element's JSON as read
  (`UOWWorld::GetElementJson`, or a read node's) with each key of a `Get<Type>Json` door's JSON laid over it in place,
  and every other key kept in its place and order (extensions, a newer schema's keys, `type`, `world`, the server's
  stamps, which the write nodes strip or keel drops). An unset over a set value lands; over an unset or absent one the
  read's form is kept, and an integer of the same value keeps the read's spelling, so an unchanged struct merges back
  to the element as read. A key the read lacks and the struct sets is appended. The read's id when the struct's is
  `""`; two different ids, an id that is not a string, and text that is not a JSON object on either side are `false`
  with a `Reason` naming the side. Each door's tooltip now names the merge. Held by `OnlyWorlds.Models.Merge.RoundTrip`
  (every element of Sikelia and Moppetopia), `.Changes`, `.Ids`, `.Refusals`, `.Upsert` (the body sent carries the
  extensions and none of the read-only keys) and `.Function`.
- A generated struct back to its element's JSON, so a graph can write what it read. `OWToJson(const FOW<Type>&)`
  per type (`OWModelsJson.h`, beside `OWFromJson`), for C++, and on Blueprints `UOWElements::Get<Type>Json(<Type>)`,
  pure, the text a write node takes (`GetCharacterJson`, ...: 22 new functions, no new struct). Every schema field is
  written in the schema's order, an unset one as keel serves it (a string `""`, an integer without its `bHas` and a
  link `null`, a link list `[]`, the Pin's pair `""` and `null`); never `world`, `type` or the stamps; the id left out
  when it is `""`. A struct holds no extension key: an `UpsertElement` of its JSON clears the element's extensions,
  a `PatchElement` keeps them, and each door's tooltip says so. Generated by `codegen/generate_models.py`, on new
  helpers beside the reading ones (`Private/OWModelWrite.h`); the generator now refuses a member named as its own
  locals. Held by `OnlyWorlds.Models.Write.RoundTrip`, `.Unset`, `.Doors` and `.Node`.
- A Blueprint graph saved as an asset, cooked into the pak and run by the packaged game. The host gains an
  editor-only module, `OWHostEditor`, whose commandlet (`-run=OWMakeGraph`) writes `/Game/OW/BP_OWBootGraph`
  (`ReadWorld(Path, Id) -> ElementCount, ElementName, Error` through `UOWLibrary::OpenWorldFolder` and the opened
  world's `GetElementCount` and `GetElementName`) and `/Game/OW/BP_OWBootGraphWrong` (the world's name wired into
  `ElementName`, a graph that compiles and answers wrong). `Host/Config/DefaultGame.ini` cooks `/Game/OW` although
  no map references it; the folder is generated and ignored. The game's `-OWGraph` (bare, or `=<a soft class path>`)
  calls the graph after its own readings and compares the two: exit 3 when they differ, a new exit 4 when the class or
  its `ReadWorld` is not there.
- The API client's element writes on Blueprints, on the read nodes' base: `UOWClientWrite` (`CreateElement`,
  `UpsertElement`, `PatchElement`, `DeleteElement`, `EditElementLinks`) and `UOWClientBulk` (`BulkWrite`), each an
  async node with `OnSuccess` and `OnFailure`, the element in and out as JSON text, `If-Match` as `bHasIfMatch` and
  `IfMatch`, `IdempotencyKey` on a create and a bulk. A bulk answers an `FOWBulkResult` (`bErrors`, `Items` of
  `FOWBulkSlot`, `bWasReplay`, `Failed`, `SucceededCount`, `bAllSucceeded`); its items are `FOWBulkItem`s.
  `FOWClientConfig` gains `Client` (X-OW-Client, `""` for `unreal-sdk`) and `bAgent` (X-OW-Agent); `FOWApiError`
  gains `bIsProposed` and `Proposal` (a 202 proposal, as the core reports it: an error of status 202) and `Current`
  (a 412's element now). The core's refusals reach `OnFailure` with nothing sent; so does element text that is not
  JSON, the node's own. Held by `OnlyWorlds.Client.WriteNodes.*` through the read nodes' fake transport, now shared
  (`Private/Tests/OWFakeTransport.h`). The plugin now has 53 structs.
- The element writes in the core's API client: `OWClient::Create`, `Upsert`, `Patch`, `Delete`, `EditLinks` and
  `Bulk`, ported from the Unity SDK's `OWClient` and held to keel's wire document. Each body is a copy cleaned by
  `OWClient::Sanitize` (`ReadOnlyFields` left out, a null scalar string sent as `""`, extension keys untouched) and
  written compact; a known-of stub (`IsKnownOf`), a body that is not an object, a negative `if_match`, an empty bulk
  or a value that is not one path segment is refused with nothing sent, and so is any write from an account token
  or a keyless client. Every write sends the PIN and `X-OW-Client` (`OWClientConfig::client`, keel's
  `[a-z0-9-]{1,32}`, `unreal-sdk` by default), `X-OW-Agent` on a client declared one (`OWClientConfig::agent`),
  `If-Match` on a patch, a delete and a link edit (`OWWriteOptions`), `Idempotency-Key` on a create and a bulk.
  `OWId` mints the ids an element without one gets: a UUIDv7, or under an idempotency key the UUIDv5 the Python
  package, the Unity SDK and the TypeScript SDK derive, so a keyed retry sends the same body. A bulk answers an
  `OWBulkResult` (`errors`, `items` of `OWBulkSlot`, `was_replay`; `Failed`, `SucceededCount`, `AllSucceeded`). New
  on `OWApiError`: `proposal` and `IsProposed` (a 202 on an upsert, a patch or a link edit, kind `Api`),
  `IsPreconditionFailed` with `Current()`, `IsIdConflict`, `IsIdempotencyConflict`, `IsKnownOfNotWritable`. The
  wire tests now record every write on every world key and an agent seat, and hold each body key to what keel
  declares on that operation (a bulk item's element to its type's create). On Blueprints: the entry above.
- The lens, its graph and the charter on Blueprints, over an opened world read as the core reads a snapshot (every
  element as served, world.json's `time_current` as its now, the hidden mark on each body). `UOWLens`: `GetLenses`,
  `IsLens`, `IsTemplate`, `LensesFor` (the membership's choice, else the Character's `x_lens_uses`), `IsHidden`,
  `IsProposed`, `IsRoomElement`, `IsReadingAid`, `DateOf`, and `Resolve` (a lens by id) and `ResolveJson` (a lens body
  as JSON text) for an `FOWLensReader` (`EOWLensRung`, `Character`, a guest's `Whole` and `KnownOf`, `bOwnerCounts`)
  and `FOWLensOptions` (`AsOf`, `Clock`, `MembersJson`), answering an `FOWLensReading` (`Kept`, `KnownOf`, `Rows` as
  JSON text, `Cut` of `FOWLensCut`, `NotesIgnored`, `Mail` of `FOWLensMail`, `Unreadable`). `UOWGraph`: `Read` (the
  core's `WorldGraph`; `FOWGraphOptions`: a lens by id or body, its reading, the five flags) answering an
  `FOWWorldGraph` (`FOWGraphNode`, `FOWGraphEdge`, `EdgesCut`, `Cut`, `Unreadable`), a flag from the wrong rung refused
  by its word; `DegreeOf` and `OutDegreeOf` (`FOWGraphDegree`). `UOWCharter`: `CharterOf` and `DomainsOf` over the world
  (`bOpen` for a world open to anyone), `ReadingAidsOf` over a front door's packet as JSON text (the `ReadMe` node's
  answer), `IsCharterElement`, `ReadingAidOf`, answering an `FOWReadingAids` (`FOWCharterLaw`, `FOWCharterGuide`,
  `FOWCharterDomain`, `FOWCharterHub`, and a served strand as JSON text). The names are the Unity SDK's, the behaviour
  the core's. Held by automation tests on worlds built in memory, every case of the lens profile's fixture (201) and
  of the charter profile's (27) through the libraries, every function through reflection as a graph calls it, and
  Sikelia's graph against the core's. The plugin now has 50 structs (`OnlyWorlds.Graph.Pins` counts them).
- The API client's read calls on Blueprints: an async node per call of `OWClient`, each with `OnSuccess` and
  `OnFailure`. `UOWClientRead` (`ReadHealth`, `ReadWorld`, `ReadMe`, `ReadMembers`, `ReadElement`: the body as JSON text),
  `UOWClientList` (`ListElements`: a page's rows as JSON text, `bHasMore`, `NextCursor`), `UOWClientListAll`
  (`ListAllElements`: every page, or no half list) and `UOWClientChanges` (`ReadChanges`, `ReadChangesHead`: an
  `FOWChangesPage` of `FOWChange` rows). `ReadMembers` answers the JSON array `UOWMessages` takes as `MembersJson`. A
  node is configured by an `FOWClientConfig` (key, PIN, world, base URL, page size) and fails with an `FOWApiError`
  (`EOWApiErrorKind`, the status, keel's envelope, `Retry-After`, the resync's reason, the printed line); the list's
  options are an `FOWListOptions`. Each node builds its own client on `FOWHttpTransport` and answers on the game
  thread; the core's refusals answer inside the activation with nothing sent. A node stays in the root set until it
  answered, once; `SetTransport` (C++ only) gives it another transport before it is activated. Held by
  `OnlyWorlds.Client.Nodes.*` through a fake transport (no network): every node on seven credentials, the options,
  the answers, the errors and refusals, one answer only, a garbage collection in flight, a node activated through
  reflection with its members read by `UOWMessages`, and the editor's pins. The plugin now has 35 structs.
- The message profile on Blueprints: `UOWMessages`, over an opened world, each element asked by its id as the API
  serves it (a body that names no type takes its folder's). `GetMessages` and `GetNotes` (ids in `created_at` order),
  `IsMessage`, `IsNote`, `RecipientsOf`, `IsAddressedTo`, `IsToEveryone`, `HasUnseenRecipients` (`EOWUnseenRecipients`:
  `Yes`, `No`, `Unknown` for a keyless reader), `IsSentBy`, `ThreadOf`; and against the members list as JSON text (what
  `OWClient::Members` answers; `""` for no list) `SenderStatus` (`FOWSender`: an `EOWSenderStatus`, `Claimed`,
  `WrittenBy`), `EditedBy` (`FOWEdited`), `ReadMarkOf`, `UnreadFor` and `NoteOf`. A call that reads the members list or
  a read mark refuses as `false` and the reason (no world, no such element, members text that does not parse); every
  other call reads an id the world does not hold as the profile's no. The names are the Unity SDK's `OWMessage`'s, the
  behaviour the core's. Held by automation tests that call every function through reflection as a graph does: a world
  built in memory, the profile's 88 fixture cases and 76 of the 79 readings beside it, and every element of Sikelia.
  The plugin now has 30 structs (`OnlyWorlds.Graph.Pins` counts them).
- `OWMapSpace::MapSpaceOf(elements)`, the map-space profile's reader (schema-dist serial 105, `profiles/map-space.md`):
  over the elements as served, `{maps, zones, pins, out}` in the Map's pixels, z an elevation and never a height. A
  Map's frame (`width` and `height` measured from its own points, marked `extent: "points"`, when not written whole;
  `depth`; `z_max`; `scale` from `x_metres_per_pixel` when positive), a Zone's outline per Map from its Markers by whole
  `order` (a Marker without one after every ordered one, ties by id; `shape` `polygon` unless `line`), a Pin's point
  with its element as both wire keys or neither, and what is left out counted once by reason (`no_point`,
  `no_markers`, `dangling`, `no_map`). Whole numbers by value (`2.0` is 2), ids by code point. Parity with the
  TypeScript SDK 4.129.0's `mapSpace` and the Python package 0.104.0's `map_space` at serial 105 (serial 106 below);
  held to the profile's fixture, 34 cases since serial 106. Core only: there is no Blueprint door.
- `OWGraphOptions::reading_aids`, a fifth flag on `OWGraph::WorldGraph` (schema-dist serial 104, the lens profile's
  "Out by default, a flag brings each back"): the reading aids, the charter, its guide and its domains (by the pair and
  the mark `x_charter_of`), come back into an unlensed graph, each node marked `reading_aid`; without it they stay out,
  counted `reading_aid`, as before. Any signed reader may ask it; a public reading (the owner reading as the public
  too) is refused by name, last in the refusal order `hidden`, `proposed`, `rooms`, `map_space`, `reading_aids`. A
  flag still never widens a lens, and a reading aid a lens keeps now carries the `reading_aid` mark, as a room element a
  lens keeps carries `room`; a node's marks run `room`, `hidden`, `proposed`, `reading_aid`. `OWGraph::Flags()` lists
  five. Parity with the TypeScript SDK's `readingAids` and the Python package's `reading_aids`. Core only: there is no
  Blueprint graph door.
- `OWOpeners::OpenersFor(tools, kind, address, surface, OWOpenersOptions{&body})` and `OpenersForJson(..., &options)`:
  no row for a tool that cannot open the element. The tools list's `open.element_skips` (`OWToolOpen::element_skips`)
  says what a tool's element door does not open, in the lens profile's graph section's words
  (`OWOpeners::ElementMarks()`: `room`, `hidden`, `proposed`, `map_space`, a Map, Pin or Marker, a Zone being content,
  and `reading_aid`). Given the element as served, a row whose door skips a mark it carries is left out, each mark read
  by the reader `OWGraph::WorldGraph` uses (`OWLens::IsRoomElement`, `OWLens::IsHidden`, new and now the lens view's
  own reader, `OWLens::IsProposed`, the type folded among `OWGraph::MapSpaceTypes()`, new, `OWLens::IsReadingAid`;
  `OWOpeners::MarksOf`); a test holds the menu and the graph equal on every element of the lens fixture's worlds.
  Without the element every row is there, as before (the options default to none, so every caller compiles as it
  did). Refused by name: options that are not an object, an unknown options key, an element for a world's menu or
  naming another id than the address, a skip word outside the list's, and skips on a row with no element door. Parity
  with the TypeScript SDK 4.126.0 and the Python package 0.101.0, their test vector ported: the shared openers
  fixture at 90 cases, the same bytes. `OWTools::All()` repinned at the tools list's `6da4d55` (Little Lens skips
  room, hidden, proposed and map space; Atlas and Lantern none); `generate_tools.py` bakes a list of words. On
  Blueprints, `UOWOpeners::OpenersFor` on an opened world passes the element as served (`UOWOpeners::ServedElement`:
  its body, with the folder's type when the body declares none), so a Map, Pin or Marker of Sikelia has no Little
  Lens row; held on all 20,570 of its elements, as a member and as the public, against the core. `OpenersForAddress`
  has no element and gives every row; the fixture's 22 cases that carry one are named as not carried there.
- A tool that opens one kind of element is offered only on that kind. The tools list's `open.element_types`
  (`OWToolOpen::element_types`, absent for the list's `null`) is the element types its element door opens, as type
  words (`character`); on an element's menu, `OWOpeners::OpenersFor` and `OpenersForJson` leave out a row whose list
  does not hold the address's type word, world door or not: the tool opens other kinds, so its world is not offered
  from this element either. The type is read from the address, so no element body is needed; a world's menu reads no
  types. Refused by name, as a row that is not one: a word outside the 22 (`OWAddress::TypeWords`, an uppercase one
  included), an empty list, a repeat, a list that is not one of strings, and `element_types` on a row with no element
  door. A row without the key reads as before. On Blueprints, `UOWOpeners::OpenersFor` and `OpenersForAddress` both
  leave the row out (each passes the address's type). Parity with the TypeScript SDK 4.132.0 and the Python package
  0.106.0: the shared openers fixture at 100 cases, the same bytes.

### Fixed

- `OWFrame::RootMap` (the frame test's scene and the host's `-OWFrame`) never takes a layout map as the root: among
  the Maps with no `parent_map` it skips any whose `x_layout` is an object with a non-empty string `method` (the
  map-space profile's rule 8, read through `OWMapSpace::LayoutOf`). A parentless layout map read after the ground
  (its file name sorting later, as `sikelia-layout--…` after `sikelia--…`) was taken as the map to draw. A world whose
  only Maps are layout maps has no root Map. Held by `OnlyWorlds.Frame.RootMapSkipsLayoutMaps`, which needs no renderer.
- `OWFrame::RootMap` puts a Pin on the root Map only when its `map` is the Map's id as written. `FString`'s `==`
  ignores case, so a Pin whose `map` spelled the Map's id in upper case was drawn on it, where the core's map-space
  reader on the same world compares exactly. Held by `OnlyWorlds.Frame.RootMapPinsByTheMapsIdAsWritten`. The Map's
  image is still found under `media/<map id>/base-image/` without case: that is a path, and a world's paths are one
  name whatever their case.

### Changed

- **One time grammar: a record's `played_at` is read by the message profile's grammar.** `OWRecord::MarkOf` and
  `OWRecord::InstantOf` now read `played_at` through `OWMessage::TimeKey`, the grammar the message, lens, report and
  charter readers already order by, in place of a grammar of their own. A lowercase `t` or `z` (`2026-11-01t00:30:00z`),
  an offset (`2026-11-01T01:30:00+01:00`, read as the instant it names) and a time with no offset (read as UTC) are now
  marks; a time without its seconds (`2026-11-01T00:30Z`) is no longer one. Year `0000` reads, as the message grammar
  reads it (before the first instant of 0001, so `InstantOf` is negative there). `RecordsOf` still orders newest first
  by the instant to the millisecond, a fraction truncated past three digits, a tie by the Event's id. A game that wrote
  `played_at` without seconds writes them now: such a mark is no longer read. The record fixture at dist.123 holds
  the grammar (the pin below); four of its cases at dist.122 held the old one.
- **One clock per lens: an element's clock is the newest of its three readable stamps.** `OWLens::Resolve` reads one
  clock per element, the newest of its `updated_at`, `server_updated_at` and `local_updated_at` that read as a time
  (a folder's element carries the last two), decided element by element; a value that cannot be read is ignored.
  Rule 5's order past the anchors (the schema's type order, newest first, then by id) and `changed_within` both read
  it. Rule 5 read `updated_at` alone before, so one reading could cut an element `stale` by a folder stamp and rank
  it by an `updated_at` the cut had not used, and an element stamped only by a folder sorted as undated.
  `changed_within` read a folder element's newer folder stamp before, else `updated_at`; it now moves where
  `updated_at` is newer than both folder stamps (a wire stamp newer than a local edit wins) or a folder stamp cannot
  be read. The budget keeps the newest, and an element edited locally yesterday is newer than one the server stamped
  last week. An element with no readable clock still comes last of its type and is never cut `stale`; on the wire,
  where an element carries `updated_at` alone, nothing moves.
- **The read mark's tie goes to the higher id.** `OWMessage::ReadMarkOf` reads the message profile's sentence: a lone
  mark is the mark. Among several, the newest readable `updated_at` wins; one that cannot be read never beats one that
  can; among equal instants, or when none can be read, the higher id (code point, as written) is the mark. Before, the
  first in the list won both ties. An instant written two ways (`10:00:00Z` and `11:00:00+01:00`) is the same instant,
  so it ties too. The answer no longer depends on the order the server lists the Relations in, and matches the
  Charter's newest pick, which already broke its ties by the higher id. The three readings beside the message fixture
  that held the old ties (`tests/data/message-beside-the-fixture.json`: two marks written at the same time, two equal
  once the fraction is cut at its sixth digit, and two that cannot be read) now want the higher id.
- Schema pin to `v0.34.0-dist.123` (canonical `00.34.00` unchanged; no schema file or `schema.json` moved). The
  record, lens and message pages rule the three moves above: one time grammar, one clock per lens, and the read
  mark's ties. The fixtures gain cases: `profiles/record.fixture.json` eight, three of dist.122's gone with the old
  grammar (46; of the four dist.122 cases that held the old grammar, three read as wanted under their names and
  `no-mark-played-offset` is gone, its Event now written with no colon in the offset), `profiles/lens.fixture.json`
  eleven `resolve` and one `graph` (233), `profiles/message.fixture.json` seven `mark` (100),
  `profiles/strand.fixture.json` one with its world (27, 24 worlds: a mark naming its story in upper case is no
  strand); every one of these reads as wanted here. The address page rules an address found in prose (where it ends,
  and the run after `/w/` read whole) and `profiles/address.fixture.json` gains fifty-seven `prose` cases (197): they
  are a finder's, and this SDK has none (`OWAddress::Parse` reads one string whole), so the tests check the half `Parse` owns (where a case names an address, the line holds it at the index and `Parse`
  reads it whole as the case's reading) and then skip each case by name, counted.
  The checks fixture gains two rules and two cases on `one_of` over a list; this SDK has no checks reader. The
  core's and the plugin's fixture tests moved their sha256 and counts. The generated files changed in their stamp
  lines only.
- **A Charter's skill ids match an Object's own id as written.** `OWCharter::CharterOf` still finds an id in the
  Charter's text in either case and takes it in lowercase, but now matches it against the ids of the Objects the
  reader sees as they are written, where it lowercased those too. So an Object whose own id is held in upper case is
  not the Object a lowercase id in the text names, and is no longer kept; an id named in upper case in the text still
  names the Object held in lowercase, and a kept skill is always the Object's own id. This follows the address
  profile's rule for the same id: an id is read in either case only at an address and in a text, and what it is then
  matched against is read as written, as every other reader of the core already reads ids.
- **An element held in upper case has a menu of no rows, not a refusal.** `OWOpeners::OpenersFor` and
  `OpenersForJson` given an element whose own id differs from the address's only by case (held as `0199C3A1-…`, the
  address naming `0199c3a1-…`) answer ok with no rows: no address can name such an element, so no door opens it, and
  that is not a caller's mistake (a surface still draws its mark). An id that differs past case is still refused
  ("its id differs"), and the empty answer comes only after every other check, so a tools row that is not one is
  refused for such an element too. On Blueprints, `UOWOpeners::OpenersFor` on a world element held in upper case
  answers true with no rows. Parity with the TypeScript SDK 4.161.0 and the Python package 0.122.0: the shared
  openers fixture is re-vendored (111 cases, sha256 `8af6f6da…`), its `skips-id-case-held-upper` wanting no rows.
- **The openers' check reads the given element's own id as written.** `OWOpeners::OpenersFor` with an
  `OWOpenersOptions` element still reads the address's id in either case and takes it in lowercase, but now compares
  the element's own `id` to it as written, where it lowercased that too. An element held in upper case is not the
  element the address names, whatever the address's case, and is refused ("its id differs"); an element held in
  lowercase is the element of an address in either case. The same rule for the same id as the Charter's skill ids
  above. The shared openers fixture is re-vendored at 111 cases (sha256 `ac14435b…`): its old id-case case is now
  two, an element held in upper case under a lowercase address (refused) and a lowercase element under an upper-case
  address (the rows); the plugin's Blueprint path carries neither, as it carries no element as served.

- **`OWBulkSlot::id` is `std::optional<std::string>`** (breaking for C++ callers): a string, or `nullopt` when the slot
  answers none (a failure before the item was identified), as the API document types it and the TypeScript and Unity
  SDKs read it; it read `""` before. The Blueprint `FOWBulkSlot.Id` stays an `FString`, empty for none, as its
  timestamps are.
- Folder format 0.3.22 (a draft world, one no host holds yet, conforms as any other folder; a writer adds no `world`
  to an element file and a reader gives one no meaning). **`OWWorld::WriteFolder` and `OWWorld::Pack` now refuse a
  world they would write with no id**: a `world.json` whose `id` is not a non-empty string, or a new or edited
  element with no id. `WriteFolder` returns false and writes nothing, `Pack` answers an `OWPacked` that is not ok,
  and the new `OWWorld::WriteRefusal()` says why (also `WriteFolder`'s `error`). Before, `WriteFolder` wrote such a
  `world.json` (0 bytes, or an object with no id) and dropped an id-less new element with a line in `errors` while
  reporting success, and `Pack` packed the world without it. A writer mints no id: the caller sets `world.json`'s
  (`SetInfo("id", ...)`; a draft world's id is the tool's own until a host gives one) and each element's. No writer
  here adds a `world` key; this core's writers stamp no `format_version`, so there is no version to move.

- A signed-out visitor sees the tools that need an account. `OWOpeners::OpenersFor` and `OpenersForJson` with the
  `public` audience give a row for a tool whose `open.needs` is `account`, with the new word "Needs an account"
  (`OWOpeners::NeedsAnAccount`), whatever `signed_in` and `browser` say; before, the public saw no such row. The href
  stays in the row; a menu draws it as one that opens nothing. A tool that needs the device is still not there for
  the public. The word comes after "You are here" and "Needs a connection", before every other word. On Blueprints,
  `UOWOpeners` gives the public Atlas and Lantern so worded (`FOWOpener::State`), each `Href` kept, as it gives a row
  that needs a connection. Parity with the TypeScript SDK 4.130.0 and the Python package 0.105.0: the shared openers
  fixture at 92 cases.
- Tests only: the shared openers fixture's 24 type refusals each name their kind beside the key: `unknown_key` (a key
  the object does not take, refused by name) or `bad_value` (a key it takes whose value it refuses, a missing one
  included). `tests/test_openers.cpp` tells the two apart by the core's own words ("unknown key 'k'"), so a reader
  that refuses a known key as unknown, or the other way round, now fails the case where before the name alone passed
  it. Still 92 cases, the same bytes as the TypeScript SDK's (`85ef7dd`) and the Python package's (`b1a4a45`);
  nothing shipped changes.
- `OWTools` repinned at the tools list's `f10afdc` (tools.json sha256 `ff5e7c67…`), the list the TypeScript SDK 4.131.0
  bakes and the Python package pins: one new row, Tactical Tangle (`tangle`, listed, the API door only), with no
  `open`, so the Open with menu offers it nowhere. No other row changed.
- `OWTools` repinned at the tools list's `60a590e` (tools.json sha256 `e9e8ad9d…`), the list the TypeScript SDK 4.132.1
  bakes and the Python package 0.106.1 pins: Tactical Tangle opens a world or a Character. Its row gains a world door
  and an element door (`needs` `account`, `element_types` `["character"]`, skipping rooms, hidden and proposed
  elements), so the Open with menu offers it on a world's menu and on a Character's, and on no other type. Atlas,
  Lantern and Little Lens gain `element_types: null` in the list (the list's `d0e52c4`), which bakes as before: no menu
  of theirs changes. `generate_tools.py` bakes a list of type words. On Blueprints, a Character's menu and the world's
  have five rows where they had four.
- `OWMapSpace::WholeOf` reads a number written with a fraction or an exponent as whole only up to 2^53 - 1 (a double
  holds whole numbers exactly that far; past it the parse may have rounded it), where before it read any integral
  double within int64 as whole: `9007199254740993.0` read as 9007199254740992, and `-9223372036854775809.0` as int64's
  least. An integer literal is still read exactly within int64. Parity with the TypeScript SDK 4.129.2 (a reader's
  integer range is the range it holds exactly), its test vector ported.
- Schema pin to `v0.34.0-dist.122` (canonical `00.34.00` unchanged; no schema file or `schema.json` moved). The
  address profile rules the same id: inside a world two ids are the same when they are written the same, an id is
  read in either case and taken in lowercase only at an address and in a text a page says is searched for ids, and
  what it is then matched against is read as written. The two moves above (a Charter's skill ids, the openers'
  check) are this SDK's part of it. The pages that order by a time read `created_at` and `updated_at` as the message
  profile reads one, and refusal 9 of the address page says the `lens` key is compared as written. The fixtures gain
  cases: `profiles/address.fixture.json` three (140), `profiles/charter.fixture.json` seven (38),
  `profiles/lens.fixture.json` one (221), `profiles/record.fixture.json` one (41), `profiles/report.fixture.json` two
  (29 thread cases); every case reads as wanted here. Against the code before the two moves, one answer moves: the
  charter case whose skill Object is held in upper case kept that Object (lowercased) and now keeps it not. The checks
  fixture gains a rule and two cases, `views.json` one clause on the conversation layout and the folder lint a
  warning for an id not lowercase in the form; this SDK has no reader of the three. The core's and the plugin's
  fixture tests moved their sha256 and counts. The generated files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.121` (canonical `00.34.00` unchanged; no schema file, `schema.json` or walk reader
  moved, and no answer of a reader moves). The message profile gives the one form a `created_at` is read in (a time
  with no offset is UTC, every part in its range, a year any four digits, a fraction counted to its sixth digit), and
  `profiles/message.fixture.json` gains five `unread` cases (93). The report profile's rule 9 says a parent that is no
  string is no parent, a padded one is read as written, one id given twice keeps the first, and reports that name each
  other in a ring are each grouped and linted; `profiles/report.fixture.json` gains seven thread cases (27).
  `OWMessage` and `OWReport::ThreadsOf` answer every new case as they stood; the core's and the plugin's fixture tests
  moved their counts. The generated files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.120` (canonical `00.34.00` unchanged; no schema file, `schema.json` or walk reader
  moved, and no answer of a reader moves). The report profile gains `parent_narrative` in its table, a section on a
  report that continues earlier work (it names the first report of that work, never the one before it), and rule 9,
  threads: a reader that groups reports groups them by `parent_narrative` as written, a report's thread in the
  message profile's `created_at` order, a board entry's group the reports of the talks under it. Its first fixture,
  `profiles/report.fixture.json` (20 cases), is vendored here. The views fixture's two quotes of the report page
  moved one line down with the new row (lines 22 and 24); this SDK has no reader of that fixture. The generated
  files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.119` (canonical `00.34.00` unchanged; no schema file, `schema.json` or walk reader
  moved, and no answer of a reader moves). The map-space page gains rule 11 (a layout map is no candidate when a tool
  picks the Map that stands for the world's space, and a Pin or a Marker on one is a place in that drawing, never in
  the world) and its reading H says a row's id is a non-empty string, as `OWMapSpace` already read it. The fixture
  gains one map-space case (a layout map read as any Map, 35 in all) and two layout cases (a layout Map and its
  ground, neither with a `parent_map`, 23 in all), each answered by `MapSpaceOf` and `LayoutOf` as before; the
  map-space test's pinned sha256 of the fixture moved with the new bytes, the test asserts both counts and names the
  new cases, and a new test holds a Map row whose `id` is `""` as no layout map. The distribution's folder lint knows
  folder format 0.3.22 and gains no check. The generated files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.118` (canonical `00.34.00` unchanged; no schema file or walk moved). The views table's
  sentence rule and `conversation` layout each gain a closing clause (a tool that shows names only need show no
  sentence; a tool that shows one element and no thread draws it as a document), and the checks fixture gains two
  room-mark rules with eight cases; this core vendors both and has no views or checks reader, so nothing here reads
  them. The room page says a sitting's own mark holds the world's id and a sitting's winning spec is named in its
  `x_star`, and its fixture's note says the first; the pairs and cases are the same, so `OWLens` is unchanged, and the
  room test's pinned sha256 of the fixture moved with the new bytes. The generated files changed in their stamp lines
  only.
- Schema pin to `v0.34.0-dist.117` (canonical `00.34.00` unchanged; no schema file or walk moved). The views table's
  description says a tool's own words for a heading or a fold are its drawing, and its fixture gains a 46th case (a
  Pin's generic link with one key holding and the other White_Space alone holds nothing). This core vendors both and
  has no views reader yet, so nothing here reads them. The generated files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.116` (canonical `00.34.00` unchanged; no schema file, fixture or walk moved). The review
  page says who a mark's `by` names: the writer's own word, a claim, read as stored and never trimmed (White_Space
  alone, or a value that is not a string, names nobody), shown as the mark's word and in a name's place only where the
  platform resolves it. This core reads the review marks by presence and reads no mark's `by`, so no reader moved. The
  review cases follow the TypeScript SDK 4.155.0 (one row renamed, two added, 31 in all; every answer here is presence
  and unchanged). The generated files changed in their stamp lines only.
- Schema pin to `v0.34.0-dist.115` (canonical `00.34.00` unchanged, no schema file moved). A lens's `first_line` row
  now carries as `description` the first line, the description split at every carriage return and every line feed
  (no other character breaks a line), that holds something after trimming White_Space, as stored with its own spaces;
  a description with no such line, or one that is not a string, gives no `description`. Before, `OWLens::Resolve`'s
  row carried everything up to the first line break, so a description opening with one gave `""` and one of
  White_Space alone was carried. The lens fixture's ten new cases pass (220 in all); built to the profile's words and
  the fixture, since no package had published a reader of the rule at this serial. The charter page's empty mark
  (settled) and a Domain's sentence (trimmed, cut at the first line break, trimmed again) are how `OWCharter` already
  read them; the charter fixture's four new cases pass (31 in all). `views.json` is version 3 (a generic link, a Pin's
  `element`, is one field of the drawing order, read by its two keys) with a fixture of 45 cases; both vendored, still
  with no reader here. The report page's keeper report in one write and its reporter by `created_by`, the packet
  page's optional producer `version`, and the folder lint's review warnings bind readers this SDK has none of. The
  generated files moved their stamp lines only; the lens and charter tests pin the fixtures' new sha256.
- Schema pin to `v0.34.0-dist.114` (canonical `00.34.00` unchanged, no schema file moved). A lens's short rows
  (`show` `first_line` or `name`) now carry `supertype` and `subtype`, each only where the body the reader is served
  holds it as a string with something left after trimming White_Space, as stored (never trimmed): `OWLens::Resolve`'s
  rows do, and the lens fixture's nine new cases pass (210 in all). Parity with the TypeScript SDK 4.151.0. The review
  page's new rule 5 (an object holding a key is a mark, whatever its keys) is how `OWLens::IsProposed` already read
  it; npm's two vectors join the review cases. The report page's rule 8 (a `consequences` line may stand for a change
  made to many, with no address) binds a reader of report lines, which this SDK has none of. `views.json` is version
  2 (the order a page draws its fields in) with a fixture of 41 cases; both vendored, still with no reader here. The
  lens page's `lenses_from` gains `none`, which the client hands on as served. The generated files moved their stamp
  lines only; the lens and room tests pin the fixtures' new sha256.
- Schema pin to `v0.34.0-dist.113` (canonical `00.34.00` unchanged, no schema file moved). The serial adds two
  presentation files at the distribution's root, `views.json` (how a report, a charter, a message and a line in a
  room are shown: the one sentence, what leads, what folds, the layout, the marks at a page's head) and its fixture,
  `views.fixture.json`. This SDK vendors both and has no reader of them yet: a kind is drawn as its type is. The room
  page now says the order of its pairs carries no meaning (no pair or case moved; the tests still hold the fixture's
  order, which is stricter than the page), and two texts changed in words only (the report page's example, one
  checks fixture case's description). The generated files moved their stamp lines only; the room test pins the
  fixture's new sha256.
- Schema pin to `v0.34.0-dist.112` (canonical `00.34.00` unchanged, no schema file moved). A report is a room element:
  `OWLens::RoomPairs()` holds the room profile's tenth pair, `narrative/report`, third in the fixture's order, so
  `OWLens::IsRoomElement` (and the plugin's `UOWLens::IsRoomElement`) answers true for a Narrative with supertype
  `Report` that carries `x_room_of`, a lore view leaves it out and a lens's `types` may name it; without the mark it
  stays lore. The room fixture's three new cases pass (35 in all, 17 in the room) and the lens fixture's `room_pairs`
  is held to the pairs in the new order. The new report profile's key `x_report_answers` and the packet profile's new
  optional `x_packet_for` have no reader here (none in the TypeScript SDK or the Python package either);
  `LensesInUse`'s null membership is a folder world, as the lens page now says, its behaviour unchanged. The
  generated files moved their stamp lines only; the room and lens tests pin the fixtures' new sha256.
- Schema pin to `v0.34.0-dist.111` (canonical `00.34.00` unchanged, no schema file moved). Four more `layout_cases`
  (twenty-one: code point order, a repeated Pin id, a Pin row and a Map row without a string id), the page's sentence
  that a Map row without a string id is no Map, and a case that names the Map it reads by `map_index` (positions in
  its elements) when the row has no id; the map-space test reads every row a case names. `LayoutOf` needed no change.
  The generated stamps moved, and the map-space test pins the new fixture's sha256.
- Schema pin to `v0.34.0-dist.110` (canonical `00.34.00` unchanged, no schema file moved). The map-space profile
  gains its layout keys, rules 8 to 10 (`x_layout` on a Map, its ground, `x_hand` on a Pin), and a second, small
  reading of one Map, with seventeen `layout_cases` beside the fixture's `cases`, which keep their 34 entries
  unchanged. The generated stamps moved, and the map-space test pins the new fixture's sha256.
- Schema pin to `v0.34.0-dist.109` (canonical `00.34.00` unchanged). The calendar profile's refusal list now says an
  era's `year_base` is refused when it is "present and not 0 or 1", as its field table always did, and its fixture
  gains `refuse-year-base-five` (77 cases, 43 refusals). `OWCalendar` refused it already; no code change. The
  generated stamps moved, and the calendar test pins the new fixture's sha256 and counts.
- Schema pin to `v0.34.0-dist.108` (canonical `00.34.00` unchanged; descriptions only, canonical PR 20: no field, type,
  link, wire, fixture or walk change). Seventeen time-field descriptions on thirteen types say one thing, "in the
  world's time unit (World `time_basic_unit`)"; `Event.constructs` reads "concepts, systems or built entities";
  `Map.hierarchy` reads "an integer to order or group the maps that share a parent; how a tool reads it is the tool's
  own". `OWModels.h` carries those 19 descriptions in its field comments; the other generated files moved their stamp
  lines only.
- Schema pin to `v0.34.0-dist.107` (canonical `00.34.00` unchanged: no model, wire, fixture or walk change). Two
  profile pages' prose: the charter page names its `open` as the World's `public_read` (the standard's field: read
  from world.json in a folder, from the world GET over the API; the fixture's `open` is the same thing), and the
  map-space page says a reader's integer range is what it holds exactly. `OWCharter::CharterOf`'s `open` key and
  `UOWCharter::CharterOf` and `DomainsOf`'s `bOpen` say so in their doc comments; no code changed. `OWMapSpace` already
  reads that way (an integer literal exactly within int64; a fraction or an exponent whole only to 2^53 - 1). Parity
  with the TypeScript SDK 4.132.2 and the Python package 0.106.2.
- Schema pin to `v0.34.0-dist.106` (canonical `00.34.00` unchanged: no model or wire change). The map-space page
  rules readings I to K and four sentences from what the engine ports found; its fixture moves from 24 cases to 34.
- `OWMapSpace::MapSpaceOf` reads two rows of one type with one id as the last one served for Markers and Pins too, as
  it already read Maps and Zones (schema-dist serial 106, reading I), even when the last reads less: a placed Marker
  followed by an unplaced one with its id counts `no_point`, a Pin followed by one naming no served Map counts
  `no_map`. Before, every Marker and Pin row was read, as the TypeScript SDK 4.129.0 reads them (the Python package
  0.104.0 kept one Pin per id, every Marker row). Parity with the TypeScript SDK 4.129.1's `mapSpace` and the Python
  package 0.104.1's `map_space`, both at serial 106. `OWMapSpace::WholeOf` reads an integer literal past int64 as not
  whole (the page's platform range), where one just below int64's least read as that least. Unchanged, now the page's
  own words: a frame measured from placed points only, a `z` that is not whole as 0, a Zone with no placed Marker
  counted `no_markers`, a Pin with neither a served Map nor a point counted once as `no_map` (J), and `type` and
  `shape` read case-exactly (K).
- Schema pin to `v0.34.0-dist.105` (canonical `00.34.00` unchanged: no model or wire change). The new map-space
  profile and its fixture (`OWMapSpace`, above), and the landing page's door sentence, words only: any door may serve
  the answer, and a key beside the five is the door's own. `OWLanding::LandingOf` is unchanged; its 33 cases pass.
- Schema pin to `v0.34.0-dist.104` (canonical `00.34.00` unchanged: no model or wire change). The lens profile gains
  the graph's `reading_aids` flag (above; the lens fixture at 201 cases, 54 of them graph cases), and the landing
  profile rules `orphan` and `near_orphan` by its own degree (a link to itself once, each place in a list once) and
  reads a `heads` too large to hold as the largest (its fixture at 33 cases). `OWLanding::LandingOf` already read both
  ways; its three new cases pass with no reader change.
- Parity with the TypeScript SDK 4.123.2: `OWOpeners::OpenersFor` and `OpenersForJson` refuse a tools row whose
  `open.world` or `open.element` link does not begin with its scheme, or whose scheme (in any case) is `javascript:`,
  `data:`, `vbscript:`, `file:`, `blob:` or `about:`, a link that opens no app. The refusal is the reader's own for a
  malformed row: no rows, and a message naming the row and the key. A missing or null link is still none. The shared
  openers fixture is at 61 cases.
- Parity with the TypeScript SDK 4.123.3: `OWClient`'s `Get`, `List` and `ListAll` refuse, before sending anything, a
  value that is not one path segment. The type must be a lower-case word (letters, digits, `_`); the id must be
  letters, digits, `_`, `.` and `-`, and must not start with a dot or a dash. The API decodes an escaped slash before
  it routes, so escaping alone could not keep an id like `abc/hidden` on its own route. The refusal is the client's
  own for a bad argument, an `OWApiError` of kind `Refused` naming the value (`id "abc/hidden" is not one path
  segment. Nothing was sent.`). A value that passes is still escaped.
- The shared openers fixture is re-vendored at 65 cases, in parity with the TypeScript SDK: one refused case for each
  scheme that opens no app (`vbscript:`, `file:`, `blob:`, `about:` beside `javascript:` and `data:`), so a reader that
  forgot one of the six no longer passes. No reader changed; the six were already refused.
- `OWTools::All()` carries Little Lens, in parity with the TypeScript SDK 4.123.5. The tools list is repinned at `a1573ac`
  (from `86208c8`): Little Lens, a world as a web of its elements and links, read only, is a listed row, so
  `OWOpeners::OpenersFor` offers it in the Open with menu for a world and for an element, to anyone (it needs no account).
  No type or function changed.

### Added

- `OWLanding::LandingOf(graph, options)` in the core (`OWLanding.h`): the landing profile's reader (schema-dist
  `profiles/landing.md`, serial 103), a world's first picture at a door, read from the graph `OWGraph::WorldGraph`
  answers for the visitor, so it sees exactly what that reader may see. The answer (`OWWorldLanding`, `ToJson()` the
  profile's `{mode, size, clusters, unclustered, gaps}`): the most-linked Characters as heads, each with the nodes
  within `hops` steps of it (the nearest head wins, a tie going to the higher-ranked one), every node no head reaches,
  and the gaps (an orphan, a near-orphan, a Character with no place, a Location nobody is in), with the mode (`gaps`
  under `size` nodes, `graph` at it). Options (`OWLandingOptions`, or as JSON the way the packages take them): `heads`
  (5), `hops` (1), `size` (24), each a whole number of 1 or more; an unknown key or another value is refused by name.
  Counted in integers, ids in code point order. Parity with the TypeScript SDK's `landingOf` and the Python package's
  `landing_of`; held to the profile's 30-case fixture by its sha256. Not yet in the plugin's Blueprint library.
- `OWRecord::RecordsOf(character, elements)` in the core (`OWRecord.h`): the record profile's reader (schema-dist
  `profiles/record.md`, serial 102), a Character's records in a world, each a game's run kept as an Event marked by
  `x_record`, read over the elements as served: the run's tool, `played_at`, version, seed and outcome, the
  Character's seat, and their stories of it (`protagonist`, `narrator` or `both`), newest first (`OWCharacterRecord`,
  `ToJson()` the profile's entry). Who took part is the Event's `characters` alone, never `seats`; a run or story still
  proposed is not read; a mark on anything but an Event is no record. Beside it `OWRecord::MarkOf(element)` (the mark
  read, or none for a broken writer: a tool or outcome out of form, a seed or version that is not a string, seats that
  are not an object of strings) and `OWRecord::InstantOf(played_at)`: `played_at` is checked by its parts on the
  Gregorian calendar, never by a date parser, and ordered as an instant to the millisecond, a fraction truncated past
  three digits; ties and stories by id in code point order. Held to the profile's 32-case fixture by its sha256. Not
  yet in the plugin's Blueprint library.
- `OWClient` in the core (`OWClient.h`): the first API client, so a game reads an onlyworlds.com world over keel's v2
  API. The read calls: `Health`, `GetWorld`, `Me` (`OWCharter::Served` reads its `reading_aids`), `Members` (the rows
  `OWMessage` takes), `List` with its typed options and single-link filters, `ListAll` (every page by `has_more`; a
  page that hands back the cursor it was sent stops the walk), `Get`, `Changes` and `ChangesHead` (the tip read,
  conditional on its ETag: a 304 hands back the last 200's tip). Elements are answered as their bodies (`OWJsonValue`),
  errors as one `OWApiError` (keel's envelope, `Retry-After`, the read helpers). The credential rules are the Unity
  SDK's: the PIN only on `GET /me` among the reads and on every request of a legacy key, an account token as a Bearer
  with `world=`, no key reading a public world, and what a client may not send refused before sending. Every value a
  call puts in a request path, the element type and the id, is sent as one escaped segment, as the TypeScript SDK
  sends it since 4.122.1: a string like `abc/hidden` never asks a different route. Requests go
  through `OWTransport` (`OWTransport.h`), one call answered once through a callback, so Unreal's asynchronous HTTP and
  a synchronous test fake both fit; a client may be destroyed with a request in flight. Held to keel's OpenAPI
  document, vendored from the Unity SDK (`tests/data/keel-wire.json`), which follows keel's (at `c1d6b0ff0cdd`: the Pin
  list takes `element`, the Pins that place that element of any type, `""` for the Pins placing nothing); `List`
  already sends any field-named filter as given, so `filter["element"] = id` needs no change here.
- `FOWHttpTransport` in the plugin (`OWHttpTransport.h`): the client's transport on Unreal's `FHttpModule`. It
  answers each request once, on the game thread, with the body's bytes as received, or with status 0 and a failure
  (no connection, the timeout, a request that would not start). Unreal's HTTP follows a redirect itself, and nothing
  above it can stop that, so a followed answer is refused as a failure: the client never reads another address's
  answer as keel's. Its automation tests: `OnlyWorlds.Client.Transport.NoConnection` (no network), and
  `OnlyWorldsLive.Client.Health`, `.Moppetopia` and `.Redirect` against production, run by name. Not yet: Blueprint
  calls for the client.
- `OWCharter::Served(answer)` in the core (`OWCharter.h`): the front door's packet as the server serves it,
  `reading_aids` in a `GET /me` answer or an agent's join answer, read as served for that key and never recomputed:
  the Charter, the Guide and the Domains as served, then the world's strand (serial 100) as `strand`, an
  `OWWorldStrand` read by `OWStrand::Served` (each slot `{id, name}` or empty, `same` as served). Six null slots read
  as an empty strand, the world having none or its story being out of the key's view; a packet with no `strand` (a
  server from before serial 100) leaves `strand` absent; an answer with no packet (a player's join answer) reads
  none. `ToJson` writes the packet in the charter profile's order, the strand fourth, and only when it was served.
  `CharterOf` still reads the three aids alone.
- `OWStrand::StrandOf(world, reader)` in the core (`OWStrand.h`): the strand profile's reader (schema-dist
  `profiles/strand.md`, serial 98), a world's strand as one reader sees it through the lens reader's own view: the
  story the World's `x_strand` names, one of its events, that event's place, the story's `protagonist`, where the hero
  is now, and whether then and now are one place by id (`OWWorldStrand`, `ToJson()` the profile's six keys). A slot
  holds only its own kind, no slot is filled from another, a list's order is never read, and a story the reader knows
  only as a stub fills its own slot alone. Only the story's id is required (serial 99): the event and the place null
  or absent read as unset, a place with no event is an empty place, and any other value there (an empty string, a
  number, an object) is no mark, as is a story that is not a string. A multi-link that is not a list lists nothing; a
  nameless slot reads an empty string; the public rung reads only a world open to anyone. Held to the profile's
  fixture by its sha256. Not yet in the plugin's Blueprint library.
- The plugin in a packaged game: the host's game target built, cooked and staged by BuildCookRun (Win64 Development),
  and run outside the editor with `-OWWorld=<folder or .owworld>`, `-nullrhi`. It opens the world through `UOWLibrary`,
  logs the element count by type, an address written and read back, a step in the world's calendar, the Open with rows
  online and offline, and exits 0 by itself; a refused or missing world logs its reason and exits 2. Run on Sikelia's
  folder (20570 elements), the conformance set's `pass-good.owworld`, Moppetopia (its calendar) and two refusals.

### Changed (breaking)

- A generated struct's integer is two members, `bool bHas<Field>` and `int64 <Field>` (`false` and `0` for null or a
  missing key), not a `TOptional<int64>`: the editor gave a `TOptional` member no pin on a Break or a Make node, so a
  graph could not read a single integer or date and a Make left them unset. C++ callers change: `C.BirthDate.IsSet()` is
  `C.bHasBirthDate`, `C.BirthDate.GetValue()` is `C.BirthDate`. Every integer of Sikelia's and Moppetopia's elements
  reads as before (the two readings written out and compared byte for byte); `codegen/generate_models.py` refuses a
  struct whose members would share a name.

### Changed

- The Open with menu's own site row says "You are here" when the surface's `tool` is `onlyworlds` (the site itself),
  where before it said "Needs a connection" offline or nothing. Parity with the TypeScript SDK 4.123.0, the shared
  openers fixture re-vendored at 57 cases.
- Schema pin to `v0.34.0-dist.103` (canonical `00.34.00`): Location gains `vehicle`, a single link to the Object the
  place is the inside of (a ship, a train, a walking city). The generated `FOWLocation` gains `Vehicle` (an `FString`,
  the Object's id), and the core's field table gains it, so the lens reader hops through it and the graph draws it as
  an edge. The serial also brings the landing profile (`profiles/landing.md`, 30 cases), degree and out-degree defined
  on the lens page (the lens fixture at 196 cases, a new `degree` kind) and seven new readings in the record profile
  (its fixture at 40 cases).
- `OWRecord::RecordsOf` reads a run or a story as proposed exactly when the review profile's `proposedMarkOf` does:
  an `x_review_proposed` object with a key. An empty mark (`{}`, `""`, `[]`) is settled, so the run and its stories
  are read, where before any value but null held them back; a value that is not an object is settled too. The record
  profile at serial 103, parity with the TypeScript SDK and the Python package. The serial's other record readings
  (`played_at`'s upper-case `T` and `Z`, ASCII digits, an optional fraction; ties and stories by code point; an Event
  or a Narrative whose id is not a string never read; a name that is not a string read as `""`) this reader already
  kept; the fixture's 40 cases pass.
- Schema pin to `v0.33.0-dist.102`: the record profile, new (`profiles/record.md` and its fixture, 32 cases): one
  run of a game kept in the world its Characters live in, an Event marked by `x_record`, a Character's records read
  from the Event's `characters` and the stories' `protagonist` or `narrator`. The strand profile's ruled line moved
  one line, no rule with it; no schema change; the generated headers move with the serial.
- Schema pin to `v0.33.0-dist.101`: the strand profile's Where it shows says what a reader draws. An empty slot as
  the next thing to make is a writer's rule; a surface that only reads may draw the filled slots alone, and still
  shows a half-built strand as the strand. No fixture, schema or reader rule moved; the generated headers move
  with the serial.
- Schema pin to schema-dist serial 100: the strand on the front door. The strand profile's rule 2 says the server's
  front door (`GET /me` and an agent's join reply) serves `reading_aids.strand`, the six slots of the strand as that
  key reads the world, six null slots when the world has none or its story is out of the key's view; the charter
  profile's rule 2 orders the aids the Charter, the Guide, the Domains, then the strand. No fixture or schema file
  moved; the generated stamp lines move with the tag.
- Schema pin to schema-dist serial 99: the strand profile's mark relaxed (only the story's id required; the event
  and the place null or absent read as unset, any other value there no mark) and its readings for a non-list
  multi-link, a nameless slot and the public rung, nine new cases (26). `OWStrand::StrandOf` already answered them so.
- Schema pin to schema-dist serial 98: the strand profile, new (`profiles/strand.md` and its fixture, 17 cases): one
  story in a world in miniature, named on the World by `x_strand`'s three ids and read through four existing links.
  No schema file moved; the generated stamp lines move with the tag, the presentation table's too (its stamp had
  stayed at serial 91, so its drift check failed; the table itself is unchanged).
- Schema pin to schema-dist serial 97: the address profile's four format cases on an empty string, one per part (an
  empty world, type, id or lens refuses by that part's own reason; only null or a missing key is absent), which
  `OWAddress` already answered so.
- Schema pin to schema-dist serial 96: two graph cases on a guest's own hidden creation (out of an unlensed graph with
  no count and no counted cut edge; a node of a lensed graph that keeps it, marked hidden, its edge to an element the
  lens never reached `outside_lens`), which `OWGraph::WorldGraph` already answered so.
- Schema pin to schema-dist serial 95: five new cases across the charter, lens and message fixtures, and `pairs.json`
  beside `presentation.json` (vendored with the rest; no reader for it here yet). `OWMessage::ReadMarkOf` takes
  `keyless`, as `IsToEveryone` does: a mark served without a credential is ignored, since the keyless public reader is
  served no `created_by` or `changed_by` on any body.
- Hidden beats `known_of`: a hidden element named in a guest's `known_of` that is not its own creation is not in that
  guest's view (`OWLens::Resolve`, `OWGraph::WorldGraph`, and the owner reading as that guest): no stub, no row, no
  node, no count and no mark; a link to it is left out of the guest's served lists, and the owner reading as the guest
  counts an edge to it `not_visible` and an anchor on it `anchor_not_visible`. A hidden id in a guest's `whole` stays
  its own creation, served whole and marked.
- Schema pin to schema-dist serial 92: a Zone leaves map space in the lens profile's graph. A Zone is content that
  gains spatial presence through its Markers, so `OWGraph::WorldGraph` carries Zones as nodes by default, for a
  public reader too; Map, Pin and Marker stay out until `map_space`.
- Schema pin to schema-dist serial 94: the charter, lens and names fixtures' twenty-four new cases (lens 186: 99
  resolve, 47 graph; names 29; charter 26). `OWLens::Resolve` reads an anchor list's empty or White_Space-only string
  as no id, so a lens holding only such strings is a filter; an anchor resolves by its field's type and the id while
  the cut counts each element once, so a kept element is never also cut; a served body carries `hidden: true` where
  the hidden mark keeps the element (the owner's body, and below the owner a guest's own creation, never a stub), and
  no `hidden` key otherwise, a stored `false` dropped. `OWCharter`'s Domain hubs leave out an element the reader does
  not see as the field's type, so a proposed Object is no hub, the owner's view included.
- Schema pin to schema-dist serial 93: the lens fixture carries a case for each of the readings below, nineteen new
  cases over a ninth world (171 in all: 86 resolve, 45 graph, 5 uses), and `profiles/lens.md` states each reading.
  The text_only keys case `keys-an-integral-float-is-refused-where-the-text-shows` is retired for
  `keys-an-integral-float-reads-whole`. Every case passes.

Readings `OWLens` first took from keel, ruled by the lens profile's owner on 2026-10-30 to the npm and Python
packages' reading (the fixture's cases for them came with serial 93):

- `show: first_line` cuts the description at its first CR or LF, and a row whose body has no description (or one that
  is not text) carries none: no `""` is written.
- Under rule 5's order an element with no `updated_at` sorts after every element of its type that has one, never
  as the reading's now; two without one by id. A budget cuts them first.
- `LensesInUse` reads each id once, in the order given, and drops `""`; the membership still wins whole.
- A whole float as an `x_lens` key value reads whole (`depth: 2.0` is 2, `budget: 7.0` is 7); only a fraction is
  refused. Serial 83's refusal of `7.0` where a reader can see the number's text is retired.
- The mail part keeps two messages with one `created_at` in the order of the world's list, as the message profile
  rules and `OWMessage::UnreadFor` reads a tie; they are no longer ordered by id first.

Two readings of `OWGraph`, ruled the same day to keel's reading:

- An edge's `index` is the target's place in the link list as keel serves that reader: a proposed id stays in every
  rung's list but the public's, so a contributor's index counts it (it is still no node).
- A node's `type` is the element's type as stored, never folded; its colour word and icon still come from the type it
  names.

### Added

- The Open with rows offline, as the npm SDK 4.118.0 and the Python package 0.96.0 give them: `OWOpenerSurface::online`
  (default true; `online` in `OpenersForJson`'s surface, anything but a boolean refused by name) and
  `FOWOpenerSurface.bOnline` on Blueprints. Offline, every row whose href is a web address (`http:` or `https:`, in any
  case) says "Needs a connection", the site's own row included, after "You are here" and before the browser, sign-in
  and phone words; a row whose link opens an app keeps its own word, and every href stays. Held to the packages' shared
  fixture, now 54 cases (sha256 `5a9ebbc8...`), and to an exact-refusal test for `online` beside it.
- Blueprint graphs built, compiled and run in the editor's automation tests (`OnlyWorlds.Graph.Pins`, `.Run`): a
  function graph of real K2 nodes over every door, compiled by the Kismet compiler with no error or warning, its
  answers the core's on every element of Sikelia and Moppetopia. The editor's answer to the open question: no
  generated struct's `TOptional<int64>` member gets a pin on a Break or a Make node (70 members). The module links
  `UnrealEd` and `BlueprintGraph` in an editor target only, for these tests.
- The address, the Open with rows and the names on Blueprints: `UOWAddresses` (`ParseAddress`, `WriteAddress`, the
  `FOWAddress` struct with a `bHas` flag per part, so an empty string refuses as that part would), `UOWOpeners`
  (`OpenersFor` an element of an opened world or the world itself, `OpenersForAddress`, over the baked tools list;
  `FOWOpenerSurface`, `FOWOpener`) and `UOWNames` (`TypeWord`). A refusal is `false` and the reason. Each function is
  held by automation tests that call it through reflection as a graph does: the address fixture's parse and format
  cases, the openers and names fixtures where a Blueprint pin can carry a case, and every element of Sikelia.
- `OWNames::TypeWord(world, type, supertype, many)`: a world's own word for a type, the names profile's `x_names`
  on the World (schema-dist `profiles/names.md`), a port of the npm SDK's `typeWord` and the Python package's
  `type_word`. The supertype's entry first, its key trimmed and folded as the room profile reads (`OWMessage::Trim`,
  `Fold`), then the type's, else absent for the standard's name; a value that is not a names map is absent
  everywhere. `version` is the number 1 in any spelling (`1.0`, `1e0`), never `true` or `"1"`; a `types` or
  `supertypes` key present and null refuses the map, as npm reads it. Held to the profile's 23-case fixture (its
  sha256 held to `MANIFEST.json`). Core only, no Blueprint function yet.
- The review profile's rule 1 held by the npm and Python packages' 24 shared cases (npm `test/review.test.mjs`
  at 4.102.2, Python `tests/test_review.py` at 0.84.1), same names and order, against `OWLens::IsProposed`: whether
  a reader leaves an element out as proposed, a doubt included. The reader is unchanged; three mutants of it (an
  empty list or object counted, a null mark counted, a mark string untrimmed) each fail the cases.
- The room profile's conformance fixture (schema-dist `profiles/room.fixture.json`, its sha256 held to
  `MANIFEST.json`) run against `OWLens::IsRoomElement`: all 32 cases, each in the room exactly when the pair and
  the mark both hold. The reader itself is unchanged; four mutants of it (the mark or the pair ignored, a mark
  string untrimmed, the pair unfolded) each fail the cases.
- `OWCharter` in the core, the charter profile's reader (schema-dist `profiles/charter.md`): `CharterOf(world, reader)`
  (the profile's `charterOf`: the packet `{charter, guide, domains}` as one reader sees the world, the Charter
  `{id, name, rules, skill}` and the Guide `{id, name}` the newest marked ones, the Domains `{id, name, sentence, hubs}`
  in the Guide's order, then by folded name and id), `DomainsOf`, `IsCharterElement` (the pair and the mark),
  `ReadingAidOf` (the pair alone), `FirstLine` and the profile's tables. It reads who sees what, and the body each
  reader is served, through `OWLens`'s own view. keel's reading aids (`reading_aids`, `charter_skill`) are the rule
  followed: the Charter is read whole as stored, so a guest who knows it only as a stub still reads its rules and
  skill, and a skill id matches an Object's whatever its case. A proposed element is in no reader's view here, as in
  the lens reader. Tested by every case of the profile's fixture (18 `charter`, 5 `element`).
- `OWGraph` in the core, the lens profile's graph section: `WorldGraph(world, reader, options)` (the profile's
  `worldGraph`: the nodes by id with their colour word and icon from `OWPresentation`, their marks and `known_of`; the
  edges by source, field and index, the index the reader is served; `edges_cut` and `cut` by reason; `unreadable`; or
  `refused` naming a flag asked from the wrong rung), `DegreeOf`, `OutDegreeOf` and the profile's tables. It reads
  who sees what, and a lens's kept and cut, through `OWLens`'s own view and resolution. keel's graph reader for the
  profile is the rule followed. Tested by the fixture's 39 graph cases (every case of the lens fixture now runs) and
  the profile's invariants over each.
- `OWPresentation` in the core: the presentation sidecar (schema-dist `presentation.json`), generated by
  `codegen/generate_presentation.py` into `Core/Private/OWPresentation.generated.cpp` (`--check` fails on drift):
  `Order()` (the colour words, `colors.order`), `FamilyOf`, `IconOf` and `PublicOf` per type. A family word not in
  `colors.order` fails loudly, naming the word: the generator writes nothing, and the generated table holds the rule as
  a `static_assert`. `OWLens::PublicFieldsOf` reads it in place of its copied table. Tested against the vendored file
  (`tests/test_presentation.cpp`) and by `codegen/test_generate_presentation.py`.
- `OWLens` in the core, the lens profile's reader: `Resolve(world, lens, reader, options)` (the profile's `resolveLens`:
  the kept ids in the profile's order, `show`'s rows, the cut counted by reason, `notes_ignored`, the mail part, or
  `unreadable` naming the key), `IsLens`, `IsTemplate`, `CheckKeys` (the legal values of `x_lens`), `LensesInUse`
  (where a seat's choice lives), `DateOf` (the profile's date table), `IsRoomElement`, `IsReadingAid`, `IsProposed`
  and the profile's tables. keel's reader (`elements/lens.py`, with the bodies `elements/serialize.py` serves each
  reader) is the rule followed; the mail rule reads through `OWMessage`. Tested by the fixture's 114 shape, keys, uses,
  resolve and invariant cases, the tables held to the
  fixture, the field table to `schema.json` and the stub's public fields to `presentation.json`.
- `OWOpeners::OpenersFor(tools, kind, address, surface)` in the core: the rows of an "Open with" menu for a world
  (`"world"`) or one of its elements (`"element"`), a port of the npm SDK's `openersFor` and the Python package's
  `openers_for`. The site's page comes first, then each listed tool with a link for that kind, in the list's order,
  each an `OWOpener` (`id`, `name`, `href`, `state`: the row's word in sentence case, or none). The address is
  `OWAddressParts` or an `OWAddressReading`; `OpenersForJson` takes the tools, the address and the surface as JSON.
  An `OWOpenersResult` answers: `rows` when `ok`; else `reason`, the address profile's refusal, or an empty `reason`
  and a `message` naming the key. The whole address is checked by the writer, so a malformed form, lens or moment
  refuses (`bad_form`, `bad_lens`, `bad_moment`), as the npm SDK 4.110.0 and the Python package 0.91.0 do. Tested
  by the packages' shared fixture (45 cases, held by its sha256). Core only: no Blueprint function yet.
- `OWTools::All()`: the OnlyWorlds tools list (github.com/OnlyWorlds/tools, `tools.json` format 1) baked in at commit
  `86208c8`, the keys `OpenersFor` reads. The list is vendored at `codegen/tools/tools.json` and pinned in
  `codegen/tools-pin.json`; `codegen/generate_tools.py` writes `Core/Private/OWTools.generated.cpp` and `--check`
  fails when the two disagree. A tool listed after a release is offered from the next one. The Runtime module
  compiles both new files with the rest of `Core/Private`, with no list to change.
