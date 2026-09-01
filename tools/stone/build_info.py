#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Emit stone_build_info.auto.h.

`git describe` records the nearest tag and the commit but not the branch, and
the on-watch version field is only 32 bytes — too small to carry a branch name
alongside the version. So once a build is installed there is no way to tell
which branch it came from, which is exactly what you need to know when several
WIP builds are in flight.

Deliberately no build wall-clock here: the header is only rewritten when its
contents change, and a timestamp would change on every invocation and force a
full rebuild each time.
"""

import argparse
import os
import subprocess
import sys


def _git(*args, default=""):
    try:
        return subprocess.check_output(
            ["git", *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (subprocess.SubprocessError, OSError):
        # No git, not a repo, or the command failed: fall back rather than
        # failing the build over build metadata.
        return default


def branch():
    """The branch this was built from.

    CI checks out a detached HEAD, so git alone reports "HEAD". GitHub knows the
    real ref: GITHUB_HEAD_REF on a pull request, GITHUB_REF_NAME otherwise.
    """
    for var in ("GITHUB_HEAD_REF", "GITHUB_REF_NAME"):
        value = os.environ.get(var)
        if value:
            return value
    name = _git("rev-parse", "--abbrev-ref", "HEAD", default="")
    if name and name != "HEAD":
        return name
    return _git("rev-parse", "--short", "HEAD", default="unknown")


def c_string(value, limit):
    """Escape for a C string literal and clamp to what the UI can show."""
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    return value[:limit]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--template", required=True)
    p.add_argument("--output", required=True)
    args = p.parse_args()

    dirty = bool(_git("status", "--porcelain", "--untracked-files=no"))
    fields = {
        "STONE_BRANCH": c_string(branch(), 31),
        "STONE_COMMIT": c_string(
            _git("rev-parse", "--short=7", "HEAD", default="unknown"), 15
        ),
        # The upstream release this fork currently sits on.
        "STONE_BASE": c_string(
            _git(
                "describe",
                "--tags",
                "--abbrev=0",
                "--match",
                "v[0-9]*",
                default="unknown",
            ),
            31,
        ),
        "STONE_DIRTY": "1" if dirty else "0",
    }

    with open(args.template) as f:
        content = f.read()
    for key, value in fields.items():
        content = content.replace(f"@{key}@", value)

    # Only rewrite when it actually changed, so a no-op build stays a no-op.
    try:
        with open(args.output) as f:
            if f.read() == content:
                return 0
    except FileNotFoundError:
        pass
    with open(args.output, "w") as f:
        f.write(content)
    return 0


if __name__ == "__main__":
    sys.exit(main())
