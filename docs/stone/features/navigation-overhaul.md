---
orphan: true
---

# Navigation overhaul

| | |
| --- | --- |
| **Branch** | `feat/navigation-overhaul` |
| **Started** | 2026-09-01 |
| **Status** | Seven pieces built and published. On John's wrist for testing; nothing confirmed yet |

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

Three more were asked for once the first four were building, and are part of this branch:

5. **Real watchface miniatures** in the picker, instead of icons.
6. **A haptic vocabulary**, with the three right-hand buttons carrying direction:

   > make the right side 3 buttons have direction, the top one can have top right, middle can
   > have right center, the bottom can have right bottom

   Worth knowing what that could and could not become: there is **one** motor, so nothing can be
   *located* in a corner. Direction is encoded in time instead — a rising envelope reads as up, a
   falling one as down. See `src/fw/applib/haptics/CLAUDE.md`.
7. **Edge-gated back swipe**, after researching how iOS does it:

   > research how apple does swiping from the edge of the screen logic and lets make a really
   > good detector for that

## Where it stands

**Everything below is written, builds for obelix, and is published as an installable channel.
John is testing it on the watch; nothing has been confirmed working there yet.** Treat every
"should" in this file as unverified until he says otherwise.

Latest build: `v200.0.0.1-47-g7d0cb89ce`. He was on `34ec6f2` for a while, which predates
haptics, thumbnails and edge-back — if he reports something missing, **check Settings → Stone →
Commit before debugging it.**

**2026-09-01: he was still on `34ec6f2`, and reported haptics and previews as broken.** They were
not broken; they were not installed. `34ec6f2` is seven commits behind the branch head and every
one of features 5, 6 and 7 lands after it. This is the second time the installed commit has been
the answer, so it is now the first thing to check and not the last.

Investigating the report anyway found that **two of the three would not have worked on the newest
build either**, for reasons no amount of wrist testing would have explained. Both are fixed; see
the 2026-09-01 fixes log entry.

### The seven pieces, and how sure we are

| # | Feature | State |
| --- | --- | --- |
| 1 | BACK opens the app list, and toggles back | **Verified in `qemu_emery`** by screenshot, both directions |
| 2 | Curated app list (Golf, Send Text, Reminders, Sports moved out) | **Verified in `qemu_emery`** — list ends Settings…Timeline, Golf gone |
| 2b | Settings → Apps exists, between System and Stone | **Verified in `qemu_emery`** |
| 3 | Grab-and-move reordering | Compiles. **Never run.** First on-watch writer of `lnc_ord`, and a write failure is silent by design |
| 4 | Watchface picker (app `-101`) | Compiles. **Never run.** Likeliest failures: the paging animation, and whether opting out of the touch-nav bridge really leaves horizontal swipes to its own recognizers |
| 5 | Real watchface miniatures | **Could not have worked as first written** — every store was rejected. Fixed; still never run |
| 6 | Navigation haptics + directional side buttons | **Light effects were below the motor's floor as first written.** Fixed; still never run, and cannot be — QEMU has no vibe motor |
| 7 | Edge-gated back swipe | Compiles. **Never run**, and cannot be — QEMU emits no gestures. The recognizer's reliability fixes *are* covered by unit tests |

### Two bugs that a wrist could not have diagnosed

Worth reading before trusting anything else in this file, because both were invisible in exactly
the way that makes on-watch testing useless:

1. **No thumbnail was ever stored.** A settings record's value length is an 11-bit field, so
   `SETTINGS_VAL_MAX_LEN` is 2046 bytes. A thumbnail is `(200/3) * (228/3)` = **5016 bytes**, so
   `settings_file_set()` returned `E_RANGE` for every capture ever attempted — and `prv_store()`
   discarded the status. The picker then fell back to the icon, which is *exactly what it is
   supposed to do for a face that has never been worn*. A correct-looking fallback hiding a
   total failure is why this survived a build, a review and a wrist.
2. **The light haptics asked the motor for an amplitude it cannot produce.** The envelope scaled
   the shape between zero and the peak, so a Light effect (peak 25%) opening at 40% of shape
   asked for 10% — about 12 of 128 gain counts, well under the LRA's starting threshold, for
   10ms. The first third of every button press was silent, and since all three side buttons are
   Light, so was much of the directional feel they exist for.

Both are fixed. Neither could have been found by pressing buttons: the first is arithmetic
against a header constant, the second is arithmetic against the driver's gain register.

