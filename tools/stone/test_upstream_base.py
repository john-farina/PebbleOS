# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""The upstream release must survive the two tags that are not upstream releases.

Both wrong answers actually shipped: the firmware reported the floor tag and the manifest
reported a safe-build tag, from the same expression in two files.
"""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from upstream_base import pebbleos_base


class TestUpstreamBase(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.repo = Path(self._dir.name)
        self.addCleanup(self._dir.cleanup)

        self._cwd = os.getcwd()
        self.addCleanup(os.chdir, self._cwd)

        self._floor = os.environ.pop("STONE_VERSION_FLOOR", None)
        self.addCleanup(self._restore_floor)

        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "Test User")
        os.chdir(self.repo)

    def _restore_floor(self):
        os.environ.pop("STONE_VERSION_FLOOR", None)
        if self._floor is not None:
            os.environ["STONE_VERSION_FLOOR"] = self._floor

    def git(self, *args):
        subprocess.run(
            ["git", "-C", str(self.repo), *args], check=True, capture_output=True
        )

    def commit(self, message):
        (self.repo / "f").write_text(message)
        self.git("add", "f")
        self.git("commit", "-q", "-m", message)

    def tag(self, name):
        # Annotated: git describe considers only annotated tags, which is the same reason the
        # floor tag in stone-build.yml has to be created with -a.
        self.git("tag", "-fa", name, "-m", name)

    def test_plain_release_is_found(self):
        self.commit("one")
        self.tag("v4.36.0")
        self.commit("two")
        self.assertEqual(pebbleos_base(), "v4.36.0")

    def test_safe_tag_is_ignored(self):
        """A safe build had the manifest publishing v4.35.0-safe2 as the upstream release."""
        self.commit("one")
        self.tag("v4.36.0")
        self.commit("two")
        self.tag("v4.36.0-safe2")
        self.commit("three")
        self.assertEqual(pebbleos_base(), "v4.36.0")

    def test_version_floor_is_ignored_when_named(self):
        """The floor sits nearer than the release, so it wins unless excluded by name."""
        self.commit("one")
        self.tag("v4.36.0")
        self.commit("two")
        self.tag("v200.0.0.1")
        self.commit("three")

        # Without the environment variable the floor is indistinguishable from a release, which
        # is exactly what the workflow's env: is for.
        self.assertEqual(pebbleos_base(), "v200.0.0.1")

        os.environ["STONE_VERSION_FLOOR"] = "v200.0.0.1"
        self.assertEqual(pebbleos_base(), "v4.36.0")

    def test_both_together(self):
        """What a real CI build sees: a floor and a safe tag, both nearer than the release."""
        self.commit("one")
        self.tag("v4.36.0")
        self.commit("two")
        self.tag("v4.36.0-safe1")
        self.commit("three")
        self.tag("v200.0.0.1")
        self.commit("four")

        os.environ["STONE_VERSION_FLOOR"] = "v200.0.0.1"
        self.assertEqual(pebbleos_base(), "v4.36.0")

    def test_no_tags_falls_back(self):
        """Build metadata must never be the reason firmware fails to compile."""
        self.commit("one")
        self.assertEqual(pebbleos_base(), "unknown")


if __name__ == "__main__":
    unittest.main()
