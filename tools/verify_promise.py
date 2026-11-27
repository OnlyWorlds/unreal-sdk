#!/usr/bin/env python3
"""Hold the plugin to its written promise (the docs' Stability page, "The standard, the packages and this site").

A patch release fixes and a minor release adds; neither removes nor renames a public name. A name that will go is
marked first: in C++ `[[deprecated("Since X.Y.Z. Leaves in 1.0. Use Y.")]]` (or UE_DEPRECATED with the same
message), on a Blueprint node `meta=(DeprecatedFunction, DeprecationMessage="Since X.Y.Z. Leaves in 1.0. Use Y.")`;
the mark says since which release and the release it leaves in; the name stays at least six months after that release
and goes only in a major (1.0 for this 0.x plugin). Three rules, against the names file `tools/public-names.json`:

1. Marks. Every mark on a public name says `Since X.Y.Z` (a release at or before this one, or the next one while the
   CHANGELOG has an Unreleased section) and `Leaves in N.0`, N a major above this one.
2. Names. Within the major the file was written at, every name it lists is still public (declared, or reachable
   through an in-tree base), every public name is listed, and the file records each name's mark as the source has it.
   A release that adds names or marks runs `--write`.
3. Six months. When the major has moved, a listed name gone from the source was marked in the file, and the release
   its mark names is dated (the CHANGELOG's `## [x.y.z] - YYYY-MM-DD`) at least six months before this release.

A public name, read as text from the headers of Plugin/OnlyWorlds/Core/Public (the core) and
Plugin/OnlyWorlds/Source/OnlyWorlds/Public (the plugin): at namespace scope a function, a class or struct, an enum and
its enumerators, a constant, an alias, a delegate type (DECLARE_*_DELEGATE); in a class or struct, what sits under
`public:` (a struct starts public, a class private), nested types included. Comments, strings and preprocessor lines
are blanked first (the rule tools/check_exports.py reads by), and Unreal's reflection macros are read for their
deprecation meta and set aside. Function bodies are skipped. Overloads share a name, and a name counts as marked when
every declaration of it is. It is a reader of text, not a compiler: tools/mutants_promise.py watches it on plants.
Not checked: whether a mark's words are true, a call's parameters, a change of behaviour under an unchanged name. A
Blueprint property has no mark that keeps its name (Unreal's own renames it), so none is checked.

Usage:
  python tools/verify_promise.py           check the headers against the names file
  python tools/verify_promise.py --write   add new names and update the marks; a name leaves the file only when the
                                           major moved and rule 3 lets it go
Python's standard library only. Exit 0 PASS, 1 FAIL (one line per break), 2 BLIND (a header, the version, the
CHANGELOG or the names file could not be read): never a pass.
"""

from __future__ import annotations

import argparse
import calendar
import datetime
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get("OW_PROMISE_ROOT") or HERE.parent)
NAMES_FILE = Path(os.environ.get("OW_PROMISE_NAMES") or ROOT / "tools" / "public-names.json")
PLUGIN = ROOT / "Plugin" / "OnlyWorlds"
HEADERS = [PLUGIN / "Core" / "Public", PLUGIN / "Source" / "OnlyWorlds" / "Public"]
CHANGELOG = ROOT / "CHANGELOG.md"
SINCE = re.compile(r"\bsince (\d+\.\d+\.\d+)\b", re.IGNORECASE)
LEAVES = re.compile(r"\bleaves? in (\d+)\.(\d+)\b", re.IGNORECASE)
HEADING = re.compile(r"^## \[(\d+\.\d+\.\d+)\]\s*[-—]\s*(\d{4}-\d{2}-\d{2})", re.MULTILINE)
UNRELEASED = re.compile(r"^## \[Unreleased\]", re.MULTILINE | re.IGNORECASE)
ABOUT = (
    "Every public name of the OnlyWorlds SDK for Unreal and its deprecation: written by tools/verify_promise.py "
    "--write, read by it in CI. A name leaves this file only in a major."
)
MARK_WORDS = '[[deprecated("Since X.Y.Z. Leaves in 1.0. Use ...")]], or DeprecationMessage="..." on a Blueprint node'
OPTIONS = argparse.Namespace()


