---
orphan: true
---

# Navigation overhaul

| | |
| --- | --- |
| **Branch** | `feat/navigation-overhaul` |
| **Started** | 2026-09-01 |
| **Status** | All four plus haptics, real previews and edge-back. Hardware-only from here |

## What this is

Make navigation feel as good as an Apple Watch. John's words:

> I want the back button on the homepage bring you into the apps … and expose the middle
> button in the settings so I can change it to something else maybe make it so u can hold it
> to get the same hold back functionality cuz u can hold or click it.
>
> Then I wanna make a new list solely for apps and other pebble things I consider apps. And
> then I wanna make a setting app that will hold all the other stuff I don't want in the list.
>
> I wanna keep these in the main app page: Music, Settings (this is where everything else
> will go), alarms, notifications, weather, health, workout, watchfaces, timeline, and all
> other third party apps automatically go into here.
>
> And I wanna be able to control the order of the list in a new settings page called apps or
> something.
>
> And then I want it so when I long press on the main watch face it shrinks each watch face
> and gives me a visible list of each watch face I have and I can scroll right or left in a
> sticky list to see each one and click on one when I wanna keep it there (just like Apple
> Watch).

On what BACK means — this was the open design question and it is now settled:

> the back button should always go back to the main view just like apple watch interaction
> check how they make that the back button and what it does and do exactly that itll still
> always go back. but once u back all the way out to the apps view then pressing it again
> brings u bak to the watch

On touch:

> also there is a touch screen i have a pebble time 2 theres touch interaction get familiar
> with that and make it also use that as well as the buttons it can use both but i should be
> able to use mostly touch screen with my watch. get familiar with it and learn how to
> control it and handle it well maybe make a service if not already so we can do custom
> things like multifinger touch in the future

Four separable features:

1. **Button and touch map** — BACK on the watchface opens the app list; SELECT becomes
   user-configurable for tap as well as hold; the watchface starts responding to touch.
2. **A curated app list** — one list holding only "apps", with the rest under Settings.
3. **Reorderable list** — a Settings → Apps page that controls the order.
4. **Watchface carousel** — long-press the face for a paged, snapping picker.

## Where it stands

**All four features are written and build.** Features 1 and 2 were verified by screenshot in
`qemu_emery`; features 3 and 4 have been compiled but never run. Nothing has been on real
hardware.

Verified by screenshot in the simulator:

- BACK on the watchface opens the app list, and BACK again returns to the watchface. The
  top-level toggle works in both directions.
- Golf is gone from the app list, which now ends: Settings, Music, Notifications, Alarms,
  Watchfaces, Workout, Health, Timeline.
- **Settings → Apps** exists, between System and Stone, and lists the main apps.

**Not yet verified, and this is the whole of what is left to do:**

- **Grab-and-move reordering** — that Select picks a row up, Up/Down moves it, Select drops it,
  and the order survives a reboot. This is the medium-risk feature: it is the first on-watch
  writer of `lnc_ord`, and a write failure is silent by design.
- **The "Not in the list" heading**, with Golf under it, and that the selection skips the heading.
- **The whole watchface picker.** It compiles and is registered as app `-101`, and not one line
  of it has ever executed. Treat it as unproven. The most likely failure is the paging animation
  (`property_animation_create` against a custom int16 implementation, copied from
  `crumbs_layer.c`) and, on touch, whether opting out of the touch-nav bridge really does leave
  horizontal swipes to the picker's own recognizers.

Testing stopped because John started the simulator in his own clone and `./sim` pkills every
`qemu-pebble` process, so two of us cannot drive it at once. Everything above is a `./sim boot`
away once it is free.

Two pieces of tooling bit here and will bite the next session:

- **`./sim boot` exits 1, silently, on a fresh worktree.** `configured_board()` greps
  `build/c4che/_cache.py`, a waf artifact that a CMake build never writes; under `set -o
  pipefail` the failing `sed` makes the assignment fail and `set -e` exits with no message. Work
  around it with `source ~/pebbleos-sdk-*/env.sh && source .venv/bin/activate && ./pbl configure
  --board=qemu_emery && ./pbl build && ./pbl qemu`.
