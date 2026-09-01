# What is not built yet

The firmware and CI halves are done. What remains is blocked on decisions or
credentials rather than on work, and this page exists so none of it gets
re-derived from scratch.

## Blocked on a hosting decision

### Publishing builds

`stone-build.yml` produces a bundle and a `manifest.json` describing it, but
**nothing publishes them anywhere durable**. Where builds get served from
depends on where the channel server runs:

| Option | |
| --- | --- |
| Release assets | Public, no auth, but a tag per build and tag churn in `git describe` |
| Artifacts | No tags, but expire after 90 days and need a token to fetch |

Picking before the server exists would be guessing, and the wrong guess means
either tag churn or an auth design that does not fit the host.

### The channel server

Small: a KV of `{channel → latest build}`, plus

- `GET /ota/latest` in eng-dash's exact shape —
  `{version, notes, is_downgrade, artifacts:[{url}]}`, `204` when nothing is new
- `PUT /channel` to switch
- the **priority re-stamp on serve** described in {doc}`recovery`

The shape is fixed by `EngDashOta.kt` in the companion app, which already speaks
it. Query parameters are `device_serial`, `hardware_version` and
`current_version`.

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

## Blocked on the channel server

### Channel lifecycle and cleanup

When a `feat/*` branch merges, its channel is dead. The rule the design turns
on: **nothing is deleted until the watch is off it.** A channel goes
`active → merged → retired → deleted`, and the merged→retired step is gated on
migrating the device to `stone` first — otherwise it strands silently on
firmware that will never update again.

Retention: keep the last 3 builds per channel; never delete the running build,
the `stone` head, anything pinned, or any loghash dictionary for firmware that
ever ran.

Rebased `feat/*` branches orphan their builds' SHAs. Those bundles stay
installable — key builds by `(branch, commit, build number)` and mark
pre-rebase ones stale rather than deleting them.

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
  bootloader is not in this repo. The channel server's re-stamp design depends
  on it. See {doc}`recovery`.
- **Whether the official app would offer a Core release over a Stone build.**
  Decided in the app, not the firmware. With your own app build pointed at your
  own server it is moot, but worth knowing before then.
