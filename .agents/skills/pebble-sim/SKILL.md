---
name: pebble-sim
description: Use when running, iterating on, or visually checking PebbleOS in the simulator on this machine — booting the emulator, rebuilding firmware and seeing the change, navigating the watch UI, taking screenshots, or sideloading a .pbw watchapp.
---

# Running PebbleOS in the simulator

Fork-local setup notes for this machine. Upstream's `working-with-qemu` skill
and `docs/development/qemu.md` document `./pbl` itself; this covers the
`./sim` wrapper and the traps that cost real time here.

## Use `./sim`, not raw `./pbl`

Every `./pbl` command needs two things sourced first — the PebbleOS SDK
(`~/pebbleos-sdk-*/env.sh`, supplies `arm-none-eabi-gcc` and `qemu-pebble`)
and the repo `.venv`. `./sim` does that for you and adds process cleanup.

```sh
./sim boot                 # configure if needed, build if needed, launch, wait until up
./sim boot qemu_flint      # different board (reconfigures + rebuilds)
./sim rebuild              # ./pbl build, then relaunch if already running
./sim shot [path]          # PNG, defaults to build/screenshot.png
./sim key back select up down
./sim tap 100 114          # touch-capable boards; coordinates are screen pixels
./sim swipe 100 200 100 40
./sim logs                 # tail uart1.log
./sim console              # interactive firmware console
./sim app path/to/foo.pbw  # sideload a watchapp (needs `pebble` on PATH)
./sim stop
```

## Traps

- **Never pipe `./sim boot` into `tail`/`head`.** The detached QEMU keeps the
  pipe's write end open, so the pipe never closes and the call hangs until it
  times out — the boot itself takes ~7s. Run it bare. Same for any command
  that leaves QEMU running.
- **Only one instance can run.** `./pbl` hardcodes serial ports 12344/12345,
  so a stale QEMU makes the next launch fail with `Address already in use`
  while still appearing to start. `./sim boot` kills stragglers first; if you
  bypass it, run `./sim stop`.
- **Give it a moment before screenshotting.** `./sim boot` already sleeps ~6s
  so the OS reaches the watchface. After a keypress, sleep ~0.5s before
  `./sim shot` or you may capture the pre-transition frame.
- **The emulator boots wherever it was left** in the flash image. If a
  screenshot shows an unexpected settings screen, send `./sim key back` a few
  times to get to the watchface rather than assuming a bug.

## Verifying UI changes

Drive the UI over the socket monitor and read the PNG — do not ask the user to
look at the window.

```sh
./sim rebuild
./sim key back back back      # return to watchface
./sim shot /tmp/before.png
```

Then Read the PNG. `qemu_emery` renders 200x228, the Pebble Time 2 panel size,
so a screenshot is pixel-accurate to the real device's geometry.

## Boards

| Board | Platform | Device |
| --- | --- | --- |
| `qemu_emery` | emery | Pebble Time 2 (default here) |
| `qemu_flint` | flint | Pebble 2 Duo |
| `qemu_gabbro` | gabbro | Pebble Round 2 |

`obelix` is the *real* Pebble Time 2 board (SiFli SF32LB52). QEMU only
emulates the STM32-class targets, so obelix cannot be booted in the
simulator — `qemu_emery` is the stand-in, matching the platform and screen
but not the SoC. Anything SoC-specific (SiFli peripherals, BLE stack,
power management) has to be tested on hardware.

## Watchapps vs. firmware

Two different loops — don't confuse them.

- **Firmware** (this repo): `./sim rebuild`. Changes the OS itself.
- **Watchapps** (`~/repos/pebble-playground`, SDK-based): `pebble build` then
  `./sim app build/<name>.pbw`, or the standalone SDK emulator via
  `pebble install --emulator emery`. The two emulators both bind the same
  ports, so stop one before starting the other.
