---
orphan: true
---

# Channel smoke test

| | |
| --- | --- |
| **Branch** | `feat/channel-smoke-test` |
| **Started** | 2026-09-01 |
| **Status** | Built and published — waiting on John to install it |

## What this is

Not a feature. A throwaway branch whose only job is to prove the channel
pipeline works end to end on real hardware, before a real feature depends on it:

> push a branch → CI builds it as a release bundle → it appears as a channel →
> it installs on the watch → the watch says which branch it is running → it goes
> away cleanly when the branch does

It also exists as the worked example of a notes file. If you are a session
starting your first feature here, this is the shape.

Delete this branch once John has run through it. It carries no product change
and must never merge.

## Where it stands

The notes commit is the only commit, which is deliberate — **no code change is
needed to prove the pipeline.** Every commit produces a distinct version string,
and `Settings → Stone` already reads the branch name out of the build metadata
header. So installing this build and seeing `feat/channel-smoke-test` on the
watch is itself the proof, with nothing invented to demonstrate it.

Waiting on John to install it and report back. Nothing here is blocked on code.

## Decided

- **No product change on this branch.** A visible string edit would have meant
  touching an upstream-owned file for a branch that is going to be thrown away,
  and the branch name in `Settings → Stone` proves the same thing for free.
- **Never merge it.** It gets retired the way any dead channel does, which
  exercises `stone-cleanup.yml` as a second smoke test.

## Tried and rejected

- Nothing yet. When something here fails, write down *what* broke — an error
  string, a screen, a status code. "Didn't work" costs the next session a full
  seven-minute cycle to rediscover.

## Open questions

- **Does the public Railway URL route correctly?** The service is up and its
  internal healthcheck passes, but this session's egress proxy blocks
  `*.up.railway.app`, so nobody has yet made a request from outside. John's
  first `curl` settles it.
- **Boot priority on real hardware.** Unrelated to this branch but it lands in
  the same session on the wrist — see {doc}`../recovery`.

## How to test it

You need the watch, the phone, and about fifteen minutes. Set these first:

```shell
export URL=https://stone-channel-production.up.railway.app
export CTL=<STONE_CONTROL_TOKEN, from Railway → stone-channel → Variables>
export SERIAL=<Settings → System → Information on the watch>
```

**1. The channel exists.**

```shell
curl "$URL/channels"
```

Expect a `feat/channel-smoke-test` entry alongside `stone`, with a version like
`v4.35.0-<n>-g<sha>`.

**2. Point the watch at it.**

```shell
curl -X PUT "$URL/device/$SERIAL/channel" \
  -H "Authorization: Bearer $CTL" \
  -H 'Content-Type: application/json' \
  -d '{"channel":"feat/channel-smoke-test"}'

curl "$URL/ota/latest?device_serial=$SERIAL&hardware_version=obelix_pvt"
```

Expect the branch's version, **`is_downgrade: false`** (it must be false — both
builds sit on the same upstream base, and a spurious downgrade would send the
watch through PRF, which invalidates both slots), and an `artifacts[0].url`.

**3. Install it.** Open that URL in Safari *on the phone* — it lands in Files.
Then in the Pebble app: `Settings` → **Show debug options**, then Devices → tap
the watch → `Firmware Update Debug` → **Sideload FW** → pick the `.pbz`.

**4. The proof.** On the watch, `Settings` → **Stone**:

| Row | Should read |
| --- | --- |
| Branch | `feat/channel-smoke-test` |
| Commit | the short SHA from step 1 |
| Upstream | `v4.35.0` |
| Slot | `0` or `1` — whichever was *not* running before |

If Branch says `feat/channel-smoke-test`, the whole pipeline works.

**5. Go back.** Same call as step 2 with `{"channel":"stone"}`, then install the
`stone` build the same way.

**6. Cleanup refuses while you are on it.** Put the watch back on this channel,
then try to retire it — it must answer `409` with your serial listed, and leave
the channel alone. Deleting it would strand the watch on firmware that never
updates again, silently.

```shell
curl -X DELETE "$URL/channels/feat%2Fchannel-smoke-test" \
  -H "Authorization: Bearer $PUB"
```

Then with `?migrate=true`, which moves the watch to `stone` first and only then
deletes. Check with `curl "$URL/device/$SERIAL"` — it should read
`"channel":"stone"` and `"migrated_from":"feat/channel-smoke-test"`.

## Log

- **2026-09-01** — branch created with `tools/stone/new_feature.py`, as the
  first real use of it. Notes filled in; no code change on purpose. Pushed to
  publish a channel.
