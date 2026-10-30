#!/usr/bin/env python3
"""generate_presentation.py: a family word not in colors.order fails loudly, naming the word, and nothing is written;
--check is green on the tree as committed and red on a one-byte change to the generated table.

The refusal is read through the generator's own `refusal`, on a copy of the vendored sidecar with one word changed (the
vendored file itself is pinned by its hash, so a changed copy never gets past the pin). --check runs in a temporary
tree of the same shape, so the repo's own files are never touched.

    python codegen/test_generate_presentation.py
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

import generate_presentation as gp  # noqa: E402

PRESENTATION = json.loads(gp.PRESENTATION.read_text(encoding="utf-8"))
SLUGS = list(json.loads(gp.SCHEMA.read_text(encoding="utf-8"))["types"])
GENERATED = "Plugin/OnlyWorlds/Core/Private/OWPresentation.generated.cpp"


class RefusalTest(unittest.TestCase):
    def test_the_vendored_sidecar_reads(self) -> None:
        self.assertEqual(gp.refusal(PRESENTATION, SLUGS), "")

    def test_a_word_not_in_colors_order_is_named(self) -> None:
        p = copy.deepcopy(PRESENTATION)
        p["types"]["trait"]["family"] = "teal"
        self.assertEqual(gp.refusal(p, SLUGS),
                         'presentation.json: trait\'s family "teal" is not one of blue, green, pink, amber')

    def test_a_word_order_drops_is_named(self) -> None:
        p = copy.deepcopy(PRESENTATION)
        p["colors"]["order"] = ["blue", "green", "pink"]
        self.assertIn('ability\'s family "amber"', gp.refusal(p, SLUGS))

    def test_a_missing_type_and_an_extra_one_are_named(self) -> None:
        p = copy.deepcopy(PRESENTATION)
        del p["types"]["zone"]
        self.assertEqual(gp.refusal(p, SLUGS), "presentation.json: zone has no entry")
        p = copy.deepcopy(PRESENTATION)
        p["types"]["vessel"] = {"family": "blue", "icon": "sailing"}
        self.assertEqual(gp.refusal(p, SLUGS), "presentation.json: vessel is not one of the 22 types")


class CheckTest(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory(prefix="graph-presentation-")
        self.tree = Path(self._dir.name)
        for rel in ("codegen/generate.py", "codegen/generate_presentation.py", "codegen/PIN", GENERATED):
            (self.tree / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, self.tree / rel)
        shutil.copytree(ROOT / "codegen" / "schema-dist", self.tree / "codegen" / "schema-dist",
                        ignore=shutil.ignore_patterns("__pycache__"))

    def tearDown(self) -> None:
        self._dir.cleanup()

    def check(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.tree / "codegen" / "generate_presentation.py"), "--check"],
            capture_output=True, text=True, cwd=str(self.tree),
        )

    def test_the_committed_tree_is_green(self) -> None:
        run = self.check()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_one_byte_in_the_generated_table_is_red(self) -> None:
        path = self.tree / GENERATED
        data = path.read_bytes()
        self.assertIn(b'"crib"', data)
        path.write_bytes(data.replace(b'"crib"', b'"crab"', 1))
        run = self.check()
        self.assertEqual(run.returncode, 1)
        self.assertIn("DRIFT", run.stdout)


if __name__ == "__main__":
    unittest.main()
