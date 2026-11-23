#!/usr/bin/env python3
"""The zip a person downloads: onlyworlds-unreal-<version>.zip, made from a commit or a tag, never the working folder.

    python tools/release_zip.py v0.1.0              # the tag: refused unless the .uplugin there says 0.1.0
    python tools/release_zip.py HEAD --out dist     # any commit

The zip holds one folder, OnlyWorlds/, to drop into a project's Plugins/ folder: Plugin/OnlyWorlds/ as the named
commit has it (its tracked files, so never a build's Binaries/ or Intermediate/), and a copy of the repository's
LICENSE inside it. It is a source plugin: the project that takes it compiles it. Left out: the plugin's automation
tests (Source/OnlyWorlds/Private/Tests/). They need the repository's fixtures and run from a clone, and in another
project they would compile to nothing but two test-only classes reflected into every target. Nothing outside that
folder includes a file of it or names one of its classes.

The version is OnlyWorlds.uplugin's VersionName, read at that commit. A tag must be vX.Y.Z and say the same version, or
nothing is written. The file is the same bytes for the same commit: entries in path order, each dated with the
commit's time. Prints the zip's path, its file count, its size and its sha256. Python's standard library and git,
nothing else.
"""

import argparse
import datetime
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN = "Plugin/OnlyWorlds"
FOLDER = "OnlyWorlds"
TESTS = PLUGIN + "/Source/OnlyWorlds/Private/Tests/"   # left out (above)


def git(*args: str) -> bytes:
    r = subprocess.run(["git", "-C", ROOT, *args], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("git %s: %s" % (" ".join(args), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout


def blob(sha: str, path: str) -> bytes:
    return git("cat-file", "blob", "%s:%s" % (sha, path))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ref", help="a tag vX.Y.Z, or a commit")
    ap.add_argument("--out", default="dist", help="the folder the zip is written to (default: dist)")
    a = ap.parse_args()

    tag = None
    if subprocess.run(["git", "-C", ROOT, "rev-parse", "-q", "--verify", "refs/tags/" + a.ref],
                      capture_output=True).returncode == 0:
        tag = a.ref
    r = subprocess.run(["git", "-C", ROOT, "rev-parse", "-q", "--verify", a.ref + "^{commit}"], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("refused: no tag or commit is named %s" % a.ref)
    sha = r.stdout.decode().strip()

    uplugin = json.loads(blob(sha, PLUGIN + "/OnlyWorlds.uplugin").decode("utf-8"))
    version = uplugin.get("VersionName")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("OnlyWorlds.uplugin's VersionName %r is not X.Y.Z" % (version,))
    if tag is not None:
        if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
            raise SystemExit("refused: the tag %s is not vX.Y.Z" % tag)
        if tag[1:] != version:
            raise SystemExit("refused: the tag says %s, OnlyWorlds.uplugin at %s says %s" % (tag[1:], sha[:7], version))

    paths = [p for p in git("ls-tree", "-r", "--name-only", "-z", sha, "--", PLUGIN + "/").decode("utf-8").split("\0") if p]
    if not paths:
        raise SystemExit("%s holds no files at %s" % (PLUGIN, sha[:7]))
    left_out = [p for p in paths if p.startswith(TESTS)]
    paths = [p for p in paths if not p.startswith(TESTS)]
    files = {FOLDER + p[len(PLUGIN):]: blob(sha, p) for p in paths}
    files[FOLDER + "/LICENSE"] = blob(sha, "LICENSE")

    when = datetime.datetime.fromtimestamp(int(git("log", "-1", "--format=%ct", sha).decode().strip()),
                                           datetime.timezone.utc)
    stamp = (when.year, when.month, when.day, when.hour, when.minute, when.second)

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for p in sorted(files):
            info = zipfile.ZipInfo(p, stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            info.create_system = 3   # unix, so the mode above is read on unpack
            z.writestr(info, files[p], compresslevel=9)
    data = buf.getvalue()

    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, "onlyworlds-unreal-%s.zip" % version)
    with open(out, "wb") as f:
        f.write(data)

    print("zip:     %s" % out)
    print("from:    %s%s" % (sha, " (tag %s)" % tag if tag else " (a commit, not a tag)"))
    print("version: %s" % version)
    print("files:   %d (%d test files left out)" % (len(files), len(left_out)))
    print("size:    %d bytes" % len(data))
    print("sha256:  %s" % hashlib.sha256(data).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