### If you are picking this up cold, read these three first

1. **`src/fw/applib/haptics/CLAUDE.md`** — the haptic model, the four traps, and the driver
   TODOs. Do not touch haptics without it.
2. **"The simulator cannot test any of this"**, below. Two sessions have now lost time here.
3. **"Build for obelix locally"**, below. `qemu_emery` is not a superset of obelix and a green
   local build proved nothing for a whole feature.

### What to do next, in order

0. **Make sure John is actually on the branch head.** Settings → Stone → Commit. Every report so
   far has come from `34ec6f2`, which has none of features 5–7 in it.
1. **Wait for John's report on the head build.** Most of the risk is in features 3–7 and only a
   wrist can retire it.
2. **Haptics will need tuning, not redesign.** Strength, character and motion are three tables in
   `stone_haptics.c`; "too weak" or "the direction doesn't read" are table edits. Resist adding
   per-call-site millisecond values — that is exactly what the vocabulary exists to prevent.
3. **The two driver bugs in the CLAUDE.md TODO list are small and real**: VBAT compensation is
   never enabled for normal playback (haptics fade as the battery drains), and
   `vibe_get_braking_strength()` reads a CONT-mode register while playing in RAM mode, which
   makes `brake_duration_ms` meaningless in every `.vibe` resource. Either is an afternoon.
4. **Do not reroute the rest of the firmware's haptics through Stone wholesale.** The reason is
   in the CLAUDE.md TODO: `vibes_enqueue_custom_pattern` is shared with alert-shaped callers, and
   the boolean path carries the wearer's intensity preference. Call site by call site, UI only.

## Build for obelix locally, not just qemu_emery

`./pbl configure --board=obelix@pvt && ./pbl build` works with the SDK toolchain and takes a
couple of minutes. **Do this before pushing anything board-specific**, because `qemu_emery` is
not a superset of obelix and a green local build proves less than it looks.

This cost a CI cycle: `boards/qemu_emery/defconfig` has **no `CONFIG_VIBE`**, so the entire
haptics implementation compiled to its no-op stub locally while the real code was never built at
all. CI compiled it for obelix and rejected `haptic < 0` on an unsigned enum
(`-Werror=type-limits`) — a fault that had been sitting in a file that "built clean" three times.

Anything behind a board-gated Kconfig has the same hazard. `CONFIG_TOUCH` and `CONFIG_VIBE` are
both obelix-only among the boards worth testing.

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

### Features 5–7, hardware only

24. **Press UP, then SELECT, then DOWN.** They should feel *different* — rising, level, falling.
    If they feel identical, the envelope shaping is not landing, and that is a redesign rather
    than a retune. If they feel like *nothing*, check that the build contains the floor fix
    (`PERCEPTIBLE_FLOOR_PCT` in `stone_haptics.c`) before touching the tables.
25. **Swipe right starting from the left fifth of the screen** → goes back, with a falling tick.
    The strip is `DISP_COLS / 5`, so 40px of the 200px panel — reachable without aiming.
26. **Swipe right starting from the middle of the screen** → *nothing should happen.* This half
    is the point of the change; before it, that gesture went back from anywhere.
26b. **Swipe back in an arc, and slowly, and after resting your finger first.** All three used to
    fail and should now work; they are the three reliability fixes, and they are the ones John
    asked for. Each has a unit test, so a failure here means the gesture never reached the
    recognizer, not that the recognizer rejected it.
27. **Wear a face, switch to another, then open the picker.** The first face should now show a
    real miniature rather than its icon. A face you have never worn stays an icon — that is
    correct, not a bug.
28. **Swipe quickly through the picker.** If the ticks feel *intermittent* rather than dense, the
    rate limit in `stone_haptics.c` wants **raising**, not lowering: the pattern service drops
    anything enqueued while a pattern plays, so ticks that are too close together are lost.

