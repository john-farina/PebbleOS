/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! A small ring of structured events, for debugging things that only go wrong on a wrist.
//!
//! Two of this fork's features have now shipped broken because the only way to observe them was
//! to wear the watch and describe what happened. Prose is a poor bug report: "swiping sucks"
//! cannot distinguish a gesture the controller never reported from one that was reported and
//! rejected, and those have opposite fixes.
//!
//! So the interesting paths record what they saw, into RAM, cheaply enough to sit in a touch
//! handler. Holding the three right-hand buttons prints the ring to the log, where it can be
//! pulled off the watch and read.
//!
//! It is deliberately *not* PBL_LOG. A log line costs a format string, a flash write and a
//! filename, which is far too much to put on every touch sample; and the interesting events are
//! drowned by everything else in the log by the time anyone looks. A fixed-size record with two
//! integers is cheap enough to leave switched on, and the ring means the capture is always the
//! last few seconds -- the ones just before the thing you are trying to explain.

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

//! What produced an event. Kept short: it is printed on every line.
typedef enum {
  StoneTraceTouch = 0,   //!< raw contact from the controller
  StoneTraceGesture,     //!< a gesture the controller decided on
  StoneTraceSwipe,       //!< Stone's own swipe detection
  StoneTraceNav,         //!< what navigation did about it
  StoneTracePicker,      //!< the watchface picker
  StoneTraceThumb,       //!< the thumbnail cache
  StoneTraceCount,
} StoneTraceSource;

//! Record one event. Safe from any task, and from the touch path specifically -- no allocation,
//! no formatting, no flash.
//!
//! @param source what is reporting
//! @param code a source-defined event code; the decoder names them, see tools/stone/decode_trace.py
//! @param a,b two source-defined values, usually coordinates or a duration
void stone_trace(StoneTraceSource source, uint8_t code, int16_t a, int16_t b);

//! Print the ring to the log, newest capture last, and clear it.
//!
//! Runs on whatever task calls it and emits one log line per entry, so it is a deliberate act
//! rather than something to call from a hot path.
void stone_trace_dump(void);

//! How many events the ring holds. Sized so a capture is a few seconds of touch activity and
//! still pastes into a chat window without being trimmed.
#define STONE_TRACE_ENTRIES (192)

#else

#define STONE_TRACE_ENTRIES (0)
static inline void stone_trace(int source, uint8_t code, int16_t a, int16_t b) {}
static inline void stone_trace_dump(void) {}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
