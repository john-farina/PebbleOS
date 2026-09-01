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

The fix is `tools/stone/restamp_priority.py`, which rewrites that field in a
built bundle so the thing you are about to install outranks whatever is already
on the watch:

```shell
# What is stamped in a bundle right now
python3 tools/stone/restamp_priority.py --check stone_obelix_pvt_v4.36.0-98-g137d1852.pbz

# Make it the one that boots
python3 tools/stone/restamp_priority.py stone_obelix_pvt_v4.36.0-98-g137d1852.pbz
```

With no `--priority` it stamps the dev band with the current time — the same
thing a fresh build gets — so **re-stamping an archived bundle is how you roll
back**. A rebuild would work too, but it takes twenty minutes and produces
different bytes; a re-stamp takes a second and produces the build you already
tested.

It is safe because of where the CRCs sit. The pblboot header's own CRC covers
only the firmware body *after* the header, so the priority can be rewritten
without invalidating it. The bundle manifest's CRC is taken over the whole file,
header included, so that one is recomputed — with `tools/stm32_crc.py`, which is
the variant the manifest uses, not zlib's.

This deliberately does **not** happen in the channel server. Re-stamping means
unzipping, patching, recomputing a CRC over 3 MB and rezipping; on Cloudflare
Workers' free tier that is well past the 10 ms CPU budget per request, and
paying for compute to avoid a step CI already does for free is the wrong trade.
The server hands out bundles as built; a rollback re-stamps first and publishes
the result.

Every Stone build prints its stamped priority to the run summary, so comparing
two builds does not mean reading bytes out of a zip.

```{warning}
The boot-priority behaviour above is read from `tools/pblboot.py`, the tool that
*writes* the header. The bootloader that consumes it is not in this repository
or its submodules, so this is a strong inference rather than a verified fact.
Confirm it on hardware — install two builds in the wrong order and read
Settings → Stone — before relying on it.
```

## Why Stone versions start at 200

Stone builds are tagged from `v200.0.0.1`, so `git describe` on `stone` yields
`v200.0.0.1-<n>-g<sha>` and the companion app parses the version as `200.0.0`.

This is not vanity. It is load-bearing.

The app compares `major.minor.patch` against the running firmware and calls
anything lower a **downgrade** — and its downgrade path reboots the watch into
PRF. Once in PRF, its routine update check runs, finds Core's shipping build
newer, and **installs that over the firmware you just sideloaded.** The build
you asked for is never transferred. It looks exactly like a failed install; it
is not. It is a successful install of something else.

That is not avoidable by syncing upstream. Core ships `v4.36.2` from
`4.36-branch`, a release branch that upstream `main` never reaches — `main`
describes as `v4.36.0-<n>`, which is permanently *lower* than what is on the
wrist. Every main-derived build would be a downgrade forever.

So the fork carries its own version floor, chosen high enough to never be
overtaken. The fourth component is ours to bump.

**Why 200 and not 999.** `GIT_MAJOR_VERSION` / `MINOR` / `PATCH` are assigned
into `WatchInfoVersion` in `applib/app_watch_info.c`, and all three fields are
`uint8_t`. Anything over 255 is a hard build failure, not a silent wrap:

```
git_version.auto.h:5: error: unsigned conversion from 'int' to 'unsigned char'
changes value from '999' to '231' [-Werror=overflow]
```

200 clears everything Core will ship and still leaves headroom under the cap.

```{warning}
**The tag must stay reachable from `stone`.** The nightly sync rebases the patch
queue, and a tag left on a rebased-away commit stops being an ancestor —
`git describe` then silently falls back to `v4.36.0-<n>` and every install
becomes a downgrade again, with no error anywhere to say so.

After a sync that rewrites the queue, check it:

```shell
git describe --tags stone      # must start with v200.
```

Re-tag if it does not. `main` must **not** carry this tag — Safe Builds are
built from `main` and their versions should stay truthful.
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
