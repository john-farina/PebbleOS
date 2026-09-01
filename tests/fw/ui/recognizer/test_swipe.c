/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "applib/ui/recognizer/recognizer.h"
#include "applib/ui/recognizer/recognizer_impl.h"
#include "applib/ui/recognizer/recognizer_private.h"
#include "applib/ui/recognizer/swipe.h"

#include "pbl/drivers/rtc.h"

// Fakes
#include "fake_rtc.h"

// Stubs
#include "stubs_passert.h"
#include "stubs_pbl_malloc.h"
#include "stubs_logging.h"

#include "test_recognizer_impl.h"

// The manager is not under test here; swallow the notification.
void recognizer_manager_handle_state_change(RecognizerManager *manager, Recognizer *changed) {}

#define ALL_DIRECTIONS \
  (SwipeDirection_Up | SwipeDirection_Down | SwipeDirection_Left | SwipeDirection_Right)

static RecognizerEvent s_last_event;

static void prv_event_cb(const Recognizer *recognizer, RecognizerEvent event) {
  s_last_event = event;
}

// setup and teardown
void test_swipe__initialize(void) {
  s_last_event = -1;
  fake_rtc_init(0, 0);
}

void test_swipe__cleanup(void) {}

// Helpers
static void prv_dispatch(Recognizer *r, TouchEventType type, int16_t x, int16_t y) {
  const TouchEvent e = {
    .type = type,
    .x = x,
    .y = y,
  };
  recognizer_handle_touch_event(r, &e);
}

static void prv_advance_ms(uint32_t ms) {
  fake_rtc_increment_ticks((RtcTicks)ms * RTC_TICKS_HZ / MS_PER_SECOND);
}

// SWIPE_MAX_DURATION_MS (300) / SWIPE_MIN_LENGTH_PX (30) are private to swipe.c;
// the caps are part of the swipe contract these tests pin. swipe.c floors
// ticks -> ms, so to land the floored duration exactly on a target ms we round
// the tick count UP (ceil), keeping the boundary exact for any RTC_TICKS_HZ.
#define SWIPE_CAP_MS (300)
#define SWIPE_MIN_PX (30)
static RtcTicks prv_ticks_for_floored_ms(uint32_t ms) {
  return ((RtcTicks)ms * RTC_TICKS_HZ + (MS_PER_SECOND - 1)) / MS_PER_SECOND;
}

// Drive a single straight swipe from (sx, sy) to (ex, ey) over a fast duration, with the liftoff
// reported at (0, 0) (which must be ignored).
static void prv_swipe(Recognizer *r, int16_t sx, int16_t sy, int16_t ex, int16_t ey) {
  prv_dispatch(r, TouchEvent_Touchdown, sx, sy);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, (int16_t)((sx + ex) / 2), (int16_t)((sy + ey) / 2));
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, ex, ey);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);
}

// tests

// One completed swipe per accepted direction, with the correct reported direction.
void test_swipe__completes_right(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);
  prv_swipe(r, 10, 50, 70, 50);
  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(s_last_event, RecognizerEvent_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
  // A completed swipe reports a non-zero velocity along the swipe axis.
  cl_assert(swipe_recognizer_get_velocity(r).x > 0);
}

void test_swipe__completes_left(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);
  prv_swipe(r, 70, 50, 10, 50);
  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Left);
}

void test_swipe__completes_down(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);
  prv_swipe(r, 50, 10, 50, 70);
  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Down);
}

void test_swipe__completes_up(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);
  prv_swipe(r, 50, 70, 50, 10);
  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Up);
}

// A swipe shorter than the minimum length fails on liftoff.
void test_swipe__too_short_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 30, 50);  // only 20px, below the 30px minimum
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// A swipe that takes longer than the maximum duration fails early (too slow to be a flick).
void test_swipe__too_slow_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(400);  // exceeds the 300ms max duration
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// A path that wanders too far off the major axis (minor projection > half the major) fails early.
void test_swipe__too_crooked_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 10);
  prv_advance_ms(20);
  // major = 40, minor = 30: past half the major plus the straightness slack, so this path is
  // travelling diagonally rather than wandering while it starts.
  prv_dispatch(r, TouchEvent_PositionUpdate, 50, 40);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// Contact noise in the first few samples must not fail the swipe. A ratio-only straightness test
// is at its harshest exactly where the signal is weakest: 12px of travel with 7px of jitter is a
// swipe beginning, not a diagonal.
void test_swipe__early_jitter_survives(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 22, 57);  // major 12, minor 7
  cl_assert(recognizer_get_state(r) != RecognizerState_Failed);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 55);  // straightens out into a real swipe
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
}

