/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! Gesture event type
typedef enum GestureEventType {
  GestureEvent_Tap,
  GestureEvent_DoubleTap,
#ifdef CONFIG_STONE
  // Appended so the upstream values keep their numbering. `type` is a :8 bitfield, so there is
  // room for these without changing the size of PebbleGestureEvent.
  GestureEvent_SwipeUp,
  GestureEvent_SwipeDown,
  GestureEvent_SwipeLeft,
  GestureEvent_SwipeRight,
  GestureEvent_LongPress,
#endif
} GestureEventType;

//! Gesture event data, carried directly in PebbleGestureEvent
typedef struct GestureEvent {
  GestureEventType type:8;
  int16_t x;
  int16_t y;
} GestureEvent;