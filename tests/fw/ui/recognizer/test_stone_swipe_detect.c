/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// The watchface's swipe detection. Every case here is one a finger can produce and the CST816's
// own gesture engine gave us no way to check.

#include "clar.h"

#include "applib/ui/recognizer/stone_swipe_detect.h"

#include <stdint.h>

static StoneSwipeDetect s_detect;
static uint32_t s_now;

void test_stone_swipe_detect__initialize(void) {
  stone_swipe_detect_reset(&s_detect);
  s_now = 1000;
}

static void prv_down(int16_t x, int16_t y) {
  stone_swipe_detect_down(&s_detect, x, y, s_now);
}

static void prv_move(int16_t x, int16_t y, uint32_t after_ms) {
  s_now += after_ms;
  stone_swipe_detect_move(&s_detect, x, y, s_now);
}

static SwipeDirection prv_up(uint32_t after_ms) {
  s_now += after_ms;
  return stone_swipe_detect_up(&s_detect, s_now);
}

// A clean swipe in each direction.
void test_stone_swipe_detect__completes_right(void) {
  prv_down(10, 100);
  prv_move(40, 100, 20);
  prv_move(80, 100, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_Right);
}

void test_stone_swipe_detect__completes_left(void) {
  prv_down(80, 100);
  prv_move(50, 100, 20);
  prv_move(10, 100, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_Left);
}

void test_stone_swipe_detect__completes_down(void) {
  prv_down(100, 10);
  prv_move(100, 50, 20);
  prv_move(100, 90, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_Down);
}

void test_stone_swipe_detect__completes_up(void) {
  prv_down(100, 90);
  prv_move(100, 50, 20);
  prv_move(100, 10, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_Up);
}

// A thumb pivots at its base, so a horizontal swipe travels an arc. This is the single most
// common real swipe and the ratio-only rule rejected it.
void test_stone_swipe_detect__thumb_arc_completes(void) {
  prv_down(10, 100);
  prv_move(22, 107, 20);   // early jitter: 12 across, 7 down
  prv_move(45, 118, 20);
  prv_move(80, 126, 20);   // 70 across, 26 down overall
  cl_assert_equal_i(prv_up(20), SwipeDirection_Right);
}

// Resting a finger and then swiping is one gesture. The pause is not part of it.
void test_stone_swipe_detect__rest_then_swipe_completes(void) {
  prv_down(10, 100);
  prv_move(11, 101, 800);
  prv_move(12, 100, 800);
  prv_move(80, 100, 40);
  cl_assert_equal_i(prv_up(20), SwipeDirection_Right);
}

// A quick flick lifts off early, so it measures short. Velocity is what says it was deliberate.
void test_stone_swipe_detect__short_fast_fling_completes(void) {
  prv_down(10, 100);
  prv_move(20, 100, 10);
  prv_move(37, 100, 10);   // 27px total, moving at 1700px/s
  cl_assert_equal_i(prv_up(10), SwipeDirection_Right);
}

// Slow and short is a drag, not a swipe.
void test_stone_swipe_detect__short_and_slow_fails(void) {
  prv_down(10, 100);
  prv_move(20, 100, 100);
  prv_move(27, 100, 150);
  cl_assert_equal_i(prv_up(20), SwipeDirection_None);
}

// A genuinely diagonal path is not a swipe in either axis.
void test_stone_swipe_detect__diagonal_fails(void) {
  prv_down(10, 10);
  prv_move(50, 50, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_None);
}

// A slow drag across the screen is a pan.
void test_stone_swipe_detect__too_slow_fails(void) {
  prv_down(10, 100);
  prv_move(40, 100, 200);
  prv_move(80, 100, 200);
  cl_assert_equal_i(prv_up(20), SwipeDirection_None);
}

// A tap that does not move is not a swipe, and must not divide by zero on the way to saying so.
void test_stone_swipe_detect__tap_is_not_a_swipe(void) {
  prv_down(50, 50);
  cl_assert_equal_i(prv_up(30), SwipeDirection_None);
}

void test_stone_swipe_detect__tap_with_jitter_is_not_a_swipe(void) {
  prv_down(50, 50);
  prv_move(52, 51, 20);
  prv_move(51, 52, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_None);
}

// Liftoff arriving late must not fail a swipe that was itself quick: the controller reports
// finger-up some time after the finger has gone.
void test_stone_swipe_detect__late_liftoff_report_still_completes(void) {
  prv_down(10, 100);
  prv_move(40, 100, 20);
  prv_move(80, 100, 20);
  cl_assert_equal_i(prv_up(500), SwipeDirection_Right);
}

// Once ruled out, a contact stays ruled out even if it straightens up later -- otherwise a
// scribble ending in a straight line would count.
void test_stone_swipe_detect__crooked_stays_failed(void) {
  prv_down(10, 10);
  prv_move(50, 50, 20);
  prv_move(90, 52, 20);
  cl_assert_equal_i(prv_up(20), SwipeDirection_None);
}

// Events arriving with no contact in progress are ignored rather than fabricating a gesture.
void test_stone_swipe_detect__move_without_down_is_inert(void) {
  stone_swipe_detect_move(&s_detect, 50, 50, s_now);
  cl_assert_equal_i(prv_up(10), SwipeDirection_None);
}

// A second contact starts clean.
void test_stone_swipe_detect__consecutive_swipes(void) {
  prv_down(10, 100);
  prv_move(80, 100, 30);
  cl_assert_equal_i(prv_up(10), SwipeDirection_Right);

  prv_down(80, 100);
  prv_move(10, 100, 30);
  cl_assert_equal_i(prv_up(10), SwipeDirection_Left);
}
