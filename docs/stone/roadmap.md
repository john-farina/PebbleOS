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

## Built since

### The companion app fork — done

Forked, configured and shipping to TestFlight on every push:
<https://github.com/john-farina/mobileapp> (branch `stone`). See
{doc}`companion-app` for the contract between the two repos.

What follows is the original analysis, kept because it explains *why* the fork is
shaped the way it is.

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

## Answered on hardware

- **Whether the official app would offer a Core release over a Stone build.**
  **Yes, and it will install it over yours.** A build whose version is lower than
  the running firmware is treated as a downgrade: the app reboots into PRF, its
  update check then finds Core's build newer, and installs that instead. The
  sideloaded build is never transferred. This is why Stone carries a version
  floor — see {doc}`recovery`.
- **PRF recovery works on a retail Pebble Time 2.** Confirmed the hard way on
  2026-09-01: the watch went to PRF, the iOS app saw it, restored firmware, and
  came back with settings, apps and pairing intact. The 21 MB `FILESYSTEM`
  region was untouched, as documented.

## Open questions

- **Boot priority on real hardware.** Still inferred from `tools/pblboot.py`; the
  bootloader is not in this repo. Not yet tested deliberately, because the first
  install failed for the version reason above rather than for a priority reason.
  Install an older build over a newer one and read `Settings` → `Stone`. See
  {doc}`recovery`.