- **`./pbl qemu` in a worktree puts its monitor socket in the *main clone's* `build/`**, not the
  worktree's, presumably from the shared git common dir. So the socket is at
  `~/repos/PebbleOS/build/qemu-mon.sock` even when the firmware being run is the worktree's.

Neither belongs on this branch; they are `./sim`'s own bugs.

**The "there is no ARM toolchain here" rule is out of date.** The SDK at `~/pebbleos-sdk-*`
ships `arm-none-eabi-gcc`, and a full firmware build takes a couple of minutes locally. Compile
before pushing; CI is no longer the only compiler.

| Commit | What |
| --- | --- |
| `shell: let the middle button be assigned a tap action` | The `qlSingleClickSelect` preference and its "Tap Center" settings row |
| `shell: make Back open the app list from the watchface` | The Stone click handlers and the click table |
| `touch: let the watchface respond to gestures` | Controller gestures routed to the shell; CST816 swipe + long press dispatched |
| `apps: keep only apps in the main list` | `stone_app_list_is_app()`, the launcher filter hook, Settings > Apps |
| `settings: let the app list be reordered on the watch` | Grab-and-move reordering, written to `lnc_ord` |
| `apps: add the watchface picker` | The carousel: a system app, entered by holding the face |
| `ui: add navigation haptics` | `stone_haptics`: Tick/Select/Enter/Bump, amplitude-controlled |
| `apps: show real watchface miniatures in the picker` | Progressive thumbnail cache, captured at the app switch |
| `touch: require the back swipe to start at the left edge` | Back is an edge gesture, gated in one place |

Everything is behind `CONFIG_STONE`, with upstream's handlers kept on the `#else` path, so
`CONFIG_STONE=n` still builds and still behaves like stock.

The research result that matters: **three of the four features were wiring, not new subsystems.**
The launcher already filtered its list; app order already persisted to flash with a working write
path; Quick Launch already configured click *and* hold per button. And there is already a full
touch stack -- service, session gating, tap/pan/swipe recognizers, and a tiered touch-navigation
bridge, mostly Core Devices 2026. **Do not write a touch service.**

The thing that was *not* free: **watchfaces are deliberately excluded from touch.**
`applib/touch_service.c:22-25` returns a NULL service state for `sys_app_is_watchface()` with the
comment *"Touch is reserved for watchapps; watchfaces must not consume it."* That rule is right
for apps and is unchanged; the shell gets gestures from the kernel event loop instead, exactly as
it already gets buttons.

## The simulator cannot test any of this

Recorded prominently because two sessions have now tried.

- **QEMU never reports controller gestures.** `src/fw/drivers/qemu/qemu_touch.c` calls only
  `touch_handle_update()` for finger down/up; it never calls `touch_handle_gesture()`. Every
  watchface gesture here rides on the CST816's own gesture engine — long press `0x0C`, swipes
  `0x01`–`0x04` — so **touch long-press and watchface swipes cannot fire in `qemu_emery` at all.**
  `info mice` does report "Pebble Touch (absolute)", so mouse drags are real touch events; they
  just never become gestures.
- **`sendkey <key> <hold_ms>` does not produce a long click.** The monitor accepts the hold time,
  but a `sendkey right 700` on the watchface opened the *launcher* — the tap path — rather than
  the picker. So the hold-to-open-picker route is not drivable from the monitor either.

What the simulator *can* still prove: the BACK toggle, the app list contents, Settings → Apps,
and anything reachable by a plain button press. Everything gesture- or hold-driven needs the
watch.

## Decided

