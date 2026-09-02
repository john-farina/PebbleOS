# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Tests for the trace decoder.

Run with `python -m unittest discover -s tools/stone`. The decoder is the only
part of the capture pipeline that can be tested without a watch, so it carries
the cases that would otherwise only show up while reading a real capture.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from decode_trace import parse, render


def capture(*lines):
    body = "\n".join(lines)
    return f"noise before\nSTONE-TRACE BEGIN entries={len(lines)} dropped=0\n{body}\nSTONE-TRACE END\nnoise after\n"


class TestParse(unittest.TestCase):
    def test_finds_the_capture_inside_a_larger_log(self):
        entries = parse(capture("ST 100 touch 0 12 100"))
        self.assertEqual(entries, [(100, "touch", 0, 12, 100)])

    def test_ignores_lines_that_are_not_entries(self):
        text = capture("ST 100 touch 0 1 2", "some other log line entirely")
        self.assertEqual(len(parse(text)), 1)

    def test_tolerates_a_log_prefix_on_each_line(self):
        # Real logs arrive with a timestamp and filename in front of the message.
        text = capture("2026-09-02 01:02:03 watchface.c:120> ST 100 touch 0 1 2")
        self.assertEqual(parse(text), [(100, "touch", 0, 1, 2)])

    def test_unwraps_the_16_bit_timestamp(self):
        # The watch sends milliseconds modulo 65536; entries are in order, so a value that went
        # backwards is a wrap rather than a reordering.
        entries = parse(capture("ST 65500 touch 0 0 0", "ST 20 touch 1 0 0"))
        self.assertEqual([e[0] for e in entries], [65500, 65536 + 20])

    def test_unwraps_repeatedly(self):
        entries = parse(capture("ST 65000 touch 0 0 0", "ST 10 touch 1 0 0", "ST 5 touch 1 0 0"))
        self.assertEqual([e[0] for e in entries], [65000, 65546, 131077])

    def test_negative_values_survive(self):
        self.assertEqual(parse(capture("ST 1 pick 2 -1 4")), [(1, "pick", 2, -1, 4)])

    def test_takes_the_last_capture_when_a_log_holds_several(self):
        # Someone who pressed the combo twice wants the second one.
        text = capture("ST 1 touch 0 1 1") + capture("ST 2 touch 0 9 9")
        self.assertEqual(parse(text), [(2, "touch", 0, 9, 9)])

    def test_no_capture_is_empty_rather_than_an_error(self):
        self.assertEqual(parse("nothing to see here\n"), [])


class TestRender(unittest.TestCase):
    def test_names_codes_and_labels_arguments(self):
        out = render(parse(capture("ST 0 touch 0 12 100")))
        self.assertIn("down", out[0])
        self.assertIn("x=12 y=100", out[0])

    def test_shows_the_gap_between_events(self):
        # The gap is the whole point: a swipe that failed on duration looks like a long gap.
        out = render(parse(capture("ST 0 touch 0 0 0", "ST 250 touch 1 5 5")))
        self.assertIn("+250", out[1])

    def test_a_rejected_swipe_is_called_out(self):
        out = render(parse(capture("ST 0 swipe 0 0 0")))
        self.assertIn("rejected", out[0])

    def test_an_unknown_code_still_renders(self):
        # A capture from a newer build must stay readable by an older decoder.
        out = render(parse(capture("ST 0 touch 99 1 2")))
        self.assertIn("code 99", out[0])

    def test_empty_capture_says_how_to_take_one(self):
        out = render([])
        self.assertIn("UP+SELECT+DOWN", out[0])


if __name__ == "__main__":
    unittest.main()
