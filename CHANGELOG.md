# Changelog

Notable changes to OnlyWorlds for Unreal. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Nothing is released yet; the history before this file is in the git log.

## [Unreleased]

### Added

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

### Changed

- A signed-out visitor sees the tools that need an account. `OWOpeners::OpenersFor` and `OpenersForJson` with the
  `public` audience give a row for a tool whose `open.needs` is `account`, with the new word "Needs an account"
  (`OWOpeners::NeedsAnAccount`), whatever `signed_in` and `browser` say; before, the public saw no such row. The href
  stays in the row; a menu draws it as one that opens nothing. A tool that needs the device is still not there for
  the public. The word comes after "You are here" and "Needs a connection", before every other word. On Blueprints,
  `UOWOpeners` gives the public Atlas and Lantern so worded (`FOWOpener::State`), each `Href` kept, as it gives a row
  that needs a connection. Parity with the TypeScript SDK 4.130.0 and the Python package 0.105.0: the shared openers
  fixture at 92 cases.
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
  document, vendored from the Unity SDK (`tests/data/keel-wire.json`).
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
