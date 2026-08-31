# Stone OS

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

## Recovery

A retail Pebble Time 2 has no exposed SWD header, so `./pbl flash` is not
available. PRF is both the rollback path and the only way back from a bad flash.
Confirm a recovery boot and a stock restore work **before** installing any
custom firmware.
