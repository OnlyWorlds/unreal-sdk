#!/usr/bin/env python3
"""The core's generated data: the Unicode tables the archive's name rules need, from Unicode's own files.

    python codegen/generate.py            # verify the vendored dist against the pin, then write every output
    python codegen/generate.py --check    # verify, then exit 1 if any output would change (the drift guard)

Standard library only, and never Python's `unicodedata` (folder spec 4.7, OW-18: every reader builds NFC and the fold
from the files the fixture set vendors, never from a runtime). What it reads and writes:

  codegen/PIN                           the pin: the dist's tag and the sha256 of its MANIFEST.json
  codegen/schema-dist/                  the dist at that tag, verbatim; never edited. Read here: fixtures/owworld/
                                        UnicodeData-15.1.0.txt, CompositionExclusions-15.1.0.txt, CaseFolding-15.1.0.txt
  -> Core/Private/OWUnicodeData.generated.cpp   the tables, as sorted C++ arrays (OWUnicodeData.h declares them)

The NFC data is read as the reference checker reads it (`run_py.py`'s `load_nfc`; the workshop's README): field 3 of
UnicodeData is the combining class, field 5 the decomposition (one in angle brackets is compatibility, not canonical),
the primary composites are the two-part canonical decompositions minus the full exclusion set (CompositionExclusions'
listed code points, plus singletons and non-starter decompositions derived from UnicodeData). The fold is the C and S
rows of CaseFolding. Two more tables serve the core beside the name rules: white space as Python's `str.strip()` has
it (general category Zs, or bidirectional class WS, B or S: how the reference checker trims a world id), and the
compatibility decompositions and nonspacing marks (category Mn) that the element filename's slug needs (folder spec
3.2; the Unity writer's `Slugify` decomposes by NFKD and drops nonspacing marks).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

CODEGEN = Path(__file__).resolve().parent
ROOT = CODEGEN.parent
DIST = CODEGEN / "schema-dist"
PIN = CODEGEN / "PIN"
UNICODE_VERSION = "15.1.0"
OWWORLD = DIST / "fixtures" / "owworld"
UNICODE_DATA = OWWORLD / f"UnicodeData-{UNICODE_VERSION}.txt"
COMPOSITION_EXCLUSIONS = OWWORLD / f"CompositionExclusions-{UNICODE_VERSION}.txt"
CASE_FOLDING = OWWORLD / f"CaseFolding-{UNICODE_VERSION}.txt"
OUT = ROOT / "Core" / "Private" / "OWUnicodeData.generated.cpp"


# -- the pin --------------------------------------------------------------------------------------------------------

def read_pin() -> dict:
    pin = {}
    for line in PIN.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            k, v = line.split(":", 1)
            pin[k.strip()] = v.strip()
    return pin


def verify_dist(pin: dict) -> list[str]:
    """The vendored tree is what the pin says: its manifest hashes to the pinned sha256, every file it lists hashes to
    its entry, and nothing else is in the tree. Returns the problems found."""
    problems = []
    manifest_bytes = (DIST / "MANIFEST.json").read_bytes()
    got = hashlib.sha256(manifest_bytes).hexdigest()
    if got != pin["manifest_sha256"]:
        problems.append(f"MANIFEST.json hashes to {got}, the pin says {pin['manifest_sha256']}")
    files = json.loads(manifest_bytes.decode("utf-8"))["files"]
    for rel, sha in files.items():
        p = DIST / rel
        if not p.is_file():
            problems.append(f"the manifest lists {rel}, which is not vendored")
        elif hashlib.sha256(p.read_bytes()).hexdigest() != sha:
            problems.append(f"{rel} does not match its manifest entry")
    for p in DIST.rglob("*"):
        rel = p.relative_to(DIST).as_posix()
        if p.is_file() and rel != "MANIFEST.json" and rel not in files and "__pycache__" not in rel:
            problems.append(f"untracked file inside the vendored dist: {rel}")
    version = dict(l.split(": ", 1) for l in (DIST / "VERSION").read_text(encoding="utf-8").splitlines() if ": " in l)
    if f"dist.{int(version['serial'])}" not in pin["tag"]:
        problems.append(f"VERSION says serial {version['serial']}, the pin's tag is {pin['tag']}")
    return problems


# -- Unicode --------------------------------------------------------------------------------------------------------

def unicode_data() -> list[list[str]]:
    return [line.split(";") for line in UNICODE_DATA.read_text(encoding="utf-8").splitlines() if line.strip()]


def nfc_tables(rows) -> dict:
    """run_py.py's load_nfc: the classes, each canonical decomposition applied recursively (run_py's `decompose`,
    before canonical ordering, which the runtime does over the whole string), and the composition pairs."""
    ccc, raw = {}, {}
    for fields in rows:
        cp = int(fields[0], 16)
        if fields[3] != "0":
            ccc[cp] = int(fields[3])
        if fields[5] and not fields[5].startswith("<"):
            raw[cp] = [int(x, 16) for x in fields[5].split()]
    excluded = set()
    for line in COMPOSITION_EXCLUSIONS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            lo, _, hi = line.partition("..")
            excluded.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    for cp, d in raw.items():
        if len(d) == 1 or ccc.get(cp, 0) or ccc.get(d[0], 0):
            excluded.add(cp)

    def full(cp):
        return [c for d in raw[cp] for c in full(d)] if cp in raw else [cp]

    decomp = {cp: full(cp) for cp in raw}
    compose = {(d[0], d[1]): cp for cp, d in raw.items() if len(d) == 2 and cp not in excluded}
    return {"ccc": ccc, "decomp": decomp, "compose": compose, "excluded": len(excluded)}


def compat_tables(rows) -> dict:
    """NFKD's data for the slug: every decomposition, compatibility and canonical, applied recursively (Hangul is left
    to the runtime's arithmetic), and the nonspacing marks (category Mn) as ranges."""
    raw, mn = {}, []
    for fields in rows:
        cp = int(fields[0], 16)
        if fields[5]:
            parts = fields[5].split()
            if parts[0].startswith("<"):
                parts = parts[1:]
            raw[cp] = [int(x, 16) for x in parts]
        if fields[2] == "Mn":
            if mn and mn[-1][1] == cp - 1:
                mn[-1][1] = cp
            else:
                mn.append([cp, cp])

    def full(cp):
        return [c for d in raw[cp] for c in full(d)] if cp in raw else [cp]

    return {"decomp": {cp: full(cp) for cp in raw}, "mn": mn}


