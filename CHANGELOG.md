# Changelog

Notable changes to the OnlyWorlds SDK for Unreal. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Where it stands.** 0.1.0 is the first release. `onlyworlds-unreal-0.1.0.zip`, on the repository's Releases page,
holds the plugin folder to drop into a project's `Plugins/`, as source the project compiles; the [README](README.md)
has the install and a first run. What a game can do with it: open a world on disk (a world folder or an `.owworld`
file) and read its elements of all 22 types as Blueprint structs; read and write a world over the onlyworlds.com API
from Blueprint nodes, which answer JSON text, and from C++; and read a world it holds without a server (as a graph, as
map geometry, by its own calendar, as one reader may see it), each reader giving the same answers as the other
OnlyWorlds SDKs on a shared set of test cases. Built and tested on Unreal Engine 5.8, Win64. The entries below are in
the detail a maintainer needs; the history before this file is in the git log.

## [Unreleased]

### Added

- The README's Blueprint first run ("On Blueprints, from a world on disk") built in code and run. The host's editor
  module builds its graph node for node as an Actor Blueprint (`OWFirstRunGraph.cpp`: Event BeginPlay, Open World
  Folder, Get All Characters, For Each Loop, Break OWCharacter, Print String of Name; each node placed as the editor
  places one, each link made through the K2 schema). `-run=OWMakeFirstRun` saves it as `/Game/OW/BP_OWFirstRun` with
  Path `Worlds/hyperion`; `-Play` runs it after the save and `-PlayOnly` runs the saved asset, each placing it in a
  game world and dispatching its BeginPlay, so Print String's lines land in the log. The automation test
  `OnlyWorlds.FirstRun.Blueprint` holds it: on the example world the repository carries, the three Characters' names
  in order (Mr. Steinway, Rachmaninov, The Consul); on a folder that is not there, nothing printed and the plugin's
  `no world.json in <path>`.

### Changed

- README, the Blueprint first run: its steps now say what the graph needs that they left out. The Blueprint is the
  Level Blueprint or an Actor Blueprint placed in the level, and the For Each Loop is entered from Open World Folder's
  exec output (without that link the loop never runs). The Output Log's form of the names, and of the reason when the
  folder is missing, is given. The note that the steps had never been run is replaced by what runs them now.

## [0.1.0] - 2026-11-23

The first numbered release: the plugin as built so far, every call as [docs/reference.md](docs/reference.md) describes
it, in one zip.

### Added

- `EOWLensRung::CoBuilder`, the fifth rung the core reads (`co-builder`): a keeper beside the owner, who reads the whole
  world but what is proposed, hidden elements included and marked, with none of the owner's two counts. Last in the
  enum, so no earlier rung's value moves. `UOWLens`, `UOWGraph` and `UOWCharter` take it as every other rung.

- `BulkWrite` gains `bCreateOnly` (off by default), the core's `OWBulkOptions::create_only`: sent as `create_only`, every
  item with an id is created and never replaced; an id the world already holds answers that item's slot 409, its
  `Error.Code` `id_conflict`, and the rest of the batch goes on. Off, the body is the one sent before.

- The release zip, `onlyworlds-unreal-<version>.zip`: one `OnlyWorlds/` folder to drop into a project's `Plugins/`,
  `Plugin/OnlyWorlds/` as the tagged commit holds it, with a copy of the licence inside. It leaves out the automation
  tests (`Source/OnlyWorlds/Private/Tests/`): they need the repository's fixtures and run from a clone.
  `tools/release_zip.py <tag>` makes it from git, never from the working folder (so never a build's `Binaries/` or
  `Intermediate/`), refuses a tag that the `.uplugin`'s `VersionName` does not match, and writes the same bytes for the
  same commit; the tests workflow makes it on every push.

- A front door for a first-time reader: `LICENSE` (MIT), `SECURITY.md`, a README with an install and a first run on
  the public sample world Moppetopia with its demo key (`Host/Source/OWHost/OWFirstRun.cpp`, the console command
  `OnlyWorlds.FirstRun`), the full reference moved to `docs/reference.md`, and two workflows on every push and pull
  request (the commit messages' words; the drift checks and the core's suite built with MSVC on Windows, against zlib
  1.3 built from its release). `build.cmd` takes zlib's folder and library from `ZLIB` and `ZLIB_LIB` when set.

- Joining a world by invite link, in the core (`OWJoin.h`): `OWJoin::ParseLink(text)` reads the `ow_j_` code out of a
  pasted link or a bare code (`""` when the text holds none; trimmed of Unicode's White_Space, read after the last
  `#`, no white space inside, text that is not UTF-8 none). `OWJoin::Preview(code, where, done)` (POST `/join/preview/`)
  and `OWJoin::Redeem(code, options, where, done)` (POST `/join/`) send no credential, only `{code}` and the
  `OWRedeemOptions` set (`name`, `agent_name`, `character`, in that order) with `Content-Type`, through the
  `OWJoinConfig`'s transport and `base_url` (production's by default, `""` too). They answer an `OWJoinPreview` (the
  reader, the world without its id, the inviter, the role, a person link's note, the glimpse as an `OWJoinGlimpse` of
  things and links with `ToJson()`, and a table's `max_uses`, `uses_left`, `name_required` and `OWJoinPick`s, with
  `IsTable()`) and an `OWJoined` (a player's read key and Character, or an agent's seat key, secret, seat, base URL,
  `.env` lines, its `front_door` as served and its `reading_aids` as `OWCharter::Served` reads them; `RoleInWorld()`;
  `served` the answer whole). `OWClient::AcceptJoin(code, done)` (POST `/account/join/`) accepts a person's link on an
  account token, the Bearer alone, answering an `OWJoinedMembership`. `OWJoin::PreviewOf`, `JoinedOf` and
  `MembershipOf` read a kept answer. `OWApiError` tells keel's join refusals apart: `IsCharacterTaken`,
  `IsCharacterInvalid`, `IsWorldFull`, `IsAlreadyMember`, `IsPinRequired`. Refused with nothing sent: an empty code
  (all three), no transport, and `AcceptJoin` on any key but an `ow_a_` token. All three requests are made by the
  client's one request builder (`OWClient.cpp`), so the path-segment scan reads them. The wire test records the three
  routes and holds each join model to keel's document, the preview both ways. Parity with the Unity SDK's `OWJoin`
  and `AcceptJoinAsync`, the Godot SDK's `OWJoin` and `accept_join`, and the TypeScript SDK's `previewJoin`,
  `redeemJoin` and `acceptJoin`. Not on Blueprints yet.
