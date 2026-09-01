# stone-channel

The server that tells the watch a new build exists.

It runs on **Railway** (a Node process and a volume) or on **Cloudflare Workers**
(a Worker and a KV namespace), from the same source. No dependencies, no build
step, and no framework: the whole service is one `fetch` handler.

The port is small because the handler only ever touches storage through
`get`/`put`/`delete`/`list`. `src/index.js` is that handler and is host-agnostic;
`src/server.js` and `src/kv_fs.js` are the ~200 lines that make it a Node
service. There is no second copy of the routing, and the same tests cover both.

## Why this shape

`EngDashOta.kt` in the companion app already polls `GET $bugUrl/ota/latest` and
already understands `{version, notes, is_downgrade, artifacts:[{url}]}`. Setting
`bugUrl` to this server is the entire integration — no protocol work, no
firmware change, and update cards for your own branches appear natively.

Bundles live in the same store as the metadata — the volume on Railway, the KV
namespace on Cloudflare. That is not the obvious choice, but the obvious one is
worse: GitHub release assets need a tag per build, and `tools/gitinfo.py` runs
`git describe` with no `--match`, so tag churn would make every firmware version
relative to the last *build* instead of to an upstream release.

The boot-priority re-stamp that makes a rollback actually boot happens in CI
with `tools/stone/restamp_priority.py`, not here. Re-stamping on serve would
mean unzipping, patching and rezipping 3 MB inside a request, which is real
compute to avoid a step CI already does for free — and on Cloudflare's free tier
it is well past the 10 ms CPU budget outright.

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

**The download URL is derived per request, not stored.** CI knows the bundle's
filename but not the hostname it will be served from, and storing whichever host
CI happened to reach would orphan every build the day a custom domain is added.
An explicit `url` in the manifest still wins, for a bundle hosted elsewhere.

## Deploy

### Railway

Service settings: root directory `tools/stone/channel`, a volume mounted at
`/data`, and the two tokens as variables. The service refuses to start without
them rather than coming up as an open server that anyone can hand your watch
firmware through.

| Variable | |
| --- | --- |
| `STONE_PUBLISH_TOKEN` | CI publishing builds, retiring channels |
| `STONE_CONTROL_TOKEN` | switching which channel a watch follows |
| `STONE_DATA_DIR` | where the volume is mounted; defaults to `/data` |
| `PORT` | set by Railway |

The volume is the whole persistence story — lose it and the watch forgets which
channel it follows. 1 GB holds around three hundred bundles.

### Cloudflare Workers

```shell
cd tools/stone/channel
npm test

npx wrangler kv namespace create STONE     # paste the id into wrangler.toml
npx wrangler secret put STONE_PUBLISH_TOKEN
npx wrangler secret put STONE_CONTROL_TOKEN
npm run deploy:cloudflare
```

### Either way

Point the app at it by setting `bugUrl` in the app fork's `gradle.properties`,
and add the URL and the publish token to this repository's secrets so CI can
publish:

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

39 cases and about a second. No wrangler, no Railway, no network: 28 drive the
handler directly against a fake KV, and 11 run a real HTTP server over a real
temp directory to cover the two things only the Node host can get wrong —
persistence across a restart, and the translation between `node:http` and
`fetch`.