Haptic feedback is best reported in words — "too weak", "too buzzy", "the direction doesn't
read" — rather than in milliseconds. The numbers have hardware constraints behind them, and the
mapping from feel to number is what `stone_haptics.c` exists to own.

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
- **2026-09-02 (tracing, and the real swipe fix)** — John: the picker shows a miniature for the one
  face he has worn and icons for the rest, and swiping still feels bad. The second report was the
  useful one, because it turned out the swipe work had never been in the path he was using.

  **Watchface swipes were never going through `swipe.c`.** applib's touch service hands a
  watchface a NULL state on purpose, so the recognizers cannot run there, and the earlier session
  wired watchface swipes to the CST816's own gesture engine instead. That engine is a black box
  with no thresholds we can reach. So every fix made to the recognizer -- the straightness
  corridor, the motion-start clock, the fling rule -- improved swiping *inside apps* and did
  nothing at all on the face, which is where John does most of his swiping. That is the whole
  explanation for "still sucks".

  Fixed by detecting swipes on the face from the raw contact points, which the kernel event loop
  already receives, under the same rules as everywhere else
  (`applib/ui/recognizer/stone_swipe_detect.c`). Written as a plain state machine over `(x, y, t)`
  rather than a Recognizer, for one reason that matters more than tidiness: **it is testable
  without a watch.** `tests/fw/ui/recognizer/test_stone_swipe_detect.c` has 16 cases, including
  the thumb arc and the rest-then-swipe that were the actual complaints. The shape constants moved
  from `swipe.c` to `swipe.h` so the two paths cannot drift; a watch where the same gesture needs
  a different flick depending on what is on screen would be worse than one where it is merely hard.
  The controller's swipe gestures are now ignored on the face. Its long press is still used --
  holds are the one thing it is genuinely better at.

  **The previews are not broken.** A face can only be photographed while it is running: one app
  task, one framebuffer. So the cache fills as faces are *worn*, and John has worn one. Picking a
  face from the picker and opening the picker again captures it, so it fills over a few uses.
  Wanting all of them at once means launching every face in turn, which is a deliberate
  "generate previews" action rather than something to do behind the wearer's back -- **not built,
  and it should be John's call whether a few seconds of flicker is worth it.** The thumbnail path
  is now traced (`thumb absent` / `thumb loaded` / `thumb store FAILED`), so the next report can
  distinguish this from a real fault instead of us re-deriving it a third time.

  **And the thing that should have existed first: traces.** See {doc}`../tracing`. Hold UP,
  SELECT and DOWN for five seconds and the watch prints the last 192 events its touch, gesture,
  swipe, picker and thumbnail paths saw. Three notes for later:

  - **Not BACK in the combo.** `SELECT+BACK` held is the hardware reset back door, so a debug
    combo including BACK would reboot the watch instead of capturing what you were trying to
    capture.
  - **Not `PBL_LOG`.** A log line costs a format string, a flash write and a filename, which
    cannot go on every touch sample; and the interesting events would be drowned by the time
    anyone looked. A fixed-size record with two integers is cheap enough to leave switched on.
  - **The decoder names the codes, the watch does not.** A name costs flash there and nothing in
    `tools/stone/decode_trace.py`. Add the name in the same commit as the trace point, or the next
    person reads a bare number.

  Verified: 332 firmware tests, 47 tools tests, 47 channel-server tests, and a clean `obelix@pvt`
  build. Nothing here has been on hardware -- but for the first time the swipe rules themselves
  have been, in the only way they can be off a wrist.

- **2026-09-02 (first wrist test: two crashes)** — John held the watchface. He felt the haptic --
  **the first confirmation that any of the haptics work on hardware** -- and then the screen
  thrashed, input stopped landing, and the watch reset. No preview either.

  **The hold was dispatching tens of long presses a second.** `CST816_GESTURE_ID` *holds* its
  value rather than pulsing, and `prv_process_pending_messages()` runs on every interrupt the
  controller raises, which is every report while a finger is down. So a single hold read back
  `0x0C` report after report, and each one ran the whole handler: a Firm haptic (unthrottled, by
  design -- only Light is throttled) and an app launch. The watch spent the hold thrashing launch
  transitions until it gave up. Fixed in the driver, which is the layer that knows when a contact
  begins and ends: a gesture is dispatched at most once per contact, the latch clears on
  finger-up, and `GESTURE_NONE` deliberately does not clear it because the controller can report
  it mid-hold. This was never a picker bug -- **the picker had not run yet.**

  Worth carrying: this is the failure mode of *every* held gesture on this controller, not just
  this one. Tap and double-tap hid it because they latch briefly around release; a hold cannot.

  **The second one would have hung the first open even with the flood fixed.**
  `settings_file_open()` allocates its maximum up front, the thumbnail cache's maximum is ~80KB,
  and `stone_face_thumb_load()` is called from the picker's window load -- on the *app* task. The
  first open on a fresh watch would have erased twenty NOR sectors before the window appeared.
  Now opened with `settings_file_open_growable()` seeded at 4KB, so it grows as thumbnails
  actually arrive and the slow flash work stays on the system task where the write already lives.

  Two things checked and cleared while looking, so nobody re-checks them: the picker's
  `property_animation_create()` copies its from/to values (`prv_init` dereferences them), so the
  stack locals in `prv_step` are not aliased; and the paging animation is horizontal, so it was
  never what "shaking up and down" was.

  **The missing preview is expected on a first open and is not a third bug.** A face can only be
  photographed while it is running, so the cache fills as faces are worn; and the capture of the
  face you just left is handed to the system task, so it has not reached flash by the time that
  same picker launch reads it. **The face you were wearing shows up on the *second* open.** If a
  face still has no miniature after being worn and left twice, that is a real fault -- look at the
  `thumb:` warnings, which now exist for exactly this.

