# Changelog

Notable changes to OnlyWorlds for Unreal. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Nothing is released yet; the history before this file is in the git log.

## [Unreleased]

### Changed

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