// The duration budget is spent from when the finger moves, not from when it lands. Resting a
// finger on the screen and then swiping is one gesture, and the pause is not part of it.
void test_swipe__rest_before_moving_completes(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  // Held still, well past SWIPE_MAX_DURATION_MS, drifting inside the stationary zone.
  prv_advance_ms(500);
  prv_dispatch(r, TouchEvent_PositionUpdate, 12, 51);
  prv_advance_ms(500);
  prv_dispatch(r, TouchEvent_PositionUpdate, 11, 50);
  // Then swiped.
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
}

// A path under the minimum length that was still moving quickly at liftoff is a flick. The last
// position update always lands before the finger leaves, so a fast gesture measures short.
void test_swipe__short_fast_fling_completes(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 10 + SWIPE_MIN_PX - 1, 50);  // 29px in 20ms
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
}

// A fast, straight path whose finger then lingers past the max duration before
// lifting off fails at the LIFTOFF duration gate. Every position update landed
// within the window, so the position-update early check never fires — this is
// the only test that exercises (and thus pins) the otherwise mutation-dead
// fast_enough term on the liftoff branch.
void test_swipe__slow_liftoff_after_fast_path_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(50);
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);  // 60px, still within the window
  prv_advance_ms(400);  // finger lingers; no further update, so the early check stays silent
  prv_dispatch(r, TouchEvent_Liftoff, 70, 50);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// A path whose floored duration is exactly the cap still completes (inclusive
// bound). Flipping the liftoff `<=` to `<` reddens this.
void test_swipe__duration_at_boundary_completes(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);  // 60px, same tick (fast)
  fake_rtc_increment_ticks(prv_ticks_for_floored_ms(SWIPE_CAP_MS));
  prv_dispatch(r, TouchEvent_Liftoff, 70, 50);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
}

// Major-axis travel of exactly the minimum length is long enough (inclusive).
void test_swipe__length_at_boundary_completes(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 10 + SWIPE_MIN_PX, 50);  // exactly 30px
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
}

// One pixel short of the minimum length fails when it is not also a flick. Pins the off-by-one
// that a 20px/60px pair leaves open; flipping `>=` to `>` reddens the boundary case. The path is
// drawn slowly on purpose, so the liftoff velocity cannot carry it over the fling rule instead --
// that rule has its own test.
void test_swipe__length_below_boundary_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 15, 50);
  prv_advance_ms(150);
  prv_dispatch(r, TouchEvent_PositionUpdate, 10 + SWIPE_MIN_PX - 1, 50);  // 29px, slowly
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// Minor projection exactly half the major (minor * 2 == major) is straight
// enough — the inclusive-pass boundary, complement of the too-crooked early
// fail. Pins the straightness `>` against a `>=` regression.
void test_swipe__straightness_at_boundary_completes(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 0, 0);
  prv_advance_ms(20);
  // major = 40 (x), minor = 20 (y): minor * 2 == major, right on the edge.
  prv_dispatch(r, TouchEvent_PositionUpdate, 40, 20);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
}

// A swipe whose direction is not in the accepted mask fails on liftoff.
void test_swipe__forbidden_direction_fails(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, SwipeDirection_Right);

  // A clean leftward swipe: valid shape, but Left is not in the (Right-only) mask.
  prv_swipe(r, 70, 50, 10, 50);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Failed);
}

// Liftoff reported at (0, 0) must not fabricate an up-left swipe: the gesture end is the last
// position update, so a clean rightward path stays a rightward swipe.
void test_swipe__liftoff_origin_ignored(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);
  prv_advance_ms(20);
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  cl_assert_equal_i(swipe_recognizer_get_direction(r), SwipeDirection_Right);
}

// dt == 0: position updates at the same tick yield zero velocity with no divide-by-zero, and the
// swipe still completes on its geometry.
void test_swipe__zero_dt_velocity_zero(void) {
  NEW_RECOGNIZER(r) = swipe_recognizer_create(prv_event_cb, NULL, ALL_DIRECTIONS);

  prv_dispatch(r, TouchEvent_Touchdown, 10, 50);
  prv_dispatch(r, TouchEvent_PositionUpdate, 40, 50);  // same tick
  prv_dispatch(r, TouchEvent_PositionUpdate, 70, 50);  // same tick
  prv_dispatch(r, TouchEvent_Liftoff, 0, 0);

  cl_assert_equal_i(recognizer_get_state(r), RecognizerState_Completed);
  const GPoint v = swipe_recognizer_get_velocity(r);
  cl_assert_equal_i(v.x, 0);
  cl_assert_equal_i(v.y, 0);
}
