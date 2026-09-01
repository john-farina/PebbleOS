---
orphan: true
---

# Stone version number

| | |
| --- | --- |
| **Branch** | `feat/stone-version-number` |
| **Started** | 2026-09-01 |
| **Status** | Written and building. Not yet seen on a screen |

## What this is

John's words:

> i want stone to have its own version updates - where we can show the real version in the
> settings off of main so real pebbleOS and then show a seperate version number for stoneOS and
> stoneOS version goes first then the real pebbleOS that its running underneath

So Settings → Stone leads with Stone's own version, then names the PebbleOS release it runs on:

| Row | Value |
| --- | --- |
| **Stone** | `0.1.0` |
| **PebbleOS** | `v4.35.0` |
| Branch | `stone` |
| Commit | `97945fe` |
| Firmware | `v4.35.0-126-gf33a5d751` |
| Slot | `0` |

Two numbers meaning different things: what this fork is at, and what upstream release is
underneath it.

## Where it stands

Written, and it builds — `./pbl build` for `qemu_emery` links clean and the generated header
comes out right:

```c
#define STONE_VERSION "0.1.0"
#define STONE_BRANCH "feat/stone-version-number"
#define STONE_COMMIT "2f22d77"
#define STONE_BASE   "v4.36.0"
#define STONE_DIRTY  1
```

Rebased onto the `stone` that carries the version floor (`f65f7025d`), which is what surfaced
the `STONE_BASE` bug above.

**Not yet looked at on a screen.** John had the simulator running in his own clone, and `./sim`
pkills every `qemu-pebble` process, so booting a second one would have taken his down. Confirming
Settings → Stone renders the two new rows is the one outstanding task.

## Decided

- **The version lives in `VERSION.stone` at the repo root, not in a git tag.** This is the whole
  design decision, and it is not a matter of taste. Every tag reachable from `stone` is found by
  `git describe`, and three parsers depend on the shape of what comes back: `tools/gitinfo.py`
  **raises on a tag it cannot parse and fails the build**, `tools/pblboot.py` sorts release and
  dev bands by it, and `libpebble3` orders updates on it. A tag like `stone-0.1.0` would be
  picked as the nearest tag and break *every* later build — the same trap
  `docs/stone/firmware.md` already records for `safe-2026-08-31`. A file keeps Stone's version
  and upstream's version as two numbers that never meet in `git describe`.
- **`src/fw/CMakeLists.txt` depends on `VERSION.stone`**, so a bump regenerates the header.
  Without that the file would be edited and the build would silently keep the old value, which is
  the kind of bug that wastes an afternoon.
- **A missing or empty `VERSION.stone` yields `"unknown"` rather than failing the build.** Every
  other field in `build_info.py` already degrades that way; build metadata should never be the
  reason firmware does not compile.
- **The PebbleOS row excludes the version floor and safe tags.** This is not cosmetic: it is a
  live bug in the existing "Upstream" row. `STONE_BASE` was
  `git describe --tags --abbrev=0 --match "v[0-9]*"`, and two kinds of tag are not upstream
  releases. `stone-build.yml` stamps an annotated `v200.0.0.1` on the merge-base with `main` at
  build time so an install is not read as a downgrade — and because it sits *nearer* than the
  real release tag, plain `describe` returns it, and the row would claim the watch runs PebbleOS
  v200. Safe-build tags do the same quietly: locally the row already resolved to
  `v4.35.0-safe2` rather than `v4.35.0`. With both excluded it now reads **v4.36.0**, which is
  the actual release underneath.

  The floor is excluded by name, from `STONE_VERSION_FLOOR` — a workflow-level `env:` in
  `stone-build.yml`, inherited by every step and passed through `cmake -E env`, so the constant
  is not duplicated and **no workflow edit was needed**.
- **The old "Upstream" row became "PebbleOS".** Same value (`STONE_BASE`), clearer name — John
  asked for "the real pebbleOS that its running underneath", and "Upstream" does not say that to
  someone reading it on a wrist. It also moved up to sit directly under the Stone row, because
  the two versions belong together.

## Tried and rejected

- Nothing built and rejected yet. One approach was **considered and dropped before writing
  code**: a `stone-X.Y.Z` git tag as the source of truth, which is the obvious way to version a
  fork and is specifically the thing that would break the build. See Decided.

## Open questions

- **What bumps the version, and when?** Right now it is a hand edit. It could be automated —
  bumped on merge to `stone`, or derived from commits since the last bump — but nothing needs
  that yet, and a version that changes by itself is worse than one that changes when John decides
  it has. **Worth revisiting once there is a second release.**

## How to test it

Install this channel, then:

1. Open **Settings → Stone**.
2. The first row reads **Stone**, with **0.1.0** under it.
3. The second row reads **PebbleOS**, with the upstream release under it — **v4.35.0** at the
   time of writing.
4. Below those, Branch, Commit, Firmware and Slot are unchanged.

There should be **no "Upstream" row** any more; it is the same value, renamed and moved up.

To check a bump works: edit `VERSION.stone` to `0.2.0`, rebuild, and the Stone row should read
`0.2.0`. If it still says `0.1.0`, the CMake dependency on `VERSION.stone` is not doing its job.

## Log

- **2026-09-01** — branch created.
- **2026-09-01** — Added `VERSION.stone`, threaded `STONE_VERSION` through `build_info.py` and
  the generated header, and reordered Settings → Stone so the Stone version leads and the
  PebbleOS release follows. Builds clean locally for `qemu_emery`; the generated header was
  checked by hand. Not yet viewed in the simulator — John's own instance was running and `./sim`
  kills every `qemu-pebble`, so a second one would have taken his down.
- **2026-09-01** — Rebased onto the `stone` that added the build-time version floor, and fixed
  what that did to this feature. The floor tag `v200.0.0.1` sits nearer than the real release
  tag, so `STONE_BASE` would have reported PebbleOS v200; safe tags were already making it report
  `v4.35.0-safe2`. Both are now excluded and it reads `v4.36.0`. Checked both ways by running
  `build_info.py` with and without `STONE_VERSION_FLOOR` set. Worth knowing: that env var is a
  workflow-level `env:` and reaches the generator through `cmake -E env` without any workflow
  change, which is the only reason this did not need one.