- **2026-09-01 (build identity)** — Builds now name themselves. A WIP build carries
  `navigation.7` -- the branch and how far into it the build is -- plus one sentence from
  `tools/stone/build_summary.txt` saying what changed, and the pair is rendered by a **Stone app
  at the top of the app list** rather than by a row five deep inside Settings.

  This exists because of the session logged directly below: a whole investigation was spent on
  features that were absent from the build being described, and the only identifier available was
  a commit hash. Seven hex digits do not order themselves, so nothing about `34ec6f2` says it is
  older than `7d0cb89`. That is the failure this closes, and it is why the summary is a **CI gate
  rather than a convention** -- the check is "is the summary older than the newest code change?",
  which cannot be satisfied by one edit at the start of a branch the way "was it ever touched?"
  can.

  Three things a later session should not have to rediscover:

  - The label is **derived, not stored**. A counter in a file is one two sessions can disagree
    about; one in CI cannot be reproduced locally. Counting commits since the branch left `stone`
    is neither, and a given number always names the same code.
  - Stone is still a **settings module**, just not a *listed* one. `SettingsMenuItem_ListedCount`
    now splits "rows in the Settings list" from "categories that exist", so the app pushes the
    same window with `settings_menu_push()` and there is one implementation of the list rather
    than two. Adding another unlisted category means adding it after that marker.
  - The app-list row's subtitle comes from a bespoke glance (`app_glance_stone.c`), modelled on
    the Watchfaces one because it is the same shape: a title, an icon, and one line that cannot
    change while you are looking at it. It shows the label on a WIP build and the version string
    on a release build -- never both, since showing a version on a WIP build is how the two get
    confused in the first place.

  **Unverified:** the row's appearance. The app list is button-reachable and so is exactly the
  kind of thing `qemu_emery` can screenshot, but this environment has no PebbleOS SDK and so no
  `qemu-pebble`. It compiles for `obelix@pvt`, and the glance follows the Watchfaces one
  structurally, but nobody has looked at it. **First thing to check on the watch: does the Stone
  row sit at the top of the app list, with its build label underneath, and does the icon read at
  that size?** It borrows `RESOURCE_ID_SETTINGS_TINY`, the same gear Settings uses, because no
  better icon exists in the tree yet; a dedicated one is a small resource change if it looks
  wrong next to Settings.

