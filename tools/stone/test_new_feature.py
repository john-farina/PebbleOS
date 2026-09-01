# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Tests for the feature-branch starter.

Run with `python -m unittest discover -s tools/stone`. These drive the real
script against a throwaway repository, because the failure that matters -- it
refuses when it should, or commits something gitlint rejects -- only shows up
against real git.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from new_feature import MAX_TITLE, slugify

SCRIPT = Path(__file__).resolve().parent / "new_feature.py"
TEMPLATE = Path(__file__).resolve().parent / "feature_notes.md.in"


class Slugify(unittest.TestCase):
    def test_makes_a_branch_safe_name(self):
        self.assertEqual(slugify("Sleep tracking tweaks"), "sleep-tracking-tweaks")
        self.assertEqual(slugify("HRM: 24/7 sampling!"), "hrm-24-7-sampling")
        self.assertEqual(slugify("  spaced  out  "), "spaced-out")

    def test_refuses_a_name_with_nothing_in_it(self):
        with self.assertRaises(SystemExit):
            slugify("!!!")


class Template(unittest.TestCase):
    def test_renders_with_no_leftover_placeholders(self):
        body = TEMPLATE.read_text().format(
            title="Thing", branch="feat/thing", started="2026-09-01"
        )
        self.assertNotIn("{", body.split("```")[0])
        for heading in (
            "## What this is",
            "## Where it stands",
            "## Decided",
            "## Tried and rejected",
            "## Open questions",
            "## How to test it",
            "## Log",
        ):
            self.assertIn(heading, body)


class AgainstARealRepo(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.repo = Path(self._tmp.name) / "repo"

        # The script locates the repo from its own path, so it has to be run
        # from a copy that sits where it expects: <repo>/tools/stone/.
        (self.repo / "tools" / "stone").mkdir(parents=True)
        for src in (SCRIPT, TEMPLATE):
            (self.repo / "tools" / "stone" / src.name).write_bytes(src.read_bytes())
        self.script = self.repo / "tools" / "stone" / SCRIPT.name

        self.git("init", "-q", "-b", "stone")
        self.git("config", "user.name", "Test Person")
        self.git("config", "user.email", "test@example.invalid")
        (self.repo / "README.md").write_text("hello\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "chore: initial")
        # A local `origin` so `git fetch origin stone` and `origin/stone`
        # resolve the way they do in the real repository.
        self.git("remote", "add", "origin", str(self.repo))
        self.git("fetch", "-q", "origin")

    def git(self, *args, **kw):
        return subprocess.run(
            ["git", "-C", str(self.repo), *args],
            check=kw.pop("check", True),
            text=True,
            capture_output=True,
        )

    def run_script(self, *args, expect=0):
        result = subprocess.run(
            [sys.executable, str(self.script), *args],
            check=False,
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            result.returncode, expect, f"stdout={result.stdout}\nstderr={result.stderr}"
        )
        return result

    def test_creates_the_branch_the_notes_and_the_commit(self):
        self.run_script("sleep tracking tweaks")

        head = self.git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
        self.assertEqual(head, "feat/sleep-tracking-tweaks")

        notes = self.repo / "docs/stone/features/sleep-tracking-tweaks.md"
        self.assertTrue(notes.is_file())
        self.assertIn("feat/sleep-tracking-tweaks", notes.read_text())

        subject = self.git("log", "-1", "--format=%s").stdout.strip()
        self.assertEqual(subject, "docs: start notes for sleep tracking tweaks")
        self.assertLessEqual(len(subject), MAX_TITLE)

        trailers = self.git("log", "-1", "--format=%(trailers)").stdout
        self.assertIn("Signed-off-by: Test Person", trailers)
        self.assertIn("Co-Authored-By: Claude", trailers)
        self.assertEqual(trailers.count("Signed-off-by"), 1)

        # Nothing left behind: the commit is the only change.
        self.assertEqual(self.git("status", "--porcelain").stdout.strip(), "")

    def test_refuses_a_branch_that_already_exists(self):
        self.run_script("thing")
        # Back on stone the notes file is gone from the working tree, so this
        # is the branch check firing and not the file check.
        self.git("switch", "-q", "stone")
        self.assertFalse((self.repo / "docs/stone/features/thing.md").exists())
        result = self.run_script("thing", expect=1)
        self.assertIn("already exists", result.stderr)

    def test_refuses_a_dirty_working_tree(self):
        # Otherwise unrelated edits ride along onto the new branch and the next
        # session cannot tell which changes were the feature.
        (self.repo / "README.md").write_text("edited\n")
        result = self.run_script("thing", expect=1)
        self.assertIn("not clean", result.stderr)
        self.assertEqual(
            self.git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip(), "stone"
        )

    def test_refuses_a_name_that_would_fail_gitlint(self):
        result = self.run_script("x" * 80, expect=1)
        self.assertIn("gitlint", result.stderr)

    def test_dry_run_changes_nothing(self):
        self.run_script("--dry-run", "thing")
        self.assertEqual(
            self.git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip(), "stone"
        )
        self.assertFalse((self.repo / "docs/stone/features/thing.md").exists())


if __name__ == "__main__":
    unittest.main()
