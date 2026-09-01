# PebbleOS

PebbleOS is the operating system running on Pebble smartwatches.

## Organization

- `docs`: project documentation
- `resources`: firmware resources (icons, fonts, etc.)
- `sdk`: application SDK generation files
- `src`: firmware source
- `subsys`: OS subsystems, e.g. logging
- `tests`: tests
- `third_party`: third-party code in git submodules, also includes glue code
- `tools`: a variety of tools or scripts used in multiple areas, from build
  system, tests, etc.
- `tools/libs`: Python packages used in multiple areas, e.g. log dehashing,
  console, etc.
- `tools/cmake`: generators the CMake firmware build shells out to
- `tools/waf`: scripts used by waf, which still builds the unit tests

## Documentation

Contributor documentation lives in `docs/` (published at
https://pebbleos-core.readthedocs.io). Prefer pointing to or extending those
pages over duplicating knowledge here: `docs/development/contributing.md`
(DCO, commit and AI-usage rules), `docs/development/sdk_export.md` (SDK
export machinery), `docs/development/qemu.md` (emulator workflow).

## Code style

- clang-format for C code
- ruff for Python code
- Keep code comments short and concise. Extended descriptions can be kept in
  the Git commit message.
- Do not put references to issues in the code, only add those to the Git commit message.

## Logging

- `PBL_LOG_WRN` / `PBL_LOG_ERR` are for warnings and errors — use them as
  the names suggest.
- Default to `PBL_LOG_DBG` for routine lifecycle / state-transition logs.
  Reserve `PBL_LOG_INFO` for events that genuinely warrant attention in a
  default-level log capture; if a code path can fire repeatedly under
  normal use (e.g. play/pause spam, frequent state changes), it must not
  log at INFO.

## Firmware development

- Configure: `./pbl configure --board BOARD_NAME`

  - Board names can be obtained from `./pbl --help`
  - `-DCONFIG_RELEASE=y` enables release mode
  - `-DCONFIG_MFG=y` enables manufacturing mode
  - `--variant=normal|prf` selects build variant (default: normal)

- Build firmware: `./pbl build`
- Run tests: `./pbl test`

## Adding a new SDK function

Exposing a function to third-party apps requires three coordinated changes
(applib wrapper + syscall, `exported_symbols.json` registration, SDK
revision bump) — the firmware build alone won't surface it to apps. Follow
`docs/development/sdk_export.md` whenever an `applib/` function should
become callable from user apps.

## Git rules

Main rules:

- Commit using `-s` git option, so commits have `Signed-Off-By`
- Always indicate commit is co-authored by the current AI model
- Commit in small chunks, trying to preserve bisectability
- Commit format is `area: short description`, with longer description in the
  body if necessary
- Run `gitlint` on every commit to verify rules are followed

Others:

- If fixing Linear or GitHub issues, include in the commit body a line with
  `Fixes XXX`, where XXX is the issue number.

# Stone OS (this fork)

This repository is **Stone OS**, a personal fork of PebbleOS for a Pebble
Time 2 (`obelix@pvt`). Read `docs/stone/index.md` before changing anything —
the fork has conventions that exist to keep it maintainable against an upstream
landing ~12 commits a day, and ignoring them is how it becomes unmaintainable.

| Read | For |
| --- | --- |
| `docs/stone/index.md` | Naming rule, branch model, commit conventions, resolving a sync conflict |
| `docs/stone/ci.md` | The `stone-*.yml` workflows, the CI gates, and how to reproduce them locally |
| `docs/stone/features.md` | **How to work on a feature here.** Start here if you are about to build something |
| `docs/stone/simulator.md` | Booting a build in QEMU before it ever reaches the watch |
| `docs/stone/firmware.md` | What the fork changes in the firmware and how to add more |
| `docs/stone/channels.md` | Installing a branch on the watch, switching back, retiring one |
| `docs/stone/companion-app.md` | The phone app fork, and what must change in lockstep with it |
| `docs/stone/recovery.md` | Flash layout, what survives a bad build, every way back |
| `docs/stone/roadmap.md` | What is not built yet and which decisions are still open |

## Building a feature

**Start the branch with the script, not by hand:**

```shell
tools/stone/new_feature.py "what you are building"
```

It branches from `origin/stone` and writes `docs/stone/features/<slug>.md`,
which every feature branch carries. That file is the handoff: sessions here do
not remember each other, so what was tried, what broke, and what was decided
survives only if it is written down as you go — not at the end, which is where
sessions run out of context.

If you are picking up an existing `feat/*` branch, **read its notes file before
its diff**, and check that "Where it stands" still matches
`git log --oneline origin/stone..HEAD`.

Read `docs/stone/features.md` for the rest: the sections and what belongs in
each, what to verify locally before burning a seven-minute CI cycle, and how
John installs a branch to test it.

The five things most likely to waste your time:

- **Work on `stone`, never `main`.** `main` is an exact upstream mirror. PRs
  target `stone` and are merged with **rebase**, never a merge commit.
- **Keep the diff against upstream-owned files small.** Prefer new files behind
  `CONFIG_STONE`; when a line must go into an upstream file, append it at the
  end of its block. Check the cost with
  `git diff --numstat origin/main..origin/stone`.
- **Upstream's workflows only trigger on `main`**, so our PRs get CI only from
  `stone-build.yml` and `compliance.yml`. Never edit an upstream workflow file.
- **Boot it in the simulator before John installs it.** `./sim rebuild && ./sim
  boot`, then `./sim shot` and read the PNG. CI compiles the firmware but never
  runs it, and the first Stone build to reach the real watch sent it to PRF.
  `qemu_emery` is 200x228 like the real Pebble Time 2, but it cannot boot
  `obelix` itself (no SiFli target in QEMU), so it proves shared code only.
  See `docs/stone/simulator.md`.
- **CI is the only thing that builds an installable obelix bundle**, and each
  cycle is ~7 minutes. Verify locally and statically first.
- **`gitlint` installs as `gitlint-core`** (plain `gitlint` fails to build), and
  its default rules reject the word "WIP" in a commit title.
- **Boot priority is stamped at build time, not install time.** Installing an
  older build does not make it boot; re-stamp it first with
  `tools/stone/restamp_priority.py`. See `docs/stone/recovery.md`.
- **The companion app is a second repository with a shared contract.** The
  `/ota/latest` response shape, the channel server URL and the version floor are
  agreed with <https://github.com/john-farina/mobileapp> (branch `stone`), and
  breaking either side fails silently. See `docs/stone/companion-app.md` before
  changing any of them.
- **Stone builds must report `v200.x`.** `stone-build.yml` derives that floor per
  build. If a build reports `v4.36.x`, the floor did not apply and the companion
  app will treat the install as a downgrade — it reboots into PRF and restores
  Core's firmware over yours, which looks like a failed install but is an install
  of something else. Check the channel entry before John installs anything.

The channel server is `tools/stone/channel/` — one Cloudflare Worker, no
dependencies, `npm test` runs its 28 cases with no network. Its README explains
why bundles live in KV rather than in release assets.
