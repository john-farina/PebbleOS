# What is not built yet

The firmware and CI halves are done. What remains is blocked on decisions or
credentials rather than on work, and this page exists so none of it gets
re-derived from scratch.

## Blocked on a deploy

### The channel server

The server is written and tested — `tools/stone/channel/`, 39 cases, no network
needed to run them. It runs on Railway (a Node process and a volume) or on
Cloudflare Workers (a Worker and a KV namespace) from the same source; the
routing is one host-agnostic `fetch` handler and the host adapters are about
200 lines.

Whichever it is, it needs two variables — `STONE_PUBLISH_TOKEN` and
`STONE_CONTROL_TOKEN` — and then `STONE_CHANNEL_URL` plus `STONE_PUBLISH_TOKEN`
added to this repository's secrets. Until both repository secrets exist, CI
skips publishing and the bundle is still an artifact you can sideload by hand.

See `tools/stone/channel/README.md` for the deploy steps and for why bundles
live in the same store as the metadata rather than in release assets.

## Blocked on Apple credentials

### The companion app fork

The official app — <https://github.com/coredevices/mobileapp> — is fully open
source (Kotlin Multiplatform, GPLv3) and built to be forked. The whole identity
change is three values in `iosApp/Configuration/Config.xcconfig`
(`TEAM_ID`, `BUNDLE_ID`, `APP_NAME`), plus trimmed entitlements and the provided
dummy Firebase plist.

Two findings that make this worth doing:

- **`bugUrl` is a build-time Gradle property**, empty in the public repo, and
  `useEngDashOta` defaults to true. Setting it points OTA at a server you
  control — that one line is what makes channels and update cards work.
- **Rollback already exists.** `needsPrfToDowngrade()` and
  `rebootIntoPrfForDowngrade()` in `libpebble3` handle going backwards, driven
  by the `is_downgrade` flag your server sets.

The app version derives from a git tag `X.Y.Z.B` where `B` becomes
`CFBundleVersion` — already the shape TestFlight wants. Tag parts must be
**strictly numeric**; the build script hard-errors otherwise.

Name it without "Pebble" (upstream's README asks the trademark stay
referential), and uninstall the official app once yours works — both would fight
over the watch's BLE companion connection.

## Not built yet

### Build retention

`stone-cleanup.yml` retires whole channels, but nothing prunes *within* a live
channel: every build `stone` ever published keeps its bundle in KV. At ~3 MB a
build that is around three hundred before the free 1 GB runs out, so this is
months away rather than urgent.

The rule when it is written: keep the last 3 per channel, and never delete the
running build, the `stone` head, anything pinned, or any loghash dictionary for
firmware that ever ran.

### The branch-picker watchapp

`apps/stone/` — lists branches via PebbleKit JS (which runs on the phone and has
network access) and `PUT`s the choice to the channel server. Note the limit:
**PKJS only runs while the app is open**, so this is a remote control, not a
background notifier.

A live list inside *Settings* instead would need a new BlobDB or protocol
endpoint matched in both repositories, touching upstream-owned registration
files on both sides. Same UX, much larger permanent diff.

## Open questions

- **Boot priority on real hardware.** Inferred from `tools/pblboot.py`; the
  bootloader is not in this repo. Whether rolling back needs
  `tools/stone/restamp_priority.py` at all depends on it, and so does whether
  installing an older build works without it. See {doc}`recovery`.
- **Whether the official app would offer a Core release over a Stone build.**
  Decided in the app, not the firmware. With your own app build pointed at your
  own server it is moot, but worth knowing before then.
