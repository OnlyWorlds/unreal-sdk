"""Checks that every declaration in Plugin/OnlyWorlds/Core/Public that a module outside the core calls carries OW_API.

In Unreal's modular (editor) build the core lives in the plugin's DLL, and a game module links against only what that
DLL exports. A declaration without the mark compiles everywhere and fails only when a game module links against it,
which nothing in this repository's CI builds; this check is that failure, early.

The rule, at namespace scope of each header (OWApi.h itself excepted):
  - a class or struct needs the mark when it has a member function declared without a body (other than `= default`,
    `= delete` or `= 0`) or a static data member that is not constexpr; one mark on the class covers its members;
  - a function declared without a body needs it (one mark per declaration, each overload its own);
  - an `extern` constant needs it;
  - a template, an inline function or a function with a body, a constexpr value, an alias, an enum and a forward
    declaration need none.
A class nested inside a class is covered by its outer class's mark. An attribute (`[[deprecated("...")]]`) is read as
no part of the declaration it stands before: the mark goes after it.

  python tools/check_exports.py           exit 0 when every mark is there, 1 (and each missing one listed) when not
  python tools/check_exports.py --write   inserts the missing marks (and the include of OWApi.h), bytes kept as read
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PUBLIC = ROOT / "Plugin" / "OnlyWorlds" / "Core" / "Public"
MARK = "OW_API"
INCLUDE = '#include "OWApi.h"'


def masked(text):
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
    # An attribute ([[deprecated("...")]], its words already blanked) is no part of the declaration it stands before.
    masked_text = "".join(out)
    for a in reversed(list(ATTRIBUTE.finditer(masked_text))):
        for k in range(a.start(), a.end()):
            if masked_text[k] != "\n":
                out[k] = " "
    return "".join(out)


ATTRIBUTE = re.compile(r"\[\[.*?\]\]", re.DOTALL)


BODY_END = re.compile(r"(\)|\bconst|\bnoexcept|\boverride|\bfinal)\s*$")
SKIP = re.compile(r"(using|typedef|template|inline|constexpr|static_assert|enum|friend)\b")
TYPE_HEAD = re.compile(r"(struct|class)\s+(?:OW_API\s+)?(\w+)\s*(?::[^{]*)?$")


def scan(text):
    """Every declaration the rule names: (offset where OW_API goes, whether it is there, a label)."""
    m = masked(text)
    found = []
    stack = []  # frames: {"kind": ns|class|body|init, ...}
    start = None  # where the statement in hand starts

    def scope():
        for f in reversed(stack):
            if f["kind"] != "init":
                return f
        return None

    def at_namespace():
        s = scope()
        return s is None or s["kind"] == "ns"

    def member(stmt, cls):
        if "(" in stmt and ("=" not in stmt or stmt.index("(") < stmt.index("=")):
            if re.search(r"=\s*(default|delete|0)\s*$", stmt) or re.match(r"(using|friend)\b", stmt):
                return
            cls["needs"] = True
        elif re.match(r"static\b", stmt) and "constexpr" not in stmt:
            cls["needs"] = True

    for i, c in enumerate(m):
        if start is None and not c.isspace() and c not in "{};":
            start = i
        if c == "{":
            stmt = " ".join(m[start:i].split()) if start is not None else ""
            prev = m[:i].rstrip()[-1:] if m[:i].rstrip() else ""
            head = TYPE_HEAD.match(stmt)
            if re.match(r"namespace\b", stmt):
                stack.append({"kind": "ns"})
                start = None
            elif head and not re.match(r"template\b", stmt):
                frame = {"kind": "class", "name": head.group(2), "needs": False}
                kw = re.search(r"\b(struct|class)\s+", m[start:i])
                frame["at"] = start + kw.end()
                frame["has"] = m[start:i][kw.end():].lstrip().startswith(MARK)
                stack.append(frame)
                start = None
            elif stmt and not re.match(r"(enum|template)\b", stmt) and (prev in "=(,[" or (prev.isalnum() or prev == "_")
                                                                         and not BODY_END.search(stmt)):
                stack.append({"kind": "init"})  # a brace initializer: the statement goes on
            else:
                stack.append({"kind": "body"})
                start = None
        elif c == "}":
            frame = stack.pop() if stack else None
            if frame and frame["kind"] == "init":
                continue
            if frame and frame["kind"] == "class" and frame["needs"]:
                found.append((frame["at"], frame["has"], frame["name"]))
            start = None
        elif c == ";":
            if start is not None:
                raw = m[start:i]
                stmt = " ".join(raw.split())
                s = scope()
                if s is not None and s["kind"] == "class":
                    member(stmt, s)
                elif at_namespace() and stmt:
                    if re.match(r"extern\b", stmt):
                        kw = re.match(r"extern\s+", raw)
                        found.append((start + kw.end(), raw[kw.end():].startswith(MARK), stmt))
                    elif SKIP.match(stmt) or re.match(r"(struct|class)\s+\w+$", stmt):
                        pass
                    elif "(" in stmt:
                        found.append((start, stmt.startswith(MARK), stmt))
            start = None
    return found


def with_include(text):
    if INCLUDE in text:
        return text
    lines = text.splitlines(keepends=True)
    eol = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"
    quoted = [k for k, ln in enumerate(lines) if ln.startswith('#include "')]
    if quoted:
        at = next((k for k in quoted if lines[k] > INCLUDE), quoted[-1] + 1)
        lines.insert(at, INCLUDE + eol)
    else:
        system = [k for k, ln in enumerate(lines) if ln.startswith("#include <")]
        if system:
            lines[system[0]:system[0]] = [INCLUDE + eol, eol]
        else:
            once = next(k for k, ln in enumerate(lines) if ln.startswith("#pragma once"))
            lines[once + 1:once + 1] = [eol, INCLUDE + eol]
    return "".join(lines)


def main(argv):
    write = "--write" in argv
    missing = 0
    total = 0
    for path in sorted(PUBLIC.glob("*.h")):
        if path.name == "OWApi.h":
            continue
        text = path.read_bytes().decode("utf-8")
        found = scan(text)
        total += len(found)
        lack = [f for f in found if not f[1]]
        missing += len(lack)
        if found and INCLUDE not in text:
            print(f"{path.name}: does not include OWApi.h")
            missing += 1
        if write and (lack or (found and INCLUDE not in text)):
            for at, _, _ in sorted(lack, reverse=True):
                text = text[:at] + MARK + " " + text[at:]
            path.write_bytes(with_include(text).encode("utf-8"))
        elif not write:
            for at, _, label in lack:
                line = text.count("\n", 0, at) + 1
                print(f"{path.name}:{line}: no {MARK}: {label}")
    if write:
        print(f"{total} declarations need the mark; {missing} were missing and are written")
        return 0
    print(f"{total} declarations need the mark; {missing} missing")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