- The map-space profile's frame reading (rules 13 and 14, serial 139), in the core: `OWMapSpace::CarryOf(map, x, y,
  maps)` answers an `OWMapCarry`, `{parent_frame, lint, points, stop}`: where a point on a child Map lies on its
  parent, by the child's `x_parent_frame` (`[x0, y0, x1, y1]` in the parent's pixels), and on each Map above it, one
  level at a time, as `x0 + ((x * (x1 - x0)) / width)` and its y in 64-bit floats in that order. The Map's own frame
  and lint (`no_parent`, `not_four_numbers`, `not_a_box`), the points carried, and why the walk stopped (`top`,
  `dangling`, `cycle`, `no_frame`, `no_carry`); `OWMapSpace::CarryStops()` and `CarryLints()` name the words, and
  `OWMapSpace::NumberOf` reads a number as the rule does (a finite double, the nearest to its text; one past a
  double's range none). A carry size is any whole double above 0, with no bound at 2^53, as npm's `carryOf` reads it,
  where `WholeOf` (the map-space reading's) stops at 2^53. A caller's mistake is named in `error`, nothing read. Parity
  with the TypeScript SDK 4.183.0 (`carryOf`). Every one of the fixture's `frame_cases` (66 at this pin) passes, each
  number compared exactly as a double; the fixture's 115 numbers with a fraction or an exponent are first held to
  the bits Python reads (`tests/data/map-space-frames.bits.json`, written by the new
  `codegen/generate_frame_bits.py`, `--check` its drift guard), and the core's reader reads every one exactly. The
  `down_cases` (`downOf`) are still read by no test. No Blueprint layer.
- The profiles' fold and order under public names, in the core: the new `OWText.h`, `OWText::TrimAndFold(text)` (a
  text trimmed of Unicode's White_Space at both ends, nothing inside touched, then simple case folding by Unicode
  15.1's C and S rows, with no normalization first: a byte order mark, a zero width space and U+001C to U+001F stay,
  the long s folds to s, the sharp s stays) and `OWText::CompareByCodePoint(a, b)` (negative, 0 or positive as `a`
  sorts before, with or after `b` by code point, never by UTF-16 unit or a collation), with `OWText::ByCodePoint`, the
  same order for `std::sort`, `std::map` and `std::set`. Every profile reader here already matched and ordered by
  them: `TrimAndFold` is `OWMessage::Fold` under the profiles' name (both stay; `OWUnicode::Fold` is OW-18's, NFC
  first, and is another fold), and the map-space reader's file-local order is now `OWText::ByCodePoint`. Parity with
  the TypeScript SDK 4.180.0 (`trimAndFold`, `byCodePoint`): its test file ported case for case
  (`tests/test_text.cpp`), but its refusal of a value that is not a string, which a `std::string_view` cannot be. No
  Blueprint layer.
- `OWApiError::FieldsRefusal()`, in the core: on a 422 whose `param` is `fields` (a `List` or a `Get` whose `fields`
  list named fields its type does not have), the three keys keel sends inside the envelope's `error` object, typed as
  an `OWFieldsRefusal`: `unknown_fields` (every unknown name, in the order sent), `types_with_field` (for each unknown
  name another type has, those types, in the order served; `TypesWith(name)` looks one up) and `allowed_fields` (the
  names the type takes). Absent on any other error, when keel sends no `unknown_fields` list, when the keys are not
  inside `error`, or when the body is not JSON; an entry that is not a string, or a `types_with_field` entry that is
  not a list, is left out, and a key keel left out reads empty. Parity with the TypeScript SDK 4.179.0
  (`fieldsRefusal`), its three tests ported. Core only: the Blueprint `FOWApiError` carries `Param` and `RawBody`,
  not the three lists yet.
- `OWBulkOptions::create_only`, in the core: `Bulk` with it true sends `create_only`, and every item with an id is
  created as `Create` creates it, never replaced. An id the world already holds answers that item's slot 409
  `id_conflict`, and the element stays as it was; the rest of the batch goes on as before, and the slot reads as any
  failed slot does (`OWBulkResult::Failed`, its error's `IsIdConflict`). False (the default), the body is the one
  earlier versions sent. Parity with the TypeScript SDK 4.185.0, its test vector ported (its refusal of a value that is
  not a boolean has no counterpart here: the member is a `bool`). Core only: the Blueprint node `BulkWrite` takes
  `atomic` and a key, not this option yet.
- The third default layout, `by-links-3d` (the default layouts page, serial 137), in the core:
  `OWLayout::LayoutByLinks3d(graph, elements, held, heldUnit)` lays a whole graph out in three dimensions by its links.
  The body of the graph is cut into groups round its most linked elements, packed apart in directions read from a
  table (`OWLayoutDirections.generated.cpp`, written by the new `codegen/generate_layout_3d.py` from the pinned dist's
  table, never computed), and settled by 300 seeded force steps; what hangs off the body fans out from it. The world's
  Map that holds the most of the graph's elements by a Pin, three or more, is a flat plate they stand on, and a Map of a
  place on it a small plate above; a saved layout Map is never the plate. Every other piece of linked elements is laid
  out on its own, round its pinned or held elements or on a ring beside the main shape; an element linked to no other
  and pinned on no map is not placed and is listed in `unplaced`. `held` and `heldUnit` hand back points an earlier
  answer gave, so those elements stand where they stood. The answer, `OWLayoutByLinks3d` (`method` `by-links-3d`,
  `version` 2, `points` with `how`, `group`, `part` and `held`, `centres`, `unplaced`), or a refusal by the page's
  code (`held-not-object`, `held-not-point`, `held-unit`, `held-box`). Every case of the method's fixture (116) passes:
  each point to the last bit, save those the page places with a cosine, a sine or an arcsine, within its 6e-8. No
  Blueprint layer.
- What a saved layout Map does not place (the default layouts page's `unplaced`), in the core:
  `OWLayout::UnplacedByKind`, `UnplacedAround` and `UnplacedByLinks3d` answer the ids of the nodes each method's reading
  counts that are not in `placed` (the element ids of the Map's Pins), by id in code point order; a member of `placed`
  that is not a non-empty string names no element. Every case of both fixtures' `unplaced` suites passes (17 and 9).
- A co-builder, and a contributor's own hidden creations (the lens profile, serial 138), in the core: the hidden mark
  keeps an element from every reader but a keeper (the owner or a co-builder) and the one who made it. A reader's
  `rung` may be `co-builder`, which reads the whole world as the owner does, hidden elements included and bodies as
  stored, with none of the owner's two counts. A contributor reader's `whole` lists the hidden elements it made: each
  is in its view and its body carries `hidden: true`. `OWLens::Resolve`, `OWGraph::WorldGraph`, `OWCharter::CharterOf`
  and `DesignPartsOf` and `OWStrand` read both through the view. The Blueprint layer's `EOWLensRung` and `FOWLensReader` do not carry either
  yet.
- A talk on an Object or a Construct (the room and report profiles, serial 135), in the core:
  `OWLens::TalksOf(element)` answers each `{type, id}` (`OWLens::OWTalk`) a line in the room (the pair `narrative` +
  `Message` and the mark) names in its `objects` (`object`) or `constructs` (`construct`), read from the line alone,
  and `OWReport::TalksOf(element)` the same of a report (the pair `narrative` + `Report` and the mark), the talks a
  reader shows it with, never which one it closed. An entry that is not a string or is empty once trimmed of
  White_Space names nothing, a field that is not a list names none, and every other entry is an id read as written;
  anything that is no line, or no report, answers none. `OWLens::TalkFields` names the two fields. Every case of the
  room fixture's `talk_cases` (13) and the report fixture's (6) passes, each list's count held. No Blueprint layer.
- The design state (the charter profile, serial 135), in the core: `OWCharter::DesignPartsOf(world, reader)` answers
  each Object or Construct the reader is served whole whose subtype, trimmed and folded, is `in design` or `locked`
  (`OWCharter::DesignStates`), as `OWDesignPart {type, id, state}`, only in a world where `CharterOf` finds a Charter
  for that reader: a known-of stub has none, and a reader the hidden mark keeps the Charter from reads none. Every
  case of the charter fixture's `design_cases` (13) passes, the count held. No Blueprint layer.
- A line of a report's `consequences`, read as every reader reads it (the report profile's rule 10, schema-dist serial
  129), in the core: `OWReport::ReadLine(line, world)` and `OWReport::ReadLines(text, world)`, the TypeScript SDK's
  `readReportLine` and `readReportLines` (4.169.0) and the Python package's twins. `ReadLines` splits a text at every
  carriage return and line feed, trims each piece of White_Space (`OWMessage::Trim`) and drops the empty ones.
  `ReadLine` answers an `OWReportLine`: `line`, `address` (the element it names, `ow://<type>/<id>` with the id in
  lowercase, or absent), `reading`, `start` and `end` (the address as written, UTF-8 byte offsets) and `words` (what a
  reader shows). The line's address is the last one `OWAddress::FindAll` finds that names an element of this world; an
  address of another world, a slug, a world alone or a lens with no element stays prose. The address leaves the words
  only when it is canonical, has no moment and no lens, and stands apart, alone or in a matched pair, and a colon
  before it goes too; otherwise the whole line is shown, still naming its element. The page's lines fixture (101 cases
  and 16 texts) runs whole in `tests/test_report_lines.cpp`, read from the vendored folder. Not yet for Blueprints.
