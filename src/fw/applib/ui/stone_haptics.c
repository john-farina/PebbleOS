/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Navigation haptics.
//
// Three things shape every number in this file.
//
// 1. The motor is an LRA behind an AW86225, and strength is a real gain register rather than a
//    duty cycle, so "lighter" is an actual amplitude and not a shorter buzz.
// 2. There is a floor. A pulse that is both short and soft sits under the motor's start-up
//    threshold and is felt as *nothing* -- the weather app's globe view found this the hard way
//    and settled on 25ms @ 25% for flat hardware, which is the tick reused here.
// 3. The pattern service drops any enqueue made while a pattern is already playing
//    (services/vibe_pattern/service.c), and the driver's stop path can poll the chip for up to
//    80ms. So an unthrottled per-swipe tick does not produce dense feedback; it produces
//    *inconsistent* feedback, which is worse than none. Hence the rate limit below.

#include "stone_haptics.h"

#if defined(CONFIG_STONE) && defined(CONFIG_VIBE)

#include "vibes.h"

#include "applib/graphics/gtypes.h"

#include "pbl/drivers/rtc.h"
#include "pbl/util/size.h"
#include "syscall/syscall.h"

//! Long enough to clear a tick plus the driver's stop latency, so a throttled tick is one the
//! wearer would not have felt anyway. Short enough that deliberate swipes each get their own.
#define MIN_TICK_INTERVAL_MS (100)

#define MS_TO_TICKS(ms) (((uint64_t)(ms) * RTC_TICKS_HZ) / 1000u)

typedef struct {
  uint32_t duration_ms;
  uint32_t amplitude;
} StoneHapticSpec;

// Round hardware needs more of both to cross the same perceptual threshold; this mirrors the
// split the globe view already makes rather than inventing a second rule.
static const StoneHapticSpec s_specs[] = {
  [StoneHaptic_Tick]   = { PBL_IF_ROUND_ELSE(45, 25), PBL_IF_ROUND_ELSE(60, 25) },
  [StoneHaptic_Select] = { PBL_IF_ROUND_ELSE(45, 30), PBL_IF_ROUND_ELSE(75, 45) },
  [StoneHaptic_Enter]  = { PBL_IF_ROUND_ELSE(55, 40), PBL_IF_ROUND_ELSE(85, 55) },
  [StoneHaptic_Bump]   = { PBL_IF_ROUND_ELSE(45, 25), PBL_IF_ROUND_ELSE(70, 35) },
};

static RtcTicks s_last_played;

static bool prv_throttled(StoneHaptic haptic) {
  // Only the repeating one is throttled. Select, Enter and Bump are discrete acts -- a wearer
  // who presses a button and feels nothing concludes the press did not register.
  if (haptic != StoneHaptic_Tick) {
    return false;
  }
  const RtcTicks now = sys_get_ticks();
  return (now - s_last_played) < MS_TO_TICKS(MIN_TICK_INTERVAL_MS);
}

bool stone_haptics_would_play(void) {
  return !prv_throttled(StoneHaptic_Tick);
}

void stone_haptics_play(StoneHaptic haptic) {
  if ((haptic < 0) || (haptic >= (StoneHaptic)ARRAY_LENGTH(s_specs))) {
    return;
  }
  if (prv_throttled(haptic)) {
    return;
  }
  s_last_played = sys_get_ticks();

  const StoneHapticSpec *spec = &s_specs[haptic];
  // Deliberately the amplitude API: the boolean one multiplies by the wearer's *alert* intensity
  // preference, which is about how loud notifications are and has nothing to say about how a
  // swipe should feel.
  vibes_enqueue_custom_pattern_with_amplitudes((VibePatternWithAmplitudes) {
    .durations = &spec->duration_ms,
    .amplitudes = &spec->amplitude,
    .num_segments = 1,
  });
}

#else  // !CONFIG_STONE || !CONFIG_VIBE

void stone_haptics_play(StoneHaptic haptic) {}

bool stone_haptics_would_play(void) {
  return false;
}

#endif
