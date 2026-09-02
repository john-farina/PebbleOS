#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Turn a Stone trace capture into something readable.

The watch prints a capture as terse fixed-shape lines, because the ring that
produced them has to be cheap enough to write from a touch handler. This names
the codes, unwraps the timestamp and shows the gap between events, which is
what a gesture problem actually looks like.

Feed it a whole log; it finds the capture inside. Usage:

    tools/stone/decode_trace.py capture.txt
    pbl-console ... | tools/stone/decode_trace.py -
"""

import argparse
import re
import sys

BEGIN = "STONE-TRACE BEGIN"
END = "STONE-TRACE END"

#: The line the watch emits, after whatever prefix the log adds.
LINE = re.compile(r"\bST (\d+) (\w+) (\d+) (-?\d+) (-?\d+)\b")

#: What each (source, code) means. Kept here rather than on the watch: a name costs flash there
#: and nothing here, and this is the file someone reads while looking at a capture.
NAMES = {
    ("touch", 0): "down",
    ("touch", 1): "move",
    ("gest", 0): "gesture none",
    ("gest", 1): "gesture tap",
    ("gest", 2): "gesture double-tap",
    ("gest", 3): "gesture swipe-up",
    ("gest", 4): "gesture swipe-down",
    ("gest", 5): "gesture swipe-left",
    ("gest", 6): "gesture swipe-right",
    ("gest", 7): "gesture long-press",
    ("swipe", 0): "SWIPE none (rejected)",
    ("swipe", 1): "SWIPE up",
    ("swipe", 2): "SWIPE down",
    ("swipe", 4): "SWIPE left",
    ("swipe", 8): "SWIPE right",
    ("pick", 1): "picker page shown",
    ("pick", 2): "picker step",
    ("thumb", 1): "thumb capture",
    ("thumb", 3): "thumb store FAILED",
    ("thumb", 4): "thumb loaded",
    ("thumb", 5): "thumb absent (icon shown)",
}

#: Where the two integers mean something specific enough to be worth labelling.
ARGS = {
    ("touch", 0): ("x", "y"),
    ("touch", 1): ("x", "y"),
    ("gest", 0): ("x", "y"),
    ("pick", 1): ("page", "had_thumb"),
    ("pick", 2): ("delta", "faces"),
    ("thumb", 3): ("chunk", "status"),
}


def parse(text):
    """Extract the capture's entries, as (ms, source, code, a, b) with ms unwrapped."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if BEGIN in line:
            start = i  # the last capture in the log is the interesting one
    if start is None:
        return []

    entries = []
    last_raw = None
    base = 0
    for line in lines[start:]:
        if END in line and entries:
            break
        m = LINE.search(line)
        if not m:
            continue
        raw = int(m.group(1))
        # The watch sends 16 bits of milliseconds, so it wraps about every 65 seconds. Entries are
        # in order, so a timestamp that went backwards is a wrap rather than a reordering.
        if last_raw is not None and raw < last_raw:
            base += 1 << 16
        last_raw = raw
        entries.append((base + raw, m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))))
    return entries


def render(entries):
    """One line per event: elapsed, gap, what it was."""
    if not entries:
        return ["no capture found -- hold UP+SELECT+DOWN for five seconds, then dump the log"]

    out = []
    first = entries[0][0]
    prev = first
    for ms, source, code, a, b in entries:
        name = NAMES.get((source, code), f"{source} code {code}")
        labels = ARGS.get((source, code))
        if labels:
            detail = f"{labels[0]}={a} {labels[1]}={b}"
        elif (a, b) == (0, 0):
            detail = ""
        else:
            detail = f"{a} {b}"
        gap = ms - prev
        prev = ms
        out.append(f"{ms - first:6d}ms  +{gap:<5d} {name:26s} {detail}")
    out.append(f"-- {len(entries)} events over {entries[-1][0] - first}ms")
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("log", help="file containing the capture, or - for stdin")
    args = p.parse_args()

    text = sys.stdin.read() if args.log == "-" else open(args.log).read()
    for line in render(parse(text)):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
