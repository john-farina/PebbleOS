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

Stone builds report `v200.0.0.1-<n>-g<sha>`, so the companion app parses the
version as `200.0.0`. This is not vanity. It is load-bearing.

The app compares `major.minor.patch` against the running firmware and calls
anything lower a **downgrade** — and its downgrade path reboots the watch into
PRF. Once in PRF, its routine update check runs, finds Core's shipping build
newer, and **installs that over the firmware you just sideloaded.** The build you
asked for is never transferred.

This is what it looks like in a bug report, and it is the whole failure in three
lines:

```
[I] FWUpdate: Downgrade to v4.35.0-140-ga88d17811: rebooting watch into PRF
[I] PebbleConnector: PRF running; going into recovery mode
[D] FirmwareUpdateManager: firmwareUpdateAvailable = FoundUpdate(version=v4.36.2)
```

It presents as a failed install. It is a successful install of something else.
The giveaway is that the build never reaches `InProgress`, and its build ID never
appears in the watch logs at all.

Syncing upstream cannot fix it. Core ships `v4.36.2` from `4.36-branch`, a
release branch that upstream `main` never reaches — `main` describes as
`v4.36.0-<n>`, permanently *lower* than what is on the wrist.

### How the floor is applied

`stone-build.yml` creates the tag **during the build** and never pushes it:

```yaml
base=$(git merge-base HEAD origin/main)
git tag -fa "${STONE_VERSION_FLOOR}" -m "Stone version floor" "$base"
```

Three details, each of which cost a cycle to learn:

- **Derived, not pushed.** The nightly sync rebases the patch queue, so a pushed
  tag is orphaned by the next rebase — `git describe` would fall back to
  `v4.36.0-<n>` and every install would quietly be a downgrade again, with no
  error anywhere. Deriving it each build is immune.
- **Annotated (`-fa`), not lightweight.** `tools/gitinfo.py` calls plain
  `git describe`, which considers **annotated tags only**. A lightweight tag is
  silently ignored and the version falls back — the same failure, harder to spot.
- **On the upstream base, not on `HEAD`.** Tagging `HEAD` makes `git describe`
  return a bare `v200.0.0.1` with no `-<n>-g<sha>`, so every build would carry an
  identical version string.

A pushed `v*` tag also fires upstream's `release.yml`, which builds firmware,
PRF, QEMU and the SDK shell and publishes a GitHub Release — every time. Not
pushing the tag avoids that too.

### Why 200 and not 999

`GIT_MAJOR_VERSION` / `MINOR` / `PATCH` are assigned into `WatchInfoVersion` in
`applib/app_watch_info.c`, and all three fields are `uint8_t`. Anything over 255
is a hard build failure, not a silent wrap:

```
git_version.auto.h:5: error: unsigned conversion from 'int' to 'unsigned char'
changes value from '999' to '231' [-Werror=overflow]
```

200 clears anything Core will ship and leaves headroom. Bump the fourth
component for milestones; it is ours.

### Checking it

The version is visible in the build's channel entry and in `Settings` → `Stone`
on the watch. If either shows `v4.36.x` instead of `v200.x`, the floor did not
apply and that build will be treated as a downgrade — do not install it.

```{note}
`main` deliberately does **not** carry the floor. Safe Builds come from `main`
and their versions should stay truthful, which is also why `stone-safe.yml`
excludes `*safe*` when deriving its base: a previous safe tag sits on the same
history and is nearer than the real upstream tag, so without the exclusion each
safe build derives its version from the last one and the base freezes.
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
installing any custom firmware.**

**This path is now known to work** (2026-09-01, `obelix@pvt`, iOS app): the watch
showed the PRF QR screen, the app detected it, restored firmware over Bluetooth,
and came back with settings, apps, watchfaces and pairing intact. No button combo
was needed — the install itself put the watch into PRF, and the app drove the
recovery. `FILESYSTEM` was untouched.

The manual entry gesture is still undocumented here. If you ever need to reach
PRF without the app putting you there, ask Core Devices rather than guessing —
nothing in this repository specifies it for SF32LB52 hardware.

Keep the `_loghash_dict.json` that ships beside each bundle. Release builds do
not carry it, and without the matching one a crash log is a wall of hashes.
