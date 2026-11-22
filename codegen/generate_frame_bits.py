#!/usr/bin/env python3
"""The map-space fixture's frame cases' numbers as bits -> tests/data/map-space-frames.bits.json, for
tests/test_map_space.cpp (OWMapSpace::CarryOf and OWMapSpace::NumberOf).

The frame reading's answer is compared exactly, as 64-bit floats (profiles/map-space.md, rule 14), so a carried point
is only checked against its want when the core's JSON reader gives every number of the case the double Python's json
gives it (Python reads a decimal exactly: the nearest double). The bits file holds, for each case of the vendored
profiles/map-space.fixture.json `frame_cases`, every number Python reads as a float (a decimal point or an exponent)
anywhere in the case's `elements`, `point` and `want`, as a path into the case and 16 hex digits (big-endian).
Numbers written as integers parse exactly and are left to the fixture. It names the fixture's sha256 and its count of
frame cases. The same file, byte for byte, as the Godot SDK's generator writes from the same fixture.

    python codegen/generate_frame_bits.py           write it
    python codegen/generate_frame_bits.py --check   exit 1 if it is stale

Standard library only. Written LF-only, as the repository's .gitattributes keeps it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "codegen" / "schema-dist" / "profiles" / "map-space.fixture.json"
BITS = ROOT / "tests" / "data" / "map-space-frames.bits.json"


def hexbits(v: float) -> str:
    return struct.pack(">d", float(v)).hex()


def floats_in(value, path: list) -> list:
    """Every float (not an int, not a bool) under value, as [path, hex], in document order."""
    if isinstance(value, float):
        return [[path, hexbits(value)]]
    out = []
    if isinstance(value, list):
        for i, v in enumerate(value):
            out += floats_in(v, path + [i])
    elif isinstance(value, dict):
        for k, v in value.items():
            out += floats_in(v, path + [k])
    return out


def render() -> str:
    fixture_bytes = FIXTURE.read_bytes()
    fx = json.loads(fixture_bytes.decode("utf-8"))
    cases = {}
    for c in fx["frame_cases"]:
        if c["name"] in cases:
            raise ValueError(f"two frame cases are named {c['name']}")
        floats = []
        for key in ("elements", "point", "want"):
            floats += floats_in(c[key], [key])
        cases[c["name"]] = floats
    bits = {
        "fixture_sha256": hashlib.sha256(fixture_bytes).hexdigest(),
        "frame_cases": len(fx["frame_cases"]),
        "cases": cases,
    }
    return json.dumps(bits, indent=1, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="exit 1 if the output is stale")
    args = parser.parse_args()
    text = render()
    if args.check:
        if not BITS.exists() or BITS.read_bytes().decode("utf-8") != text:
            print(f"stale: {BITS.relative_to(ROOT)} (run python codegen/generate_frame_bits.py)", file=sys.stderr)
            return 1
        print("ok: the frame bits file matches codegen/schema-dist/profiles/map-space.fixture.json")
        return 0
    BITS.write_bytes(text.encode("utf-8"))
    print(f"wrote {BITS.relative_to(ROOT)} ({len(text.encode('utf-8'))} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
