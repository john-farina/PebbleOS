# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Which upstream PebbleOS release this fork is sitting on.

Shared because two tools need the answer and both had got it wrong in different ways:
`build_info.py` bakes it into the firmware for Settings > Stone, and `make_manifest.py`
publishes it to the channel, where the companion app shows it on the update card.

The naive `git describe --tags --abbrev=0 --match "v[0-9]*"` finds two kinds of tag that are
not upstream releases:

* **The version floor.** `stone-build.yml` creates an annotated `v200.0.0.1` on the merge-base
  with `main` at build time, so an install is not read as a downgrade of the firmware Core
  ships. It sits *nearer* than the real release tag, so `describe` returns it and the watch
  claims to be running PebbleOS v200. The workflow exports the value as `STONE_VERSION_FLOOR`,
  which every step inherits and `cmake -E env` passes through, so it is excluded by name rather
  than by a constant duplicated here.
* **Safe-build tags** (`vX.Y.Z-safeN`), which land on `main`. These are the quieter of the two:
  they had the manifest reporting `v4.35.0-safe2` as the upstream release.

The two tools saw *different* wrong answers, which is why this lives in one place. The floor tag
is created inside the `build` job and never exists in the `bundle` job, so the firmware was
picking up the floor while the manifest picked up the safe tag.
"""

import os
import subprocess


def _git(*args, default=""):
    try:
        return subprocess.check_output(
            ["git", *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (subprocess.SubprocessError, OSError):
        # No git, not a repo, or the command failed: fall back rather than failing a build over
        # build metadata.
        return default


def pebbleos_base(default="unknown"):
    """@return the nearest upstream release tag, ignoring the floor and safe-build tags."""
    args = [
        "describe",
        "--tags",
        "--abbrev=0",
        "--match",
        "v[0-9]*",
        "--exclude",
        "*safe*",
    ]
    floor = os.environ.get("STONE_VERSION_FLOOR")
    if floor:
        args += ["--exclude", floor]
    return _git(*args, default=default)