- The default layout method, in the core: `OWLayout::LayoutByKind(graph)` and `OWLayout::LayoutAround(graph, focus,
  steps = 1)`, a world's default picture and one element's, the TypeScript SDK's `layoutByKind` and `layoutAround`
  (4.167.0) and the Python package's `layout_by_kind` and `layout_around` (0.127.0). By kind: each kind of element gets
  a point on a circle, links act as springs and neighbours push apart for a fixed number of seeded steps, and the
  answer is fitted into a frame of -1 to 1. Around: the focus at 0, 0, its neighbours on rings round it, and with
  `steps` 2 their neighbours fanned out beyond. Each takes an `OWWorldGraph` or an `OWJsonValue` `{nodes, edges}`, puts
  it in one order first, and answers `OWLayoutByKind` or `OWLayoutAround` (`method`, `version` 1, `points` in the order
  the nodes were handed in, or an `error` naming what it cannot read). By kind gives the TypeScript SDK's points to
  the last bit, around within 1e-12. The kinds' anchors are a table of bit patterns
  (`OWLayoutAnchors.generated.cpp`, written by the new `codegen/generate_layout.py` from the shared fixture), never
  computed. The fixture (`tests/data/by-kind.json`, the TypeScript SDK's bytes) runs whole in `tests/test_layout.cpp`:
  12 by-kind cases bit for bit, 7 around cases. `tools/twin_run.cpp` answers the layout twin's two kinds as well: on
  1,500 random graphs against the TypeScript SDK, 159,081 by-kind points equal to the bit and 705 around runs within
  1e-12. Not yet for Blueprints.

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
  document, vendored from the Unity SDK (`tests/data/keel-wire.json`), which follows keel's (at `a42c3fa5ed09`: the Pin
  list takes `element`, the Pins that place that element of any type, `""` for the Pins placing nothing; a read-only
  sign-in's token answers carry `scope`; the AI lens answer carries `charged`; the member rows carry `follows_sponsor`
  and `may_change_character_note`, and two 422 codes, `seat_above_sponsor` and `seat_follows_sponsor`, are listed; the agent seat rows carry `may_change_character`, its note and `follows_sponsor`; the front door on `GET /me`, `POST /join` and two `PUT /me/...` answers serves
  `front_door.charter_element` and `front_door.charter_skill_elements`; a member's PATCH body and answer carry
  `may_change_character`; the draught's answer carries `work[].by_role`; the request body of `POST /account/worlds/{world_id}/messages`
  takes `constructs`, `objects` and `relations` beside `characters`; `POST /bulk` takes `create_only`, and every
  proposal answer declares `house`; both invite link bodies take `note`, and `POST /join/preview` answers it and its `glimpse`); `List` already sends any
  field-named filter as given, so `filter["element"] = id` needs no change here, and no path, method, parameter or
  request key moved.
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

