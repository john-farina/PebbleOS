#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Emit stone_build_info.auto.h.

`git describe` records the nearest tag and the commit but not the branch, and
the on-watch version field is only 32 bytes — too small to carry a branch name
alongside the version. So once a build is installed there is no way to tell
which branch it came from, which is exactly what you need to know when several
WIP builds are in flight.

A commit hash is a poor answer to "which build is this?" — seven hex digits do
not order themselves, so `34ec6f2` and `7d0cb89` say nothing about which is
newer, and a whole session has already been lost to debugging a build that was
simply older than the one being described. So a WIP build also carries a
human-orderable label, `navigation.7`: the branch, and how many commits into it
this build is. It increments on its own as the branch grows, and it is
comparable at a glance, which is the entire point.

Release branches do not get one. On `stone` the version string is the answer
and a second identifier would only compete with it.

Deliberately no build wall-clock here: the header is only rewritten when its
contents change, and a timestamp would change on every invocation and force a
full rebuild each time.
"""

import argparse
import os
import pathlib
import subprocess
import sys

#: Branches whose builds are identified by their version string alone.
RELEASE_BRANCHES = frozenset({"stone", "main"})

#: Read by the watch and rendered in a menu cell, so it has to fit one.
SUMMARY_MAX_LEN = 72

SUMMARY_PATH = pathlib.Path(__file__).resolve().parent / "build_summary.txt"


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


def is_custom(branch_name):
    """Whether this branch's builds carry a Stone build label."""
    return branch_name not in RELEASE_BRANCHES


def slug(branch_name):
    """The short, human name of a branch: `feat/navigation-overhaul` -> `navigation`.

    The first word of the last path segment, because that is the word someone
    would actually use for the branch out loud. Everything after it is
    qualification -- `-overhaul`, `-haptics-preview`, a generated suffix -- and
    a label you have to read to the end to tell apart is no better than a hash.
    """
    name = branch_name.rsplit("/", 1)[-1]
    word = "".join(c for c in name.split("-", 1)[0].lower() if c.isalnum())
    return word[:12] or "build"


def build_number():
    """How many commits into its own branch this build is.

    Counted from where the branch left `stone`, so it starts at 1 on the first
    commit of a branch and rises by one per commit after that -- which is one
    per build, because a build is what a commit is for here.

    Deliberately derived rather than stored: a counter in a file is a counter
    two sessions can disagree about, and one in CI is a counter you cannot
    reproduce locally. This is neither, and a given number always names the
    same code.
    """
    for ref in ("origin/stone", "stone"):
        base = _git("merge-base", ref, "HEAD", default="")
        if not base:
            continue
        count = _git("rev-list", "--count", f"{base}..HEAD", default="")
        if count.isdigit():
            return int(count)
    return 0


def summary(path=SUMMARY_PATH):
    """The first line of the summary file that is neither blank nor a comment."""
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    return line
    except OSError:
        # Metadata must never fail a build; an absent summary renders as absent.
        pass
    return ""


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
    branch_name = branch()
    custom = is_custom(branch_name)
    fields = {
        "STONE_CUSTOM": "1" if custom else "0",
        # Empty on a release branch, which is what the UI tests to decide
        # whether to show the label or the version.
        "STONE_BUILD": c_string(
            f"{slug(branch_name)}.{build_number()}" if custom else "", 31
        ),
        "STONE_SUMMARY": c_string(summary() if custom else "", SUMMARY_MAX_LEN),
        "STONE_BRANCH": c_string(branch_name, 31),
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
