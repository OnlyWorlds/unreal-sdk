#!/usr/bin/env python3
"""The core's views table: how an element is shown by its kind, from schema-dist's views.json.

    python codegen/generate_views.py            # verify the vendored dist against the pin, check the file, write
    python codegen/generate_views.py --check    # verify and check, then exit 1 if the output would change

Standard library only. It shares generate.py's pin check (the vendored tree is what codegen/PIN says, or nothing is
written). What it reads and writes:

  codegen/schema-dist/views.json            the banners, the layouts and the rows (presentation, not the standard)
  codegen/schema-dist/views.fixture.json    its cites: every field a row names, quoted from the page that defines it
  codegen/schema-dist/schema.json           the walk: each type's fields in the schema's own order, the base fields,
                                            and each generic link's two keys (a Pin's `element`)
  codegen/schema-dist/profiles/room.fixture.json, lens.fixture.json   the pairs each profile's reader matches
  -> Plugin/OnlyWorlds/Core/Private/OWViews.generated.cpp   OWViews.h's table (OWViewsIn.h declares the private part)

views.json is checked as the Godot and Unity SDKs' codegens check it, and nothing is written when it fails: version 3
only (serial 115: a generic link is one field of the drawing order); the four top-level keys; distinct banners; each
row's keys ones OWViews reads (a new column is a new meaning); a kind that is a type, a type with a supertype, or a
profile's pair that profile's own reader matches; no two rows holding one kind (the file's `match`: such a file "is
refused where it is built"); every field a row names a field of its type by the walk, the base fields included; every
layout one the file defines; distinct row ids; and the fixture's cites, each quote at its line of the vendored page.
Two checks are this SDK's own: a string the C++ table holds as "none when empty" (a row's id, a kind's type,
supertype, profile and pair, a layout's name and `by`, a banner mark) is refused when it is "", so "" can stand for
none without loss; and a character outside printable ASCII is refused rather than escaped. `_meta` is not baked: its
sentences are the file's rules, which OWViews.cpp reads in code.

Written LF-only, as the repo's .gitattributes keeps it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

from generate import DIST, ROOT, read_pin, verify_dist

VIEWS = DIST / "views.json"
FIXTURE = DIST / "views.fixture.json"
SCHEMA = DIST / "schema.json"
OUT = ROOT / "Plugin" / "OnlyWorlds" / "Core" / "Private" / "OWViews.generated.cpp"
ROW_KEYS = {"id", "kind", "from", "sentence", "lead", "fold", "layout", "banner"}
PROFILES = ("room", "charter")


def refuse(msg: str) -> None:
    sys.exit(f"views.json: {msg}")


def fold(s: str) -> str:
    return s.strip().lower()  # the pairs are ASCII, checked against the profiles' own lists


def check(doc, schema: dict, fixture: dict, dist: Path) -> None:
    """Exits naming the first fault; returns when the file reads."""
    meta = doc.get("_meta") if isinstance(doc, dict) else None
    if not isinstance(meta, dict) or meta.get("version") != 3:
        refuse(f"`_meta.version` is {meta.get('version') if isinstance(meta, dict) else meta!r}, expected 3")
    if set(doc) != {"_meta", "banners", "layouts", "rows"}:
        refuse(f"the top-level keys are {sorted(doc)}, expected _meta, banners, layouts, rows")
    banners, layouts, rows = doc["banners"], doc["layouts"], doc["rows"]
    if (not isinstance(banners, list)
            or not all(isinstance(b, dict) and set(b) == {"mark", "defined_by"}
                       and isinstance(b["mark"], str) and isinstance(b["defined_by"], str) for b in banners)
            or len({b["mark"] for b in banners}) != len(banners)):
        refuse("`banners` must be a list of distinct {mark, defined_by}")
    if not isinstance(layouts, dict) or not layouts or not all(isinstance(v, str) for v in layouts.values()):
        refuse("`layouts` must be an object of layout names to their sentences")
    if not isinstance(rows, list) or not rows:
        refuse("`rows` must be a non-empty list")

    base = {f["name"] for f in schema["base"]} - {"id", "world"}
    fields = {slug: {f["name"] for f in fs} | base for slug, fs in schema["types"].items()}
    room = {(fold(p["type"]), fold(p["supertype"]))
            for p in json.loads((dist / "profiles" / "room.fixture.json").read_text(encoding="utf-8"))["pairs"]}
    charter = {tuple(fold(x) for x in w.split("/")) for w in json.loads(
        (dist / "profiles" / "lens.fixture.json").read_text(encoding="utf-8"))["reading_aid_pairs"]}

    ids: set[str] = set()
    kinds: set[tuple] = set()
    for row in rows:
        rid = row.get("id") if isinstance(row, dict) else None
        if not isinstance(rid, str) or not rid or rid in ids:
            refuse(f"a row's id is {rid!r}: each row needs its own non-empty id")
        ids.add(rid)
        stray = sorted(set(row) - ROW_KEYS)
        if stray:
            refuse(f"row {rid} carries {stray}, which OWViews does not read: a new column is a new meaning, "
                   "read it in OWViews first, then here")
        kind = row.get("kind")
        if isinstance(kind, dict) and set(kind) == {"profile", "pair"}:
            prof, pair = kind["profile"], kind["pair"]
            parts = pair.split("/") if isinstance(pair, str) else []
            if prof not in PROFILES or len(parts) != 2:
                refuse(f"row {rid}'s kind {kind!r} names a profile outside {PROFILES} or a pair not `type/Supertype`")
            if (fold(parts[0]), fold(parts[1])) not in (room if prof == "room" else charter):
                refuse(f"row {rid}'s pair {pair!r} is not one the {prof} profile's reader matches")
            slug = parts[0]
            key = ("profile", prof, fold(pair))
        elif (isinstance(kind, dict) and set(kind) in ({"type"}, {"type", "supertype"})
              and all(isinstance(v, str) for v in kind.values())):
            slug = kind["type"]
            key = ("type", fold(kind["type"]), fold(kind.get("supertype", "\0")))
        else:
            refuse(f"row {rid}'s kind {kind!r} is neither a type, a type with a supertype, nor a profile's pair")
        if slug not in fields:
            refuse(f"row {rid} names the type {slug!r}, not one of the 22 slugs as written")
        if any(v == "" for v in kind.values()) or (kind.get("pair") and "" in kind["pair"].split("/")):
            refuse(f"row {rid}'s kind {kind!r} holds an empty word, which the table reads as none")
        if key in kinds:
            refuse(f"row {rid} holds a kind another row holds; the file's `match` says no two rows do")
        kinds.add(key)

        def names(k: str) -> list[str]:
            v = row.get(k, [])
            if not isinstance(v, list) or not all(isinstance(x, str) for x in v):
                refuse(f"row {rid}'s `{k}` is {v!r}, expected a list of field names")
            return v

        named = names("sentence") + names("lead")
        folds = row.get("fold", [])
        if not isinstance(folds, list) or not all(isinstance(f, dict) and set(f) == {"field", "label"}
                                                 and isinstance(f["field"], str) and isinstance(f["label"], str)
                                                 for f in folds):
            refuse(f"row {rid}'s `fold` is {folds!r}, expected a list of {{field, label}}")
        named += [f["field"] for f in folds]
        layout = row.get("layout")
        if not isinstance(layout, dict) or layout.get("name") not in layouts or not set(layout) <= {"name", "by"}:
            refuse(f"row {rid}'s layout {layout!r} is not {{name, by?}} with a name the file defines")
        if "by" in layout:
            if not isinstance(layout["by"], str) or not layout["by"]:
                refuse(f"row {rid}'s layout.by is {layout['by']!r}, expected a field name")
            named.append(layout["by"])
        unknown = sorted({n for n in named if n not in fields[slug]})
        if unknown:
            refuse(f"row {rid} names {unknown}, which {slug} does not have by the walk")
        banner = row.get("banner", [])
        if not isinstance(banner, list) or not all(isinstance(b, str) and b for b in banner):
            refuse(f"row {rid}'s `banner` is {banner!r}, expected a list of mark names")
        if not isinstance(row.get("from"), str):
            refuse(f"row {rid}'s `from` is {row.get('from')!r}, expected the page that defines the kind")
    if "" in layouts or any(b["mark"] == "" for b in banners):
        refuse("a layout name or a banner mark is empty, which the table reads as none")
    check_cites(rows, fixture, dist)
    check_ascii(doc)


def check_cites(rows: list, fixture: dict, dist: Path) -> None:
    """views.fixture.json's cites (its third note): each quote at its line of the vendored page (the page split at LF,
    the line 1-based), each cite naming a field its row names, and each field a row names cited."""
    named: set[tuple[str, str]] = set()
    for row in rows:
        for f in row.get("sentence", []) + row.get("lead", []) + [x["field"] for x in row.get("fold", [])]:
            named.add((row["id"], f))
        if "by" in row["layout"]:
            named.add((row["id"], row["layout"]["by"]))
    cited: set[tuple[str, str]] = set()
    for c in fixture.get("cites", []):
        where = f"the cite of {c.get('row')}.{c.get('field')} ({c.get('page')}:{c.get('line')})"
        if (c.get("row"), c.get("field")) not in named:
            refuse(f"{where} names a field its row does not name")
        page = dist / c["page"]
        if not page.is_file():
            refuse(f"{where}: the page is not in the vendored dist")
        lines = page.read_bytes().decode("utf-8").split("\n")
        if not (isinstance(c["line"], int) and 1 <= c["line"] <= len(lines) and c["quote"] in lines[c["line"] - 1]):
            refuse(f"{where}: the quote is not at that line of the page")
        cited.add((c["row"], c["field"]))
    if named - cited:
        refuse(f"fields a row names with no cite in views.fixture.json: {sorted(named - cited)}")


def check_ascii(doc: dict) -> None:
    """Every string baked is printable ASCII (U+0020 to U+007E): JSON's escapes for it are C++'s too."""
    def walk(v, where: str) -> None:
        if isinstance(v, str):
            bad = [c for c in v if not (0x20 <= ord(c) <= 0x7E)]
            if bad:
                refuse(f"{where} holds {bad[0]!r}, outside printable ASCII, which this emitter does not write")
        elif isinstance(v, list):
            for i, x in enumerate(v):
                walk(x, f"{where}[{i}]")
        elif isinstance(v, dict):
            for k, x in v.items():
                walk(k, f"{where} key")
                walk(x, f"{where}.{k}")
    for k in ("banners", "layouts", "rows"):
        walk(doc[k], k)


def c(text: str) -> str:
    """A C++ string literal (printable ASCII, checked: JSON's escapes are then only \\" and \\\\)."""
    return json.dumps(text)


def strings(values: list[str]) -> str:
    return "{" + ", ".join(c(v) for v in values) + "}"


def row_literal(row: dict) -> list[str]:
    kind = row["kind"]
    if "profile" in kind:
        t, s = kind["pair"].split("/")
        k = f"{{{c(t)}, {c(s)}, {c(kind['profile'])}, {c(kind['pair'])}}}"
    else:
        k = f"{{{c(kind['type'])}, {c(kind.get('supertype', ''))}, \"\", \"\"}}"
    folds = "{" + ", ".join(f"{{{c(f['field'])}, {c(f['label'])}}}" for f in row.get("fold", [])) + "}"
    layout = row["layout"]
    return [
        f"        {{{c(row['id'])}, {k}, {c(row['from'])},",
        f"         {strings(row.get('sentence', []))},",
        f"         {strings(row.get('lead', []))},",
        f"         {folds},",
        f"         {{{c(layout['name'])}, {c(layout.get('by', ''))}}},",
        f"         {strings(row.get('banner', []))}}},",
    ]


def emit(doc: dict, sha: str, schema: dict, tag: str) -> str:
    out = [
        f"// GENERATED by codegen/generate_views.py from schema-dist {tag} (views.json, sha256 {sha[:12]}...).",
        "// Do not edit: regenerate.",
        "",
        '#include "OWViews.h"',
        '#include "OWViewsIn.h"',
        "",
        "#include <string>",
        "#include <utility>",
        "#include <vector>",
        "",
        "namespace OnlyWorlds {",
        "namespace OWViews {",
        "",
        f"const int Version = {int(doc['_meta']['version'])};",
        f'const char* const Sha256 = "{sha}";',
        "",
        "// The marks every page shows at its head, in the file's order.",
        "const std::vector<OWViewBanner>& Banners() {",
        "    static const std::vector<OWViewBanner> banners = {",
    ]
    out += [f"        {{{c(b['mark'])}, {c(b['defined_by'])}}}," for b in doc["banners"]]
    out += [
        "    };",
        "    return banners;",
        "}",
        "",
        "// The layouts the file defines, by name, each with its sentence, in the file's order.",
        "const std::vector<std::pair<std::string, std::string>>& Layouts() {",
        "    static const std::vector<std::pair<std::string, std::string>> layouts = {",
    ]
    out += [f"        {{{c(name)}, {c(sentence)}}}," for name, sentence in doc["layouts"].items()]
    out += [
        "    };",
        "    return layouts;",
        "}",
        "",
        "// The rows, in the file's order: id, kind {type, supertype, profile, pair}, from, sentence, lead, fold,",
        "// layout {name, by}, banner; \"\" is none.",
        "const std::vector<OWViewRow>& Rows() {",
        "    static const std::vector<OWViewRow> rows = {",
    ]
    for r in doc["rows"]:
        out += row_literal(r)
    out += [
        "    };",
        "    return rows;",
        "}",
        "",
        "// Each type's section fields in the schema's own order (schema/<type>.yaml: its sections as listed, each",
        "// section's fields as listed), as the walk lists them, in the schema's type order; the base fields are in no",
        "// section. A generic link is its one name (`element` on a Pin), as the walk lists it.",
        "const std::vector<std::pair<std::string, std::vector<std::string>>>& SectionTable() {",
        "    static const std::vector<std::pair<std::string, std::vector<std::string>>> table = {",
    ]
    out += [f"        {{{c(slug)}, {strings([f['name'] for f in fs])}}}," for slug, fs in schema["types"].items()]
    out += [
        "    };",
        "    return table;",
        "}",
        "",
        "// Each type's generic links and the two keys a served body carries for each, from the walk. The document",
        "// layout draws a generic link as one field at its place, holding something only when both keys do.",
        "const std::vector<std::pair<std::string, std::vector<OWViewGenericLink>>>& GenericTable() {",
        "    static const std::vector<std::pair<std::string, std::vector<OWViewGenericLink>>> table = {",
    ]
    for slug, fs in schema["types"].items():
        links = [f for f in fs if f["kind"] == "generic"]
        for f in links:
            if not (isinstance(f.get("type_key"), str) and f["type_key"]
                    and isinstance(f.get("id_key"), str) and f["id_key"]):
                refuse(f"{slug}.{f['name']} is a generic link without both its keys in the walk")
        if links:
            body = ", ".join(f"{{{c(f['name'])}, {c(f['type_key'])}, {c(f['id_key'])}}}" for f in links)
            out.append(f"        {{{c(slug)}, {{{body}}}}},")
    out += [
        "    };",
        "    return table;",
        "}",
        "",
        "}  // namespace OWViews",
        "}  // namespace OnlyWorlds",
        "",
    ]
    return "\n".join(out)


def outputs() -> dict[Path, str]:
    pin = read_pin()
    problems = verify_dist(pin)
    if problems:
        for p in problems:
            print("FAIL:", p)
        sys.exit("The vendored dist is not what the pin says. Re-vendor from the pinned tag; never edit a hash.")
    data = VIEWS.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    listed = json.loads((DIST / "MANIFEST.json").read_text(encoding="utf-8"))["files"].get("views.json")
    if listed != sha:
        sys.exit(f"views.json: hashes to {sha}, MANIFEST.json lists {listed}")
    doc = json.loads(data.decode("utf-8"))
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
    check(doc, schema, fixture, DIST)
    return {OUT: emit(doc, sha, schema, pin["tag"])}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the output would change")
    a = ap.parse_args()
    pin = read_pin()
    drift = 0
    files = outputs()
    print(f"dist OK: {pin['tag']}, MANIFEST.json sha256 {pin['manifest_sha256'][:12]}... matches the pin")
    for path, text in files.items():
        data = text.encode("utf-8")
        if a.check:
            if not path.is_file() or path.read_bytes() != data:
                print("DRIFT:", path.relative_to(ROOT).as_posix())
                drift += 1
        elif not path.is_file() or path.read_bytes() != data:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print("wrote", path.relative_to(ROOT).as_posix())
    if a.check:
        print("no drift" if not drift else f"{drift} file(s) drifted: run codegen/generate_views.py")
    return 1 if drift else 0


if __name__ == "__main__":
    sys.exit(main())
