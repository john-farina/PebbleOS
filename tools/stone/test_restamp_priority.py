# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Round-trip tests for the boot-priority re-stamp.

Run with `python -m unittest discover -s tools/stone`. These build a synthetic
bundle rather than a real one so they stay runnable without a firmware build.
"""

import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pblboot
import stm32_crc
from restamp_priority import (
    read_priority,
    restamp_bundle,
    restamp_image,
)

OFFSET = 512
BODY = bytes(range(256)) * 8


def make_firmware(priority):
    """A firmware image shaped like the one `pblboot.insert_header_bin` writes."""
    crc = 0  # zlib CRC of the body; unread by the re-stamp, so any value works.
    header = struct.pack("<LLQLLL", pblboot.MAGIC, 28, priority, OFFSET, len(BODY), crc)
    return header + b"\xff" * (OFFSET - len(header)) + BODY


def make_bundle(path, slots=(0, 1), priority=0x8000000012345678):
    """A merged dual-slot bundle: `slotN/manifest.json` plus its firmware."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for slot in slots:
            image = make_firmware(priority)
            prefix = f"slot{slot}/"
            z.writestr(prefix + "pebbleos.bin", image)
            z.writestr(
                prefix + "manifest.json",
                json.dumps(
                    {
                        "manifestVersion": 2,
                        "firmware": {
                            "name": "pebbleos.bin",
                            "type": "normal",
                            "slot": slot,
                            "size": len(image),
                            "crc": stm32_crc.crc32(image) & 0xFFFFFFFF,
                        },
                    }
                ),
            )
            z.writestr(prefix + "pebbleos_loghash_dict.json", "{}")


class ReadPriority(unittest.TestCase):
    def test_reads_what_pblboot_stamped(self):
        self.assertEqual(read_priority(make_firmware(0x1234)), 0x1234)

    def test_rejects_an_image_with_no_header(self):
        with self.assertRaisesRegex(ValueError, "no pblboot header"):
            read_priority(b"\x00" * 1024)

    def test_rejects_a_truncated_image(self):
        with self.assertRaisesRegex(ValueError, "shorter than"):
            read_priority(b"\x00" * 4)


class RestampImage(unittest.TestCase):
    def test_replaces_only_the_priority(self):
        before = make_firmware(0x1111111111111111)
        after = restamp_image(before, 0x2222222222222222)

        self.assertEqual(read_priority(after), 0x2222222222222222)
        self.assertEqual(len(after), len(before))
        # Everything on either side of the 8-byte field is untouched, which is
        # what lets the header's own body CRC stay valid.
        self.assertEqual(after[:8], before[:8])
        self.assertEqual(after[16:], before[16:])


class RestampBundle(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_restamps_both_slots_and_fixes_their_manifests(self):
        src = self.tmp / "in.pbz"
        dst = self.tmp / "out.pbz"
        make_bundle(src, priority=0x8000000011111111)

        stamped = restamp_bundle(src, dst, 0x8000000022222222)
        self.assertEqual(len(stamped), 2)

        with zipfile.ZipFile(dst) as z:
            for slot in (0, 1):
                image = z.read(f"slot{slot}/pebbleos.bin")
                manifest = json.loads(z.read(f"slot{slot}/manifest.json"))

                self.assertEqual(read_priority(image), 0x8000000022222222)
                # A stale CRC here is the failure mode that matters: the watch
                # rejects the bundle rather than installing something wrong.
                self.assertEqual(
                    manifest["firmware"]["crc"], stm32_crc.crc32(image) & 0xFFFFFFFF
                )
                self.assertEqual(manifest["firmware"]["slot"], slot)
                self.assertEqual(manifest["firmware"]["size"], len(image))

    def test_keeps_every_other_entry(self):
        src = self.tmp / "in.pbz"
        dst = self.tmp / "out.pbz"
        make_bundle(src)

        with zipfile.ZipFile(src) as z:
            before = sorted(z.namelist())
        restamp_bundle(src, dst, 0x8000000033333333)
        with zipfile.ZipFile(dst) as z:
            self.assertEqual(sorted(z.namelist()), before)
            self.assertEqual(z.read("slot0/pebbleos_loghash_dict.json"), b"{}")

    def test_a_dev_stamp_of_now_outranks_any_earlier_build(self):
        # The whole point: what you just installed has to win the bootloader's
        # comparison against whatever is in the other slot.
        earlier = pblboot.boot_priority()
        later = pblboot.boot_priority()
        self.assertGreaterEqual(later, earlier)
        self.assertGreater(later, (pblboot.PRIORITY_BAND_RELEASE << 56) | 0xFFFFFFFF)

    def test_rejects_something_that_is_not_a_bundle(self):
        src = self.tmp / "in.zip"
        with zipfile.ZipFile(src, "w") as z:
            z.writestr("readme.txt", "hello")
        with self.assertRaisesRegex(ValueError, "not a bundle"):
            restamp_bundle(src, self.tmp / "out.pbz", 1)

    def test_rejects_a_manifest_naming_a_missing_firmware(self):
        src = self.tmp / "in.pbz"
        with zipfile.ZipFile(src, "w") as z:
            z.writestr(
                "manifest.json", json.dumps({"firmware": {"name": "absent.bin"}})
            )
        with self.assertRaisesRegex(ValueError, "missing firmware"):
            restamp_bundle(src, self.tmp / "out.pbz", 1)


if __name__ == "__main__":
    unittest.main()
