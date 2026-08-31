# Stone OS

```{image} logo.svg
:alt: Stone OS
:width: 128px
```

Stone OS is a personal fork of [PebbleOS](https://github.com/coredevices/PebbleOS),
running on a Pebble Time 2 (`obelix@pvt`).

Upstream moves fast — roughly twelve commits a day — so this fork is built
around one property: **`git log main..stone` is always, precisely, our
changes.** Every convention below exists to protect that, because once the
answer to "what have I actually changed?" stops being cheap to compute,
upstream syncs get scary and stop happening.

```{toctree}
:glob:
:hidden:
*
```

## Naming: Stone outside, Pebble inside

Stone replaces every **user-visible** mention of Pebble. It does not replace
identifiers in code.

`Pebble`/`pebble` appears around four thousand times across roughly two thirds
of the source tree. Renaming those would conflict with nearly every upstream
commit forever, and would also break the SDK ABI that third-party watchapps
link against, the `.pbw`/`.pbz` formats, and the protocol the companion app
speaks.

| Stays "Pebble" | Becomes "Stone" |
| --- | --- |
| C identifiers, `PBL_*` macros, `pebble_*` filenames | Boot splash |
| `.pbw` / `.pbz`, protocol endpoints, BlobDB ids | On-watch UI strings |
| `sdk/`, `exported_symbols.json`, applib headers | The companion app's name |
| Upstream files generally | Our branches, workflows, directories |

Everything this fork adds is named Stone: the `stone` branch, `CONFIG_STONE_*`,
`Kconfig.stone`, `src/fw/stone/`, `apps/stone/`, `tools/stone/`, and
`.github/workflows/stone-*.yml`.

## Branches

| Branch | Contains | Who moves it |
| --- | --- | --- |
| `main` | An exact mirror of upstream. Never commit here. | The nightly sync, fast-forward only |
| `stone` | Upstream plus our patch queue. The default branch and the base for every PR. | The nightly sync rebases it; we push to it |
| `feat/*` | One topic in progress. Each gets its own build channel. | Us |

Our commits never merge into upstream history — they are replayed on top of it.
That is what keeps `main..stone` readable at commit 3 and at commit 300, and
what makes a conflict land on one identifiable patch instead of one large merge.

## Keeping the rebase cheap

These matter more than the automation. A rebase queue only stays painless if
the patches are shaped to avoid upstream's hot files.

- **Never edit an upstream workflow file.** Add new ones named `stone-*.yml`.
  Upstream touches `.github/workflows/` constantly, and every edit there is a
  guaranteed recurring conflict.
- **Prefer new files behind a Kconfig symbol over edits to existing ones.**
  Fork-local code goes in new directories and depends on `CONFIG_STONE`, so the
  permanent diff against upstream files stays near zero.
- **Append, don't insert.** When a line must go into an upstream file, put it at
  the end of the relevant block. `rsource "Kconfig.stone"` at the foot of the
  root `Kconfig` is the model.
- **One logical change per commit.** A rebase resolves commit by commit, so
  small commits are directly less work.
- **Order the queue**: build and CI patches first, then features, then
  experiments. When a rebase gets ugly, drop from the bottom.

Current permanent diff against upstream files:

| File | Change |
| --- | --- |
| `Kconfig` | one `rsource "Kconfig.stone"` line |
| `docs/index.md` | one toctree block |
| `src/fw/board/splash.h` | three lines selecting the Stone wordmark |
| 11 source files | one user-visible string each (the rebrand) |

The eleven string edits are the unavoidable cost of the rebrand: they live
inside upstream files by definition. Each is a single line, so a conflict is a
one-line resolution that `rerere` learns once.

## Commits

Upstream's rules, enforced by `gitlint`:

- `area: short description` — lower-case area, imperative description
- sign off with `git commit -s`
- keep the body wrapped at 100 columns
- run `gitlint` before pushing

Use `stone:` as the area for fork-local work that doesn't belong to an existing
upstream area.

## Set up rerere once

```shell
git config rerere.enabled true
git config rerere.autoupdate true
```

The nightly sync rebases the same patches over and over, so the same conflicts
recur. `rerere` records how you resolved one and replays it, which makes the
second and later occurrences invisible. The CI sync caches its `rr-cache` for
the same reason.

## Resolving a sync conflict

When the nightly rebase fails it aborts, leaves the branch on its old base, and
opens an issue naming the patch and the files. Nothing is lost and nothing is
half-applied — the branch still builds exactly as it did.

```shell
git fetch upstream main
git checkout stone
git rebase upstream/main
# fix the conflict, then
git rebase --continue
git push --force-with-lease origin stone
```

If a patch has been overtaken by upstream — they implemented it, or the code it
touched is gone — drop it with `git rebase --skip` rather than forcing it
through, and delete it from the queue.

## Building something installable

A Pebble Time 2 is dual-slot and sealed, which constrains what it will accept:

- An OTA writes into the watch's **inactive** slot, so a flashable bundle is
  built twice and stitched together with `tools/merge_pbz.py`.
- It must be a **release** build. The watch refuses a debug bundle over release
  firmware.
- The board is `obelix@pvt`. `@dvt` will not match a retail unit.

```shell
for slot in 0 1; do
  ./pbl configure --board obelix@pvt -DCONFIG_FIRMWARE_SLOT=$slot -DCONFIG_RELEASE=y
  ./pbl build
  ./pbl bundle
done
python3 tools/merge_pbz.py \
  --slot0-pbz build/normal_obelix_pvt_<version>_slot0.pbz \
  --slot1-pbz build/normal_obelix_pvt_<version>_slot1.pbz \
  --output stone_obelix_pvt_<version>.pbz
```

Release builds drop the debug serial console, which is not reachable on a sealed
watch anyway, and disable arbitrary file and flash reads over `get_bytes`. Crash
coredump retrieval is **not** gated, so crashes still come off the watch through
the companion app. Release bundles do not carry the loghash dictionary, so keep
`build/pebbleos_loghash_dict.json` alongside every build you install — you need
the matching one to read a crash.

## Versions

Fork builds are tagged `v<upstream base>-f<build number>`, for example
`v4.36.2-f42`. Not a fourth version component: the companion app parses versions
as `v?(\d+)\.(\d+)(?:\.(\d+))?(?:-(.*))?` and orders them on major, minor and
patch only, so anything after a third component is invisible to it.

Because the app compares only those three numbers, every build sharing an
upstream base compares as equal — so moving between branch builds on the same
base installs directly into the inactive slot. Only crossing an upstream base
backwards reads as a downgrade.

## What survives a bad build

Firmware and user data live in different flash regions. On obelix:

| Region | |
| --- | --- |
| `FIRMWARE_SLOT_0` / `_1` | 3 MB each — what an OTA overwrites |
| `SYSTEM_RESOURCES_BANK_0` / `_1` | 2 MB each — double-banked, also replaced |
| `FILESYSTEM` | **21 MB — settings, apps, watchfaces, health, pairing** |

An install writes the firmware into the *inactive* slot and resources into the
unused bank. **It never touches `FILESYSTEM`.** So a bad build costs you a
reinstall, not your data.

Two things do wipe data, and both are deliberate acts rather than accidents:

- **Factory reset.** Wipes the filesystem and invalidates both firmware slots.
- **A filesystem newer than the firmware understands.** `pfs_init()` treats a
  filesystem whose version exceeds `PFS_CUR_VERSION` as inactive and formats it.
  In practice `PFS_VERS` has never been bumped, so this is theoretical — but it
  is the one way a *downgrade* could cost you data, so check before rolling back
  across a version that touched `pfs.c`.

## Going back

In increasing order of disruption:

1. **Reinstall a known-good bundle.** Data intact, minutes. Use a Safe Build
   release (below). Note that the bootloader picks the slot with the highest
   boot priority and that priority is stamped at *build* time, so re-installing
   an older build does not necessarily make it boot — the channel server
   re-stamps priority on serve to make this reliable.
2. **PRF.** Recovery boot, then reinstall from the phone. Data intact. Note that
   booting PRF **invalidates both firmware slots** (`src/fw/main.c`), so this is
   a full firmware reinstall rather than a quick flip back.
3. **Factory reset.** Wipes data. Only when something is genuinely broken.
4. **Stock firmware via the official app.** The floor you can always return to.

## Safe Builds

`stone-safe.yml` builds a bundle and publishes it as a **GitHub Release**, not
an artifact — artifacts expire after 90 days, and a safety net with an expiry
date is not a safety net.

Run it from Actions → Stone Safe Build. It defaults to `main`, which is pristine
upstream with none of our patches, so that build doubles as the "is this our bug
or upstream's?" baseline. Point it at any ref to pin a Stone build you liked.

The release tag is deliberately version-shaped (`v4.36.0-safe12`): `git
describe` feeds `tools/gitinfo.py`, which hard-fails on a tag it cannot parse,
so a tag like `safe-2026-08-31` would break every later build that can reach it.

## Recovery

A retail Pebble Time 2 has no exposed SWD header, so `./pbl flash` is not
available. PRF is both the rollback path and the only way back from a bad flash.
Confirm a recovery boot and a stock restore work **before** installing any
custom firmware.
