/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "swipe.h"

#include "applib/graphics/gtypes.h"

#include <stdbool.h>
#include <stdint.h>

//! Swipe detection over raw contact points, for the watchface.
//!
//! The watchface cannot use the recognizers. applib's touch service hands a watchface a NULL
//! state on purpose -- touch is reserved for watchapps -- so `swipe.c` never runs there, and
//! watchface swipes were taken from the CST816's own gesture engine instead. That engine is a
//! black box with no thresholds we can reach, and it is why swiping on the face kept feeling
//! unreliable while swiping inside an app improved: the fixes to `swipe.c` were never in that
//! path at all.
//!
//! This is the same rules over the raw points the driver already reports, which the kernel event
//! loop already sees. It is a plain state machine rather than a Recognizer because a Recognizer
//! needs a manager and an activation lifecycle that the shell has no use for -- and because a
//! state machine taking (x, y, t) can be tested exhaustively without a watch, which is the whole
//! problem with everything else in this area.
//!
//! Time is passed in rather than read, for the same reason.

typedef struct {
  GPoint start;         //!< where the contact began
  GPoint last;          //!< most recent point
  GPoint prev;          //!< the point before that, for the liftoff velocity estimate
  uint32_t motion_ms;   //!< when the finger first moved; the duration budget runs from here
  uint32_t last_ms;
  uint32_t prev_ms;
  bool active;          //!< a contact is in progress
  bool moving;          //!< it has left the stationary zone
  bool failed;          //!< it has already been ruled out
} StoneSwipeDetect;

//! Abandon any contact in progress.
void stone_swipe_detect_reset(StoneSwipeDetect *detect);

//! Begin a contact.
void stone_swipe_detect_down(StoneSwipeDetect *detect, int16_t x, int16_t y, uint32_t now_ms);

//! Feed a position update. Cheap, and safe to call for every sample.
void stone_swipe_detect_move(StoneSwipeDetect *detect, int16_t x, int16_t y, uint32_t now_ms);

//! End the contact and decide.
//! @return the direction swiped, or SwipeDirection_None if this contact was not a swipe
SwipeDirection stone_swipe_detect_up(StoneSwipeDetect *detect, uint32_t now_ms);