### Changed

- The tests read their optional inputs only from the environment: `OW_SIKELIA` and `OW_MOPPETOPIA` (two large real
  worlds) and `OW_NORMALIZATION_TEST` (Unicode's NormalizationTest.txt, 15.1.0), with no default. In the core's runner
  an input not given is a skip that names its variable; an automation test of a world not given adds a warning and
  does not run, and a world named but not there still fails. `tests/check_writer.py` takes the reference checker's
  folder from `--run-py` or `OW_RUN_PY`.
- The plugin's automation tests are compiled only inside this repository (`OW_REPO_TESTS`, set by
  `OnlyWorlds.Build.cs` when `codegen/schema-dist/` is two folders above the plugin): a copy of the plugin in another
  project lists none, where before each one failed for want of its files.
- `OWClient::Me`'s comment, in the core, and the Blueprint node `ReadMe`'s tooltip: the PIN is sent on `GET /me`, so
  a wrong PIN shows on this call first, which makes it the connection test for a key that can write; beside a key
  that cannot (a read key, a player's), onlyworlds.com never reads a PIN. The TypeScript SDK 4.185.0's sentence on
  `me()`. Docs only: nothing is sent differently.
- `OWGraph::WorldGraph`, in the core: a co-builder reads the graph as a keeper (the lens page, serial 140). The flags
  `hidden` and `proposed`, and the `hidden` and `proposed` counts in `cut` and `edges_cut`, are a keeper's (the owner
  or a co-builder), where they were the owner's alone, so a co-builder's unlensed graph is answered as the owner's is
  on the same inputs; `not_visible` and `anchor_not_visible` stay the owner's. `OWGraph::OwnerOnlyEdgeReasons()` now
  holds those two, the new `OWGraph::KeeperOnlyEdgeReasons()` holds `hidden` and `proposed`, and `OwnerOnlyFlags()`
  keeps its name (the fixture's key) for the two flags a keeper may ask. Every case of the lens fixture's `graph` kind
  passes. The Blueprint layer (`UOWGraph`, `OWGraphLibrary.h`) still says the owner's alone in its docs, and its
  automation tests still count 56 graph cases: a plugin build's.
- The reading aids and the strand say what type each of their elements is (the charter and strand profiles, serial
  133), in the plain C++ core: `OWCharterLaw::type` (`law`), `OWCharterGuide::type` and `OWCharterDomain::type`
  (`construct`), and `OWStrandSlot::type` (the kind the slot holds, `location` for `now`), each the element's own type
  as the lens folds it, written beside `id` by `ToJson` (`{id, type, name, rules, skill}`, `{id, type, name}`,
  `{id, type, name, sentence, hubs}`, each filled slot `{id, type, name}`). A reader handed the packet alone fetches an
  id by its type. `OWCharter::Served` and `OWStrand::Served` keep a served `type`, and an entry served without one (a
  keel from before serial 133) reads its fixed type. Every case of serial 133's two fixtures passes. The Blueprint layer (`FOWCharterLaw`, `FOWCharterGuide`,
  `FOWCharterDomain`) does not carry `type` yet.
