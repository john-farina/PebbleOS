# stone-channel

The server that tells the watch a new build exists.

It is one Cloudflare Worker and one KV namespace, with no build step and no
dependencies. That is not minimalism for its own sake: this workload is one
phone polling a few times an hour, which sits inside Cloudflare's free tier
(100k requests/day, 1k KV writes/day) with several orders of magnitude to spare.

## Why this shape

`EngDashOta.kt` in the companion app already polls `GET $bugUrl/ota/latest` and
already understands `{version, notes, is_downgrade, artifacts:[{url}]}`. Setting
`bugUrl` to this server is the entire integration — no protocol work, no
firmware change, and update cards for your own branches appear natively.

Bundles live in the same KV namespace as the metadata. That is not the obvious
choice, but the obvious ones are worse: GitHub release assets need a tag per
build, and `tools/gitinfo.py` runs `git describe` with no `--match`, so tag
churn would make every firmware version relative to the last *build* instead of
to an upstream release. A KV value holds 25 MB against a ~3 MB bundle, and 1 GB
of free storage is around three hundred of them.

The boot-priority re-stamp that makes a rollback actually boot happens in CI
with `tools/stone/restamp_priority.py`, not here. Re-stamping on serve would
mean unzipping, patching and rezipping 3 MB inside a request — far past the free
tier's 10 ms CPU budget, and paid compute to avoid a step CI does for free.

## Routes

| | | Auth |
| --- | --- | --- |
| `GET /ota/latest` | What the app polls. `204` when there is nothing new. | — |
| `GET /channels` | Every channel and its current build. | — |
| `GET /builds/:channel` | That channel's history, newest first. | — |
| `GET /device/:serial` | Which channel a watch follows and what it last ran. | — |
| `GET /bundles/:channel/:name` | The bundle itself. Unauthenticated — the app has no token, and a token in a firmware URL leaks into logs on both sides. | — |
| `PUT /bundles/:channel/:name` | CI handing over the bytes. | publish |
| `POST /builds` | CI publishing a manifest. | publish |
| `PUT /device/:serial/channel` | Switch which channel a watch follows. | control |
| `DELETE /channels/:channel` | Retire a merged or abandoned branch. | publish |

Two tokens, so a CI token that leaks cannot repoint your watch and a control
token in a phone app cannot publish firmware.

## Three decisions worth knowing

**`is_downgrade` is computed here, not in the app.** Versions are
`v4.36.0-98-g137d1852` and the app's parser ignores everything past the patch
number, so two builds off the same upstream base look identical to it. A
downgrade is only a downgrade when the *upstream base* goes backwards; calling a
branch hop one would send the watch through PRF, which invalidates both slots.

**Retiring a channel migrates devices first.** `DELETE` returns `409` while any
watch still follows the channel, and `?migrate=true` moves them to `stone`
before deleting. Deleting a channel out from under a device strands it on
firmware that will never update again, and it fails silently — the watch simply
stops being offered anything.

**Switching to a channel with no builds is refused.** A typo would otherwise
park the watch somewhere that never updates, and you would find out days later.

**The download URL is derived here, not in CI.** The build job knows the bundle
filename but not the deployed hostname; `POST /builds` fills in the rest. One
less place for the URL to drift out of sync.

## Deploy

```shell
cd tools/stone/channel
npm test

npx wrangler kv namespace create STONE     # paste the id into wrangler.toml
npx wrangler secret put STONE_PUBLISH_TOKEN
npx wrangler secret put STONE_CONTROL_TOKEN
npx wrangler deploy
```

Then point the app at it by setting `bugUrl` in the app fork's
`gradle.properties`, and add the deploy token and the worker URL to this
repository's secrets so CI can publish:

| Secret | |
| --- | --- |
| `STONE_CHANNEL_URL` | the deployed worker's URL |
| `STONE_PUBLISH_TOKEN` | the same value you gave `wrangler secret put` |

Until both exist, CI skips publishing and the bundle is still an artifact you
can sideload by hand — the server is an upgrade to the workflow, not a
dependency of it.

Publishing is best-effort in CI: a build is still a build if the server is down,
so a failed publish warns rather than failing the run.

## Tests

```shell
npm test
```

28 cases, no wrangler and no network — the Worker is a plain fetch handler, so a
fake KV and a `Request` are the whole harness.