def white_space(rows) -> list[int]:
    """What Python's str.strip() strips: str.isspace, which is general category Zs or bidirectional class WS, B or S
    (Python's own definition, read here from UnicodeData's fields 2 and 4, never from a runtime)."""
    return sorted(int(f[0], 16) for f in rows if f[2] == "Zs" or f[4] in ("WS", "B", "S"))


def simple_folds() -> dict[int, int]:
    """The C and S rows of the vendored CaseFolding file (OW-18: the file, never a runtime's casefold)."""
    folds = {}
    for line in CASE_FOLDING.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        code, status, mapping = [p.strip() for p in line.split(";")[:3]]
        if status in ("C", "S"):
            folds[int(code, 16)] = int(mapping, 16)
    return folds


def emit(nfc: dict, compat: dict, folds: dict, spaces: list[int], tag: str) -> str:
    def h(cp):
        return "0x%X" % cp

    def flat(table):
        index, data = [], []
        for cp in sorted(table):
            index.append("    {%s, %s, %s}," % (h(cp), h(len(data)), h(len(table[cp]))))
            data.extend(table[cp])
        lines = []
        for i in range(0, len(data), 12):
            lines.append("    " + " ".join(h(x) + "," for x in data[i:i + 12]))
        return index, lines

    d_index, d_data = flat(nfc["decomp"])
    k_index, k_data = flat(compat["decomp"])
    out = [
        "// GENERATED by codegen/generate.py. Do not edit: regenerate.",
        "//",
        f"// Data, not code: Unicode {UNICODE_VERSION}, from the files schema-dist vendors at {tag} (fixtures/owworld/):",
        f"// - NFC: UnicodeData-{UNICODE_VERSION}.txt fields 3 and 5 and CompositionExclusions-{UNICODE_VERSION}.txt,",
        "//   read as the reference checker's load_nfc reads them.",
        f"// - Fold: the C and S rows of CaseFolding-{UNICODE_VERSION}.txt, OW-18's simple case folding.",
        "// Every number is written in hex, counts included, and the comments carry none: the bay's pre-push secrets gate",
        "// matches a short held value between non-word characters, and a decimal offset in a table once matched one.",
        "// - White space: general category Zs or bidirectional class WS, B or S (Python's str.isspace).",
        "// - The slug's NFKD: every decomposition in field 5 applied recursively, and the nonspacing marks (Mn).",
        "// Every table is sorted by code point (or by pair key) for binary search.",
        "",
        '#include "OWUnicodeData.h"',
        "",
        "namespace OnlyWorlds { namespace UnicodeData {",
        "",
        f'const char* const Version = "{UNICODE_VERSION}";',
        "",
        "const ClassRow Classes[] = {",
    ]
    out += ["    {%s, %s}," % (h(cp), h(c)) for cp, c in sorted(nfc["ccc"].items())]
    out += ["};", f"const size_t ClassCount = {h(len(nfc['ccc']))};", "", "const DecompRow Decomps[] = {"]
    out += d_index
    out += ["};", f"const size_t DecompCount = {h(len(nfc['decomp']))};", "", "const char32_t DecompData[] = {"]
    out += d_data
    out += ["};", "", "const PairRow Pairs[] = {"]
    out += ["    {%sull, %s}," % (h(a << 21 | b), h(c)) for (a, b), c in sorted(nfc["compose"].items(), key=lambda kv: kv[0][0] << 21 | kv[0][1])]
    out += ["};", f"const size_t PairCount = {h(len(nfc['compose']))};", "", "const FoldRow Folds[] = {"]
    out += ["    {%s, %s}," % (h(k), h(v)) for k, v in sorted(folds.items())]
    out += ["};", f"const size_t FoldCount = {h(len(folds))};", "", "const char32_t WhiteSpace[] = {"]
    out += ["    " + " ".join(h(x) + "," for x in spaces)]
    out += ["};", f"const size_t WhiteSpaceCount = {h(len(spaces))};", "", "const DecompRow CompatDecomps[] = {"]
    out += k_index
    out += ["};", f"const size_t CompatDecompCount = {h(len(compat['decomp']))};", "", "const char32_t CompatDecompData[] = {"]
    out += k_data
    out += ["};", "", "const RangeRow NonspacingMarks[] = {"]
    out += ["    {%s, %s}," % (h(a), h(b)) for a, b in compat["mn"]]
    out += ["};", f"const size_t NonspacingMarkCount = {h(len(compat['mn']))};", "", "}}  // namespace OnlyWorlds::UnicodeData", ""]
    return "\n".join(out)


def outputs() -> dict[Path, str]:
    pin = read_pin()
    problems = verify_dist(pin)
    if problems:
        for p in problems:
            print("FAIL:", p)
        sys.exit("The vendored dist is not what the pin says. Re-vendor from the pinned tag; never edit a hash.")
    rows = unicode_data()
    return {OUT: emit(nfc_tables(rows), compat_tables(rows), simple_folds(), white_space(rows), pin["tag"])}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if any output would change")
    a = ap.parse_args()
    pin = read_pin()
    drift = 0
    for path, text in outputs().items():
        print(f"dist OK: {pin['tag']}, MANIFEST.json sha256 {pin['manifest_sha256'][:12]}... matches the pin")
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
        print("no drift" if not drift else f"{drift} file(s) drifted: run codegen/generate.py")
    return 1 if drift else 0


if __name__ == "__main__":
    sys.exit(main())