- A test in the core holds what `OWCharter::Served` hands over from each served entry: its kind's named keys in the
  charter page's shape, the Charter `{id, type, name, rules, skill}`, the Guide `{id, type, name}`, each Domain `{id,
  type, name, sentence, hubs}` and each hub `{type, id}`. A key the page does not give (a host's `description`, say)
  is dropped, as it always was: the page's packet carries none of the text an AI reader follows, which it reads from
  the element whole. Nothing the core sends or reads changed.
- The plugin's module compiles with precise floating point on every target (`FPSemantics = FPSemanticsMode.Precise`
  in `OnlyWorlds.Build.cs`). A game, client or server target defaulted to MSVC's `/fp:fast`, so a packaged game
  compiled the core under it, where the compiler may reorder arithmetic and, on a CPU target with FMA, fuse a multiply
  and an add into one rounding. The layout must round each operation as JavaScript does: built with `/fp:fast
  /arch:AVX2`, the layout twin found 1,268 of 1,405 by-kind graphs and 182 of 705 around runs off. A game target's
  build now passes `/fp:precise` (`-ffp-contract=off` under clang) and its layout object holds no fused instruction.

- An address found in prose (the address page's "An address in prose", schema-dist serial 123), the TypeScript SDK's
  `readOwLinkAt` and the Python package's `read_ow_link_at`. In the core, `OWAddress::ReadAt(text, index = 0)`
  answers an `OWAddressFound` (`reading`, `end`): the address that starts exactly at `index` and the index just past
  it, or nothing when the text there is not an address. Where it ends is the grammar's, never the next space's: after
  the path only `@` and a moment and then `?lens=` and a lens; any other query key, a fragment and a full stop are
  prose. In the site's form the run after `/w/` is read whole (exactly an id's form is the id, any other legal slug the
  slug, else no address), and a run followed by a letter of any script, a combining mark or an underscore is cut
  inside a word and is no address, the character after it decoded whole from its UTF-8. The core's `index` and `end`
  are UTF-8 byte offsets. For Blueprints, `UOWAddresses::ReadAddressAt(Text, Index, Address, End)`, whose `Index` and
  `End` count the FString's UTF-16 units, as the TypeScript SDK's do. `OWUnicode::IsLetterOrMark` reads general
  category L or M from a table `codegen/generate.py` now emits (725 ranges of the vendored UnicodeData 15.1.0). The
  address fixture's 61 prose cases, skipped by kind until now, run in the core's runner and in the plugin's automation.
- `OWAddress::ReadAt` and `UOWAddresses::ReadAddressAt` answer nothing when the address would begin inside a word
  (schema-dist serial 127): after a letter of any script, a combining mark or an underscore (`elbow://object/<id>` and
  `_ow://…` are prose), the same set that cuts a run after it; a hyphen, a digit, white space, punctuation or the start
  of the text before the prefix does not glue. The character before the index is decoded whole from its UTF-8, so a
  letter past ASCII (two to four bytes) glues as any letter does; bytes that are not UTF-8 do not glue. Two addresses
  written back to back read as two after an id that ends in a digit and as one after an id that ends in a hex letter.
  The address fixture's fifteen new prose cases run in the core's runner and the plugin's automation: the core's
  fixture loop walks each case's code point `index` into its UTF-8 byte offset (it held every case ASCII before its
  index until now), and the plugin's reads `index_utf16` where a case gives it, the FString's UTF-16 units.
