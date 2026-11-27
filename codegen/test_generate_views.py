#!/usr/bin/env python3
"""generate_views.py: its check of views.json green on the vendored file, and each refusal red on a copy changed in one
place (a file of another version, a column OWViews does not read, a field its type lacks, a pair its profile's reader
does not match, an undefined layout, two rows of one kind, two rows of one id, a named field with no cite, a cite whose
quote is not at its line, a cite of a field its row does not name; and this SDK's own two: an empty word the table
would read as none, a character outside printable ASCII); --check green on the tree as committed and red on a one-byte
change to the generated table.

Each refusal changes a deep copy of the parsed file (or of the fixture) and calls `check` on it, so the vendored dist is
never touched; the pin's own check (verify_dist) refuses a changed byte before `check` is reached, which is why the
refusals are tested here on the parsed copy. --check runs in a temporary tree of the same shape.

    python codegen/test_generate_views.py
"""

from __future__ import annotations

import copy
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "codegen"))

import generate_views as gv  # noqa: E402

GENERATED = "Plugin/OnlyWorlds/Core/Private/OWViews.generated.cpp"


def _read(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


class CheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self.doc = _read(gv.VIEWS)
        self.schema = _read(gv.SCHEMA)
        self.fixture = _read(gv.FIXTURE)

    def refused(self, doc=None, fixture=None) -> str:
        with self.assertRaises(SystemExit) as cm:
            gv.check(self.doc if doc is None else doc, self.schema, self.fixture if fixture is None else fixture, gv.DIST)
        return str(cm.exception.code)

    def row(self, doc, rid: str) -> dict:
        return next(r for r in doc["rows"] if r["id"] == rid)

    def test_the_vendored_file_passes(self) -> None:
        gv.check(self.doc, self.schema, self.fixture, gv.DIST)

    def test_version_2_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        doc["_meta"]["version"] = 2
        self.assertIn("`_meta.version` is 2, expected 3", self.refused(doc))

    def test_a_column_owviews_does_not_read_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "charter")["tone"] = "formal"
        self.assertIn("which OWViews does not read", self.refused(doc))

    def test_a_field_its_type_lacks_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "charter")["lead"].append("story")  # a Narrative's field, not a Law's
        self.assertIn("which law does not have by the walk", self.refused(doc))

    def test_a_pair_its_profiles_reader_does_not_match_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "report")["kind"]["pair"] = "narrative/Chronicle"
        self.assertIn("is not one the room profile's reader matches", self.refused(doc))
        doc = copy.deepcopy(self.doc)
        self.row(doc, "charter")["kind"]["pair"] = "narrative/Message"  # a room pair, not a reading aid's
        self.assertIn("is not one the charter profile's reader matches", self.refused(doc))

    def test_an_undefined_layout_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "charter")["layout"]["name"] = "gallery"
        self.assertIn("with a name the file defines", self.refused(doc))

    def test_two_rows_of_one_kind_are_refused_folded(self) -> None:
        doc = copy.deepcopy(self.doc)
        twin = copy.deepcopy(self.row(doc, "message"))
        twin["id"] = "message-again"
        twin["kind"] = {"type": "narrative", "supertype": " message "}  # the type a slug as written, the supertype folded
        doc["rows"].append(twin)
        self.assertIn("holds a kind another row holds", self.refused(doc))

    def test_two_rows_of_one_id_are_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "charter")["id"] = "report"
        self.assertIn("each row needs its own non-empty id", self.refused(doc))

    def test_a_named_field_with_no_cite_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "message")["lead"].append("consequences")  # a Narrative's field, cited nowhere
        self.assertIn("with no cite in views.fixture.json: [('message', 'consequences')]", self.refused(doc))

    def test_a_cite_whose_quote_is_not_at_its_line_is_refused(self) -> None:
        fixture = copy.deepcopy(self.fixture)
        fixture["cites"][0]["line"] += 1
        self.assertIn("the quote is not at that line of the page", self.refused(fixture=fixture))

    def test_a_cite_of_a_field_its_row_does_not_name_is_refused(self) -> None:
        fixture = copy.deepcopy(self.fixture)
        c = next(c for c in fixture["cites"] if c["row"] == "charter")
        c["field"] = "story"  # the charter row names description only
        self.assertIn("names a field its row does not name", self.refused(fixture=fixture))

    # -- this SDK's own two ------------------------------------------------------------------------------------------

    def test_an_empty_supertype_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "message")["kind"]["supertype"] = ""
        self.assertIn("holds an empty word, which the table reads as none", self.refused(doc))

    def test_an_empty_half_of_a_pair_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "report")["kind"]["pair"] = "narrative/"
        # the pair check speaks first: an empty half is no pair the profile's reader matches
        self.assertIn("is not one the room profile's reader matches", self.refused(doc))

    def test_an_empty_layout_by_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        self.row(doc, "message")["layout"]["by"] = ""
        self.assertIn("layout.by is '', expected a field name", self.refused(doc))

    def test_an_empty_banner_mark_is_refused(self) -> None:
        doc = copy.deepcopy(self.doc)
        doc["banners"][0]["mark"] = ""
        self.assertIn("a layout name or a banner mark is empty", self.refused(doc))

    def test_a_character_outside_printable_ascii_is_refused(self) -> None:
        for text in ("The full report’s", "line one\nline two"):
            doc = copy.deepcopy(self.doc)
            self.row(doc, "report")["fold"][0]["label"] = text
            self.assertIn("outside printable ASCII", self.refused(doc), text)


class DriftTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory(prefix="views-drift-")
        self.tree = Path(self._dir.name)
        for rel in ("codegen/generate.py", "codegen/generate_views.py", "codegen/PIN", GENERATED):
            (self.tree / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, self.tree / rel)
        shutil.copytree(ROOT / "codegen" / "schema-dist", self.tree / "codegen" / "schema-dist",
                        ignore=shutil.ignore_patterns("__pycache__"))

    def tearDown(self) -> None:
        self._dir.cleanup()

    def check(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(self.tree / "codegen" / "generate_views.py"), "--check"],
                              capture_output=True, text=True, cwd=str(self.tree))

    def test_the_committed_tree_is_green(self) -> None:
        run = self.check()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_one_byte_in_the_generated_table_is_red(self) -> None:
        path = self.tree / GENERATED
        data = path.read_bytes()
        self.assertIn(b'"The full report"', data)
        path.write_bytes(data.replace(b'"The full report"', b'"The full rePort"', 1))
        run = self.check()
        self.assertEqual(run.returncode, 1)
        self.assertIn("DRIFT", run.stdout)


if __name__ == "__main__":
    unittest.main()
