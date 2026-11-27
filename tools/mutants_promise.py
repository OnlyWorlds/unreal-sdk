#!/usr/bin/env python3
"""Watch tools/verify_promise.py fail, once for each thing it holds, and pass on its controls.

Each plant runs the check on a copy of what it reads (the core's and the plugin's public headers, OnlyWorlds.uplugin,
CHANGELOG.md and tools/public-names.json), never on the real files, with one change made to the copy. The controls
that count marks count them above the tree's own (read first from an unchanged copy), and a plant that moves the major
first takes the tree's own marks off, so that only the planted name is judged there.

Usage: python tools/mutants_promise.py
Exit 0 when every plant failed as named and every control passed, 1 otherwise. Python's standard library only.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CHECK = HERE / "verify_promise.py"
CORE = Path("Plugin/OnlyWorlds/Core/Public")
NODES = Path("Plugin/OnlyWorlds/Source/OnlyWorlds/Public")
MINT = "OW_API std::string Mint("
GOOD = "Since 0.6.0. Leaves in 1.0. Use Keyed."
UFUNC = 'UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Address")\n\tstatic bool ParseAddress('


def copy() -> Path:
    d = Path(tempfile.mkdtemp(prefix="ow-promise-"))
    for folder in (CORE, NODES):
        shutil.copytree(ROOT / folder, d / folder)
    shutil.copy2(ROOT / "Plugin/OnlyWorlds/OnlyWorlds.uplugin", d / "Plugin/OnlyWorlds/OnlyWorlds.uplugin")
    shutil.copy2(ROOT / "CHANGELOG.md", d / "CHANGELOG.md")
    (d / "tools").mkdir()
    shutil.copy2(HERE / "public-names.json", d / "tools" / "public-names.json")
    return d


def edit(rel, fn):
    def plant(d: Path) -> None:
        p = d / rel
        text = p.read_bytes().decode("utf-8")
        crlf = "\r\n" in text
        out = fn(text.replace("\r\n", "\n"))
        p.write_bytes((out.replace("\n", "\r\n") if crlf else out).encode("utf-8"))
    return plant


def once(text: str, old: str, new: str) -> str:
    if old not in text:
        raise RuntimeError(f"plant anchor not found: {old}")
    return text.replace(old, new, 1)


def cpp_mark(attr: str):
    """OWId::Mint marked with `attr` (a [[deprecated]] or a UE_DEPRECATED)."""
    return edit(CORE / "OWId.h", lambda t: once(t, MINT, f"{attr} {MINT}"))


def blueprint_mark(meta: str):
    """The ParseAddress node marked with `meta` in its UFUNCTION."""
    return edit(NODES / "OWAddressLibrary.h", lambda t: once(t, UFUNC, UFUNC.replace('"OnlyWorlds|Address")', f'"OnlyWorlds|Address", {meta})')))


def names(fn):
    return edit("tools/public-names.json", lambda t: json.dumps(fn(json.loads(t)), indent=1) + "\n")


def ghost(since: str | None):
    def put(f):
        f["names"]["OnlyWorlds::GhostName"] = {"deprecated": {"since": since, "leaves": "1.0"}} if since else {}
        return f
    return names(put)


def record(name: str):
    def put(f):
        f["names"][name] = {"deprecated": {"since": "0.6.0", "leaves": "1.0"}}
        return f
    return names(put)


TREE_MARK = re.compile(r'\[\[deprecated(?:\("[^"]*"\))?\]\]\s*|\bUE_DEPRECATED\s*\([^)]*\)\s*')


def unmark(d: Path) -> None:
    """The tree's own marks taken off the copy's headers: a 1.0 would have dropped those names, which a plant cannot."""
    for folder in (CORE, NODES):
        for p in (d / folder).rglob("*.h"):
            text = p.read_bytes().decode("utf-8")
            out = TREE_MARK.sub("", text)
            if out != text:
                p.write_bytes(out.encode("utf-8"))


def major1(date: str):
    def plant(d: Path) -> None:
        unmark(d)
        edit("Plugin/OnlyWorlds/OnlyWorlds.uplugin", lambda t: re.sub(r'"VersionName": "[^"]+"', '"VersionName": "1.0.0"', t, count=1))(d)
        edit("CHANGELOG.md", lambda t: once(t, "## [Unreleased]", f"## [Unreleased]\n\n## [1.0.0] - {date}"))(d)
    return plant


def both(*plants):
    return lambda d: [p(d) for p in plants]


def unlist(f):
    del f["names"]["OnlyWorlds::OWClient"]
    return f


def reached(f):
    f["names"]["UOWClientBulk::SetTransport"] = {}  # declared on UOWClientNode, its base: still reachable
    return f


PLANTS = [
    ("a listed name gone within the major", ghost(None), 1, r"\(2\) OnlyWorlds::GhostName: listed at"),
    ("a public name the file does not list", names(unlist), 1, r"\(2\) OnlyWorlds::OWClient: public in .* not in public-names\.json"),
    ("a renamed function", edit(CORE / "OWId.h", lambda t: once(t, MINT, "OW_API std::string MintId(")), 1,
     r"\(2\) OnlyWorlds::OWId::Mint: listed at"),
    ("a mark the file does not record", cpp_mark(f'[[deprecated("{GOOD}")]]'), 1, r"\(2\) OnlyWorlds::OWId::Mint: the source marks it"),
    ("a [[deprecated]] without its since", cpp_mark('[[deprecated("Leaves in 1.0. Use Keyed.")]]'), 1,
     r'\(1\) OnlyWorlds::OWId::Mint: its message says no "Since X\.Y\.Z"'),
    ("a UE_DEPRECATED without its since", cpp_mark('UE_DEPRECATED(5.8, "Leaves in 1.0. Use Keyed.")'), 1,
     r'\(1\) OnlyWorlds::OWId::Mint: its message says no "Since X\.Y\.Z"'),
    ("a [[deprecated]] with no words", cpp_mark("[[deprecated]]"), 1, r'\(1\) OnlyWorlds::OWId::Mint: its message says no "Since X\.Y\.Z"'),
    ("a mark that leaves in a minor", cpp_mark('[[deprecated("Since 0.6.0. Leaves in 0.9. Use Keyed.")]]'), 1,
     r"\(1\) OnlyWorlds::OWId::Mint: it leaves in 0\.9, not in a major above 0"),
    ("a mark since a release after the next one", cpp_mark('[[deprecated("Since 9.9.9. Leaves in 1.0.")]]'), 1,
     r"\(1\) OnlyWorlds::OWId::Mint: marked since 9\.9\.9, after this release"),
    ("a Blueprint node's DeprecatedFunction without a message", blueprint_mark("meta = (DeprecatedFunction)"), 1,
     r'\(1\) UOWAddresses::ParseAddress: its message says no "Since X\.Y\.Z"'),
    ("a major that drops an unmarked name", both(major1("2027-12-01"), ghost(None)), 1,
     r"\(3\) OnlyWorlds::GhostName: gone in major 1 without a mark first"),
    ("a major that drops a name under six months after its mark", both(major1("2027-05-22"), ghost("0.1.0")), 1,
     r"\(3\) OnlyWorlds::GhostName: marked since 0\.1\.0 \(2026-11-23\), gone 2027-05-22, before 2027-05-23"),
    ("control: a major that drops a name six months after its mark", both(major1("2027-05-23"), ghost("0.1.0")), 0, r"^PASS"),
    ("control: a well-formed [[deprecated]] the file records", both(cpp_mark(f'[[deprecated("{GOOD}")]]'), record("OnlyWorlds::OWId::Mint")), 0,
     r"^PASS: \d+ public names in [\d.]+, <BASE+1> marked"),
    ("control: a well-formed Blueprint DeprecationMessage the file records",
     both(blueprint_mark(f'meta = (DeprecatedFunction, DeprecationMessage = "{GOOD}")'), record("UOWAddresses::ParseAddress")), 0,
     r"^PASS: \d+ public names in [\d.]+, <BASE+1> marked"),
    ("control: one overload of two marked leaves the name unmarked",
     edit(CORE / "OWId.h", lambda t: once(t, "OW_API std::string NewId();", f'[[deprecated("{GOOD}")]] OW_API std::string NewId();')), 0,
     r"^PASS: \d+ public names in [\d.]+, <BASE> marked"),
    ("control: a listed member reached through its base class", names(reached), 0, r"^PASS"),
    ("control: the copy unchanged", lambda d: None, 0, r"^PASS"),
]


def check(d: Path) -> subprocess.CompletedProcess:
    env = {**os.environ, "OW_PROMISE_ROOT": str(d), "OW_PROMISE_NAMES": str(d / "tools" / "public-names.json")}
    return subprocess.run([sys.executable, str(CHECK)], env=env, capture_output=True, text=True, timeout=600)


def main() -> int:
    d = copy()
    try:
        r = check(d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    found = re.search(r"^PASS: \d+ public names in [\d.]+, (\d+) marked", r.stdout, re.MULTILINE)
    if not found:
        print(f"mutants-promise: BLIND: the check fails on an unchanged copy of the tree\n{(r.stdout + r.stderr)[:1500]}")
        return 2
    base = int(found.group(1))
    bad = 0
    for what, plant, want, line in PLANTS:
        line = line.replace("<BASE+1>", str(base + 1)).replace("<BASE>", str(base))
        d = copy()
        try:
            plant(d)
            r = check(d)
            out = r.stdout + r.stderr
            ok = r.returncode == want and re.search(line, out, re.MULTILINE) is not None
            bad += not ok
            print(f"{'as named' if ok else 'NOT AS NAMED'}: {what} (exit {r.returncode}, wanted {want})" + ("" if ok else f"\n{out[:1500]}"))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    plants = sum(1 for p in PLANTS if not p[0].startswith("control"))
    controls = len(PLANTS) - plants
    print(f"mutants-promise: FAIL: {bad} of {len(PLANTS)} not as named" if bad
          else f"mutants-promise: PASS: {plants} plants each failed as named; {controls} controls passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
