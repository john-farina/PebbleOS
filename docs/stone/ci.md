# CI and workflows

Everything the fork adds lives in `.github/workflows/stone-*.yml`. Upstream's
own workflows are never edited — see {doc}`index`.

## Why we need our own build at all

Every upstream workflow is gated `on: pull_request: branches: [main]`. Our pull
requests target `stone`, so **without `stone-build.yml` they would get no CI at
all.**

`compliance.yml` is the exception: it has no branch filter, so gitlint, ruff and
the SPDX check do run on our PRs.

## The workflows

| Workflow | Runs on | Produces |
| --- | --- | --- |
| `stone-build.yml` | push to `stone` / `feat/**`, PRs into `stone`, `workflow_call`, manual | A merged dual-slot release bundle plus `manifest.json` |
| `stone-sync.yml` | 06:17 UTC daily, manual | Fast-forwards `main`, rebases `stone`, then calls the build |
| `stone-safe.yml` | manual only | A **GitHub Release** — a bundle kept forever |
| `stone-cleanup.yml` | after a successful `stone` build, weekly, manual | Retires dead channels and deletes their branches |

### stone-build.yml

Produces the one artifact a sealed Pebble Time 2 will accept. Three constraints
make that the only shape that works, and each is easy to get wrong:

- **`-DCONFIG_RELEASE=y` is not optional.** The watch refuses a debug bundle
  over release firmware, so a debug build is not installable at all.
- **An OTA writes into the *inactive* slot**, so both slot images must exist and
  be stitched with `tools/merge_pbz.py`.
- **The board is `obelix@pvt`.** A `@dvt` image will not match a retail unit.

A `bundle` job merges the two slots, then asserts the result is really
dual-slot — a manifest per slot, each declaring its own slot number. A green
merge only proves `merge_pbz.py` exited zero; the assertion proves the bundle is
installable-shaped.

Artifacts **expire after 90 days**. That is fine for WIP builds and is exactly
why safe builds are Releases instead.

### stone-sync.yml

Fast-forwards `main` (asserting it is still an ancestor of upstream first),
rebases `stone` onto the new base with `rerere`, and pushes with
`--force-with-lease`.

On conflict it captures the offending patch and the conflicting files **before**
`git rebase --abort`, leaves `stone` untouched, and files a single issue labelled
`stone-sync` — reusing the open one rather than filing a fresh one nightly.

### stone-cleanup.yml

Retires the channel behind a `feat/*` branch once that branch is gone or its
work is on `stone`, then deletes the branch.

**It runs after a successful `stone` build, not on the merge.** By then the
merged work is published on the `stone` channel, so a device migrated off
`feat/thing` has something newer to move to instead of being parked on a channel
whose next build has not happened yet.

Two details are load-bearing:

- **Merged-ness is decided by `git cherry`, not `git merge-base --is-ancestor`.**
  PRs land on `stone` by rebase, which rewrites the SHAs, so a merged branch's
  commits are never ancestors of `stone`. `git cherry` compares patch ids and
  gets it right; `--is-ancestor` would call every merged channel live forever.
- **`?migrate=true` moves devices to `stone` before anything is deleted.**
  Without it the server answers `409` and refuses, which is the behaviour you
  want by default — see {doc}`channels`.

Run it by hand from Actions → Stone Cleanup. `dry_run` defaults to **true**
there, so a manual run reports what it would retire and changes nothing until
you say otherwise.

With no channel server configured it does nothing and says so, rather than
failing.

`feat/*` branches are deliberately **not** rebased. Force-pushing a branch
someone has checked out locally destroys work in progress. Each is test-rebased
and reported instead, so drift is visible without being moved underneath you.

### stone-safe.yml

Manual. Defaults to building `main` — pristine upstream with none of our
patches, so the result doubles as the "is this our bug or upstream's?" baseline.
Pass any ref to pin a Stone build worth keeping. See {doc}`recovery`.

## Traps

Each of these cost real time. They are not obvious and they fail quietly.

**`schedule:` only fires on the default branch.** `stone-sync.yml` lives on
`stone`; if the repository default is `main`, the cron never runs and the fork
silently stops syncing while every check stays green. This is why `stone` must
be the default branch.

**A `GITHUB_TOKEN` push does not trigger workflows.** A rebase pushed by the sync
job would never be built, which is why the build is invoked with `uses:` as a
reusable workflow in the same run rather than left to the push event.

**`GITHUB_REF_NAME` is `8/merge` on a pull request.** Use
`${GITHUB_HEAD_REF:-$GITHUB_REF_NAME}` wherever a branch name is wanted.

**CI checks out a detached HEAD**, so `git rev-parse --abbrev-ref HEAD` returns
literally `HEAD`. Prefer `GITHUB_HEAD_REF` then `GITHUB_REF_NAME`; see
`tools/stone/build_info.py`.

**Do not derive filenames in bash if an action can glob.** An early version of
the build parsed the bundle name in shell and died with a bare `exit 1` before
printing anything, and the job-log API only returns the tail — which is
post-job submodule cleanup, not the error. `upload-artifact` with
`if-no-files-found: error` reports exactly what it matched.

## The compliance gates

Reproduce all three locally before pushing.

### gitlint

```shell
pip install gitlint-core        # NOT `gitlint` — its `sh` dep fails to build
gitlint --commits "origin/stone..HEAD"
```

Default rules include **`T5`, which rejects the word "WIP" in a title**
(case-insensitive). The custom rules in `tools/gitlint/` additionally require
the author name to have two words and to match the sign-off.

### ruff

```shell
python3 -m ruff check --no-cache .
python3 -m ruff format --check --no-cache tools/stone/
```

The action **fetches the latest ruff** (there is no `pyproject.toml` pinning
it), so a new ruff release can turn a green repo red without anything changing
here. If a `ruff` on your `PATH` is older it will give a false pass — prefer
`python3 -m ruff`, and check its version against the one in the job log.

`EXE001` catches a shebang on a non-executable file: `chmod +x` any new
`tools/stone/*.py`, matching the sibling tools.

### SPDX

The licence job checks `*.c *.h *.S *.py *.sh` for
`SPDX-License-Identifier: Apache-2.0` in the first ten lines. Other extensions
are not checked.

## Working without a toolchain

There is no ARM toolchain in the agent environment, so firmware **cannot be
compiled locally** — CI is the compiler, and every mistake is a ~7 minute cycle.
That makes static verification worth real effort: grep for every table indexed
by an enum you extend, confirm each API against its declaring header, and check
who owns an allocation rather than copying the nearest example.

Two more environment limits: the agent's integration token has no
`actions: write`, so **workflows cannot be triggered from here** — manual
dispatches are the user's click; and the egress proxy blocks the artifact host,
so **build artifacts cannot be downloaded** for inspection. That is why bundle
checks live inside CI as assertions.
