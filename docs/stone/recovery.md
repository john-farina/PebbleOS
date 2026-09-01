# Safety and going back

## What survives a bad build

Firmware and user data live in different flash regions. On obelix
(`flash_region_gd25q256e.h`):

| Region | Size | |
| --- | --- | --- |
| `FIRMWARE_SLOT_0` / `_1` | 3 MB each | what an update overwrites |
| `SYSTEM_RESOURCES_BANK_0` / `_1` | 2 MB each | double-banked, also replaced |
| **`FILESYSTEM`** | **21 MB** | **settings, apps, watchfaces, health, pairing** |

An install writes firmware into the *inactive* slot and resources into the
unused bank. **It never touches `FILESYSTEM`.** A bad build costs a reinstall,
not data.

Two things do wipe data, and both are deliberate acts rather than accidents:

- **Factory reset.** `factory_reset()` wipes the filesystem *and* invalidates
  both firmware slots.
- **A filesystem newer than the firmware understands.** `pfs_init()` treats a
  filesystem whose version exceeds `PFS_CUR_VERSION` as inactive and formats it.
  `PFS_VERS` has never been bumped, so this is theoretical — but it is the one
  way a **downgrade** could cost data. Check before rolling back across any
  change to `pfs.c`.

## Two things that are easy to get wrong

**PRF is not a cheap flip back.** `src/fw/main.c` invalidates **both** firmware
slots when PRF boots, so recovery means a full firmware reinstall from the
phone. Data still survives.

**Reinstalling an older build does not necessarily make it boot.** The
bootloader picks the valid slot with the highest priority, and that priority is
stamped at **build** time (`tools/pblboot.py`), not install time. So installing
Monday's build while running Tuesday's can land in the inactive slot and simply
never boot — which is precisely the operation you do all day when hopping
between branch builds.

The intended fix is for the channel server to **re-stamp the priority as it
serves the bundle**, so what you install is always the newest-stamped image on
the watch. The pblboot header's own CRC covers only the firmware body, so the
priority field can be rewritten without invalidating it; only the bundle
manifest's CRC needs recomputing.

```{warning}
The boot-priority behaviour above is read from `tools/pblboot.py`, the tool that
*writes* the header. The bootloader that consumes it is not in this repository
or its submodules, so this is a strong inference rather than a verified fact.
Confirm it on hardware — install two builds in the wrong order and read
Settings → Stone — before relying on it.
```

## Ways back, in increasing order of disruption

1. **Reinstall a known-good bundle.** Data intact, minutes. Use a Safe Build.
2. **PRF**, then reinstall from the phone. Data intact, slower, and it wipes
   both slots.
3. **Factory reset.** Wipes data. Only when something is genuinely broken.
4. **Stock firmware via the official app.** The floor you can always return to.

## Safe Builds

`stone-safe.yml` builds a bundle and publishes it as a **GitHub Release**, not
an artifact — artifacts expire after 90 days, and a safety net with an expiry
date is not a safety net.

Run it from Actions → Stone Safe Build. It defaults to `main`, which is pristine
upstream with none of our patches, so that build doubles as the "is this our bug
or upstream's?" baseline. Point it at any ref to pin a Stone build worth
keeping.

It is deliberately **standalone rather than reusing `stone-build.yml`**: the
recovery path should not be breakable by a change to the everyday build path, so
the duplication is intentional. Don't refactor them together.

The release tag is version-shaped (`vX.Y.Z-safeN`) because `git describe` finds
the nearest reachable tag and `tools/gitinfo.py` raises on one it cannot parse —
see {doc}`firmware`.

## Before the first custom install

A retail Pebble Time 2 has **no exposed SWD header**, so `./pbl flash` is not
available. PRF plus the app's restore is the only way back.

Confirm a recovery boot and a stock restore work **once, deliberately, before
installing any custom firmware.** The procedure is not documented in this
repository; get it from Core Devices' support rather than guessing.

Keep the `_loghash_dict.json` that ships beside each bundle. Release builds do
not carry it, and without the matching one a crash log is a wall of hashes.
