---
orphan: true
---

# Navigation overhaul

| | |
| --- | --- |
| **Branch** | `feat/navigation-overhaul` |
| **Started** | 2026-09-01 |
| **Status** | Feature 1 (buttons) done, untested on hardware. 2–4 not started |

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

**The button half of feature 1 is written and has never been compiled** — there is no ARM
toolchain here, so the first real signal is CI, and the first real *test* is John's wrist.
Two commits: `shell: let the middle button be assigned a tap action` (the `qlSingleClickSelect`
preference and its "Tap Center" settings row) and `shell: make Back open the app list from the
watchface` (both new handlers plus the click table). Everything is behind `CONFIG_STONE`, with
the upstream handlers kept on the `#else` path, so `CONFIG_STONE=n` still builds and still
behaves like stock.

Features 2, 3 and 4 are planned but not started. The **touch half of feature 1 is also not
started** — see the next paragraph, which is the thing most likely to bite the next session.

The research result that matters: **three of the four features are wiring, not new
subsystems.** The launcher already filters its list; app order already persists to flash with
a working write path; Quick Launch already configures click *and* hold per button. And there
is already a full touch stack — service, session gating, tap/pan/swipe recognizers, and a
tiered touch-navigation bridge, mostly Core Devices 2026. **Do not write a touch service.**

The one thing that is *not* free: **watchfaces are deliberately excluded from touch.**
`applib/touch_service.c:22-25` returns a NULL service state for `sys_app_is_watchface()` with
the comment *"Touch is reserved for watchapps; watchfaces must not consume it."* So no gesture
reaches a running watchface today, and the Back change above does **not** give
swipe-right-opens-apps for free the way it first appeared to. See Decided for the shape that
gets around it without fighting upstream's rule.

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
  service refuses watchfaces by design, route `PEBBLE_TOUCH_EVENT` / `PEBBLE_GESTURE_EVENT`
  in `kernel/event_loop.c` when `app_manager_is_watchface_running()`, into a new
  `watchface_handle_touch_event()` beside the existing `watchface_handle_button_event()`
  (`shell/normal/watchface.c:266`). That mirrors exactly how buttons already reach the
  watchface, keeps upstream's "watchfaces must not consume touch" rule intact for *apps*, and
  costs one appended case in the event loop plus one new function.
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

**Touch is not part of this yet.** Swiping on the watchface still does nothing; that is
expected, not a bug. See Where it stands.

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
