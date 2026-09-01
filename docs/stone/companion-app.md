# The companion app

The watch talks to a phone app, and Stone has its own fork of it. If you are
changing the channel server, the OTA response shape, or how versions are stamped,
**the app fork may need a matching change** — and nothing will tell you if you
get it wrong, because the failure is silent.

| | |
| --- | --- |
| **App fork** | <https://github.com/john-farina/mobileapp> — branch `stone` |
| **Upstream** | <https://github.com/coredevices/mobileapp> (Kotlin Multiplatform, GPLv3) |
| **Read first over there** | `AGENTS.md`, then `STONE-OS.md` |
| **Ships via** | TestFlight, on every push to its `stone` branch |

## What the fork changes

Almost nothing — which is the point.

| File | Change |
| --- | --- |
| `gradle.properties` | `bugUrl` → the channel server |
| `Config.xcconfig` | `TEAM_ID`, `BUNDLE_ID=com.johnfarina.stone`, `APP_NAME=Stone` |
| `project.pbxproj` | dropped target-level `APP_NAME`/`BUNDLE_ID` overrides |
| `strings.xml`, `AppIcon.appiconset` | name and icon |

No Kotlin was modified. `EngDashOta.kt` already polls `GET $bugUrl/ota/latest`
and already understands the shape `tools/stone/channel/` returns, so pointing
`bugUrl` at the server is the whole integration.

## The contract

`tools/stone/channel/src/index.js` must keep answering `/ota/latest` with:

```json
{
  "version": "v200.0.0.1-27-g1c42dbfb9",
  "notes": "channel: stone\ncommit: 1c42dbf\n...",
  "is_downgrade": false,
  "artifacts": [{ "url": "https://.../stone_obelix_pvt_<version>.pbz" }]
}
```

The app sends `device_serial`, `hardware_version`, and — when not in recovery —
`current_version`.

| Change here | What must change in the app fork |
| --- | --- |
| The `/ota/latest` response fields | `EngDashLatestResult` in `EngDashOta.kt` |
| Where the channel server is deployed | `bugUrl` in `gradle.properties` |
| Dropping the `v200` version floor | nothing in code — but every install silently becomes a downgrade again |
| Retiring a channel a device follows | nothing — the server migrates the device first, by design |

## What the app fork fixes that the version floor only works around

The official app checks Core's OTA service. A sideloaded Stone build whose
version is lower than Core's shipping firmware is treated as a downgrade: the app
reboots the watch into PRF, and its update check then installs Core's firmware
over the build you just sideloaded. See {doc}`recovery`.

The `v200` floor stops that comparison going the wrong way. **The app fork removes
the cause**: pointed at our server, the update check returns *our* builds, so
there is nothing to override. Keep both — the floor still protects anyone
sideloading from the official app.

## Both firmware sources at once

The app has a toggle at `Settings → Debug → "Use Core OTA service"`:

| Toggle | Source |
| --- | --- |
| On | Stone builds, from our channel server |
| Off | Stock Pebble firmware, via Cohorts (`cohorts.rebble.io`) |

The fallback to Cohorts is **failure-only**. A `204 No Content` from our server
means "nothing new", which is a success — so the app stops there rather than
falling through to Core. That is a switch, not a merge, and it is why a quiet
poll does not leak to Core's servers.

## What is still not built

The **branch-picker watchapp** (`apps/stone/`) — see {doc}`roadmap`. PebbleKit JS
only runs while the app is open, so anything on the wrist is a remote control
rather than a background notifier. Switching channels is still a `curl`; see
{doc}`channels`.