- **2026-09-01 (fixes)** — John reported haptics and previews as not working, from `34ec6f2`,
  which contains neither. The build was the whole of that answer. Auditing the head build anyway
  found that two of the three follow-ups were broken on their own terms, and fixed those plus the
  swipe reliability he asked about. In order of how badly each was hiding:

  **Thumbnails were never stored** (see "Two bugs a wrist could not have diagnosed"). Fixed by
  splitting each thumbnail across `STONE_FACE_THUMB_CHUNKS` records of 1024 bytes, with the chunk
  index in the *key* rather than the value — so a capture interrupted halfway is a missing key and
  the load fails cleanly, instead of a short read painting garbage over half the picker. A
  `_Static_assert` now pins the chunk against `SETTINGS_VAL_MAX_LEN`, and both the open and every
  set are checked and logged. The cache is capped at `THUMB_MAX_FACES` (8), which at ~5KB each is
  about 1% of the 5MB filesystem; the ninth face keeps its icon and says so in the log.

  **Light haptics were under the motor's starting threshold.** The envelope now spans
  `PERCEPTIBLE_FLOOR_PCT`..peak rather than 0..peak, so no segment can be asked for an amplitude
  the LRA will not start at. That alone would have flattened Light (its peak *was* the floor, so
  the span would be zero and all three side buttons would feel identical — the opposite of the
  feature), so the peaks moved up to 50/65/85 to leave the envelope somewhere to go. These are
  still tables; "too strong" is still a table edit.

  **Envelope segments were restarting the waveform.** `prv_vibes_set_vibe_strength()` called
  `vibe_ctl(true)` on every non-zero step, which reconfigures the driver and re-issues the play
  command — restarting the RAM waveform from its first sample. A three-segment swell was three
  restarts, each spending part of its 10ms on the start-up transient. The header comment in
  `stone_haptics.c` claimed this path was already a bare gain write; it was not, and the claim is
  now true because the service was changed to match it. **Gated on `CONFIG_STONE`** deliberately:
  it is shared with alarms and notification vibes, and the reasoning (the RAM waveform is an
  infinite hardware loop, so skipping the re-GO cannot leave the motor silent) is read from the
  driver rather than measured. **Worth a specific check on the wrist: do alarms and notification
  vibes still feel right?** If they do not, that gate is where to look.

  **Swipe-back reliability**, which John reported as bad in upstream's own build too. Three
  faults in `swipe.c`, all of which reject gestures the wearer plainly meant:

  - The straightness test was a pure ratio applied from 11px of travel, where a few pixels of
    contact noise are most of the major axis. Twelve pixels across with seven of jitter failed
    the swipe *permanently*, before it had finished starting. It is now a corridor — half the
    major axis plus `SWIPE_STRAIGHTNESS_SLACK_PX` — which is also what lets a thumb swipe in the
    arc a thumb actually travels.
  - The 300ms budget ran from touchdown, so resting a finger before swiping spent it. It now runs
    from the first sample that moved more than `SWIPE_MOTION_START_PX`.
  - A flick lifts off while still moving, so it always measures shorter than a drag of the same
    intent, and anything under 30px was rejected however fast it was going. The velocity was
    already being computed and thrown away; it now admits a short path that was moving at
    `SWIPE_FLING_MIN_VELOCITY_PX_S` at liftoff.

  These are **ungated**, unlike the vibe change. Gating a recognizer fix on `CONFIG_STONE` would
  put it outside the unit tests, which do not define it — and this is the one part of the branch
  that *can* be tested off-wrist. `tests/fw/ui/recognizer/test_swipe.c` is now 18 cases and all
  331 tests in the tree pass. Two existing cases were rewritten rather than deleted: `too_crooked`
  now uses a path that is genuinely diagonal (40/30) instead of one that was merely starting
  (20/15), and `length_below_boundary` draws its 29px slowly, so it still pins the distance rule
  instead of being carried over by the new fling rule.

  **The edge strip went from 20px to `DISP_COLS / 5`** (40px on obelix). iOS can afford 5% because
  its transition tracks the finger and a miss is visible; here a miss is indistinguishable from
  the watch ignoring you, which is precisely the complaint.

  One thing deliberately *not* done: `stone_edge_back.c` is still dead code. Nothing instantiates
  the pan recognizer — only `STONE_EDGE_BACK_WIDTH_PX` is used, by `touch_nav.c`. It stays because
  the projection maths is the hard part and is ready for whenever the compositor grows an
  interactive transition, but nobody should read its presence as meaning the edge gesture is
  pan-driven today. It is a coordinate check on the existing swipe.

  Verified: all six changed files compile for `obelix@pvt` with `CONFIG_STONE`, `CONFIG_VIBE` and
  `CONFIG_TOUCH` on (the `qemu_emery` trap below), and 331/331 unit tests pass. Nothing here has
  been on hardware.

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
- **2026-09-01** — Added the haptic vocabulary and wired the three right-hand buttons and the back
  gesture to it, after a full audit of the AW86225 driver. The audit's headline is worth carrying:
  **essentially nothing here is a hardware ceiling.** The chip can chain eight RAM waveforms with
  per-slot loop counts from a single command and stream arbitrary samples over RTP; the firmware
  uses one waveform, one slot, looped forever, gated by start/stop. The gap is all driver.

  Scope was deliberately held to the three buttons plus the back gesture. Rerouting the rest of
  the firmware's haptics was started and **reverted**: `vibes_enqueue_custom_pattern` is shared
  with alert-shaped callers, and the boolean path carries the wearer's Settings → Vibrations
  intensity preference, so a wholesale reroute would silently drop it for battery warnings, the
  hourly chime and Bluetooth disconnect. It is a TODO in the haptics CLAUDE.md, to be done call
  site by call site.
