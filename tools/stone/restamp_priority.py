#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 John Farina
# SPDX-License-Identifier: Apache-2.0
"""Rewrite the boot priority in a built bundle so it boots when installed.

The bootloader runs the valid slot with the highest priority, and `pblboot.py`
stamps that priority at *build* time. For anything that is not an exact release
tag it is `DEV band | build wall-clock`, so the most recently **built** image
wins — not the most recently installed one.

That is fine going forwards and wrong going backwards: reinstalling last week's
build while running today's writes it to the inactive slot, where it loses the
priority comparison and never runs. Rolling back appears to do nothing.

Re-stamping fixes it without a rebuild. The pblboot header's own CRC covers only
the firmware body after the header, so the priority field can be rewritten in
place; what has to be recomputed is the bundle manifest's CRC, which is taken
over the whole file, header included.
"""

import argparse
import json
import shutil
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pblboot
import stm32_crc

# struct.pack("<LLQLLL", MAGIC, hdr_len, priority, offset, length, crc)
_HDR = struct.Struct("<LLQLLL")
_PRIORITY_OFFSET = 8


def read_priority(image):
    """Return the priority stamped in `image`, raising if it has no header."""
    if len(image) < _HDR.size:
        raise ValueError("firmware is shorter than a pblboot header")
    magic, hdr_len, priority, _offset, _length, _crc = _HDR.unpack_from(image)
    if magic != pblboot.MAGIC:
        raise ValueError(
            f"no pblboot header: magic is 0x{magic:08x}, expected 0x{pblboot.MAGIC:08x}"
        )
    if hdr_len != _HDR.size:
        raise ValueError(f"unexpected pblboot header length {hdr_len}")
    return priority


def restamp_image(image, priority):
    """Return `image` with its boot priority replaced."""
    read_priority(image)
    return (
        image[:_PRIORITY_OFFSET]
        + struct.pack("<Q", priority)
        + image[_PRIORITY_OFFSET + 8 :]
    )


def describe(priority):
    band = priority >> 56
    name = {
        pblboot.PRIORITY_BAND_DEV: "dev",
        pblboot.PRIORITY_BAND_RELEASE: "release",
    }.get(band, f"band 0x{band:02x}")
    return f"0x{priority:016x} ({name})"


def restamp_bundle(src, dst, priority):
    """Copy `src` to `dst`, re-stamping every firmware it contains.

    A merged dual-slot bundle holds `slot0/` and `slot1/`, each with its own
    manifest and firmware; a single-slot bundle holds one at the top level.
    Both are handled by treating every manifest found as its own bundle.
    """
    stamped = []

    with zipfile.ZipFile(src, "r") as zin:
        names = zin.namelist()
        manifests = [n for n in names if n.rsplit("/", 1)[-1] == "manifest.json"]
        if not manifests:
            raise ValueError(f"{src}: no manifest.json, not a bundle")

        # Read everything up front: entries are rewritten, so the output cannot
        # be streamed from the input.
        blobs = {name: zin.read(name) for name in names}

    for manifest_name in manifests:
        prefix = manifest_name[: -len("manifest.json")]
        manifest = json.loads(blobs[manifest_name])
        firmware = manifest.get("firmware")
        if firmware is None:
            raise ValueError(f"{manifest_name}: no firmware section")

        fw_name = prefix + firmware["name"]
        if fw_name not in blobs:
            raise ValueError(f"{manifest_name}: names a missing firmware {fw_name}")

        was = read_priority(blobs[fw_name])
        image = restamp_image(blobs[fw_name], priority)
        blobs[fw_name] = image

        # The manifest CRC covers the header too, so it moves with the stamp.
        firmware["crc"] = stm32_crc.crc32(image) & 0xFFFFFFFF
        blobs[manifest_name] = (json.dumps(manifest) + "\n").encode()

        stamped.append((fw_name, was))

    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        for name in names:
            zout.writestr(name, blobs[name])

    return stamped


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("pbz", help="bundle to re-stamp")
    p.add_argument("--output", help="write here instead of in place")
    p.add_argument(
        "--priority",
        type=lambda v: int(v, 0),
        help="explicit 64-bit priority; default is a dev-band stamp of now",
    )
    p.add_argument(
        "--check",
        action="store_true",
        help="report the stamped priorities and change nothing",
    )
    args = p.parse_args()

    src = Path(args.pbz)
    if not src.is_file():
        sys.exit(f"no such bundle: {src}")

    if args.check:
        try:
            with zipfile.ZipFile(src, "r") as zin:
                for name in zin.namelist():
                    if name.rsplit("/", 1)[-1] != "manifest.json":
                        continue
                    prefix = name[: -len("manifest.json")]
                    manifest = json.loads(zin.read(name))
                    fw_name = prefix + manifest["firmware"]["name"]
                    print(f"{fw_name}: {describe(read_priority(zin.read(fw_name)))}")
        except (zipfile.BadZipFile, KeyError, ValueError) as e:
            sys.exit(f"{src}: {e}")
        return 0

    # boot_priority() with no tag is the dev band stamped with the current
    # time, which is exactly what "make this the one that boots" means.
    priority = args.priority if args.priority is not None else pblboot.boot_priority()

    dst = Path(args.output) if args.output else src.with_suffix(".restamped.pbz")
    try:
        stamped = restamp_bundle(src, dst, priority)
    except (zipfile.BadZipFile, KeyError, ValueError) as e:
        # In place means the original is only replaced once a whole new bundle
        # exists, so a failure here leaves the input untouched.
        dst.unlink(missing_ok=True)
        sys.exit(f"{src}: {e}")

    if not args.output:
        shutil.move(dst, src)
        dst = src

    for name, was in stamped:
        print(f"{name}: {describe(was)} -> {describe(priority)}")
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
