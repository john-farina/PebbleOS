---
orphan: true
---

# Navigation overhaul

| | |
| --- | --- |
| **Branch** | `feat/navigation-overhaul` |
| **Started** | 2026-09-01 |
| **Status** | Planned; no firmware written |

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

Research and planning are done. No firmware has been written. The branch is this file.

The research result that matters: **three of the four features are wiring, not new
subsystems.** The launcher already filters its list; app order already persists to flash with
a working write path; Quick Launch already configures click *and* hold per button. And there
is already a full touch stack — service, session gating, tap/pan/swipe recognizers, and a
tiered touch-navigation bridge, mostly Core Devices 2026. **Do not write a touch service.**

The one thing that is *not* free, and that the plan initially got wrong: **watchfaces are
deliberately excluded from touch.** `applib/touch_service.c:22-25` returns a NULL service
state for `sys_app_is_watchface()` with the comment *"Touch is reserved for watchapps;
watchfaces must not consume it."* So no gesture reaches a running watchface today. See
Decided for the shape that gets around it without fighting that rule.

Next step is feature 1 — smallest diff, biggest change in feel.

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
- **Carousel buttons: UP/DOWN step, SELECT keeps, BACK leaves unchanged.** John's pick.
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

- **Should a bare swipe on the watchface switch faces, with no long-press?** Apple shipped
  this, removed it in watchOS 10 because people triggered it by accident, then restored it in
  10.2 behind a setting that is off by default. Recommendation: long-press only to begin, and
  add the swipe behind a Settings toggle if John misses it. **John decides.**
- **Multi-finger is not possible on this hardware.** The CST816 is a single-point controller
  and the driver reads one contact record (`CST816_TOUCH_DATA_SIZE 5`). It is not that the
  stack collapses multiple points — a second point never enters the system. `TouchEvent`
  carries a bare `(x, y)` with no finger index and a `_Static_assert` capping it at 9 bytes;
  `touch_client.h` still declares `touch_dispatch_touch_events(TouchIdx, ...)` from Google's
  old multi-touch design, but `TouchIdx` is defined nowhere in the tree and the function is
  never implemented, so that header is dead code. Two-finger gestures would need a different
  controller plus a wider event. **Flagged, not actionable.**
- **Is Health actually in the launcher today?** Its `ProcessVisibilityHidden` sits inside
  `#if CAPABILITY_HAS_CORE_NAVIGATION4` (`apps/system/health/health.c:181`), and that macro
  is defined nowhere in the repository, so the line should never compile and Health should be
  visible. **Confirm on the watch** before feature 2 does anything about it.

## How to test it

Nothing to test yet — no firmware has changed. Each feature adds its own steps here as it
lands, and this paragraph goes away when the first one does.

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
