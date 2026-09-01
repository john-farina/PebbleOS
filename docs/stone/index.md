# Stone OS

Stone OS is a personal fork of [PebbleOS](https://github.com/coredevices/PebbleOS),
running on a Pebble Time 2 (`obelix@pvt`).

```{image} logo.svg
:alt: Stone OS
:width: 128px
```

Upstream moves fast — roughly twelve commits a day — so this fork is built
around one property: **`git log main..stone` is always, precisely, our
changes.** Every convention here exists to protect that, because once the
answer to "what have I actually changed?" stops being cheap to compute,
upstream syncs get scary and stop happening.

```{toctree}
:maxdepth: 1

ci
firmware
features
simulator
channels
companion-app
recovery
roadmap
```

| Page | |
| --- | --- |
| {doc}`ci` | The workflows: what runs when, how to trigger them, and the CI gates |
| {doc}`firmware` | What the fork changes in the firmware, and how to add more |
| {doc}`features` | How to work on a feature here, and the notes file every branch carries |
| {doc}`simulator` | Booting a build in QEMU before it reaches the watch |
| {doc}`channels` | Installing a branch on the watch, switching back, and cleaning up |
| {doc}`companion-app` | The phone app fork, and what must change in lockstep with it |
| {doc}`recovery` | What survives a bad build, and every way back |
| {doc}`roadmap` | What is not built yet, and the decisions still open |

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

One exception is deliberate: `certifications.h` keeps `.trademark = "Pebble"`.
That is the regulatory certification field for a device certified as a Pebble,
and changing it would misrepresent the hardware.

Everything the fork adds is named Stone: the `stone` branch, `CONFIG_STONE`,
`Kconfig.stone`, `tools/stone/`, `docs/stone/`, and
`.github/workflows/stone-*.yml`.

## Branches

| Branch | Contains | Who moves it |
| --- | --- | --- |
| `main` | An exact mirror of upstream. Never commit here. | The nightly sync, fast-forward only |
| `stone` | Upstream plus our patch queue. The default branch and the base for every PR. | The nightly sync rebases it; we push to it |
| `feat/*` | One topic in progress. Each builds as its own channel. | Us |

Our commits never merge into upstream history — they are replayed on top of it.
That keeps `main..stone` readable at commit 3 and at commit 300, and makes a
conflict land on one identifiable patch instead of one large merge.

**Merge pull requests with rebase, not merge.** A merge commit on `stone` breaks
the property the whole model depends on.

## Keeping the rebase cheap

These matter more than the automation. A rebase queue only stays painless if the
patches are shaped to avoid upstream's hot files.

- **Never edit an upstream workflow file.** Add new ones named `stone-*.yml`.
  Upstream touches `.github/workflows/` constantly, and every edit there is a
  guaranteed recurring conflict.
- **Prefer new files behind `CONFIG_STONE` over edits to existing ones.**
- **Append, don't insert.** When a line must go into an upstream file, put it at
  the end of the relevant block. `rsource "Kconfig.stone"` at the foot of the
  root `Kconfig` is the model.
- **One logical change per commit.** A rebase resolves commit by commit, so
  small commits are directly less work.
- **Order the queue**: build and CI patches first, then features, then
  experiments. When a rebase gets ugly, drop from the bottom.

Current permanent diff against upstream files: **18 files, +72 −12**. Eleven of
those are the single-line rebrand strings. Check it with:

```shell
git diff --numstat origin/main..origin/stone
```

If that number starts climbing fast, something is being done the wrong way.

## Commits

Upstream's rules, enforced by `gitlint` in CI:

- `area: short description` — lower-case area, imperative description
- sign off with `git commit -s` (exactly one `Signed-off-by`)
- body wrapped at 100 columns
- the author must match the sign-off, and have at least two words in their name

Use `stone:` as the area for fork-local work that doesn't belong to an existing
upstream area, and `ci:` for workflow changes.

Run it before pushing — see {doc}`ci` for the install that actually works, and
for the default rule that rejects the word "WIP" in a title.

## Set up rerere once

```shell
git config rerere.enabled true
git config rerere.autoupdate true
```

The nightly sync rebases the same patches over and over, so the same conflicts
recur. `rerere` records how you resolved one and replays it, making the second
and later occurrences invisible. CI caches its `rr-cache` for the same reason.

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
