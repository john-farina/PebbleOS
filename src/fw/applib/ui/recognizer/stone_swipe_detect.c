/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#include "stone_swipe_detect.h"

#include "pbl/util/math.h"

//! Rules deliberately identical to swipe.c, and taken from its constants so the two cannot drift.
//! A swipe on the watchface and a swipe inside an app should want the same finger movement; a
//! watch where the same gesture needs a different flick depending on what is on screen is worse
//! than one where it is merely hard.
#define SLACK_PX (SWIPE_STRAIGHTNESS_SLACK_PX)
#define MOTION_START_PX (SWIPE_MOTION_START_PX)

static bool prv_too_crooked(int32_t major, int32_t minor) {
  return (major > SWIPE_STRAIGHTNESS_MIN_PX) && (minor > ((major / 2) + SLACK_PX));
}

void stone_swipe_detect_reset(StoneSwipeDetect *detect) {
  if (detect) {
    *detect = (StoneSwipeDetect){0};
  }
}

void stone_swipe_detect_down(StoneSwipeDetect *detect, int16_t x, int16_t y, uint32_t now_ms) {
  if (!detect) {
    return;
  }
  *detect = (StoneSwipeDetect){
    .start = GPoint(x, y),
    .last = GPoint(x, y),
    .prev = GPoint(x, y),
    .motion_ms = now_ms,
    .last_ms = now_ms,
    .prev_ms = now_ms,
    .active = true,
  };
}

void stone_swipe_detect_move(StoneSwipeDetect *detect, int16_t x, int16_t y, uint32_t now_ms) {
  if (!detect || !detect->active || detect->failed) {
    return;
  }

  detect->prev = detect->last;
  detect->prev_ms = detect->last_ms;
  detect->last = GPoint(x, y);
  detect->last_ms = now_ms;

  const int32_t dx = ABS((int32_t)x - detect->start.x);
  const int32_t dy = ABS((int32_t)y - detect->start.y);
  const int32_t major = MAX(dx, dy);
  const int32_t minor = MIN(dx, dy);

  // Start the clock when the finger actually moves, not when it lands, so resting a finger before
  // swiping does not spend the budget.
  if (!detect->moving) {
    if (major > MOTION_START_PX) {
      detect->moving = true;
    } else {
      detect->motion_ms = now_ms;
    }
  }

  if (prv_too_crooked(major, minor)) {
    detect->failed = true;
  }
}

SwipeDirection stone_swipe_detect_up(StoneSwipeDetect *detect, uint32_t now_ms) {
  if (!detect || !detect->active) {
    return SwipeDirection_None;
  }
  const bool failed = detect->failed;
  const GPoint start = detect->start;
  const GPoint last = detect->last;
  const GPoint prev = detect->prev;
  const uint32_t motion_ms = detect->motion_ms;
  const uint32_t last_ms = detect->last_ms;
  const uint32_t prev_ms = detect->prev_ms;
  stone_swipe_detect_reset(detect);

  if (failed) {
    return SwipeDirection_None;
  }

  const int32_t total_x = (int32_t)last.x - start.x;
  const int32_t total_y = (int32_t)last.y - start.y;
  const int32_t adx = ABS(total_x);
  const int32_t ady = ABS(total_y);
  const int32_t major = MAX(adx, ady);
  const int32_t minor = MIN(adx, ady);

  if ((major == 0) || prv_too_crooked(major, minor)) {
    return SwipeDirection_None;
  }

  // The duration is measured to the last sample, not to liftoff: the controller reports finger-up
  // some time after the finger has actually gone, and charging that delay to the gesture would
  // fail slow-to-report swipes that were themselves quick.
  const uint32_t duration_ms = (last_ms >= motion_ms) ? (last_ms - motion_ms) : 0;
  if (duration_ms > SWIPE_MAX_DURATION_MS) {
    return SwipeDirection_None;
  }
  (void)now_ms;

  // A flick lifts off while still moving, so it measures shorter than a drag of the same intent.
  int32_t major_velocity = 0;
  const uint32_t dt = (last_ms > prev_ms) ? (last_ms - prev_ms) : 0;
  if (dt > 0) {
    const int32_t moved = (adx >= ady) ? ((int32_t)last.x - prev.x) : ((int32_t)last.y - prev.y);
    major_velocity = ABS((moved * 1000) / (int32_t)dt);
  }
  const bool long_enough =
      (major >= SWIPE_MIN_LENGTH_PX) ||
      ((major >= SWIPE_FLING_MIN_LENGTH_PX) && (major_velocity >= SWIPE_FLING_MIN_VELOCITY_PX_S));
  if (!long_enough) {
    return SwipeDirection_None;
  }

  if (adx >= ady) {
    return (total_x >= 0) ? SwipeDirection_Right : SwipeDirection_Left;
  }
  return (total_y >= 0) ? SwipeDirection_Down : SwipeDirection_Up;
}