- **BACK keeps exactly one meaning: go back one level.** That is already what
  `applib/app.c:109` does — it pops the window stack unless the top window overrode BACK. The
  only new rule is at the ends of the stack: on the watchface BACK dismisses a timeline peek
  if one is showing, and otherwise opens the app list; from the app list BACK returns to the
  face (already true, since popping the launcher's last window exits the app). This is the
  Apple Watch Digital Crown model — press it in an app and you land on the face, press it on
  the face and you get the Home Screen — built out of semantics Pebble already has.
- **The timeline peek does not need a new home.** `timeline_peek_get_item_id()`
  (`popups/timeline/peek.c:511`) already yields `UUID_INVALID` when nothing is peeking, so
  "dismiss the peek if there is one, otherwise open the launcher" needs no new API and loses
  no behaviour. This was flagged as the blocking design problem. It is not one.
- **Watchface touch is handled in the shell, not the app.** Because the app-side touch
  service refuses watchfaces by design, `PEBBLE_GESTURE_EVENT` is routed in
  `kernel/event_loop.c` when `app_manager_is_watchface_running()` into
  `watchface_handle_gesture_event()`, beside the existing `watchface_handle_button_event()`.
  That mirrors exactly how buttons already reach the watchface and keeps upstream's "watchfaces
  must not consume touch" rule intact for *apps*. Gestures are gated on
  `touch_session_is_active()`, whose own documentation names the idle watchface as the one
  surface it guards -- without it a sleeve opens the app list in a pocket.
- **Use the CST816's own gesture engine for watchface gestures.** The controller already
  reports tap, double-tap, swipe up/down/left/right and long-press; the driver decodes the
  constants (`drivers/touch/cst816/cst816.c:36-43`) but only dispatches tap and double-tap
  (`:333-342`). Adding the other five is a `switch` case each plus new enum values in
  `TouchGesture` and `GestureEventType`. For the watchface this is strictly better than the
  software recognizers, because the software path is the part that is unavailable there.
- **Inside apps, use the software recognizers instead.** `applib/ui/recognizer/` has tap,
  pan (axis-locked, with velocity) and swipe (direction mask, velocity), and `MenuLayer`,
  `ScrollLayer` and `SwapLayer` are already registered Tier-1 touch widgets — so every list
  in the firmware already scrolls, flings and overscrolls by finger. Nothing to do there.
- **These four apps move out of the main list:** Golf, Send Text, Reminders, Sports. John
  confirmed all four. Everything else in the launcher today already matches his keep-list.
- **The carousel is entered by holding on the watchface, and driven by touch.** John, asked
  whether a bare swipe should switch faces with no hold:

  > no lets make it hold on the watchface and then u can swipe and then u can press a watchface
  > and then it goes back to normal in that

  So: **long-press the face → swipe between faces → tap one to keep it and return.** No bare
  swipe-to-switch, which is also where Apple landed after removing it in watchOS 10 as too easy
  to trigger by accident. Buttons stay as a parallel path for the same three actions (UP/DOWN
  step, SELECT keeps, BACK leaves unchanged), because a watch with four buttons should not have
  a mode you can only leave by touching it.
- **This makes watchface touch a prerequisite for feature 4, not an optional extra.** The
  entry gesture is a touch long-press *on a running watchface*, which is exactly the case
  upstream's touch service refuses (see Where it stands). So the shell-side touch routing has
  to land before the carousel can be entered the way John wants it. Reordered accordingly: the
  touch half of feature 1 now blocks feature 4.
- **The CST816's own long-press is the mechanism for that entry gesture.** The controller
  reports `0x0C` long-press and the driver already decodes the constant and drops it
  (`drivers/touch/cst816/cst816.c:43,333-342`). Because the software recognizers in
  `applib/ui/recognizer/` never run on a watchface, the hardware gesture is not merely the
  cheap option here — it is the only one that works without fighting upstream's rule.
- **Health is already in the app list.** John confirmed on the watch. The
  `ProcessVisibilityHidden` in `apps/system/health/health.c:181` sits inside
  `#if CAPABILITY_HAS_CORE_NAVIGATION4`, a macro defined nowhere in the tree, so the line never
  compiles. Feature 2 does not need to do anything about Health.
- **Reorder interaction: grab and move.** SELECT picks the row up, UP/DOWN move it, SELECT
  drops it. John's pick over a per-row action menu.
- **The carousel ships as a system app**, so it can be bound to any hold slot through the
  Quick Launch settings that already exist, rather than adding button infrastructure. Default
  it to "Hold Center" (`qlSelect`, currently disabled). Being an app rather than a watchface,
  it gets the full touch recognizer stack, including Tier-1 pan with fling and snap.
- **No live watchface miniatures.** The carousel shows each face's icon and name. There is
  one app task and one app framebuffer, no bitmap scaler in applib (`graphics_draw_bitmap_in_rect`
  clips and tiles, it never scales), and ~132 KiB of app heap against ~45 KiB for a single
  full-screen 8-bit bitmap. Only the *running* face can be captured, so literal thumbnails of
  every face would mean launching each one in turn. Hardware limit; recorded so nobody
  re-derives it.

## Tried and rejected

- Nothing has been built and rejected yet. One thing was **considered and dropped before
  writing code**: adding a `timeline_peek_is_showing()` predicate to `peek.h`. Unnecessary —
  `timeline_peek_get_item_id()` already answers the question, so the predicate would have
  been a new upstream API for nothing.
- One earlier assumption was **wrong and is recorded so it is not repeated**: that pointing
  BACK at the launcher would give swipe-right-opens-apps for free, because the `touch_nav`
  bridge maps a right swipe to `BUTTON_ID_BACK` (`recognizer/touch_nav.c:376`). It does map
  that way — but the bridge never runs on a watchface, because `touch_service.c:22-25` hands
  back a NULL service state there. Watchface touch is real work, not a side effect.
- **The picker's stored `Animation *` is not a dangling-pointer bug and does not need
  "fixing".** Animations default to `auto_destroy = true` (`applib/ui/animation.c:1114`), so a
  completed page slide destroys itself and `data->slide_animation` is left pointing at a dead
  animation — which reads exactly like a use-after-free on the next `prv_step()`. It is not:
  `Animation *` is a handle, not a raw pointer, and `animation_unschedule()` resolves it with
  `prv_find_animation_by_handle(..., quiet=true)` and returns false when it has gone
  (`animation.c:1253-1265`). Established by reading, not by running.

## Open questions

- **Multi-finger is not possible on this hardware.** The CST816 is a single-point controller
  and the driver reads one contact record (`CST816_TOUCH_DATA_SIZE 5`). It is not that the
  stack collapses multiple points — a second point never enters the system. `TouchEvent`
  carries a bare `(x, y)` with no finger index and a `_Static_assert` capping it at 9 bytes;
  `touch_client.h` still declares `touch_dispatch_touch_events(TouchIdx, ...)` from Google's
  old multi-touch design, but `TouchIdx` is defined nowhere in the tree and the function is
  never implemented, so that header is dead code. Two-finger gestures would need a different
  controller plus a wider event. **Flagged, not actionable.**
- Nothing else is open. Both remaining questions were settled on 2026-09-01 — see Decided.

## How to test it

### Feature 1, buttons

Install this channel, then from the watchface:

1. **With no notification showing on the face**, press BACK. The app list opens — the same
   list SELECT has always opened.
2. Press BACK again. You are back on the watchface.
3. **With a notification peeking at the bottom of the face**, press BACK. The peek goes away
   and you stay on the watchface. Press BACK again: now the app list opens.
4. Press SELECT. The app list opens, exactly as before.
5. Hold BACK. Quiet Time toggles, exactly as before.
6. Go to **Settings → Quick Launch**. There is a new row, **Tap Center**, sitting under Tap Up
   and Tap Down. It reads "Disabled".
7. Select it and pick **Music**. Return to the watchface and press SELECT: Music opens instead
   of the app list.
8. Go back to Settings → Quick Launch → Tap Center and choose **Disable**. Press SELECT on the
   watchface: the app list opens again.

If step 1 opens nothing, or step 3 opens the app list on the *first* press, that is the peek
check misfiring — say which, because the two failures point at different halves of
`prv_back_click`.

### Feature 1, touch

Touch navigation has to be on: **Settings → Display → Touch** and **Touch Navigation**.

Gestures are ignored until the watch is awake and "armed" -- a button press or the wake gesture
does that -- so if a swipe seems to do nothing, press a button first and try again. That gate is
deliberate: without it a sleeve would open the app list in your pocket.

9. On the watchface, swipe **right**. The app list opens, the same as pressing BACK. With a
   notification peeking, the first right-swipe dismisses the peek instead.
10. Swipe **left**. The app list opens (or whatever Tap Center is set to).
11. Swipe **up** and **down**. Same as tapping the DOWN and UP buttons — by default Timeline and
    Health. This follows the same convention as scrolling everywhere else in the watch: the
    content moves opposite to your finger.
12. **Tap** and **double-tap** the face. The backlight comes on and nothing launches. That is
    deliberate — waking the screen must not open an app.
13. **Long-press** the face. Nothing happens yet; that gesture is reserved for the watchface
    carousel and is wired up with it.

### Feature 2, the app list

14. Open the app list. **Golf, Send Text, Reminders and Sports are gone.** Music, Settings,
    Alarms, Notifications, Weather, Workout, Health, Watchfaces, Timeline and every third-party
    app are all still there, in the same order as before.
15. Go to **Settings → Apps**. Scroll past the main list to the heading **"Not in the list"**.
    Golf is under it, along with Send Text, Reminders and Sports if your phone has enabled them.
16. Select Golf. It launches normally.

### Feature 3, reordering

17. In **Settings → Apps**, put the selection on **Weather** and press SELECT. The row gains a
    **"Moving"** subtitle — it has been picked up.
18. Press UP twice. Weather moves up two rows, travelling with the selection.
19. Press SELECT. The subtitle goes away and the order is saved.
20. Leave Settings, open the app list. **Weather is two rows higher.**
21. Reboot the watch and open the app list again. **Weather is still there.**
22. Back in Settings → Apps, try to move an app down past the "Not in the list" heading. It
    should refuse — a grabbed app stays in the main list.
23. With nothing grabbed, scroll down through the heading. The selection should **skip over it**
    rather than landing on it.

If reordering appears to work but does not survive a reboot, the write is the suspect, not the
UI: `write_uuid_list_to_file` runs on KernelBackground via `system_task_add_callback`, and a
failure there is silent by design.

## Log

- **2026-09-01** — branch created.
- **2026-09-01** — Research and planning session, no firmware changed. Mapped the watchface
  click table, the Quick Launch preference layer, the launcher's data source and filter,
  `app_order_storage`, the settings-module conventions, and the whole touch stack from the
  CST816 driver up through the recognizers and the `touch_nav` bridge. Did the Apple Watch
  research: the Digital Crown is a top-level toggle (app → face, face → Home Screen);
  long-press the face gives a horizontally paged, snapping carousel; only the *grid* Home
  Screen is user-orderable, by touch-and-hold then drag; and watchOS 10 removed
  swipe-to-switch-face as too easy to trigger by accident, restoring it in 10.2 behind an
  off-by-default setting. Settled the BACK question with John and recorded his four
  decisions. Found and corrected one wrong assumption about watchface touch — see Tried and
  rejected.
- **2026-09-01** — Wrote the button half of feature 1: the `qlSingleClickSelect` preference
  with its "Tap Center" settings row, and the two new watchface click handlers. Not compiled —
  no toolchain — so CI is the first check and the wrist is the second. Two things were done
  deliberately and are worth not undoing: `prv_back_click` calls the upstream
  `prv_dismiss_timeline_peek` rather than `timeline_peek_dismiss()` directly, which keeps that
  function referenced so `CONFIG_STONE=n` compiles without an unused-function warning; and
  every `quick_launch_single_click_*` switch got a guarded `BUTTON_ID_SELECT` case, all four of
  them, because the ones that previously fell through to `PBL_ASSERTN(0)` would croak at
  runtime rather than fail the build. If a Select tap panics the watch, a missed switch is the
  first place to look.
- **2026-09-01** — John settled both open questions: the carousel is hold-then-swipe-then-tap
  with no bare swipe, and Health is already in the list. The first answer promotes watchface
  touch from optional to blocking, because the entry gesture is a long-press on a running
  watchface and that is precisely what the app-side touch service refuses. Next session starts
  there: dispatch the CST816's long-press and swipe gesture IDs, extend `TouchGesture` /
  `GestureEventType`, and route `PEBBLE_GESTURE_EVENT` to a new `watchface_handle_gesture_event()`
  from `kernel/event_loop.c` where it currently returns after the backlight wake.
- **2026-09-01** — Wrote the touch half of feature 1, plus features 2 and 3. All three build
  clean in CI; none has been on hardware. Notes for whoever picks this up: the four upstream
  watchface click handlers are `#ifndef CONFIG_STONE`-guarded rather than left unreferenced,
  because the build is `-Wall -Werror` and an unused static is a failed build, not a warning —
  the same trap applies to anything else replaced wholesale. `stone_app_list.{c,h}` is excluded
  from the SDK shell, whose registry has four apps and none of the `APP_ID_*` constants it
  needs; that would have been a wasted cycle. Swipe directions are corrected twice on purpose,
  for board axis inversion in the driver and for left-hand mode in the service, because a
  direction that is not turned with the display reports backwards on a rotated watch.
- **2026-09-01** — Built the watchface picker and wired its two entry points: the touch long
  press on the face, and Hold Center, which now defaults to it rather than to nothing. It is an
  ordinary system app (id `-101`, gated on `CONFIG_STONE` through the registry's `ifdefs`), which
  is what let it reuse Quick Launch instead of growing new button plumbing. Compiles; entirely
  unrun. Two things a later session should not have to rediscover: the picker's window calls
  `window_set_touch_bridge_disabled(true)`, without which a horizontal swipe becomes a Back or
  Select press before the picker's recognizers see it; and committing a face goes through
  `app_manager_put_launch_app_event` rather than `watchface_set_default_install_id`, because
  upstream sets the default only on a *successful* launch and writing the pref directly would
  strand the wearer on a face whose fetch failed.
- **2026-09-01** — Built the three follow-ups John asked for: navigation haptics, real watchface
  miniatures, and the edge-gated back swipe. All three compile; none has run.

  The previews correction is the one worth carrying forward. The picker's old header comment
  claimed real previews were impossible for three reasons and **two were wrong**: applib has no
  scaler but the firmware ships two (the compositor's bilinear rescale, the weather app's squash),
  and the heap argument was about a full frame when a thumbnail is 5KB — `clock_face.c:620`
  already allocates a full 45KB frame in a system app and works. Only "a face that has never been
  worn has never rendered a pixel" was true, and that is what makes the cache progressive rather
  than impossible.

  Two things a later session should not have to rediscover. The capture point is exactly
  `prv_app_switch()` before `prv_app_cleanup()`: the outgoing face's frame is still in the system
  framebuffer, the incoming app's first render is what clears it, and that function is KernelMain
  so reading it cannot race a composite. And the framebuffer is ARGB2222 — averaging the bytes
  during downscale mixes alpha into red and produces colours that were never in the image, so the
  box filter averages each channel separately.

  On haptics: the rate limit is load-bearing, not politeness. `vibe_pattern/service.c:381` drops
  any enqueue made while a pattern plays, so an unthrottled tick gives *inconsistent* feedback.
  And there is a perceptibility floor — too short and too soft is felt as nothing, which is why
  the tick reuses the globe view's measured 25ms @ 25%.

  On edge-back: Pebble has **no interactive transition**. `window_stack` offers only canned
  compositor transitions, so the finger-tracked half of Apple's gesture cannot be built without
  compositor work. The edge *gate* shipped; `stone_edge_back.c` holds the projection maths
  (0.998 deceleration, ~half the velocity in pixels) ready for when there is something to drive.