- Every address in a text (the address page's find-all, schema-dist serial 128), the TypeScript SDK's `findOwLinks`
  and the Python package's `find_ow_links`. In the core, `OWAddress::FindAll(text)` answers each `OWAddressFound`
  `ReadAt` reads at a place where either prefix starts, in order; `OWAddressFound` gains `start`, the address's first
  byte. In the plugin, `UOWAddresses::FindAddresses(Text)` (BlueprintPure) answers an array of the new
  `FOWAddressFound` (`Address`, `Start`, `End`, the FString's UTF-16 units). The search goes on from the end of each
  address found, so no two overlap and a prefix inside an address is no start, and two addresses written back to back
  are found as two where `ReadAt` ends the first. The address fixture's two `prose-all` cases run in the core's runner
  and the plugin's automation.
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

- Schema pin to `v0.34.1-dist.145` (`f73146c`): canonical `00.34.01` unchanged; only the charter and checks pages,
  the charter fixture and the views fixture moved, and the generated files moved in their stamp only (the generators'
  `--check` hold). The charter page's rule 4 names the field an AI reader follows: the `description` of the Charter
  and of each Object in its skill, whole as the world holds it, a part or a shortened copy of it being data. A leading
  Charter whose `description` holds nothing gives that reader no house rules, and its text is taken from no other
  field and no older Charter. The checks page says its rule list is checked, never followed. The charter fixture gains
  four cases (61 in all), each a newer Charter whose `description` holds nothing leading with an empty skill;
  `charterOf` answers all four as the page says with no change, and every existing case passes; the test's sha and
  count pins move with the fixture. The views fixture's one quote of the charter page follows its new wording.
- Schema pin to `v0.34.1-dist.144` (`35a12a0`): canonical `00.34.01` unchanged; two serials, only the charter and
  map-space pages, the map-space fixture and the dist's README moved, and the generated files moved in their stamp only
  (the generators' `--check` hold). Serial 143: the charter page's rule 4 says an AI reader follows the Charter's text
  and the text of the Objects in the Charter's skill, as `charterOf` answers it for that reader, and every other
  element's text stays data; who may change that text is the host's to rule; the charter fixture is unchanged. Serial
  144: the map-space page gains rule 15, a point on a Map brought down into the pixels of a Map below it, and a fourth
  reading, `downOf`; the map-space fixture gains its own list of 28 `down_cases`, which this SDK does not read yet. Its
  46 cases and 32 layout cases are unchanged and every one passes with no reader change; the test's sha pin moves with
  the fixture.
- Schema pin to `v0.34.1-dist.142` (`5d5a8be`): canonical `00.34.01` unchanged; only the map-space page and its
  fixture moved, and the generated files moved in their stamp only (the generators' `--check` hold). Serial 142: a Zone
  outlined on a child Map and on its parent keeps two outlines, never joined and with no point added on the edge of the
  child's rectangle or its parent frame, and what a tool draws there is the tool's; rule 4 says one outline per Map the
  Zone has placed Markers on. The map-space fixture gains three cases (46 in all); every one passes with no reader
  change; the tests' sha and count pins move with the fixture.
- Schema pin to `v0.34.1-dist.141` (`f87f9ab`): canonical `00.34.01` unchanged; only the charter and lens pages and
  their fixtures moved, and the generated files moved in their stamp only (the generators' `--check` hold). Serial
  141: the charter page says folded Domain names compare by code point, as written, never by UTF-16 unit, a collation
  or a locale, with nothing normalized, and a name is trimmed and folded as the room page defines both. The charter
  fixture gains ten Domain order cases (57 in all) and three design cases (19), the lens fixture four graph cases on a
  co-builder's lens anchors (66 graph cases, 256 in all); every one passes with no reader change; the tests' sha and
  count pins move with the fixtures.
- Schema pin to `v0.34.1-dist.140` (`5195351`): canonical `00.34.01` unchanged; only the lens page and its fixture
  moved, and the generated files moved in their stamp only (the generators' `--check` hold). Serial 140: in the graph,
  the flags `hidden` and `proposed` and the hidden and proposed counts are a keeper's (the owner or a co-builder),
  where they were the owner's alone, so a co-builder's unlensed graph is answered as the owner's is on the same inputs;
  `not_visible` and `anchor_not_visible` stay the owner's. The lens fixture gains six graph cases (252 in all); no
  answer on an existing case moved; the tests' sha and count pins move with the fixture.
- Schema pin to `v0.34.1-dist.139` (`fc46657`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only, the layout anchor table in the fixture
  sha256 it names (no anchor moved; the generators' `--check` hold). Serial 137: a third default layout,
  `by-links-3d`, with its own fixture, which this SDK does not lay out yet; a member of a layout map's `placed` that is
  no non-empty string names no element (two `unplaced` cases, not read here); map-space reads a whole-number width,
  height or depth as written (four new `cases`, three new `layout_cases`). Serial 138: the hidden mark keeps an
  element from every reader but a keeper (the owner or a co-builder) and the one who made it; the lens fixture gains
  four cases, the report fixture three talk cases and the charter fixture three design cases. Serial 139: where a
  child Map lies on its parent, `x_parent_frame`, with the map-space fixture's `frame_cases`, which this SDK does not
  read yet. No answer on an existing case moved; the tests' sha and count pins move with the fixtures.
- Schema pin to `v0.34.1-dist.136` (`56f5c02`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). Serial 135:
  the room page names a talk on an Object or a Construct (a line in the room that names one in its `objects` or
  `constructs`), the report page shows a report with the talk on each Object or Construct it links, and the charter
  page gives an Object or a Construct a design state, `in design` or `locked`, in a world where `CharterOf` finds a
  Charter for the reader; a Charter the hidden mark keeps from a reader is not that reader's. The room fixture gains
  `talk_cases` (13), the report fixture `talk_cases` (6) and the charter fixture `design_cases` (13); no existing case
  or answer moved, and `tests/test_room.cpp`, `test_report.cpp` and `test_charter.cpp` hold the new bytes to
  MANIFEST.json. Serial 136: the packet page's fourth work-item state, `standing`, its optional `x_packet_from` (who
  asked) and its first fixture, `packet.fixture.json`; this SDK reads no work item's state, so nothing here moves with
  it. The plugin's charter automation test reads the charter fixture's `cases` only, which did not move.
- Schema pin to `v0.34.1-dist.134` (`700f04b`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). The strand
  page says no reader trims a member: one of white space alone is a string that names no element, so its slot is
  empty, and the empty string in any of the three members, `narrative` too, is no mark. The strand fixture holds 42
  cases on 39 worlds (thirteen more); `tests/test_strand.cpp` reads it from the vendored folder, held to
  MANIFEST.json, and every case passes with no change to `OWStrand`. The checks page's specimen is one world's own
  house rules, a specimen of the form and not a set to adopt (no rule or case moved), and the lens page says a lens is
  read for its reader, never for the Character whose draught it is read beside. No plugin test reads the strand
  fixture.
- Schema pin to `v0.34.1-dist.133` (`6c28fc1`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). The charter
  and strand pages say what type each element of their packets is, a hub is an element of the kind its link field links
  to (an id of another kind, or of no element, names no hub), and every type compared to a kind is trimmed and folded
  as the room profile defines it. The charter fixture holds 47 cases (six more), the strand fixture 29 on 26 worlds
  (two more); `tests/test_charter.cpp` and `tests/test_strand.cpp` read them from the vendored folder, each held to
  MANIFEST.json, and every case passes. The plugin's charter automation test still counts 35 charter cases.
- Schema pin to `v0.34.1-dist.132` (`c2a4652`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). The
  distribution now carries the default layouts page, `profiles/layout.md`, and its fixture, `layout.fixture.json`: the
  `by-kind` and `around` methods `OWLayout` implements, written out step by step, with 17 `by-kind` cases and 10
  `around` (five and three more than before), and `unplaced`, the reading of which elements a saved layout map does
  not place (15 cases; not implemented here). `tests/test_layout.cpp` now reads the distribution's fixture from the
  vendored folder in place of the npm SDK's `by-kind.json`, and `codegen/generate_layout.py` makes the anchor table
  from it; every case passes and no method changes. The plugin holds no layout literal. The map-space page's rule 8
  names the layouts page as the page of the method words, reads a method word whole, and says reading or drawing a
  layout map writes nothing.
- Schema pin to `v0.34.1-dist.131` (`0dc4ab7`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). The room
  page defines its fold exactly (simple case folding by the C and S rows of CaseFolding.txt 15.1, never lower-casing or
  full folding), which the core's one fold already is, and the room and message pages say a value that is not an
  object is no element: every reader answers for it as for a body that is not a message, and none fails, which
  `OWLens::IsRoomElement` and every `OWMessage` reader already did. The fixtures gain cases: room 10 (48, 21 in the
  room), message 17 (120), lens 7 (242, resolve 141), calendar 1 (78, 35 calendars, 229 steps); the core's tests
  follow. The plugin's literals (`OnlyWorlds.Messages.Fixture`'s 103 carried, `OnlyWorlds.Lens.Fixture`'s 174 carried) are not
  moved here: they wait for its build. The import-map page reads the room page's fold instead of copying it.
- Schema pin to `v0.34.1-dist.130` (`f8c5ce8`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the generators' `--check` hold). The message
  page says the order a reader asks in: whether a body is a message first, then whether it was served without a
  credential, so a body that is not a message answers false from `OWMessage::HasUnseenRecipients` (and
  `UOWMessages::HasUnseenRecipients`, `No`) whatever the key. Its fixture gains that case,
  `keyless-non-message-is-not-unseen` (103 in all), which both already answered; no reader changes. The message
  fixture's count moves in the core's test (103, `everyone` 4) and the plugin's (`OnlyWorlds.Messages.Fixture`, 103
  carried). The report page names its readers of lines.
- Schema pin to `v0.34.1-dist.129` (`8796c07`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the four generators' `--check` hold). The
  report page gains rule 10, a line of a report's `consequences` read the same in every reader, and its second
  fixture, `profiles/report-lines.fixture.json` (101 cases and 16 texts), which the core's tests read from the vendored
  folder (the `### Added` entry for `OWReport::ReadLine` builds it here). No fixture count moves in the core or the
  plugin: no test counts the dist's files, and the plugin reads no report fixture.
- Schema pin to `v0.34.1-dist.128` (`7ffb61c`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the four generators' `--check` hold). A
  multi-link names each element once: a reader served a list that names one element twice reads it once, in its first
  place, on the lens, landing and message pages (the entries below build it here). The address page adds a find-all
  with a new kind of case, `prose-all`. The folder spec is at 0.3.24 (the vendored folder check's `FORMAT_VERSION`);
  this SDK stamps no `format_version`. The spine fixture's new case has no reader here. The core tests' fixture pins
  and counts follow: address 218 (two `prose-all`), landing 33 (its doubled-target case now one edge), lens 235
  (resolve 134, graph 56), message 102 (two `recipients`, twelve kinds); the plugin's automation tests carry the same
  counts (address 218, lens 174 carried and 61 left to the graph, graph 56, message 102).
- `OWGraph::WorldGraph` (and the plugin's `UOWGraph`, which reads through it) reads each element of a list once
  (serial 128): a schema multi-link, `x_lens_leads_to` or `x_spine_depends_on` that names one target twice is one
  edge, in its first place, and a repeat to a target that is not a node is counted once in `edges_cut`. An edge's
  `index` is still its place in the list as served. Ids are compared as written. `OWLanding` reads the graph it is
  given, so a doubled target is one edge there too. The lens fixture's case 143 and the new graph case pass.
- `OWMessage::RecipientsOf` (and the plugin's `UOWMessages::RecipientsOf`, which passes its list through) answers each
  recipient once, and only for a message (serial 128): a `characters` list that names one Character twice gives that
  Character once, in its first place, ids compared as written; a body that is not a message (`IsMessage`: a Story, a
  Narrative with no supertype, a body with no pair) now has none, where before any body but a Note gave its list. The
  message fixture's two `recipients` cases run in the core and the plugin. The suite's own recipients vector
  (`tests/data/message-beside-the-fixture.json`, npm 4.102.4's answer, a repeat kept) now wants the repeat once.
- `OWMessage::IsAddressedTo`, `IsToEveryone` and `HasUnseenRecipients` (and the plugin's `UOWMessages` calls that pass
  through them) answer only for a message (serial 128, the message page's own reader gating all four on the pair): a
  body that is not a message is addressed to nobody, is never to everyone, and has no unseen recipients, false keyless
  or not; before, any body but a Note was read as a message, so `UnreadFor` never counts one unread now either. A Note
  reads as before.
- Schema pin to `v0.34.1-dist.127` (`f50c526`): canonical `00.34.01` unchanged; no schema file or `schema.json`
  moved, and the generated files moved in their stamp only (the four generators' `--check` hold). The address page
  adds a start rule for an address found in prose (the entry below builds it here); its prose note now says a reader
  that indexes by UTF-8 bytes converts the code point `index` to its own, which the core's fixture loop does (the entry
  below). `views.json`'s banners sentence and its fixture's ten new cases have no reader here (no views reader in this
  SDK). The map-space page says what the layout mark means (a Map that is a drawing of its elements) and who writes it
  (rule 12): this SDK writes no `x_layout`, and the three new layout cases pass unchanged. The checks fixture's three
  presence cases and the spine fixture's one have no reader here. The core tests' fixture pins and counts follow:
  address 216 (76 prose), map-space 29 layout cases; the plugin's address automation test carries the same counts (216
  and 76 prose).
- Schema pin to `v0.34.1-dist.126` (`86e2ff4`): canonical `00.34.01` unchanged; no schema file, `schema.json` or
  `views.json` moved, and the generated files moved in their stamp only (the four generators' `--check` hold). The
  room, charter, review and checks pages read a mark by one presence: null, a blank string, an empty list and an empty
  object are absent (the entry below builds it here). The map-space page reads a link whose value is an empty string
  as no link, as an empty id is no id: this core already read every link and id so, and the fixture's seven new cases
  pass unchanged. The address page gains four prose cases, skipped by name as serial 123's are (no finder of an address
  in prose). The core tests' fixture pins and counts follow: address 201 (61 prose), charter 41, map-space 39 cases and
  26 layout cases (a second case names its Map by `map_index`), room 38 (18 in the room); the plugin's address and
  charter automation tests carry the same counts (201 and 61 prose; 6 element and 35 charter cases).
- **A room or charter mark that is an empty list or an empty object is no mark** (serial 126). `OWLens::HasMark`
  reads `x_room_of` and `x_charter_of` as the review mark was already read: null, a blank string, `[]` and `{}` are
  absent, since a compact reader leaves an empty list or object out and the same element must read the same in either
  served form. Everything that reads the two marks follows: `OWLens::IsRoomElement` and `IsReadingAid`,
  `OWCharter::IsCharterElement` and the charter packet, the lens and the graph, the openers' marks, `OWReport`'s
  report and board entry, and the plugin's `UOWLens::IsRoomElement`, `IsReadingAid` and `UOWCharter::IsCharterElement`.
  `OWLens::IsProposed` reads the review mark through the same `HasMark`, its own empty check folded into it (no reading
  moves there). A list or object with something in it, a number and `false` are still marks. The room fixture's
  `mark-empty-list` and `mark-empty-object` and the charter fixture's two empty-mark cases pass, and
  `room_both_marks_one_presence` holds both marks to the same eleven shapes.
- Schema pin to `v0.34.1-dist.124` (`094107b`): canonical `00.34.00` moves to `00.34.01`, the first canonical move
  since serial 103 (this SDK spells the canonical version nowhere outside the vendored dist, so no constant moves).
  One description moved and no field: Map's `hierarchy` orders the maps that stand side by side (floors, decks,
  layers), the maps with the same `parent_map`, without one the maps of the same `location`, without either the maps
  of the world itself; lower is deeper or earlier, and nesting is said by `parent_map` and `location`, not by the
  number (`FOWMap`'s field comment follows). No fixture or reader moved; the generated files moved in their stamp
  only, and the four generators' `--check` hold.
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

- A generated struct's integer is two members, `bool bHas<Field>` and `int64 <Field>` (`false` and `0` for null or a
  missing key), not a `TOptional<int64>`: the editor gave a `TOptional` member no pin on a Break or a Make node, so a
  graph could not read a single integer or date and a Make left them unset. C++ callers change: `C.BirthDate.IsSet()` is
  `C.bHasBirthDate`, `C.BirthDate.GetValue()` is `C.BirthDate`. Every integer of Sikelia's and Moppetopia's elements
  reads as before (the two readings written out and compared byte for byte); `codegen/generate_models.py` refuses a
  struct whose members would share a name.

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

### Fixed

- A writer never writes over a file it holds. A new element whose short name (section 3.2) and full-id name are both
  held, by an element or by a file the reader could not read, is refused: `WriteRefusal` names both paths, so
  `WriteFolder` and `Pack` write nothing, and `ToFiles` leaves the element out. Before, it took the full-id name over
  the file there. A `world.json` that could not be read (not UTF-8, cut short, a value after the value, not an
  object) is never written over either, even once the caller sets an id: `WriteRefusal` says why it could not be read,
  and `ToFiles` gives its bytes as read. Before, `SetInfo("id", …)`, as the no-id refusal advised, wrote a new
  `world.json` over the person's. Tests in `tests/test_held_files.cpp`, beside guards that pass on the code as it was:
  one JSON value and nothing after it at every reader (a folder, an `.owworld`, `world.json`, `RECORDS.json`, a
  client's and a join's answer), a new element taking its full id beside a held file, an element file that is not
  UTF-8 carried as its bytes through every write and pack, and a number past a double written as its text after an
  edit elsewhere in the element.

- The Blueprint charter structs carry the type the core serves: `FOWCharterLaw::Type` (`"law"`), `FOWCharterGuide::Type`
  and `FOWCharterDomain::Type` (`"construct"`), from `CharterOf`, `DomainsOf` and `ReadingAidsOf` (a packet served with
  no type reads the aid's own, as the core reads it). A reader handed the packet alone fetches an aid by its type.

- A contributor's `FOWLensReader::Whole` reaches the core: the hidden elements it made are in its view, whole and
  marked hidden, as the lens profile reads a contributor. Before, `Whole` was read only for a guest, so a contributor's
  own hidden creations were left out.

- The plugin's automation tests follow the vendored fixtures (schema-dist serial 145): the charter fixture's 6 element
  and 55 charter cases, the lens fixture's 185 lens and 66 graph and 5 degree cases (its co-builder cases among them),
  and the message fixture's 120 cases, of which 118 are carried. A body that is no object is asked by an id the world
  does not hold, which the door hands the core as the same nothing; the two cases left out are `SenderStatus` and
  `EditedBy` on such a body, since those nodes refuse an id no element holds. A front door's strand is compared as
  the core reads it, each slot's type the kind it holds.

- A game module's C++ links against the core. In the editor's build each module is its own DLL, and none of the core's
  classes, functions or constants was exported from the plugin's, so a module calling them (the README's first run
  among them) failed to link. Every declaration in `Core/Public` that has a definition out of line now carries
  `OW_API` (`Core/Public/OWApi.h`, 298 marks): exported from the plugin's module and imported by its dependents when
  `OnlyWorlds.Build.cs` builds them modular, empty in a monolithic build and in `build.cmd`. No engine header enters
  the core for it. `tools/check_exports.py` fails when a declaration that needs the mark lacks it (run with the drift
  checks). Held at the engine by the host's two C++ consumers, `OWFirstRun.cpp` and `OWNativeRead.cpp` (the
  console command `OnlyWorlds.NativeRead <world folder>`: `UOWWorld::Native()` and the core's calendar reader).
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
