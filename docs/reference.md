# OnlyWorlds SDK for Unreal: reference

What each part of the plugin does, call by call, how this repository builds and tests it, and what is not built yet.
The [README](../README.md) has the install and a first run.

Words used below:

- **keel**: the server behind onlyworlds.com. Its v2 API is what `OWClient` calls; "as keel serves it" means as that
  server answers.
- **schema-dist**: the [schema distribution](https://github.com/OnlyWorlds/schema-dist), the published form of the
  standard: the 22 element types, the `.owworld` conformance set, and the **profiles** (`profiles/*.md`), each a shared
  reading of one thing (a lens, a map's space, a calendar) with a fixture of cases every SDK must answer the same way.
  This repository vendors one release of it in `codegen/schema-dist/`.
- **the folder spec**: the world folder format, [World Folders](https://onlyworlds.github.io/docs/schema/folders).
  Its section numbers are the ones cited below: 3.2 a new element's filename, 4.7 the `.owworld` archive and its
  reader's rules OW-1 to OW-22, 5 the JSON writer.
- **serial**: a schema-dist release number (`v0.34.1-dist.145` is serial 145). "Serial 110" beside a rule means the
  profile gained that rule in that release.
- **rung**: the reader's standing in a world, which decides what it is served: `owner`, `co-builder`, `contributor`,
  `guest` or `public` (the owner and a co-builder are the world's keepers).
- **known-of stub**: an element a reader may know exists but not read: its id, type, name and public fields, nothing
  else.

## Layout

**The plugin is the folder `Plugin/OnlyWorlds/`, whole and on its own**: copy it into a project's
`Plugins/OnlyWorlds/`. A release's zip, `onlyworlds-unreal-<version>.zip`, is that folder as the tagged commit holds it
(as `OnlyWorlds/`, with a copy of the licence, without the automation tests in `Private/Tests/`), made by
`tools/release_zip.py`. The rest of the repository builds and
tests it: `build.cmd` and `tests/` (the core alone), `codegen/` (the generators and the vendored schema-dist), `Host/`
(a project that builds the plugin and runs its automation tests).

The plugin is two layers:

- **`Plugin/OnlyWorlds/Core/`, the engine-independent core**: plain C++, the standard library and zlib only, no
  Unreal header. It does all the work, and builds and tests on its own with `build.cmd` (no Unreal process).
- **`Plugin/OnlyWorlds/Source/OnlyWorlds/`, the Runtime module**: compiles the core in place (its `Build.cs` adds
  `Core/` as a module directory, with no precompiled header and no unity build, so no Unreal header enters the core's
  files) and puts UObject at the edges. A game module that depends on `OnlyWorlds` links against the core's classes,
  functions and constants as well as the UObject ones: each carries `OW_API` (`Core/Public/OWApi.h`), exported from
  the module's DLL in a modular build (the editor) and empty in a monolithic one and in `build.cmd`;
  `tools/check_exports.py` fails when one that needs it lacks it.

## The core

| | |
|---|---|
| `OWJson` | strict JSON (RFC 8259) whose value keeps key order and each number's source text; the folder spec's section 5 writer (`JSON.stringify(value, null, 2)`'s bytes). `OWJsonValue::Number(double)` makes a NaN or an infinity a null, as `JSON.stringify` writes it, that remembers the number: `NonFinite()` answers `"NaN"`, `"Infinity"` or `"-Infinity"` (nullptr for any other value), and `OWClient` refuses a request body that holds one |
| `OWUnicode` | NFC, the OW-18 fold (NFC, then CaseFolding's C and S rows), strict UTF-8, Python's `strip()` white space, the slug's NFKD: Unicode 15.1.0, generated from Unicode's own files |
| `OWText` | the profiles' one comparison and one order, public: `TrimAndFold(text)` (Unicode's White_Space trimmed at both ends, then simple case folding with no NFC first: room.md's "trimmed and without regard to case", not OW-18's fold) and `CompareByCodePoint(a, b)` with `ByCodePoint` for `std::sort` and `std::map` (code point order, never UTF-16 units or a collation), the npm SDK's `trimAndFold` and `byCodePoint`. Names as the profiles order them: their folds by code point, ties by id. Every profile reader here matches and orders by these |
| `OWZip` | the archive's own central-directory walk, each entry inflated by zlib and held to both sizes it declares, a writer with fixed stamps |
| `OWWorldArchive` | reads an `.owworld` by every reader rule of section 4.7 (OW-1 to OW-22) and section 4.8's fixed names (`name_case`: `world.json`, `elements/` or `spatial/` in another case and not as written), refusing with one cause of `Reasons` (13), and for a `world.json` that is there but cannot be read its `world_fault` (`too_deep` or `not_json`); packs a folder, the author fields left out of every element-folder file unless `include_author_fields` (`StripAuthorFields`, `WithoutAuthorFields`) |
| `OWWorld` | a world folder read and written back: an unedited file keeps its bytes, an edited one is written in section 5's form, a new element takes section 3.2's filename |
| `OWAddress` | the address profile (schema-dist `profiles/address.md`): `Parse` reads an `ow://` link or a site URL into a reading (form, world, type, id, moment, lens, canonical) or refuses it by the profile's named reason; `Write` and `WriteJson` write an element's or a world's address, with the writer's own refusals. Held to the profile's 137-case fixture |
| `OWNameFinder` | element names found in prose, the TypeScript SDK's `nameFinder` (4.208.0) and the Python package's `name_finder`, a rule of the packages, not of the standard: `OWNameFinder::Of(elements, OWNameCase::Exact or Folded, names)` reads a JSON list's names once (each element's `name`, or what a `names` function gives; a value that is not a string passed over; no default case), and `Find(text)` answers every `{start, end, element}` in order, none overlapping, in UTF-8 bytes as `OWAddress::FindAll` counts, `element` the place in the list given. A whole name only: no letter, combining mark, decimal digit or underscore of any script beside it (`IsWord`, Unicode 15.1 tables, `OWUnicode::IsDecimalDigit` generated beside the letters and marks); trimmed of White_Space, three code points at least; the longest whole name where several start; a name two elements share never answered (`Shared()` lists each, the elements in the order given); nothing read inside an address, a Markdown link `[label](target)`, a bare web address (`https://…` to the next white space or `<`) or an autolink (`<scheme:…>`), and with `Find(text, true)`, for a text known to be Markdown, nothing inside an inline code span, a fenced code block, an HTML tag or comment, or a reference definition's line either; folded by CaseFolding's C and S rows (`OWUnicode::SimpleFold`), never normalised. The text is read once from its end (Aho and Corasick's failure links over the names held backwards), so its cost grows with the text alone. A mistake (elements not a list, an element with no non-empty string id, a case that is neither) is `Error()`, and nothing is found. Held to the packages' 84 shared cases (`tests/data/name-finder.cases.json`, version 3, 16 with the Markdown option) and their fourteen timed recipes within npm's 2 s each |
| `OWCalendar` | the calendar profile (`profiles/calendar.md`, version 2): `ReadValue` reads an `atlas_calendar` value (an object, or the same object as a strict JSON string) into an `OWCalendarConfig`, or refuses it (a tool then shows the bare step); `Breakdown` and `FormatStep` read a step aloud ("3 Frost, 12 AG"); `WorldCalendars` finds a world's calendars by the field, never by supertype. Numbers are read as JavaScript reads them (whole to 2^53 - 1, `1.0` is 1), as the profile's reference package does. A label is filled and closed up in one pass, and a day found by walking the months, so a stored calendar never sets what one label costs: the TypeScript SDK 4.201.2's eight timed cases (`tests/data/calendar-cost.cases.json`), each inside their 50 ms line (read past the size bounds, which five of them exceed). Past a size bound (256 eras, 64 months, a week of 64 steps and 64 names, 16 entries in `intercalary`, an `except` of 8, templates and names of 256 code points) or a longest year past 2^53 - 1, a value is not a calendar; a date whose step, year index plus a leap rule's `offset`, or shown year lies outside -(2^53 - 1) to 2^53 - 1 is the plain step (`IsPastExactRange`), its `Breakdown` holding the step alone. Held to the profile's 191-case fixture |
| `OWMessage` | the message profile (`profiles/message.md`): over element bodies as JSON, `IsMessage` and `IsNote` (the pair, trimmed and folded), addressing (`IsAddressedTo`, `IsToEveryone`, `HasUnseenRecipients` with the server's trimmed mark and a keyless reader), `SenderStatus` (verified, mismatch, unverifiable, unseen, against the world's members list), `EditedBy`, the read mark (`ReadMarkOf`: the reader's own hand, the newest), `UnreadFor`, `ThreadOf`, and a Character's line (`NoteOf`), each ordered by `created_at` as the profile orders. Held to the profile's 88-case fixture |
| `OWLens` | the lens profile (`profiles/lens.md`): `Resolve(world, lens, reader, {as_of, clock})`, the profile's `resolveLens` over a world snapshot as JSON, as keel's reader resolves it: visibility first (the hidden mark, which keeps nothing from a keeper, the owner or a co-builder, nor from a contributor its own hidden creations in its `whole`; a guest's whole and known-of ids; a proposed element in no view), a hop both ways and on through a Relation, never through a room element, the reading aids, an umbrella entering each child once, the fence and the window on each element's own date, `changed_within`, `show`'s rows as keel serves that reader, notes, the owner's two counts, and the mail rule through `OWMessage`. Beside it `IsLens`, `IsTemplate`, `CheckKeys` (a whole float reads whole, so `7.0` is 7; a fraction is refused), `LensesInUse`, `DateOf` and the room, reading-aid and review marks. Held to the fixture's 114 shape, keys, uses, resolve and invariant cases, and `IsRoomElement` to the room profile's 32. `TalksOf(element)` (serial 135): each `{type, id}` a line in the room names in its `objects` or `constructs`, ids read as written, held to the room fixture's `talk_cases` |
| `OWGraph` | the lens profile's graph section: `WorldGraph(world, reader, options)`, the profile's `worldGraph` over the same snapshot and reader `OWLens::Resolve` reads: the elements the reader sees as nodes (`{id, type, name, family, icon}`, the family and icon `OWPresentation`'s, with `marks` and `known_of`), the schema's links and the two declared extension links as edges with the index the reader is served, and what is left out counted by reason (`edges_cut`, `cut`). Room, hidden, proposed and map-space elements and the reading aids (the charter, its guide, its domains) are out unless a flag brings them back (`rooms`, `hidden`, `proposed`, `map_space`, `reading_aids`), marked (`room`, `hidden`, `proposed`, `reading_aid`; map space carries none); `hidden` and `proposed` are the owner's, a public reader takes no flag, and a flag from the wrong rung is refused by name before anything is read. With a lens, the nodes are exactly its kept (marked as above: a reading aid a lens keeps carries `reading_aid`) and its cut rides along. Beside it `DegreeOf` and `OutDegreeOf` (each node's edges, a mirror pair two and a link to itself one; the distinct nodes its own links point at), every node a key in the graph's order. Held to the fixture's 66 graph cases and the profile's invariants over each, and its 5 degree cases |
| `OWLanding` | the landing profile (`profiles/landing.md`): `LandingOf(graph, options)`, the profile's `landingOf` over what `OWGraph::WorldGraph` answers for the visitor, so it holds exactly what that reader may see: `{mode, size, clusters, unclustered, gaps}`. The heads are the most-linked Characters (never a known-of node), ties by id in code point order, at most `heads` (5); a node within `hops` steps (1) of a head joins the nearest, a tie going to the higher-ranked head, each member `{id, hops, degree}`; what no head reaches is `unclustered`; the gaps are `orphan` and `near_orphan` (degree 0 and 1, by the page's degree: a link to itself once, each place in a list once), `no_place` (a Character whose `location` names no Location node) and `nobody_here` (a Location no Character's `location` names); `mode` is `gaps` under `size` (24) nodes and `graph` at it. Options typed (`OWLandingOptions`) or as JSON, an unknown key or a value that is not a whole number of 1 or more refused by name; a whole number too large to hold reads as the largest. Held to the fixture's 33 cases. Beside it, `MostLinkedOf(graph, options)`, the TypeScript SDK 4.203.0's `mostLinkedOf` and the Python package's `most_linked_of` (a draft reading of the packages, beside the profile): for each type, its most linked nodes by the same degree, highest first, ties by id in code point order, at most `per_type` (3) of each, never a node of degree 0 or a known-of node: `{per_type, by_type}`, the types in code point order, each entry `{id, name, degree}` (a name that is not a string null). Options typed (`OWMostLinkedOptions`) or as JSON, an unknown key or a `per_type` that is not a whole number from 1 to 2^53 - 1 refused by name. Core only. Held to the packages' 18 shared cases (`tests/data/most-linked.fixture.json`) |
| `OWCharter` | the charter profile (`profiles/charter.md`): `CharterOf(world, reader)`, the profile's `charterOf` over the same snapshot and reader `OWLens::Resolve` reads: the newest marked Charter (`{id, name, rules, skill}`, read whole as keel reads it), the newest marked Guide (`{id, name}`) and the marked Domains (`{id, name, sentence, hubs}`, as the reader is served each) in the Guide's order, then by folded name and id; a public reader only on a world open to anyone. Beside it `DomainsOf`, `IsCharterElement`, `ReadingAidOf`, `FirstLine` and the profile's tables. Held to the fixture's 27 cases. `Served(answer)` reads the packet a server's front door serves (`reading_aids` in a `GET /me` or an agent's join answer) as served for that key: the Charter, the Guide, the Domains, then the strand (`strand`, an `OWWorldStrand`, six empty slots when the key sees none; absent from a server that sends none). `DesignPartsOf(world, reader)` (serial 135): each Object or Construct the reader is served whole whose subtype, trimmed and folded, is `in design` or `locked`, `{type, id, state}`, only in a world where `CharterOf` finds a Charter for that reader; held to the fixture's `design_cases` |
| `OWStrand` | the strand profile (`profiles/strand.md`): `StrandOf(world, reader)`, the profile's `strandOf` over the same snapshot and reader `OWLens::Resolve` reads, with the World's `x_strand` beside the elements: the story the mark names, one of its events and that event's place, the story's protagonist and where the hero is now (`{narrative, event, location, character, now, same}`, each slot `{id, name}` or null, `same` by id). Only the story's id is required (the event and the place may be unset). A slot holds only its own kind and is never filled from another; a story the reader only knows of fills its own slot alone; a public reader only on a world open to anyone. Held to the fixture's cases |
| `OWRecord` | the record profile (`profiles/record.md`): `RecordsOf(character, elements)`, the profile's `recordsOf` over the elements as served (a JSON list): every Event whose `x_record` mark reads and whose `characters` names the Character, not still proposed (as the review profile's `proposedMarkOf` reads it: an `x_review_proposed` object with a key, so an empty mark is settled), newest first by `played_at` as an instant to the millisecond, a tie by the Event's id in code point order, each with the tool, version, seed, outcome, the Character's seat and their stories (`{id, name, as}`, `as` `protagonist`, `narrator` or `both`, by id). Who took part is `characters` alone, never `seats`. `MarkOf(element)` reads the mark or none (only on an Event; a broken writer is no mark); `InstantOf` reads `played_at` by the message profile's grammar (`OWMessage::TimeKey`, by its parts, never a date parser). Held to the fixture's 40 cases |
| `OWReport` | the report profile (`profiles/report.md`, rule 9, serial 120): `ThreadsOf(narratives)`, the profile's `reportThreadsOf` over the Narratives as served (a list of bodies, in the order served): `{parents, groups, lints}`, its `ToJson()` the fixture's shape. A report is the pair `narrative` + `Report` and the room's mark, a board entry `narrative` + `Board` and the mark, both read by `OWLens::IsRoomElement` (`IsReport`, `IsBoardEntry`); only reports are grouped. Each report's parent, `parent_narrative` read as written and never trimmed: `none` (absent, null, no string, or empty once trimmed of White_Space), `self`, `missing` (no element given has that id), `report`, `board` or `other`; a report whose parent is a report or a board entry is in that parent's group, by `created_at` through `OWMessage::InCreatedOrder`; lints `self` and `chain`, never repaired, nothing walked further up. Of two elements with one id the first is read. Core only. Held to the fixture's 20 thread cases. `TalksOf(element)` (serial 135): the talks on an element a reader shows a report with, its `objects` and `constructs` read as the room profile reads a line's, held to the fixture's `talk_cases` |
| `OWMapSpace` | the map-space profile (`profiles/map-space.md`): `MapSpaceOf(elements)`, the profile's `mapSpace` over the elements as served (a JSON list, visibility already applied): `{maps, zones, pins, out}` in the Map's pixels, from its bottom left, z an elevation (never a height). Each Map's frame (`width` and `height`, measured from its own points with `extent: "points"` when not written; `depth`; `z_max`, so a z past `depth` shows; `scale` from `x_metres_per_pixel` when positive, else null; `location`, `parent_map`); each Zone's outline per Map from its Markers by whole `order`, a Marker without one after every ordered one, ties by id, closed by `shape` (`polygon` unless `line`); each Pin's point with its element as the wire's two keys, both or neither; what is left out counted once by reason (`no_point`, `no_markers`, `dangling`, `no_map`). Two rows of one type with one id read as the last one served, for every type; `type` and `shape` read case-exactly; a frame measured from placed points only. Whole numbers read by value (`2.0` is 2), only where they are held exactly: an integer literal within int64, a fraction or an exponent up to 2^53 - 1; ids by code point. On Blueprints through `UOWMapSpace`. Held to the fixture's 46 cases. `LayoutOf(map, maps, pins)` (serial 110, rules 8 to 10) is the profile's second, small reading of one Map, `{layout, ground, hand}`: the `x_layout` method when it is a non-empty string (`"hand"` a board), the ground Map's id when `settings.ground.map` names a served Map other than this one, and the ids of this Map's Pins a new lay-out keeps (every Pin on a board, those whose `x_hand` is the JSON `true` on any other layout map, none on a plain Map), by code point; the Pins are filtered by `map` inside, so a caller passes every Pin it holds. Held to the fixture's 32 `layout_cases` (17 at serial 110). `CarryOf(map, x, y, maps)` (serial 139, rules 13 and 14) is the third reading, `{parent_frame, lint, points, stop}`: a point on a child Map carried onto its parent by the child's `x_parent_frame` and on up the `parent_map` chain, one level at a time, in 64-bit floats in the rule's order (`x0 + ((x * (x1 - x0)) / width)`), with the Map's own lint and the word the walk stopped on; a carry size any whole double above 0, as npm reads it. Held to the fixture's 66 `frame_cases`, every number exactly, the fixture's decimals first held to the bits Python reads. The `down_cases` (`downOf`, serial 144) are not read yet. Beside the profile, `PinsKnownTo(pins, served)`, the TypeScript SDK 4.200.0's `pinsKnownTo` and the Python package's `pins_known_to` (a rule of the packages, not of the profile): which of a map's Pins one reader knows, for a map drawn as one Character sees it, `{known, unknown, out}`. A Pin is known when a served row has its `element_type` as `type` and its `element_id` as `id`, both as written, `how` `known_of` when that row's `known_of` is the JSON `true`, else `whole` (the last served row deciding); the Pins whose element was not served are a count, never ids; a Pin with only one of the two keys is `out.no_link`. Nothing else is read, so it cannot show more than the reader was served. Held to the packages' 31 shared cases and 8 refusals (`tests/data/pins-known-to.cases.json`) |
| `OWLayout` | three layout methods for a world nobody laid out, over a graph `{nodes, edges}` as `OWGraph::WorldGraph` answers it (each with an `OWWorldGraph` overload): `LayoutByKind(graph)` (each kind an anchor on a circle, links as springs, neighbours pushing apart, 320 seeded steps, 160 past 2,000 elements, the answer fitted into -1 to 1 on its longer axis), `LayoutAround(graph, focus, steps)` (the focus at 0, 0, its neighbours on rings by colour group, type word and name, at `steps` 2 theirs further out) and `LayoutByLinks3d(graph, elements, held, heldUnit)` (the default layouts page's `by-links-3d`: the graph's body cut into groups round its most linked elements and packed apart in three dimensions, a Map that holds three or more of its elements by a Pin laid flat with each at its Pin, `held` points an earlier answer gave kept where they stood; refused by the page's word, `held-not-object`, `held-not-point`, `held-unit`, `held-box`). Beside each an Unplaced reading (`UnplacedByKind`, `UnplacedAround`, `UnplacedByLinks3d`): the nodes a saved layout Map of the method does not place, by id in code point order. The same graph gives the npm SDK's and the Python package's points: by-kind to the last bit, around within about 1e-12, by-links-3d to the bit or within 6e-8; the plugin's module builds with precise floating point for it. On Blueprints through `UOWLayout`. Held to the layout fixture's 17 by-kind, 10 around and 17 unplaced cases and the by-links-3d fixture's 116 cases and 9 unplaced cases |
| `OWNames` | the names profile (`profiles/names.md`): `TypeWord(world, type, supertype, many)`, the profile's `typeWord`: a world's own word for a type from `x_names` on the World (Character shown as *Employee*, a supertype's finer word first, `many` for a plural), or absent for the standard's name, printed as written. A display lookup only: the folder, the ids and every other reader keep the standard's names. Held to the fixture's 29 cases |
| `OWPresentation` | the presentation sidecar (schema-dist `presentation.json`), generated: the colour words in their order (`Order`), each type's colour word and icon (`GroupOf`, the TypeScript SDK's `groupOf`, and `IconOf`) and the fields a known-of stub shows (`PublicOf`); `FamilyOf`, the same answer as `GroupOf`, is deprecated since 0.7.0 and leaves in 1.0. A family word not in `colors.order` fails loudly: the generator refuses it, naming the word, and the table holds the rule as a `static_assert` |
| `OWViews` | how an element is shown (schema-dist `views.json`, version 3), generated: `ViewOf(element)` answers an `OWView` of `row` (the table's own `OWViewRow`, or none: the element draws as its type), `sentence` (the one sentence for a card, a list row or a hover: the first field the row names that is a string holding something after trimming White_Space, cut at its first U+000A or U+000D and trimmed again), `banners` (the marks at the head of its page: the table's `known_of` and `hidden` when true, the two review marks when a non-empty object, `x_served_trimmed` when it names a field, then the row's own marks read by the room profile's presence rule) and `order` (the fields its page draws: the row's lead, the rest of the type's section fields in the schema's order, the folds; each once, only a field that holds something; a Pin's generic link one field, `element`, held when both its keys are). A profile's pair matches by that profile's own reader (`OWLens::IsRoomElement`, `OWCharter::IsCharterElement`), the most specific row winning, the first on a tie. Beside it the table as baked (`Version`, `Sha256`, `Banners`, `Layouts`, `Rows`) and each type's `SectionFields` and `GenericLinks`, the type trimmed and folded. Following a conversation's chain is the caller's. `codegen/generate_views.py` checks the file before it bakes it (the version, each row's columns, kinds, fields by the walk, layouts, no two rows of one kind, the fixture's cites at their lines) and refuses it whole when it fails. The Unity SDK's `OWViews.ViewOf`. Held to every case of `views.fixture.json` (its own tables read through a private `ViewIn`) and the Godot and Unity SDKs' vectors |
| `OWOpeners` | the Open with menu's rows (the tools list's README, "Open with"), as the npm SDK's `openersFor` and the Python package's `openers_for` give them: `OpenersFor(tools, kind, address, surface)` for a world or an element, the site's page first, then each listed tool with a door for that kind in the list's order, each with its word ("Sign in to Atlas first", "Opens in your browser", ...) or none; for the public (the signed-out reader) a tool that needs an account is a row that says "Needs an account" and opens nothing (its href kept), and one that needs the device is not there; with the surface's `online` false, every row whose href is a web address (`http:` or `https:`, any case) says "Needs a connection", the site's included, after "You are here" and before every other word, its href kept. `OWTools::All()` is the OnlyWorlds tools list at the pinned commit (`codegen/tools-pin.json`), generated; `OpenersForJson` takes the list, the address and the surface as JSON. On an element's menu, pass the element as you were served it (`OWOpenersOptions{&body}`, a last argument; the JSON form's `{element}`): a tool whose element link cannot open it (a room element, a hidden or proposed one, a Map, Pin or Marker, a reading aid: the tools list's `element_skips`) is left out, each mark read as `OWGraph` reads it (`MarksOf`). A tool whose element door opens only some types (the tools list's `element_types`, `OWToolOpen::element_types`: Tactical Tangle opens Characters) is left off every other type's menu, world door and all, read from the address's type with no element needed; a world's menu reads no types. A refused address answers the address profile's `reason`; any other mistake an empty `reason` and a `message` naming the key. Held to the packages' shared 100-case fixture |
| `OWClient` | the first API client (keel's v2 API, onlyworlds.com). The read calls: `Health`, `GetWorld`, `Me` (its body is what `OWCharter::Served` reads), `Members`, `List` (`OWListOptions`: the typed options and the single-link filters), `ListAll` (the cursor walk, by `has_more`, refusing a page that hands back its cursor), `Get`, `Changes` and `ChangesHead` (the tip, conditional on its ETag). The element writes: `Create` (an id minted when the element has none, by `OWId`: a UUIDv7, or under `OWCreateOptions::idempotency_key` the UUIDv5 of the key, so a retry replays), `Upsert`, `Patch` and `Delete` (`OWWriteOptions::if_match`, the `change_seq` sent as `If-Match`), `EditLinks` (`OWLinkEdit`: add and remove, the one way to change a link array), `Bulk` (`OWBulkItem`s, `OWBulkOptions`, answering an `OWBulkResult` of `OWBulkSlot`s); each body a copy cleaned by `Sanitize` (the six read-only keys left out, a null string field sent as `""`, extension keys untouched), a known-of stub refused. A body holding a NaN or an infinity (`OWJsonValue::NonFinite`) is refused before sending, named with its place (`a request body holds NaN at items[1].element.x_weight, ...`), and so is one nested more than 256 deep, counted from its top as sent, the bulk's envelope included, where onlyworlds.com stops reading a body; an element 200 deep, the most onlyworlds.com stores, counted from the element, is sent in a bulk too, and past that it is the server's 422 that names the key. A body with both is refused for its depth. Answers are the core's JSON values, an element as its body (what every reader here takes and the plugin's `OWFromJson` reads), through a handler called once (`OWReply`: the value, or an `OWApiError` of kind `Api`, `Transport`, `Refused` or `Answer`; a write that became a proposal is a 202 with `IsProposed`, a stale `If-Match` a 412 with `Current()`, a `fields` list naming fields the type lacks a 422 whose `FieldsRefusal()` lists every unknown name, the types that have each and the names allowed, a size refusal a 422 whose `Limit()` names the limit, read by `IsElementTooLarge`, `IsElementTooManyLinks` and `IsExtensionsTooLarge`, a bulk slot's error too). The credential rules are the Unity SDK's: `API-Key` for a world key with its PIN only on `GET /me` among the reads (and on every request of a legacy key) and on every write, a Bearer for an account token, never a PIN, `world=` for an account token or no key, and what such a client may not send (every write) refused with nothing sent; every write carries `X-OW-Client` (`OWClientConfig::client`, `unreal-sdk` by default) and, on a client declared an agent (`OWClientConfig::agent`), `X-OW-Agent`. It sends through `OWTransport` (`OWTransport.h`), a seam with one call and a callback the transport calls once, on the thread the client is used from; the core has no HTTP of its own. Held to keel's OpenAPI document (`tests/data/keel-wire.json`, vendored from the Unity SDK): every path, method, query parameter, header and body key it sends, every key it reads |
| `OWWorldCache`, `OWSync` | a world held by a game and kept current from the change feed, as the Unity and Godot SDKs' `OWWorldCache` and `OWSync`. `OWWorldCache(id, name, read_only)` holds each element as an `OWCachedElement` (`id`, the body's own; `type`; `body`, as served), in the order first held: `Count`, `Get`, `Has`, `IsKnownOf`, `Elements`, `OfType`, `Resolve` (a list of ids or a link field's value, dangling ids skipped), `Upsert(type, body)` (a body with no non-empty string `id` is not held; a type change moves it), `Remove`, `Clear`; where the last sync stopped (`Cursor`, `CursorToken`, `Epoch`, `SyncedAt`, set together by `SetCursor`, a bare seq dropping the token and epoch); `Syncing` while a sync is in flight; its file (`ToJson`, `FromJson`, `WriteFile`, `ReadFile`: `format` `onlyworlds-unreal-cache`, `version` 1, the Godot SDK's keys, each number as served; no file reads as an empty cache, a file of another format or a newer version as none and why); and `ToWorld`, the held world as an `OWWorld` (world.json `{id, name}`, each element added as its held type), which every reader here takes. `OWSync::Baseline(client, cache, done, on_type)` walks the 22 types (`OWAddress::TypeWords`) through `ListAll` after a tip read whose head, cursor and epoch it records; `OWSync::Incremental(client, cache, done)` applies the feed from the cache's token (else its bare seq), walks again when the cache has no cursor, when its cursor is ahead of the head (kept: `kept`) and on a feed page's 409 `resync_required` (`view` drops what the new walk does not name, `role` or no reason keeps it), reads a bodiless upsert by id (its error answered as it is, a 409 too), holds each body under its own id (the row's written into a body with none), removes on every delete, and claims the seq only at a page that says no more; `OWSync::HasChanges(client, cache, done)` answers from one tip read whether head or epoch moved. Each answers an `OWSyncResult` (`fetched`, `removed`, `cursor`, `was_rebaselined`, `rebaseline_reason`, `resync_reason`, `kept`, `ChangedAnything()`) or the failing request's error; a walk that fails puts the cache back; a read-only cache and a cache with a sync in flight are refused, nothing sent. The tip reads go through `ChangesHead`, so one kept client sends the last tag and gets a 304 while nothing moved. Each answer is queued and run in turn, so a transport that answers inside `Send` walks any feed with a flat stack. The client and the cache must outlive the answer |
| `OWJoin` | joining a world by invite link with no account: `ParseLink` (the `ow_j_` code out of a pasted link), `Preview` (what a code holds, unspent: an `OWJoinPreview` with a person link's note and the Character it names, the glimpse and a table's picks) and `Redeem` (a player's read key or an agent's seat, an `OWJoined` with the seat's front door and reading aids), both keyless POSTs through an `OWJoinConfig`'s transport; a person's link is `OWClient::AcceptJoin` on an account token (an `OWJoinedMembership`). keel's refusals have their helpers (`IsCharacterTaken`, `IsCharacterInvalid`, `IsWorldFull`, `IsAlreadyMember`, `IsPinRequired`); every bad code is the one `IsNotFound`. The Unity SDK's `OWJoin` |

An Open with menu in a game, which cannot see the browser's sign-in (a fragment: `link` is an address you hold,
`AddItem` your own menu's call):

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
| `UOWLibrary` | `OpenWorldFolder`, `OpenWorldFile` (with the reader's verdict: accepted, reason, message), `CheckWorldFile`, `PackWorldFolder` (a copy: every element, hidden ones included; the plugin has no share yet). A relative path is taken from the project's folder |
| `UOWWorld` | one world: its id and name, element ids by type, an element's type, name and JSON, `SetStringField` and `SetIntField`, `WriteFolder`, `PackToFile` (a copy, as `PackWorldFolder`'s: every element, hidden ones included); `Native()` hands C++ the core's `OnlyWorlds::OWWorld` |
| `UOWCalendars` | the calendar profile on Blueprints: `GetWorldCalendars(World)` (each calendar's element id and name, found by the field), `FormatStep(World, CalendarId, Step)` (a step in words, "Year 1 BT"; the bare step when there is no such calendar or its value is refused, never an error) and `BreakdownStep(World, CalendarId, Step, Breakdown)` (an `FOWStepBreakdown`: year, era, month, day, the day outside the months, weekday; a part the calendar lacks is 0 or `""`, and a date past the exact range holds its step alone). `CalendarId` `""` reads the world's only calendar when it holds exactly one. A step is an `int64` (Blueprint's Integer64) |
| `UOWAddresses` | the address profile on Blueprints: `ParseAddress(Text, Address, Reason)` reads an `ow://` link or a site URL into an `FOWAddress` (form, world, type, id, lens, moment, and a reading's `bWorldIsSlug` and `bCanonical`), `WriteAddress(Address, Text, Reason)` writes one. A part is given when its string is not empty or its `bHas` flag is set, so an empty string can refuse as that part would; the moment is an `int64` given by `bHasMoment` alone, past 32 bits `bad_moment`. A refusal is `false` and the profile's reason |
| `UOWOpeners` | the Open with menu on Blueprints, over the baked tools list: `OpenersFor(World, ElementId, Surface, Rows, Reason)` for an element of an opened world (`""` for the world itself) and `OpenersForAddress(Address, Surface, Rows, Reason)` for an address (an element's rows when it gives an id). A tool that opens only some types is left off every other type's menu through either call (Tactical Tangle is on a world's and a Character's, not a Location's). `FOWOpenerSurface`: `Audience` (`Public` by default, the signed-out reader: Atlas, Lantern and Tactical Tangle say "Needs an account"), `Tool`, `SignIn` (`Unknown`, `SignedIn`, `SignedOut`), `bBrowser` (false by default: a game), `bPhone`, `bOnline` (true by default; false offline, a web row then says "Needs a connection"). Each `FOWOpener` row: `Id`, `Name`, `Href`, `State` (`""` for none; a row that says "Needs a connection" or "Needs an account" keeps its `Href` and opens nothing: draw it disabled) |
| `UOWNames` | the names profile on Blueprints: `TypeWord(World, Type, Supertype, bMany, Word)`, the world's own word for a type from `x_names` on its World, or `false` for the standard's name (`Supertype` `""` for none) |
| `UOWMessages` | the message profile on Blueprints, over an opened world, each element asked by its id as the API serves it (a body that names no type takes its folder's): `GetMessages(World)` and `GetNotes(World)` (ids in `created_at` order), `IsMessage`, `IsNote`, `RecipientsOf`, `IsAddressedTo`, `IsToEveryone(World, MessageId, bKeyless)`, `HasUnseenRecipients(World, MessageId, bKeyless)` (an `EOWUnseenRecipients`: `Yes`, `No`, or `Unknown` for a keyless reader), `IsSentBy`, `ThreadOf(World, RootId)`; against the world's members list as JSON text (what `OWClient::Members` answers; `""` for no list) `SenderStatus(World, MessageId, MembersJson, Sender, Reason)` (an `FOWSender`: `Status`, one of `Verified`, `Mismatch`, `Unverifiable`, `Unseen`; `Claimed`; `WrittenBy` on a mismatch), `EditedBy(World, MessageId, MembersJson, Edited, Reason)` (an `FOWEdited`: `bEdited`, `By`), `ReadMarkOf(World, CharacterId, MembersJson, bKeyless, MarkId, Reason)`, `UnreadFor(World, CharacterId, ReadMarkId, MembersJson, Unread, Reason)` and `NoteOf(World, CharacterId, MembersJson, NoteId, Reason)`, an id `""` for none. A call that reads the members list or a read mark refuses as `false` and the reason (`no world`, `no element <id> in this world`, `members: ` and where the text does not parse); every other call reads an id the world does not hold as the profile's no |
| `UOWLens` | the lens profile on Blueprints, over an opened world read as a snapshot (every element as served, world.json's `time_current` as the world's now, a folder's hidden mark on each body): `GetLenses(World)`, `IsLens`, `IsTemplate`, `LensesFor(World, CharacterId, bMembershipChose, Membership)` (the membership's choice wins whole, else the Character's `x_lens_uses`), the marks `IsHidden`, `IsProposed`, `IsRoomElement`, `IsReadingAid`, `DateOf(World, ElementId, Date)`, and `Resolve(World, LensId, Reader, Options, Reading, Reason)` with `ResolveJson` for a lens body given as JSON text. `FOWLensReader`: `Rung` (`EOWLensRung`: `Owner`, `Contributor`, `Guest`, `Public`, the default, `CoBuilder`), `Character`, a guest's `Whole` and `KnownOf` (a contributor's `Whole`: the hidden elements it made), `bOwnerCounts` (the owner reading as this reader). `FOWLensOptions`: `AsOf` with `bHasAsOf`, `Clock` (RFC 3339, `""` the machine's), `MembersJson`. `FOWLensReading`: `Kept`, `KnownOf` (the kept ids a guest holds only as stubs), `Rows` (each as JSON text, as `show` makes it), `Cut` (`FOWLensCut`: `Reason`, `Count`), `NotesIgnored`, `Mail` with `bHasMail` (`FOWLensMail`: `Unread`, `Read`, `Rows`, `Cut`, `TotalUnread`, `TotalRead`), `Unreadable`. A lens the reader is not served whole reads as nothing; a refusal is `false` and the reason (`no world`, `lens: ` or `members: ` and where the text does not parse, the core's sentence for a clock that does not read). The names are the Unity SDK's `OWLens`'s, the behaviour the core's |
| `UOWGraph` | the lens profile's graph on Blueprints: `Read(World, Reader, Options, Graph, Reason)`, the core's `WorldGraph` over the same snapshot and reader as `UOWLens`; `FOWGraphOptions`: `LensId` or `LensJson` (never both), `AsOf` with `bHasAsOf`, `Clock`, `MembersJson`, and the five flags `bRooms`, `bHidden`, `bProposed`, `bMapSpace`, `bReadingAids`. `FOWWorldGraph`: `Nodes` (`FOWGraphNode`: `Id`, `Type`, `Name` with `bHasName`, `Family`, `Icon`, `Marks`, `bKnownOf`), `Edges` (`FOWGraphEdge`: `Source`, `Field`, `Target`, `Index`), `EdgesCut`, `Cut`, `Unreadable`. A flag from the wrong rung is refused by its word (`hidden`, `proposed`, `rooms`, `map_space`, `reading_aids`, the first unmet) before anything is read. `DegreeOf(Graph)` and `OutDegreeOf(Graph)` (`FOWGraphDegree`: `Id`, `Degree`), every node in the graph's order. The names are the Unity SDK's `OWGraph`'s |
| `UOWMapSpace` | the map-space profile on Blueprints, every node pure, each reading over the elements as served, one element's JSON text each (`TArray<FString> Elements`): an opened world's through `ServedElements(World)` (every element as the API serves it, in the world's order: the owner's view), or the API's, the `Elements` of `ListAllElements` for the Map, Zone, Marker and Pin types appended into one list. `MapSpaceOf(Elements, MapSpace, Reason)`, the core's `MapSpaceOf`: an `FOWWorldMapSpace` of `Maps` (`FOWMapSpaceMap`: `Id`, `Width`, `Height`, `Depth`, `ZMax`, each with its `bHas` flag, `Scale` with `bHasScale`, `Location`, `ParentMap`, `bMeasured`), `Zones` (`FOWMapSpaceOutline`: `Zone`, `Map`, `Shape`, `Points` of `FOWMapSpacePoint`), `Pins` (`FOWMapSpacePin`: `Pin`, `Map`, `ElementType` and `ElementId` with `bHasElement`, `X`, `Y`, `Z`) and `Out` (`FOWLensCut` rows, a reason only when counted); `LayoutOf(Elements, MapId, Layout, Reason)` (an `FOWMapLayout`: `Layout` with `bHasLayout`, `Ground`, `Hand`); `CarryOf(Elements, MapId, Point, Carry, Reason)` (an `FOWMapCarry`: `ParentFrameMin` and `ParentFrameMax` with `bHasParentFrame`, `Lint`, `Points` of `FOWMapCarryPoint` `Map` and `Point`, `Stop`); `PinsKnownTo(Pins, Served, Known, Reason)` (an `FOWPinsKnownTo`: `Known` of `FOWPinKnown` `Pin`, `Map`, `ElementType`, `ElementId`, `How`; `Unknown`; `Out`). Map pixels are `int64`, as the core reads them; a carry's points `FVector2D`, in doubles. The Map is the last row whose `type` is exactly `map` with that id, compared case-exactly. A refusal is `false` and the reason: `elements[n]: ` (or `pins[n]: `, `served[n]: `) and where a text does not parse, `no map <id>`, or the core's sentence. The names are the Unity SDK's `OWMapSpace` calls', but `OWMapSpace.Of`, which a graph would title only "Of": `MapSpaceOf` here, the core's name |
| `UOWLayout` | the three layout methods on Blueprints, every node pure, over the `FOWWorldGraph` that `UOWGraph::Read` answers (Maps, Pins and Markers are out of it by default, as by-links-3d asks): `LayoutByKind(Graph, Points, Reason)` (`FOWLayoutPoint`: `Id`, `Point`), `LayoutAround(Graph, Focus, Steps, Points, Reason)` (`FOWLayoutAroundPoint`: `Id`, `Depth`, `Point`; `Steps` 1 or 2, no default), `LayoutByLinks3d(Graph, Elements, Held, bHasHeldUnit, HeldUnit, Layout, Reason)` (an `FOWLayoutByLinks3d`: `Version`, `Points` of `FOWLayout3dPoint` `Id`, `Point`, `How`, `Group`, `Part`, `bHeld`, `Centres`, `Unplaced`; `Elements` the world's rows as `UOWMapSpace` takes them, empty for none; `Held` an earlier answer's `Points` handed back, empty for none), and `UnplacedByKind(Graph, Placed, Unplaced, Reason)`, `UnplacedAround(Graph, Focus, Placed, Steps, Unplaced, Reason)`, `UnplacedByLinks3d(Graph, Placed, Unplaced, Reason)`. A graph node's name reaches the core (around orders its rings by name). Points are `FVector2D` and `FVector`, in doubles: the core's to the bit. A refusal is `false` and the reason: the core's sentence, or by-links-3d's word alone. The names are the Unity SDK's `OWLayout`'s |
| `UOWViews` | how an element is shown, on Blueprints, every node pure: `ViewOf(ElementJson, View, Reason)` over the element as served (a read node's `Json`, `UOWWorld::GetElementJson`, a stub), false with `element: ` and where for text that does not parse, and `""` or a text that is no object the empty view; `ViewOfElement(World, ElementId)` as the owner is served it, the empty view for an id the world does not hold; `SectionFieldsOf(Type)`; `GetViewsTable()` (an `FOWViewsTable`: `Version`, `Sha256`, `Banners` of `FOWViewBanner` `Mark` and `DefinedBy`, `Layouts` of `FOWViewLayoutWords` `Name` and `Sentence`, `Rows`). An `FOWView`: `bHasRow`, `Row` (an `FOWViewRow`: `Id`, `Type`, `Supertype`, `Profile`, `Pair`, `From`, `Sentence`, `Lead`, `Fold` of `FOWViewFold` `Field` and `Label`, `Layout`, `LayoutBy`, `Banner`; `""` for none), `Sentence` (`""` for none: a sentence is never blank), `Banners`, `Order`. The names are the Unity SDK's `OWViews`' |
| `UOWCharter` | the charter profile on Blueprints: `CharterOf(World, Reader, bOpen, ReadingAids, Reason)` and `DomainsOf` over an opened world (`bOpen`: the world is open to anyone, the only way a `Public` reader reads anything; pass the World's `public_read`, from world.json in a folder or the world GET over the API, and nothing else), `ReadingAidsOf(AnswerJson, ReadingAids, Reason)` over a front door's packet as served (`ReadMe`'s `Json`, so a graph wires one into the other), `IsCharterElement` and `ReadingAidOf`. `FOWReadingAids`: `Charter` with `bHasCharter` (`FOWCharterLaw`: `Id`, `Type`, `Name`, `RulesJson`, `Skill`), `Guide` with `bHasGuide` (`FOWCharterGuide`: `Id`, `Type`, `Name`), `Domains` (`FOWCharterDomain`: `Id`, `Type`, `Name`, `Sentence`, `Hubs` of `FOWCharterHub` `Type` and `Id`), and from a front door the strand as JSON text, `StrandJson` with `bHasStrand`. A refusal is `false` and the reason (`no world`, `answer: ` and where the text does not parse, `no reading aids in the answer`) |
| `FOWHttpTransport` | the core client's transport on Unreal's own HTTP (`FHttpModule`): `Config.transport = FOWHttpTransport::Make();`, and each `OnlyWorlds::OWClient` call answers once, on the game thread. The engine follows a redirect before the transport sees it (curl, `CURLOPT_FOLLOWLOCATION`), so a followed answer is refused as a failure: point the client at `https://www.onlyworlds.com/api/v2` (its default), which redirects no API call (`OnlyWorldsLive.Client.Redirect`) |
| `UOWClientRead`, `UOWClientList`, `UOWClientListAll`, `UOWClientChanges` | the API client's read calls on Blueprints, one async node each, with `OnSuccess` and `OnFailure` (one signature per node, its value pins and an `FOWApiError`): `ReadHealth`, `ReadWorld`, `ReadMe`, `ReadMembers`, `ReadElement(Config, Type, Id, Expand, Fields, ViewAs)` (each a body as JSON text, the form `UOWWorld::GetElementJson` gives), `ListElements(Config, Type, Options)` (`Elements`, each row's JSON text, `bHasMore`, `NextCursor`), `ListAllElements(Config, Type, Options)` (every page by `has_more`, or no half list), `ReadChanges(Config, Since, After)` and `ReadChangesHead(Config)` (an `FOWChangesPage`: `Cursor`, `Changes` of `FOWChange`, an upsert's element as JSON text, `Head`, `Epoch` with `bHasEpoch`). `ReadMembers` answers the roster as the JSON array `UOWMessages` takes as `MembersJson`, so a graph wires one into the other. `FOWClientConfig`: `ApiKey`, `ApiPin`, `World` (for an account token or no key), `BaseUrl` (`""` the default), `PageSize`, and for the writes `Client` (sent as `X-OW-Client`; `""` the core's `unreal-sdk`) and `bAgent` (`X-OW-Agent`); `FOWListOptions` is the core's `OWListOptions`, its filters a map. Each node is its own `OWClient` on `FOWHttpTransport`, so it answers later, on the game thread; the core's refusals (a config it will not build, a call the key may not send, a value that is not one path segment) answer at once, inside the activation, kind `Refused`, nothing sent. An `FOWApiError` carries `Kind` (`None` on `OnSuccess`, `Api`, `Transport`, `Refused`, `Answer`), `Status`, keel's `Type`, `Code`, `Message`, `Param`, `DocUrl`, `RawBody`, `RetryAfter` with `bHasRetryAfter`, `bRetryable`, `ResyncReason`, `Limit` (a size refusal's limit, `element_bytes`, `element_link_ids` or `extensions_bytes`, or `""`), `Line`, and for a write `bIsProposed` with `Proposal` (a 202: the row as JSON text) and `Current` (a 412's element now, as JSON text). A node is kept (in the root set) from its activation until it answered, once; C++ may hand one another transport with `SetTransport` before it is activated. Never keep a key or a PIN in an asset |
| `UOWWorldCache`, `UOWSyncWorld`, `UOWHasWorldChanges` | the held world on Blueprints. `UOWWorldCache` wraps the core's `OWWorldCache` and the one `OWClient` its syncs go through (built at the first call from the `FOWClientConfig` it was made with, kept as the core's config and never as a property, so no key is saved; its tip reads keep their ETag between polls): `MakeWorldCache(Config, WorldId, WorldName, bReadOnly)`, `OpenWorldCache(Config, Path, Error)` (the core's `ReadFile`; a relative path from the project's folder), `WriteFile(Path, Error)`, and pure reads: `GetHeldWorld` (the held world as a `UOWWorld`, the same object for the cache's life, its contents replaced after each sync that changed anything, so every node that reads a `UOWWorld` reads it; world.json holds the id and name alone), `GetWorldId`, `GetWorldName`, `IsReadOnly`, `IsSyncing`, `GetElementCount`, `GetElementIds(Type)`, `HasElement`, `GetElementType`, `GetElementJson`, `IsKnownOf`, `GetCursor`, `GetCursorToken`, `GetEpoch(bHasEpoch)`, `GetSyncedAt`. The async nodes, each kept in the root set with its cache until it answered once: `SyncWorld(Cache)` (the core's `Incremental`) and `BaselineWorld(Cache)`, answering an `FOWSyncResult` (`Fetched`, `Removed`, `Cursor`, `bWasRebaselined`, `RebaselineReason`, `ResyncReason`, `Kept`, `bChangedAnything`), and `HasWorldChanges(Cache)` (`bHasChanges`). No cache, a read-only one and one with a sync in flight are refused at once, kind `Refused`, nothing sent. C++ may hand the cache another transport with `SetTransport` before its first sync |
| `UOWClientWrite`, `UOWClientBulk` | the API client's element writes on Blueprints, one async node each on the read nodes' base (`UOWClientNode`: the same config, keep, transport seam and `FOWApiError`), with `OnSuccess` and `OnFailure`: `CreateElement(Config, Type, ElementJson, IdempotencyKey)`, `UpsertElement(Config, Type, Id, ElementJson)`, `PatchElement(Config, Type, Id, PartialJson, bHasIfMatch, IfMatch)`, `DeleteElement(Config, Type, Id, bHasIfMatch, IfMatch)` and `EditElementLinks(Config, Type, Id, Field, Add, Remove, bHasIfMatch, IfMatch)` (`Json`, the element as keel wrote it as JSON text, `""` after a delete), and `BulkWrite(Config, Items, bAtomic, IdempotencyKey, bCreateOnly)` (`bCreateOnly`: every item with an id created, never replaced, an id held answering its slot 409 `id_conflict`; `Items` of `FOWBulkItem`: `Type`, `ElementJson`; `Outcome`, an `FOWBulkResult`: `bErrors`, `Items` of `FOWBulkSlot` (`Status`, `Id`, `CreatedAt`, `UpdatedAt`, `bSucceeded`, `bIsProposed`, `Error`, `Proposal`), `bWasReplay`, `Failed` (the indices), `SucceededCount`, `bAllSucceeded`). The element goes in as JSON text and the core does the rest: the body cleaned by `Sanitize`, an id minted for an element without one (under the key, the keyed id, so a retry replays), the PIN, `X-OW-Client`, `X-OW-Agent`, `If-Match` (the `change_seq` as read, sent only with `bHasIfMatch`), `Idempotency-Key` (`""` for none), an empty link list not sent. A write that became a proposal (a 202 on an upsert, a patch or a link edit) is `OnFailure` with `Error.bIsProposed` and the row in `Error.Proposal`; a stale `If-Match` is `OnFailure`, status 412, with `Error.Current`; a bulk is `OnSuccess` whenever keel answers 200, its slots saying what landed. Refused at once, nothing sent: everything the core refuses (an account token or no key, a config it will not build, a value that is not one path segment, a negative `IfMatch`, an element that is not an object or is a known-of stub, an empty bulk, an item without a type) and, the node's own word, element text that is not JSON. Element text is read by the core's strict parser, so it holds no NaN, and text nested past 200, the standard's depth and the most onlyworlds.com stores counted from the element, is refused here as `too_deep` before sending, where the TypeScript SDK sends it and onlyworlds.com refuses it with a 422 that names the key. A read key's write and a write key's without a PIN are sent, as the core sends them: keel answers those |
| `FOW<Type>`, `UOWElements`, `UOWElementJson` | one `USTRUCT(BlueprintType)` per element type (`FOWCharacter`, `FOWPin`, ...), and the library that reads them out of a world: `GetAll<Types>(World)`, `Find<Type>(World, Id, Out)`, and gives one back as the JSON text a write node takes: `Get<Type>Json(<Type>)`, pure (`GetCharacterJson(Character)`, ...; `CreateElement`, `UpsertElement`, `PatchElement`, a `BulkWrite` item), and makes one of the JSON text a read node answers: `<Type>FromJson(Json, <Type>, Reason)`, pure (`CharacterFromJson`, ...; `ReadElement`'s `Json`, a row of `ListElements` or `ListAllElements`), the struct `Find<Type>` would give for the same element, keys it has no member for left out, `false` and the reason (`not JSON: ` and the parser's, `not a JSON object`) with an empty struct otherwise. For C++, `OWFromJson(Body, Out)` and `OWToJson(Struct)` on the core's `OWJsonValue` (`OWModelsJson.h`). Generated from schema-dist's `schema.json`. Beside it, hand-written, `UOWElementJson::MergeElementJson(ElementJson, StructJson, Merged, Reason)`, pure: a door's JSON laid over the element's JSON as read (`UOWWorld::GetElementJson`, or a read node's `Json`), every other key kept in its place, so an `UpsertElement` of `Merged` writes the struct's fields and keeps the element's extensions (below) |

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
python codegen/generate_views.py --check              # views.json checked, OWViews.generated.cpp current
python codegen/generate_frame_bits.py --check         # tests/data's frame bits match the map-space fixture
python tools/check_exports.py                         # every core declaration a game module links against carries OW_API
python tests/check_writer.py build/out/pass-good-repacked.owworld --run-py DIR   # the reference checker (not in
                           # this repository: DIR, or OW_RUN_PY, holds its run_py.py) on what pack wrote
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

`RunTests` matches its text anywhere in a test's name, so `OnlyWorlds` also runs the `OnlyWorldsLive.*` tests, which
read production over Moppetopia's public key and fail offline once their wait runs out. To leave them out, run
`Automation RunTests ^OnlyWorlds.`; to run only them, `Automation RunTests OnlyWorldsLive`.

The automation tests (`OnlyWorlds.*`):

- `OnlyWorlds.Archive.Conformance.<fixture>`: each of the `.owworld` conformance set's generated fixtures, read
  through `UOWLibrary`, its reader verdict and reason asserted (`codegen/schema-dist/fixtures/owworld/verdicts.json`,
  found two folders above the plugin; `OW_FIXTURES` to point at another copy). `OnlyWorlds.Archive.Set` checks
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
  as their folder's), every answer and refusal written out; 118 of the message profile's 120 fixture cases and 78
  of the 79 readings beside it (`tests/data/message-beside-the-fixture.json`), each case placed in a world of its own
  and asked by id, a body that is no object by an id the world does not hold (the three left out: `SenderStatus` and
  `EditedBy` on such a body, which refuse an id no element holds, and a number as an id, which no world holds); and
  every element of Sikelia against the core.
- `OnlyWorlds.Lens.World`, `.Fixture`, `.Reflection`, `OnlyWorlds.WorldGraph.World`, `.Fixture`, `.Reflection`,
  `.Sikelia`, `OnlyWorlds.Charter.World`, `.Fixture`, `.Reflection`: `UOWLens`, `UOWGraph` and `UOWCharter` on worlds
  built in memory (a body that names no type, a hidden, a proposed, a room and a reading-aid element, five lenses, two
  Charters and their Domains), every rung, flag and refusal; every case of the lens profile's fixture (its 6 shape, 25
  keys, 5 uses, 145 resolve and 4 invariant cases through `UOWLens`, its 66 graph and 5 degree cases through
  `UOWGraph`; the one node name stored as a number read as no text) and the charter profile's 6 element and 55
  charter cases, each case's world placed in a world of its own (a world-level hidden list as `hidden: true` on each listed body, the one mark the
  profile reads both as); every function also called through reflection as a graph calls it, against the direct call;
  and Sikelia's whole graph for the owner against the core's.
- `OnlyWorlds.MapSpace.Fixture`, `.PinsKnownTo`, `.Door`, `.Reflection`, `.Sikelia`, `OnlyWorlds.Layout.Fixture`,
  `.ByLinks3d`, `.Door`, `.Reflection`, `.Sikelia`: `UOWMapSpace` and `UOWLayout` on every case of the map-space fixture
  (its cases, its `layout_cases` and `frame_cases` named by a Map's id), the packages' pins-known-to cases, the layout
  fixture (by-kind, around, unplaced) and the by-links-3d fixture (its cases and unplaced), each node's answer held to
  the core's on the same input, every coordinate by its bits. No count is pinned: a case the node's shape cannot hold
  (a Map named by position, a list that is no list, a held value that is not three numbers, an input the struct
  writes back so that the core answers otherwise) is named and left out by a rule, and carried and not carried must
  be every case of the fixture. The door's own refusals; every node called through reflection as a graph calls it;
  and Sikelia's map space and its owner graph laid out by kind, against the core's.
- `OnlyWorlds.Views.Cases`, `.Vectors`, `.Table`, `.SectionFields`, `.Door`, `.Sikelia`: each `UOWViews` node called as
  a graph calls it, on every case of `views.fixture.json` that reads the baked table (a case on a table of the
  fixture's own is named and left out, carried and not carried counted to the fixture's whole), the core suite's
  vectors beside it, the table and every type's section fields, the door's refusals, and every element of Sikelia
  through `ViewOfElement`, each answer held to the core's on the same input; no count is pinned.
- `OnlyWorlds.Graph.Pins`, `.Run`, `.Integers` (editor only): Blueprints built in code. `Pins` places a Break and a
  Make node on each of the plugin's structs (how many is read from the reflection, the 22 generated ones
  required among them): every member gets a pin, the 70 integer pairs a bool and an Integer64
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
- `OnlyWorlds.Sync.Answers`, `.HasChanges`, `.Refusals`, `.File`, `.World`: the held world through the same fake
  transport. Each case (a baseline, the first sync, an incremental walk with an upsert, a delete, a bodiless upsert and
  a body with no id, a 409 for `view` and for `role`, a rewound server, a walk that fails) runs the core's `OWSync`
  and the node on two fakes scripted the same, and holds the node's answer, its requests and the cache it leaves to
  the core's, every count read from the core's run. `HasChanges`: three polls on one cache, the second sending the
  first tip's tag exactly as received and its 304 answered false, as the core answers. `Refusals`: a read-only cache,
  no cache, a sync while one is held in flight (nothing sent, the first still answering). `File`: `WriteFile` read back
  by the core and by `OpenWorldCache`, no key in the file, no file and a file that is no cache. `World`: `GetHeldWorld`
  is the core's `ToWorld`, read by `UOWElements`, and the same object after a second sync, holding its element.
  `OnlyWorldsLive.Sync.HeadTag` reads production: Moppetopia over its public key and Unreal's HTTP, a `SyncWorld` then
  `HasWorldChanges` twice, the second poll's `If-None-Match` and its answer recorded.
- `OnlyWorlds.Convert.AboveTheBmp`: the plugin's string seam (`OWConvert::Str` and `Utf8`). U+1F6A2 and U+10FFFF
  read back as the bytes they were, each held in an `FString` as its surrogate pair; what the round trip does to the
  noncharacters U+FFFF, U+FFFE and U+FDD0, and to every id of the map-space fixture's `code-point-order` and
  `hand-in-code-point-order` cases, is logged, each id that changes named by its code points.
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
- `OnlyWorlds.Models.FromJson.Doors`, `.World`, `.Refused`: the way in from the API. `Doors`: the 22 `<Type>FromJson`
  nodes and their pins. `World`: every element of both worlds, its text (`GetElementJson`, the form the read nodes
  answer) through its node, called as a Blueprint calls it, is the struct `Find<Type>` gives, member for member.
  `Refused`: text that is not JSON and JSON that is not an object answer `false`, the reason and an empty struct; extension
  keys and a mistyped field still read. `OnlyWorldsLive.Models.CharacterFromJson` (it reads
  production): one Moppetopia Character listed over its public key, its body as a node's text, broken into the served
  id and name.
- `OnlyWorlds.Frame.MoppetopiaRootMap` needs a renderer (`-RenderOffscreen` instead of `-nullrhi`; a `-nullrhi`
  run leaves it out): the world's root Map as a plane carrying its image, the Pins as markers, captured from above to
  `Host/Saved/OWFrames/moppetopia-root-map.png`. The scene is the plugin's (`Public/OWFrame.h`, the one the packaged
  game's `-OWFrame` shoots); the test makes its two materials in code.
- `OnlyWorlds.Frame.RootMapSkipsLayoutMaps`: `OWFrame::RootMap` on worlds made in memory (no renderer): a layout map
  (the map-space profile's rule 8, `x_layout` an object with a non-empty string `method`) read last is not the root, a
  world whose only Map is one has none, and seven `x_layout` values that are no mark leave the Map a candidate.

Two large real worlds are read only when `OW_SIKELIA` and `OW_MOPPETOPIA` name their folders, and the core's NFC
test reads Unicode's own `NormalizationTest.txt` (15.1.0) only when `OW_NORMALIZATION_TEST` names it. In the core's
runner an input not given is a named skip. `OW_OWWORLD_FIXTURES`, a folder holding a `verdicts.json`, points the
core's runner at a draft copy of the `.owworld` conformance set in place of the vendored one. An automation test of a world not given adds a warning and does not
run; a world named but not there fails. Everything else either suite reads is in this repository.

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
whole (310 elements, 22 types, exit 0), a missing world refused (exit 2). Its automation tests are compiled only in
this repository: `OnlyWorlds.Build.cs` sets `OW_REPO_TESTS` when `codegen/schema-dist/` is two folders above the
plugin, the files the tests read, so a copied plugin lists no OnlyWorlds test.

`codegen/schema-dist/` is vendored verbatim; never edit it, nor `codegen/tools/tools.json` (the tools list at the
commit `codegen/tools-pin.json` names). Under `Plugin/OnlyWorlds/`, `Core/Private/OWUnicodeData.generated.cpp` is
written by `codegen/generate.py`, `Core/Private/OWTools.generated.cpp` by `codegen/generate_tools.py`,
`Core/Private/OWPresentation.generated.cpp` by `codegen/generate_presentation.py`,
`Core/Private/OWViews.generated.cpp` by `codegen/generate_views.py`, and
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
  `UOWLens`, `UOWGraph`, `UOWCharter`, `UOWMapSpace`, `UOWLayout` and `UOWViews` call are held through reflection only, not in a graph. The client's nodes are placed on a graph for their pins but
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
- The map-space profile's fourth reading, `downOf` (serial 144: a point on a parent Map brought down to a child),
  in the core and on Blueprints: the fixture's `down_cases` are vendored and not read.
- The name finder on Blueprints: `OWNameFinder` is in the core only (its offsets are UTF-8 bytes; a node would count
  the FString's UTF-16 units, as `UOWAddresses::FindAddresses` does). So is
  `OWLanding::MostLinkedOf`.
- The strand on Blueprints: `OWStrand::StrandOf` is in the core only; a front door's strand rides on
  `FOWReadingAids.StrandJson` as JSON text.
- A patch of only the fields a graph changed: `Get<Type>Json` gives every schema field, merged or not.
  No client node, read or write, can be cancelled or given its own timeout (the transport's 30 seconds hold).
- An element read over the API placed in an opened world: `<Type>FromJson` gives its struct, but no Blueprint call
  adds it to a `UOWWorld`. `UOWMessages` reads an opened world only: a message served by the API and not in a world has
  no Blueprint call.
- The conditional tip read on a lone node: `ReadChangesHead` is its own client and holds no ETag between reads
  (`bUnchanged` is never set; a `UOWWorldCache`'s `HasWorldChanges` keeps one). `ListAllElements` gives no
  progress per page.
- World settings in a held world: `OWWorldCache::ToWorld` and `UOWWorldCache::GetHeldWorld` give a world.json of the
  id and name alone, since `GET /world/` is not in the change feed, so a calendar or a lens's reading date read
  through it finds none. `ReadWorld` (`OWClient::GetWorld`) reads them; the cache does not keep them yet.
- Any platform other than Win64, and any engine other than 5.8.
