#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Start a feature branch with the notes file that goes with it.

Every feature branch carries `docs/stone/features/<slug>.md`, and it exists
because sessions end. Work on this fork is done by whichever Claude session is
running, none of which remember the last one, and the expensive thing to lose
is not the code -- that is in the commits -- but the reasoning: what was tried,
what broke, what was decided and why.

Creating the branch and the notes file in one step is what stops the notes from
being the thing nobody got round to.
"""

import argparse
import datetime
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TEMPLATE = Path(__file__).resolve().parent / "feature_notes.md.in"
NOTES_DIR = Path("docs/stone/features")
BASE = "stone"

# gitlint's default title limit is 72 characters, and a commit that fails it is
# a cycle wasted on something a regex can catch here.
MAX_TITLE = 72


def git(*args, check=True, capture=True):
    return subprocess.run(
        ["git", "-C", str(REPO), *args],
        check=check,
        text=True,
        capture_output=capture,
    )


def slugify(name):
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    if not slug:
        sys.exit(f"cannot make a branch name out of {name!r}")
    return slug


def branch_exists(branch):
    return (
        git(
            "rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", check=False
        ).returncode
        == 0
        or git(
            "rev-parse",
            "--verify",
            "--quiet",
            f"refs/remotes/origin/{branch}",
            check=False,
        ).returncode
        == 0
    )


def working_tree_is_clean():
    return not git("status", "--porcelain").stdout.strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("name", help='what the feature is, e.g. "sleep tracking tweaks"')
    p.add_argument("--slug", help="branch suffix; defaults to a slug of the name")
    p.add_argument(
        "--base",
        default=f"origin/{BASE}",
        help=f"what to branch from (default: origin/{BASE})",
    )
    p.add_argument("--dry-run", action="store_true", help="say what it would do")
    args = p.parse_args()

    slug = args.slug or slugify(args.name)
    branch = f"feat/{slug}"
    notes = NOTES_DIR / f"{slug}.md"
    title = f"docs: start notes for {args.name}"

    if len(title) > MAX_TITLE:
        sys.exit(
            f"commit title would be {len(title)} characters, over gitlint's "
            f"{MAX_TITLE}. Pass a shorter name, or --slug with a short name."
        )
    if branch_exists(branch):
        sys.exit(f"{branch} already exists. Check it out, or pick another --slug.")
    if (REPO / notes).exists():
        sys.exit(f"{notes} already exists.")

    started = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    body = TEMPLATE.read_text().format(
        title=args.name[0].upper() + args.name[1:],
        branch=branch,
        started=started,
    )

    if args.dry_run:
        print(f"branch:  {branch} (from {args.base})")
        print(f"notes:   {notes}")
        print(f"commit:  {title}")
        return 0

    # Refuse rather than sweep changes onto a new branch: the next session
    # would find them there with no idea they were not part of the feature.
    if not working_tree_is_clean():
        sys.exit("working tree is not clean; commit or stash first")

    git("fetch", "origin", BASE, capture=False)
    # --no-track: branching from origin/stone otherwise sets the new branch's
    # upstream to stone, so a bare `git push` aims at the shared branch. Git's
    # default push.default=simple refuses when the names differ, but relying on
    # that to stop a push to `stone` is not a design.
    git("switch", "--no-track", "-c", branch, args.base, capture=False)

    (REPO / notes).parent.mkdir(parents=True, exist_ok=True)
    (REPO / notes).write_text(body)

    git("add", str(notes))
    git(
        "commit",
        "-s",
        "--trailer",
        "Co-Authored-By=Claude <noreply@anthropic.com>",
        "-m",
        title,
        "-m",
        "Every feature branch carries its own notes file. See docs/stone/features.md.",
        capture=False,
    )

    print(f"\non {branch}, notes at {notes}")
    print("Fill in 'What this is' before writing any code, then keep it current.")
    print("Push when there is something worth building; the branch becomes a channel.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
