# The simulator

CI compiles the firmware. It does not run it. A green build says the code
compiles and the bundle is well-formed — it says nothing about whether the thing
boots.

That gap is not theoretical. The first Stone build installed on the real watch
sent it to PRF, and nothing in this repository had ever executed the firmware
before it reached a wrist. **Boot it in the simulator before you push a build
John is going to install.**

## `./sim`

Every `./pbl` command needs two things sourced first — the PebbleOS SDK
(`~/pebbleos-sdk-*/env.sh`, which supplies `arm-none-eabi-gcc` and
`qemu-pebble`) and the repo `.venv`. `./sim` does that for you and adds process
cleanup on top.

```shell
./sim boot                 # configure if needed, build if needed, launch, wait until up
./sim boot qemu_flint      # a different board (reconfigures and rebuilds)
./sim rebuild              # ./pbl build, then relaunch if it was already running
./sim shot [path]          # PNG, defaults to build/screenshot.png
./sim key back select up down
./sim tap 100 114          # touch-capable boards; coordinates are screen pixels
./sim swipe 100 200 100 40
./sim logs                 # tail uart1.log
./sim console              # interactive firmware console
./sim app path/to/foo.pbw  # sideload a watchapp (needs `pebble` on PATH)
./sim stop
```

## Boards

| Board | Platform | Device |
| --- | --- | --- |
| `qemu_emery` | emery | Pebble Time 2 — **the default here** |
| `qemu_flint` | flint | Pebble 2 Duo |
| `qemu_gabbro` | gabbro | Pebble Round 2 |

`qemu_emery` renders **200x228**, which is exactly the Pebble Time 2 panel, so a
screenshot is pixel-accurate to the real device's geometry.

```{warning}
`obelix` — the *real* Pebble Time 2 board — **cannot be booted in the
simulator.** QEMU only emulates the STM32-class targets, and obelix is a SiFli
SF32LB52. `qemu_emery` matches the platform and the screen but not the SoC.

So the simulator proves shared code: UI, apps, settings, the boot splash,
anything in `applib/` or `apps/`. It cannot prove SiFli peripherals, the BLE
stack, power management, or flash layout. Those still need hardware.

A build that boots in `qemu_emery` can still fail on obelix. A build that does
*not* boot in `qemu_emery` is broken for certain, and that is worth knowing
before it costs a PRF recovery.
```

## Verifying a UI change

Drive the UI over the socket monitor and read the PNG. Do not ask John to look
at the window.

```shell
./sim rebuild
./sim key back back back      # get back to the watchface
./sim shot /tmp/after.png
```

Then read the PNG directly.

## Traps

- **Never pipe `./sim boot` into `tail` or `head`.** The detached QEMU keeps the
  pipe's write end open, so the pipe never closes and the call hangs until it
  times out. Run it bare, or redirect to a *file*. The same applies to any
  command that leaves QEMU running.
- **Only one instance can run.** `./pbl` hardcodes serial ports 12344/12345, so
  a stale QEMU makes the next launch fail with `Address already in use` while
  still appearing to start. `./sim boot` kills stragglers first; if you bypass
  it, run `./sim stop`.
- **Give it a moment before screenshotting.** `./sim boot` already sleeps ~6s so
  the OS reaches the watchface. After a keypress, wait ~0.5s before `./sim shot`
  or you may capture the pre-transition frame.
- **The emulator boots wherever it was left** in the flash image. An unexpected
  settings screen in a screenshot usually means the last session left it there —
  send `./sim key back` a few times before assuming a bug.
- **A stale `build/` from an older layout will not build.** If `./pbl build`
  says `not a CMake build directory (missing CMakeCache.txt)`, the directory
  predates the CMake switch. Re-run `./pbl configure --board qemu_emery`.

## Watchapps vs. firmware

Two different loops — do not confuse them.

- **Firmware** (this repo): `./sim rebuild`. Changes the OS itself.
- **Watchapps** (SDK-based, a separate project): `pebble build`, then
  `./sim app build/<name>.pbw`. The standalone SDK emulator binds the same
  ports, so stop one before starting the other.
