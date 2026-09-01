#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Describe a built bundle so a channel server can serve it.

Everything the OTA check needs to answer "is there something newer for this
channel, and where do I get it" — plus enough provenance to work out what a
build actually contains once it is on a wrist.
"""

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def _git(*args, default=""):
    try:
        return subprocess.check_output(
            ["git", *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (subprocess.SubprocessError, OSError):
        return default


def notes(base_ref):
    """Commit subjects this build carries over the fork's stable branch.

    These become the release notes the companion app shows on the update card,
    so they are the only description of a build most of us will ever read.
    """
    log = _git("log", "--no-merges", "--format=%s", f"{base_ref}..HEAD")
    lines = [line for line in log.splitlines() if line]
    return lines[:20]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pbz", required=True, help="merged dual-slot bundle")
    p.add_argument("--channel", required=True, help="branch this was built from")
    p.add_argument("--version", required=True, help="firmware version string")
    p.add_argument("--hardware", required=True, help="e.g. obelix_pvt")
    p.add_argument(
        "--base-ref", default="origin/main", help="ref to diff notes against"
    )
    p.add_argument("--run-url", default="", help="link back to the build")
    p.add_argument("--output", required=True)
    args = p.parse_args()

    pbz = Path(args.pbz)
    if not pbz.is_file():
        sys.exit(f"no such bundle: {pbz}")
    data = pbz.read_bytes()

    manifest = {
        "channel": args.channel,
        "version": args.version,
        "hardware_version": args.hardware,
        # The bootloader picks the slot with the highest priority and that
        # priority is stamped at build time, so a server that re-stamps on
        # serve needs to know which build it is handing out.
        "commit": _git("rev-parse", "HEAD", default="unknown"),
        "commit_short": _git("rev-parse", "--short=7", "HEAD", default="unknown"),
        "base": _git(
            "describe", "--tags", "--abbrev=0", "--match", "v[0-9]*", default="unknown"
        ),
        "bundle": pbz.name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "notes": notes(args.base_ref),
        "built_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "run_url": args.run_url,
        # Set once a firmware change needs a companion app that understands it.
        # Until then the app should never refuse a build on these grounds.
        "min_app_build": None,
    }

    Path(args.output).write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
