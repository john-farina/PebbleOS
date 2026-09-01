# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Tests for the Stone build label and summary.

Run with `python -m unittest discover -s tools/stone`. Nothing here touches a
real repository: the label is a pure function of the branch name and the commit
count, and the summary is a pure function of a file, so both are tested as
such.
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_info import (
    SUMMARY_MAX_LEN,
    c_string,
    is_custom,
    slug,
    summary,
)


class TestSlug(unittest.TestCase):
    def test_takes_the_first_word_of_the_last_segment(self):
        # The name someone would say out loud for the branch.
        self.assertEqual(slug("feat/navigation-overhaul"), "navigation")
        self.assertEqual(slug("feat/channel-smoke-test"), "channel")

    def test_drops_a_generated_suffix_with_the_rest_of_the_qualification(self):
        # Claude branches end in random characters; taking the first word means
        # never having to special-case them.
        self.assertEqual(slug("claude/nav-overhaul-haptics-preview-ftfmje"), "nav")

    def test_survives_a_branch_with_no_separators(self):
        self.assertEqual(slug("stone"), "stone")
        self.assertEqual(slug("wip"), "wip")

    def test_strips_characters_that_would_not_read_as_a_name(self):
        self.assertEqual(slug("feat/UPPER_case.stuff-x"), "uppercasestu")

    def test_is_bounded(self):
        # A label you cannot read at a glance defeats the point of having one.
        self.assertLessEqual(len(slug("feat/" + "a" * 80)), 12)

    def test_never_empty(self):
        # An empty slug would render as a bare ".7", which names nothing.
        self.assertEqual(slug("feat/---"), "build")
        self.assertEqual(slug(""), "build")


class TestIsCustom(unittest.TestCase):
    def test_release_branches_have_no_label(self):
        self.assertFalse(is_custom("stone"))
        self.assertFalse(is_custom("main"))

    def test_everything_else_does(self):
        self.assertTrue(is_custom("feat/navigation-overhaul"))
        self.assertTrue(is_custom("claude/whatever-abc123"))
        self.assertTrue(is_custom("ci/qa-dualslot-artifacts"))

    def test_a_branch_merely_containing_a_release_name_still_does(self):
        self.assertTrue(is_custom("feat/stone"))
        self.assertTrue(is_custom("stone-pr16-notrack"))


class TestSummary(unittest.TestCase):
    def _write(self, text):
        tmp = Path(tempfile.mkdtemp()) / "build_summary.txt"
        tmp.write_text(text)
        return tmp

    def test_reads_the_first_content_line(self):
        path = self._write("# a comment\n\nWhat changed.\n")
        self.assertEqual(summary(path), "What changed.")

    def test_ignores_later_lines(self):
        # One sentence means one sentence; a second line is not shown, so it
        # must not be silently concatenated into the first.
        path = self._write("First line.\nSecond line.\n")
        self.assertEqual(summary(path), "First line.")

    def test_a_file_of_only_comments_reads_as_absent(self):
        path = self._write("# nothing here\n#\n\n")
        self.assertEqual(summary(path), "")

    def test_a_missing_file_reads_as_absent_rather_than_raising(self):
        # Build metadata must never be the thing that fails a build.
        self.assertEqual(summary(Path("/nonexistent/build_summary.txt")), "")

    def test_the_checked_in_summary_is_present_and_fits(self):
        # The rule CI enforces, asserted here too so it fails in a second
        # rather than after a seven-minute build.
        text = summary()
        self.assertTrue(text, "tools/stone/build_summary.txt has no summary line")
        self.assertLessEqual(len(text), SUMMARY_MAX_LEN)


class TestCString(unittest.TestCase):
    def test_escapes_what_would_break_the_generated_header(self):
        self.assertEqual(c_string('say "hi"', 64), 'say \\"hi\\"')
        self.assertEqual(c_string("back\\slash", 64), "back\\\\slash")

    def test_clamps_to_what_the_watch_can_show(self):
        self.assertEqual(len(c_string("x" * 200, 31)), 31)


if __name__ == "__main__":
    unittest.main()
