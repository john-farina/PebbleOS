/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#include "stone_trace.h"

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

#include "pbl/drivers/rtc.h"
#include "pbl/logging/logging.h"
#include "pbl/util/attributes.h"
#include "system/passert.h"

#include "FreeRTOS.h"
#include "task.h"

#include <inttypes.h>

typedef struct PACKED {
  //! Milliseconds since boot, wrapping every ~65s. The decoder unwraps it; what matters in a
  //! touch trace is the spacing between events, not the wall clock.
  uint16_t ms;
  uint8_t source;
  uint8_t code;
  int16_t a;
  int16_t b;
} StoneTraceEntry;

static StoneTraceEntry s_ring[STONE_TRACE_ENTRIES];
static uint16_t s_head;    //!< next slot to write
static uint16_t s_count;   //!< entries written, saturating at STONE_TRACE_ENTRIES
static uint32_t s_dropped; //!< events lost to a full ring between dumps

static const char *const s_source_names[StoneTraceCount] = {
  [StoneTraceTouch] = "touch",
  [StoneTraceGesture] = "gest",
  [StoneTraceSwipe] = "swipe",
  [StoneTraceNav] = "nav",
  [StoneTracePicker] = "pick",
  [StoneTraceThumb] = "thumb",
};

void stone_trace(StoneTraceSource source, uint8_t code, int16_t a, int16_t b) {
  if ((unsigned)source >= StoneTraceCount) {
    return;
  }

  const uint64_t ticks = rtc_get_ticks();
  const uint16_t ms = (uint16_t)(((ticks * 1000u) / RTC_TICKS_HZ) & 0xFFFFu);

  // A short critical section rather than a mutex: this is called from the touch path and from
  // more than one task, and a mutex here would be a lock ordering problem waiting to happen.
  // Losing an entry to a race would be worse than the handful of cycles this costs.
  portENTER_CRITICAL();
  s_ring[s_head] = (StoneTraceEntry){
    .ms = ms,
    .source = (uint8_t)source,
    .code = code,
    .a = a,
    .b = b,
  };
  s_head = (uint16_t)((s_head + 1) % STONE_TRACE_ENTRIES);
  if (s_count < STONE_TRACE_ENTRIES) {
    s_count++;
  } else {
    s_dropped++;
  }
  portEXIT_CRITICAL();
}

void stone_trace_dump(void) {
  portENTER_CRITICAL();
  const uint16_t count = s_count;
  const uint16_t head = s_head;
  const uint32_t dropped = s_dropped;
  s_count = 0;
  s_head = 0;
  s_dropped = 0;
  portEXIT_CRITICAL();

  // The markers are what the decoder looks for, so a capture can be pasted out of a much larger
  // log and still be found. Logged at ALWAYS because a capture the wearer deliberately asked for
  // must not be filtered out by whatever level the log is running at.
  PBL_LOG_ALWAYS("STONE-TRACE BEGIN entries=%" PRIu16 " dropped=%" PRIu32, count, dropped);
  for (uint16_t i = 0; i < count; i++) {
    // Oldest first. When the ring wrapped, the oldest entry is the one about to be overwritten.
    const uint16_t idx =
        (uint16_t)((head + STONE_TRACE_ENTRIES - count + i) % STONE_TRACE_ENTRIES);
    const StoneTraceEntry *e = &s_ring[idx];
    PBL_LOG_ALWAYS("ST %" PRIu16 " %s %" PRIu8 " %" PRId16 " %" PRId16, e->ms,
                   s_source_names[e->source] ?: "?", e->code, e->a, e->b);
  }
  PBL_LOG_ALWAYS("STONE-TRACE END");
}

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