def engine_options(parser: argparse.ArgumentParser) -> None:
    """This engine's own options: none."""


class Blind(Exception):
    """Something the check needs could not be read: exit 2, never a pass."""


# ---- this engine: the version and the public names -------------------------------------------------------------


def read_version() -> str:
    try:
        version = json.loads((PLUGIN / "OnlyWorlds.uplugin").read_text(encoding="utf-8-sig"))["VersionName"]
    except (OSError, ValueError, KeyError) as exc:
        raise Blind(f"OnlyWorlds.uplugin: no VersionName ({exc})") from exc
    if not re.fullmatch(r"\d+\.\d+\.\d+", str(version)):
        raise Blind(f"OnlyWorlds.uplugin: VersionName {version!r} is not X.Y.Z")
    return version


def masked(text: str) -> str:
    """The text with comments, string and character literals and preprocessor lines blanked, offsets kept."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "\n":
            line_start = True
            i += 1
            continue
        if not c.isspace():
            line_start = False
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != "\n":
                    out[k] = " "
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
            continue
        i += 1
    return "".join(out)


UE_MACRO = re.compile(r"\b(UCLASS|USTRUCT|UENUM|UFUNCTION|UPROPERTY|UMETA|UINTERFACE|UDELEGATE)\s*\(")
GENERATED = re.compile(r"\b(GENERATED_BODY|GENERATED_UCLASS_BODY|GENERATED_USTRUCT_BODY)\s*\(\s*\)")
EXPORT = re.compile(r"\b(OW_API|ONLYWORLDS_API)\b")
DEPRECATED_ATTR = re.compile(r"\[\[deprecated(?:\([^)]*\))?\]\]|\bUE_DEPRECATED\s*\([^)]*\)")
TYPE_HEAD = re.compile(r"(?:template\s*<[^{;]*>\s*)?(struct|class|union)\s+(?:alignas\([^)]*\)\s*)?(\w+)\s*(?:final\s*)?(?::([^{]*))?$")
ENUM_HEAD = re.compile(r"enum\s+(?:class\s+|struct\s+)?(\w+)\s*(?::[^{]*)?$")
FUNCTION_NAME = re.compile(r"(operator\s*[^\s(]+|~?\w+)\s*$")


def blank(text: str, a: int, b: int) -> str:
    return text[:a] + "".join(ch if ch == "\n" else " " for ch in text[a:b]) + text[b:]


def scan(path: Path, decls: list, members: dict, bases: dict) -> None:
    raw = path.read_bytes().decode("utf-8")
    m = masked(raw)
    attrs = [(h.start(), " ".join(raw[h.start():h.end()].split())) for h in DEPRECATED_ATTR.finditer(m)]
    for h in reversed(list(DEPRECATED_ATTR.finditer(m))):
        m = blank(m, h.start(), h.end())
    ue: dict[int, str] = {}  # where a UE macro's arguments end -> the arguments, as written
    for hit in reversed(list(UE_MACRO.finditer(m))):
        i, depth = hit.end(), 1
        while i < len(m) and depth:
            depth += {"(": 1, ")": -1}.get(m[i], 0)
            i += 1
        ue[i] = " ".join(raw[hit.end():i - 1].split())
        m = blank(m, hit.start(), i)
    for pattern in (GENERATED, EXPORT):
        for h in reversed(list(pattern.finditer(m))):
            m = blank(m, h.start(), h.end())
    ue_at = sorted(ue)
    stack: list[dict] = []
    start = None
    last = 0  # where the statement before the one in hand ended

    def path_name() -> str:
        return "::".join(f["name"] for f in stack if f["kind"] in ("ns", "class", "enum") and f["name"])

    def scope():
        return next((f for f in reversed(stack) if f["kind"] != "init"), None)

    def visible() -> bool:
        frames = [f for f in stack if f["kind"] != "init"]
        if frames and frames[-1]["kind"] not in ("ns", "class"):
            return False
        return all(f["kind"] == "ns" or f.get("access") == "public" for f in frames)

    def mark(lo: int, hi: int) -> str | None:
        for o, text in attrs:
            if last <= o <= hi:
                said = re.search(r'"(.*)"', text)
                return said.group(1) if said else ""
        for k in ue_at:
            if last <= k <= hi:  # a macro between the statement before and this one's end: a label may come first
                args = ue[k]
                if "DeprecatedFunction" in args or "DeprecationMessage" in args:
                    said = re.search(r'DeprecationMessage\s*=\s*"([^"]*)"', args)
                    return said.group(1) if said else ""
        return None

    def put(name: str, lo: int, hi: int) -> None:
        owner = path_name()
        full = f"{owner}::{name}" if owner else name
        decls.append((full, mark(lo, hi)))
        if owner:
            members.setdefault(owner, []).append(name)

    def strip_labels(stmt: str, frame) -> str:
        while True:
            lm = re.match(r"(public|private|protected)\s*:\s*", stmt)
            if not lm:
                return stmt
            if frame is not None and frame["kind"] == "class":
                frame["access"] = lm.group(1)
            stmt = stmt[lm.end():]

    for i, c in enumerate(m):
        if start is None and not c.isspace() and c not in "{};":
            start = i
        if c == "{":
            s = scope()
            stmt = strip_labels(" ".join(m[start:i].split()) if start is not None else "", s)
            before = m[:i].rstrip()
            prev = before[-1:] if before else ""
            if s is not None and s["kind"] in ("body", "enum"):
                stack.append({"kind": "body", "name": None})
                start = None
                continue
            head, ehead = TYPE_HEAD.match(stmt), ENUM_HEAD.match(stmt)
            if re.match(r"namespace\b", stmt):
                stack.append({"kind": "ns", "name": re.match(r"namespace\s+([\w:]*)", stmt).group(1)})
                start = None
            elif head:
                if visible():
                    put(head.group(2), start, i)
                    if head.group(3):
                        full = (path_name() + "::" if path_name() else "") + head.group(2)
                        bases[full] = [re.sub(r"<.*", "", b.split()[-1]).split("::")[-1]
                                       for b in head.group(3).split(",") if b.strip()]
                stack.append({"kind": "class", "name": head.group(2), "access": "private" if head.group(1) == "class" else "public"})
                start = None
            elif ehead:
                if visible():
                    put(ehead.group(1), start, i)
                stack.append({"kind": "enum", "name": ehead.group(1), "access": "public", "body_start": i + 1})
                start = None
            elif stmt and (prev in "=(,[" or ((prev.isalnum() or prev == "_")
                                              and not re.search(r"(\)|\bconst|\bnoexcept|\boverride|\bfinal)\s*$", stmt))):
                stack.append({"kind": "init", "name": None})  # a brace initializer: the statement goes on
            else:
                if stmt and "(" in stmt and visible() and not re.match(r"(if|for|while|switch|return)\b", stmt):
                    found = FUNCTION_NAME.search(stmt[:stmt.index("(")])
                    if found:
                        put(found.group(1), start, i)
                stack.append({"kind": "body", "name": None})
                start = None
            last = i
        elif c == "}":
            frame = stack.pop() if stack else None
            if frame and frame["kind"] == "init":
                continue
            if frame and frame["kind"] == "enum" and visible():
                for e in m[frame["body_start"]:i].split(","):
                    en = re.match(r"\s*(\w+)", e)
                    if en:
                        put(f"{frame['name']}::{en.group(1)}", frame["body_start"], frame["body_start"])
            start = None
            last = i
        elif c == ";":
            s = scope()
            if start is not None and s is not None and s["kind"] in ("body", "enum"):
                start = None
                continue
            if start is not None:
                stmt = strip_labels(" ".join(m[start:i].split()), s)
                if stmt and visible():
                    if re.match(r"(friend|static_assert|typedef)\b", stmt) or re.match(r"(struct|class|enum)\s+\w+$", stmt):
                        pass
                    elif re.match(r"using\s+namespace\b", stmt):
                        pass
                    elif re.match(r"using\s+(\w+)\s*=", stmt):
                        put(re.match(r"using\s+(\w+)", stmt).group(1), start, i)
                    elif "(" in stmt and not re.search(r"=\s*[^=]*\(", stmt.split("(")[0] + "("):
                        found = FUNCTION_NAME.search(stmt[:stmt.index("(")])
                        if found and found.group(1).startswith("DECLARE_"):
                            delegate = re.search(r"\(\s*(\w+)", stmt)
                            if delegate:
                                put(delegate.group(1), start, i)
                        elif found:
                            put(found.group(1), start, i)
                    else:
                        value = re.search(r"(\w+)\s*$", re.split(r"[=\[{]", stmt, maxsplit=1)[0].strip())
                        if value:
                            put(value.group(1), start, i)
            start = None
            last = i


def read_names() -> tuple[list[tuple[str, str | None]], set[str]]:
    decls: list = []
    members: dict[str, list[str]] = {}
    bases: dict[str, list[str]] = {}
    for folder in HEADERS:
        headers = sorted(folder.glob("*.h"))
        if not headers:
            raise Blind(f"{folder}: no headers")
        for header in headers:
            if header.name == "OWApi.h":
                continue
            try:
                scan(header, decls, members, bases)
            except (OSError, UnicodeDecodeError) as exc:
                raise Blind(f"{header.name}: {exc}") from exc
    by_short: dict[str, str] = {}
    for owner in list(members) + list(bases):
        by_short.setdefault(owner.split("::")[-1], owner)
    reach: set[str] = set()
    for derived in bases:
        seen, todo = {derived}, list(bases[derived])
        while todo:
            base = by_short.get(todo.pop())
            if not base or base in seen:
                continue
            seen.add(base)
            reach.update(f"{derived}::{m}" for m in members.get(base, []))
            todo.extend(bases.get(base, []))
    return decls, reach


# ---- the rules (the same in the three engine packages) -----------------------------------------------------------


def parse_version(v: str) -> tuple[int, int, int]:
    a, b, c = (int(x) for x in v.split("."))
    return a, b, c


def six_months_after(day: datetime.date) -> datetime.date:
    month = day.month + 6
    year, month = day.year + (month - 1) // 12, (month - 1) % 12 + 1
    return datetime.date(year, month, min(day.day, calendar.monthrange(year, month)[1]))


def changelog() -> tuple[dict[str, datetime.date], bool]:
    try:
        text = CHANGELOG.read_text(encoding="utf-8")
    except OSError as exc:
        raise Blind(f"{CHANGELOG.name}: {exc}") from exc
    return {v: datetime.date.fromisoformat(d) for v, d in HEADING.findall(text)}, bool(UNRELEASED.search(text))


def judge_mark(text: str | None, version: str, unreleased: bool) -> tuple[str | None, str | None, list[str]]:
    """Rule 1 for one mark: (since, leaves, failures)."""
    fails: list[str] = []
    if text is None:
        return None, None, []
    if text.startswith("\u0000"):
        return None, None, [f"its message is not a string this check can read ({text[1:][:80]}): write it as a literal or a const string"]
    major = parse_version(version)[0]
    since = None
    sinces = SINCE.findall(text)
    if not sinces:
        fails.append(f'its message says no "Since X.Y.Z" ({text[:90]!r}); the words are {MARK_WORDS}')
    elif len(set(sinces)) > 1:
        fails.append(f"its message names more than one release it was marked in ({', '.join(sorted(set(sinces)))})")
    else:
        since = sinces[0]
        s, v = parse_version(since), parse_version(version)
        nexts = {(v[0], v[1], v[2] + 1), (v[0], v[1] + 1, 0)}
        if s > v and not (unreleased and s in nexts):
            fails.append(f"marked since {since}, after this release {version} and not the next one")
    leaves = None
    found = [(int(a), int(b)) for a, b in LEAVES.findall(text)]
    above = sorted(f for f in found if f[0] > major and f[1] == 0)
    if not found:
        fails.append(f'its message says no "Leaves in N.0" ({text[:90]!r}); the words are {MARK_WORDS}')
    elif not above:
        fails.append(f"it leaves in {found[0][0]}.{found[0][1]}, not in a major above {major} (N.0)")
    else:
        leaves = f"{above[0][0]}.0"
    return since, leaves, fails


def run(writing: bool) -> int:
    version = read_version()
    major = parse_version(version)[0]
    decls, reach = read_names()
    dates, unreleased = changelog()
    today = datetime.date.fromisoformat(os.environ["OW_PROMISE_TODAY"]) if os.environ.get("OW_PROMISE_TODAY") else datetime.date.today()

    fails: list[str] = []
    by_name: dict[str, list[str | None]] = {}
    for name, mark in decls:
        by_name.setdefault(name, []).append(mark)
    source: dict[str, dict] = {}
    for name, marks in sorted(by_name.items()):
        record: dict = {}
        judged = []
        for mark in marks:
            since, leaves, bad = judge_mark(mark, version, unreleased)
            fails += [f"(1) {name}: {b}" for b in bad]
            judged.append((mark, since, leaves))
        if all(m is not None for m, _, _ in judged):
            since, leaves = judged[0][1], judged[0][2]
            record = {"deprecated": {"since": since, "leaves": leaves}}
        source[name] = record
    present = set(source) | reach

    file = None
    if NAMES_FILE.exists():
        try:
            file = json.loads(NAMES_FILE.read_text(encoding="utf-8"))
        except ValueError as exc:
            raise Blind(f"{NAMES_FILE.name} is not JSON: {exc}") from exc
        if not (isinstance(file, dict) and isinstance(file.get("major"), int) and isinstance(file.get("names"), dict)
                and isinstance(file.get("version"), str)):
            raise Blind(f"{NAMES_FILE.name} has no version, major or names")
        if file["major"] > major:
            raise Blind(f"{NAMES_FILE.name} was written at {file['version']}, a later major than {version}")
    elif not writing:
        raise Blind(f"{NAMES_FILE} does not exist: run with --write once")

    released: set[str] = set()  # names a moved major may drop
    held_back = False
    if file:
        moved = file["major"] != major
        this_day = dates.get(version, today)
        for name, info in sorted(file["names"].items()):
            if name in present:
                continue
            if not moved:
                fails.append(f"(2) {name}: listed at {file['version']}, gone from {version}, and the major is still {major}")
                continue
            since = (info.get("deprecated") or {}).get("since")
            if not since:
                fails.append(f"(3) {name}: gone in major {major} without a mark first")
            elif since not in dates:
                fails.append(f"(3) {name}: marked since {since}, a release {CHANGELOG.name} does not date")
            elif six_months_after(dates[since]) > this_day:
                fails.append(f"(3) {name}: marked since {since} ({dates[since]}), gone {this_day}, before {six_months_after(dates[since])}")
            else:
                released.add(name)
                continue
            held_back = True
        if not writing:
            for name, record in source.items():
                if name not in file["names"]:
                    fails.append(f"(2) {name}: public in {version} and not in {NAMES_FILE.name}: run --write")
                elif file["names"][name] != record and not moved:  # a major rewrites the marks: --write
                    fails.append(f"(2) {name}: the source marks it {record or 'not at all'}, the names file {file['names'][name] or 'not at all'}: run --write")

    count, marked = len(source), sum(1 for r in source.values() if r)
    if writing:
        keep = {} if not file else {n: r for n, r in file["names"].items() if n not in released and n not in source}
        names = {**keep, **source}
        back = held_back and file is not None and file["major"] != major
        out = {"about": ABOUT, "version": file["version"] if back else version, "major": file["major"] if back else major,
               "names": dict(sorted(names.items()))}
        with NAMES_FILE.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(out, indent=1, ensure_ascii=True) + "\n")
        print(f"wrote {NAMES_FILE.name}: {len(names)} names, {marked} marked")
    for f in fails:
        print(f"  FAIL {f}")
    if fails:
        print(f"FAIL: {len(fails)} break(s) of the promise in {version}.")
        return 1
    print(f"PASS: {count} public names in {version}, {marked} marked deprecated, each with its since and the release it "
          f"leaves in; none gone within major {major}. Not checked: a mark's other words, a call's parameters, "
          f"behaviour under an unchanged name.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--write", action="store_true", help="add new names and update the marks in the names file")
    engine_options(parser)
    args = parser.parse_args()
    vars(OPTIONS).update(vars(args))
    try:
        return run(args.write)
    except Blind as exc:
        print(f"BLIND: {exc}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
