#!/usr/bin/env python3
"""Judge .owworld files with the reference checker's own reader and check_writer (folder spec 4.7).

    python tests/check_writer.py FILE [FILE ...] [--source private] [--run-py DIR]
    python tests/check_writer.py FILE --folder DIR       # a real world: the source is the folder on disk

The checker (`run_py.py`, the reference reader and writer check the fixture set was made with) is not part of this
repository; this imports it from DIR (--run-py, or the folder OW_RUN_PY names). `source` (the files a conforming writer must carry byte-exact) comes from the pinned set's
verdicts.json, or, with --folder, from the world folder itself: the sha256 of every file under its parts (world.json,
elements/, spatial/, media/, records/), read here and nowhere else, so a pack that dropped or changed a file is caught
by a reading independent of the writer. Prints one line per file; exits 1 unless every file is accepted by the reader
and by check_writer. (The Godot SDK's tools/check_writer.py, with --folder added.)
"""

import argparse
import hashlib
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VERDICTS = os.path.join(HERE, "..", "codegen", "schema-dist", "fixtures", "owworld", "verdicts.json")
DEFAULT_RUN_PY = os.environ.get("OW_RUN_PY", "")
PARTS = ("elements", "spatial", "media", "records")


def folder_source(folder: str) -> dict:
    files = {}
    world = os.path.join(folder, "world.json")
    files["world.json"] = hashlib.sha256(open(world, "rb").read()).hexdigest()
    for part in PARTS:
        base = os.path.join(folder, part)
        for dirpath, _, names in os.walk(base):
            for n in names:
                p = os.path.join(dirpath, n)
                rel = os.path.relpath(p, folder).replace(os.sep, "/")
                files[rel] = hashlib.sha256(open(p, "rb").read()).hexdigest()
    return {"files": files}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--source", help="one of verdicts.json's `sources` (default: the base world's `source`)")
    ap.add_argument("--folder", help="a world folder: the source is its files on disk")
    ap.add_argument("--run-py", default=DEFAULT_RUN_PY, help="the folder holding run_py.py")
    a = ap.parse_args()
    if not a.run_py:
        ap.error("no checker: pass --run-py DIR or set OW_RUN_PY to the folder holding run_py.py")
    sys.dont_write_bytecode = True  # read the checker, never write beside it
    spec = importlib.util.spec_from_file_location("run_py", os.path.join(a.run_py, "run_py.py"))
    run_py = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(run_py)
    doc = json.load(open(VERDICTS, encoding="utf-8"))
    if a.folder:
        source = folder_source(a.folder)
    else:
        source = doc["sources"][a.source] if a.source else doc["source"]
    marker = doc["marker"].encode("ascii")
    bad = 0
    for path in a.files:
        raw = open(path, "rb").read()
        try:
            read = run_py.read_archive(raw, marker)
        except run_py.Refused as r:
            print("%s: reader REFUSES (%s: %s)" % (path, r.reason, r))
            bad += 1
            continue
        fault = run_py.check_writer(raw, read, source)
        extra = sorted(set(read[3]) - set(source["files"]))
        if fault is None and extra:
            fault = "%d file(s) the source does not hold, first %s" % (len(extra), ascii(extra[0]))
        print("%s: reader accepts (world %s, %d files; the source %d); check_writer %s" % (
            path, read[0], len(read[3]), len(source["files"]), "ACCEPTS" if fault is None else "REFUSES: " + fault))
        bad += fault is not None
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
