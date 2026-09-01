# Channels

A channel is a branch you can install. `stone` is one; every `feat/*` branch is
another. The watch follows exactly one at a time, and switching is how you put a
work-in-progress build on your wrist without touching the stable one.

## The pieces

| | |
| --- | --- |
| `stone-build.yml` | builds a bundle per push and publishes it to the channel named after the branch |
| `tools/stone/make_manifest.py` | describes that bundle — version, commit, base, notes |
| `tools/stone/channel/` | the server that answers "is there anything newer?" |
| the companion app | polls it and shows the update card |

The integration point is one Gradle property. `EngDashOta.kt` in the app already
polls `GET $bugUrl/ota/latest` and already understands the response shape, so
setting `bugUrl` to the deployed worker is the whole thing — no protocol work
and no firmware change.

## Everyday use

```shell
git switch -c feat/thing
# ... work ...
git push -u origin feat/thing
```

The push builds, and the build publishes to a channel called `feat/thing`. Point
the watch at it:

```shell
curl -X PUT "$STONE_CHANNEL_URL/device/$SERIAL/channel" \
  -H "Authorization: Bearer $STONE_CONTROL_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"channel":"feat/thing"}'
```

The next poll offers that build. Go back with the same call and `"stone"`.

Switching to a channel with no builds is refused rather than accepted — a typo
would otherwise park the watch somewhere that never updates, and the symptom
shows up days later as "updates stopped working".

## Why hopping branches does not go through PRF

`is_downgrade` in the response decides whether the app reboots into recovery
first, and PRF invalidates **both** firmware slots. So it has to be right.

Versions are `v4.36.0-98-g137d1852`, and the app's parser reads only
`major.minor.patch` — every build off the same upstream base looks identical to
it. The server therefore decides: a downgrade is when the **upstream base** goes
backwards, not when the commit count does. Branch hops are not downgrades.

## Cleaning up

When a branch merges or is abandoned, its channel is dead. The rule the whole
design turns on is that **nothing is deleted until the watch is off it**:

```shell
# refuses with 409 while any device still follows it
curl -X DELETE "$STONE_CHANNEL_URL/channels/feat%2Fthing" \
  -H "Authorization: Bearer $STONE_PUBLISH_TOKEN"

# move them to stone first, then delete
curl -X DELETE "$STONE_CHANNEL_URL/channels/feat%2Fthing?migrate=true" \
  -H "Authorization: Bearer $STONE_PUBLISH_TOKEN"
```

Retiring takes the channel's bundles and build history with it. Bundles are the
only large values in KV, so a channel that leaves its behind is the one way this
fills up a free 1 GB.

`stone` itself cannot be retired. It is where everything migrates *to*.

`stone-cleanup.yml` does all of this for you after a successful `stone` build —
it retires any channel whose branch is gone or whose work has landed, then
deletes the branch. See {doc}`ci`.

## Rolling back

`GET /builds/<channel>` lists what a channel has published, newest first,
including builds whose commits a rebase has since orphaned — those bundles are
still installable, and losing them would lose the way back.

Re-stamp the one you want before republishing it, or the bootloader will keep
running whatever was built most recently. That is not optional and it is not
obvious; see {doc}`recovery`.

## What is still manual

The switch above is a `curl`. The branch-picker watchapp that would make it a
menu on the wrist is still unbuilt — see {doc}`roadmap`. PebbleKit JS only runs
while the app is open, so even then it is a remote control rather than a
background notifier.
