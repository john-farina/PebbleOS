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
- `tools/waf`: scripts used by the waf build system

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

<!-- ===== fork-local (john-farina); keep at end of file for easy rebase ===== -->

## Simulator (fork-local)

This checkout has a working simulator. Use the `pebble-sim` skill whenever the
task involves running PebbleOS, checking how something looks, or iterating on
UI — it documents the `./sim` wrapper, which sources the PebbleOS SDK and
`.venv` for you and handles QEMU process cleanup.

Short version: `./sim boot` (never pipe it), `./sim rebuild`, `./sim shot`,
`./sim key back|select|up|down`. Default board is `qemu_emery` — the Pebble
Time 2 platform at its real 200x228 geometry.

Verify visual changes yourself by taking a screenshot and reading the PNG.
Do not ask the user to look at the emulator window.

## Git workflow (fork-local)

Two branches, deliberately separated:

- **`mine`** — the working branch, tracks `origin/mine` (the personal fork).
  All fork-local work lands here, including `./sim`, the `pebble-sim` skill,
  and this section. Default branch to be on.
- **`main`** — a clean mirror, tracks `upstream/main`. Never commit to it.
  Refresh with `git fetch upstream && git switch main && git merge --ff-only
  upstream/main`, then `git switch mine && git rebase main`.

Pushing to `upstream` is disabled at the remote, so pushes cannot go to
coredevices by accident.

**Contributing something upstream later:** branch off clean `main`, never off
`mine` — `git switch -c area/thing main` — then cherry-pick just the relevant
commits so no fork-local file rides along. Push to `origin` and open the PR
from there.

Commits are governed by upstream `CONTRIBUTING.md` and enforced by hooks:

- `prepare-commit-msg` adds the DCO `Signed-off-by` automatically; it must
  match the commit author, so never override the author.
- `commit-msg` runs `gitlint` with the repo's custom rules. Title must be
  `area: description`; body wraps at 100 columns.
- Commits must be atomic and self-contained — one logical change each,
  preserving bisectability.
- Add `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
  whenever an AI wrote part of the change. This is a required disclosure
  upstream, and it is an attestation — never add it to a commit the AI did
  not actually write, and never omit it from one it did.
- `Fixes #123` in the body for issue references; never in code comments.

Upstream's generative-AI policy makes the *submitter* responsible for
personally reviewing AI-written code before opening a PR. Flag that
expectation rather than assuming a generated change is PR-ready.
