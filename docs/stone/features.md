# Working on a feature

This page is for whichever session picks the work up next. It assumes nothing
about what came before, because nothing carries over.

That is the constraint everything here is shaped around. Work on this fork is
done by Claude sessions that do not remember each other, against an upstream
landing about twelve commits a day. Code survives in commits; **reasoning does
not survive anywhere unless it is written down**, and rediscovering a dead end
costs a full seven-minute CI cycle at best.

## Start with the script

```shell
tools/stone/new_feature.py "sleep tracking tweaks"
```

That branches `feat/sleep-tracking-tweaks` from `origin/stone`, writes
`docs/stone/features/sleep-tracking-tweaks.md` from the template, and commits
it. One step, so the notes file is never the thing nobody got round to.

It refuses to run on a dirty working tree — sweeping unrelated changes onto a
new branch leaves the next session unable to tell which of them were the
feature.

## The notes file is the handoff

Every feature branch carries exactly one, and keeping it current is part of the
work rather than something done at the end. The end is where sessions run out
of context.

| Section | What belongs there |
| --- | --- |
| **What this is** | The idea, in John's words where you have them. Intent, not a tidy restatement |
| **Where it stands** | Rewritten each session: if you stopped now, what would the next one need? |
| **Decided** | Choices already made, with the reason, so they are not re-litigated |
| **Tried and rejected** | What looked right and was not, and what broke. The most valuable section |
| **Open questions** | What is genuinely undecided, and who can settle it |
| **How to test it** | Written for John, holding the watch. Exact steps, exact expected result |
| **Log** | Append-only, one entry per session |

Update it **when something changes, not when you finish**: after a decision,
after something fails, after CI tells you something surprising. A session that
runs out of context mid-feature should still leave a branch someone can pick up.

Two failure modes worth naming. A **stale "How to test it"** is worse than an
empty one, because it fails and the failure means nothing. And **"tried X, it
didn't work"** is nearly worthless — say what broke, and how you know.

### Picking up someone else's branch

Read the notes file first, before the diff. Then check whether it is still true:
`git log --oneline origin/stone..HEAD` against what "Where it stands" claims. If
they disagree, the notes are stale — fix them before adding to them.

## Keeping the diff small

The fork's whole maintainability rests on `git log main..stone` staying a
precise description of our changes. See {doc}`index` for the full rules; the
short version, in order of preference:

1. A new file behind `CONFIG_STONE`.
2. A new file that nothing upstream references.
3. One appended line in an upstream file — at the **end** of its block, never
   inserted into the middle, because upstream's own additions go there too.

Never edit an upstream workflow. Add a `stone-*.yml` instead.

## Verify before you push

**Boot it in the simulator first.** CI compiles the firmware; it does not run
it. A green build says nothing about whether the thing boots — and the first
Stone build installed on the real watch sent it to PRF. See {doc}`simulator`.

```shell
./sim rebuild && ./sim boot
./sim key back back back
./sim shot /tmp/after.png     # then read the PNG
```

`qemu_emery` renders 200x228, the real Pebble Time 2 panel size. It cannot boot
`obelix` itself — QEMU has no SiFli target — so it proves shared code (UI, apps,
settings, the boot splash) and nothing SoC-specific. A build that fails here is
broken for certain.

CI is still the only thing that builds an installable obelix bundle, and a cycle
is about seven minutes. So the value of everything that *can* run locally is
much higher than usual:

```shell
python -m ruff check . && python -m ruff format --check .
python -m unittest discover -s tools/stone -t tools/stone
(cd tools/stone/channel && npm test)
gitlint --commits origin/stone..HEAD
```

Then read your own diff adversarially — a missing include or a typo'd
`Kconfig` symbol costs a full cycle, and both are visible by eye.

`gitlint` installs as **`gitlint-core`**; plain `gitlint` fails to build. Its
default rules reject the word "WIP" in a title and cap it at 72 characters.

## Commits

Upstream's rules, enforced in CI by `compliance.yml`:

- `area: short description` — lower-case area, imperative
- `git commit -s`, exactly one `Signed-off-by`
- author must match the sign-off and have at least two words
- co-author trailer: `Co-Authored-By: Claude <noreply@anthropic.com>`
- body wrapped at 100 columns

Use `stone:` for fork-local work with no upstream area, `ci:` for workflows.

Commit in small pieces. A rebase resolves commit by commit, so small commits
are directly less work later — see {doc}`index`.

## Pushing turns the branch into a channel

Every push to `feat/**` builds a release bundle and publishes it to a channel
named after the branch. Nothing else to do — see {doc}`channels` for how John
installs it and how to switch back.

So push when there is something worth putting on a wrist, and say in the notes
file what he should look for.

## Finishing

Open a PR into `stone` and merge it with **rebase**, never a merge commit.
`stone-cleanup.yml` then retires the channel and deletes the branch by itself,
after the `stone` build that contains the work has published — so the watch is
migrated to something newer before anything is deleted.

The notes file merges with the feature and becomes its record. That is
deliberate: the "tried and rejected" section keeps its value long after the
code lands, and it is the part nobody would reconstruct from a diff.
