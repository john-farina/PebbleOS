/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Edge-back gesture.
//
// There is almost nothing here, and that is deliberate: a pan recognizer already tracks a
// single-axis drag with velocity, and recognizer_set_touch_filter() already gets to inspect the
// touchdown before the recognizer starts. An edge-restricted pan is those two facts put together,
// which is exactly what UIScreenEdgePanGestureRecognizer is.

#include "stone_edge_back.h"

#if defined(CONFIG_STONE) && defined(CONFIG_TOUCH)

#include "pan.h"

//! Deceleration rate, x1000, matching UIScrollView's 0.998. The projection factor is
//! rate / (1 - rate), so 0.998 gives 499 -- a finger moving at 1000 px/s coasts about 500px.
#define PROJECTION_NUMERATOR (998)
#define PROJECTION_DENOMINATOR (1000 - PROJECTION_NUMERATOR)

//! Commit past the midpoint. Apple does not publish its threshold, but half is what the
//! interactive pop feels like and it is the only value that is symmetric -- anything else means
//! the gesture behaves differently depending on which way you are already going.
#define COMMIT_FRACTION_NUMERATOR (1)
#define COMMIT_FRACTION_DENOMINATOR (2)

static bool prv_started_at_edge(const Recognizer *recognizer, const TouchEvent *touch_event) {
  // Only the touchdown decides. Once the gesture owns the finger it may travel anywhere; what
  // matters is where it began.
  if (touch_event->type != TouchEvent_Touchdown) {
    return true;
  }
  return touch_event->x < STONE_EDGE_BACK_WIDTH_PX;
}

Recognizer *stone_edge_back_init_static(void *storage, RecognizerEventCb event_cb,
                                        void *user_data) {
  Recognizer *recognizer =
      pan_recognizer_init_static(storage, event_cb, user_data, PanAxis_Horizontal);
  if (!recognizer) {
    return NULL;
  }
  recognizer_set_touch_filter(recognizer, prv_started_at_edge);
  return recognizer;
}

//! Where the finger would come to rest, in pixels travelled from the edge.
static int32_t prv_projected_x(const Recognizer *recognizer) {
  const int32_t travelled = pan_recognizer_get_delta_since_start(recognizer).x;
  const int32_t velocity = pan_recognizer_get_velocity(recognizer).x;

  // velocity is px/s; dividing by 1000 first keeps the multiply inside 32 bits for any velocity
  // the hardware can report.
  return travelled + ((velocity / 1000) * PROJECTION_NUMERATOR) / PROJECTION_DENOMINATOR;
}

bool stone_edge_back_should_commit(const Recognizer *recognizer, int16_t width) {
  if (width <= 0) {
    return false;
  }
  return prv_projected_x(recognizer) >
         ((int32_t)width * COMMIT_FRACTION_NUMERATOR) / COMMIT_FRACTION_DENOMINATOR;
}

uint8_t stone_edge_back_progress(const Recognizer *recognizer, int16_t width) {
  if (width <= 0) {
    return 0;
  }
  int32_t travelled = pan_recognizer_get_delta_since_start(recognizer).x;
  if (travelled < 0) {
    travelled = 0;
  }
  const int32_t pct = (travelled * 100) / width;
  return (uint8_t)((pct > 100) ? 100 : pct);
}

#endif  // CONFIG_STONE && CONFIG_TOUCH
