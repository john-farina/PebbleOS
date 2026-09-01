/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

// Stone's haptic vocabulary. See stone_haptics.h for the model and CLAUDE.md for how to use it.
//
// Every number below follows from four facts about the hardware and the layers under this one.
//
// 1. Strength is a real gain register on the AW86225, not a duty cycle, so amplitude is a genuine
//    axis and "lighter" is not a synonym for "shorter".
// 2. There is a perceptibility floor. A pulse that is both short and soft sits under the motor's
//    start-up threshold and is felt as *nothing*. 25ms at 25% is the measured whisper on flat
//    hardware -- the weather app's globe view found it -- and is the lightest thing here.
// 3. Consecutive non-zero segments are cheap. services/vibe_pattern/service.c only calls
//    vibe_ctl(false) for a zero-amplitude step, and only that path pays the driver's 80ms
//    stop-poll; a change between two non-zero amplitudes is a single gain write. This is what
//    makes shaped envelopes affordable at all, and it is why no envelope here returns to zero
//    in the middle.
// 4. Because the motor never stops mid-envelope, the floor applies to the envelope's *total*
//    duration rather than to each segment. A three-segment 30ms rigid effect is 30ms of
//    continuous vibration, not three 10ms pulses that would each be inaudible.

#include "stone_haptics.h"

#if defined(CONFIG_STONE) && defined(CONFIG_VIBE)

#include "applib/graphics/gtypes.h"
#include "applib/ui/vibes.h"

#include "pbl/drivers/rtc.h"
#include "pbl/util/math.h"
#include "pbl/util/size.h"
#include "syscall/syscall.h"

//! Long enough to clear a light effect plus the driver's stop latency, so a throttled effect is
//! one the wearer would not have felt as separate anyway.
#define MIN_LIGHT_INTERVAL_MS (100)

#define MS_TO_TICKS(ms) (((uint64_t)(ms) * RTC_TICKS_HZ) / 1000u)

//! Amplitude below which the motor does not reliably start turning, as a percentage. The gain
//! register is 7 bits of full scale, so 25% is about 32 counts; below that a pulse this short is
//! spent entirely in the LRA's start-up transient and is felt as nothing.
//!
//! This is why the envelope is applied *between* the floor and the peak rather than between zero
//! and the peak. Scaling a 25% peak by a shape that opens at 40% asks the motor for 10%, and the
//! first third of every light effect -- which is every button press -- was inaudible.
#define PERCEPTIBLE_FLOOR_PCT (25)

//! Peak amplitude per strength, as a percentage. Round hardware needs more to cross the same
//! perceptual threshold, mirroring the split the globe view already makes rather than inventing
//! a second rule.
//!
//! Every peak sits clear of PERCEPTIBLE_FLOOR_PCT rather than merely at or above it. The span
//! between the two is what the envelope has to work with, so a peak on the floor is a strength
//! whose three motions all feel the same -- which for the side buttons is the whole feature gone.
static const uint8_t s_peak[StoneHapticStrengthCount] = {
  [StoneHapticStrength_Light]  = PBL_IF_ROUND_ELSE(75, 50),
  [StoneHapticStrength_Medium] = PBL_IF_ROUND_ELSE(85, 65),
  [StoneHapticStrength_Firm]   = PBL_IF_ROUND_ELSE(100, 85),
};

_Static_assert(PBL_IF_ROUND_ELSE(75, 50) > PERCEPTIBLE_FLOOR_PCT,
               "the lightest strength must leave the envelope somewhere to go");

//! Per-segment duration. Rigid runs shorter so the attack is abrupt; soft runs longer so the
//! swell has time to be felt as a swell.
static const uint8_t s_segment_ms[StoneHapticCharacterCount] = {
  [StoneHapticCharacter_Rigid] = PBL_IF_ROUND_ELSE(15, 10),
  [StoneHapticCharacter_Soft]  = PBL_IF_ROUND_ELSE(22, 16),
};

#define MAX_SEGMENTS (3)

//! Envelope shapes as percentages of peak. Motion picks the shape; character picks how the
//! plateau is approached.
//!
//! Rising and falling are exact mirrors on purpose. If "up" and "down" differed in more than
//! direction they would be two effects rather than one effect and its opposite, and the wearer
//! would have to learn both.
static const uint8_t s_shape[StoneHapticMotionCount][StoneHapticCharacterCount][MAX_SEGMENTS] = {
  [StoneHapticMotion_None] = {
    [StoneHapticCharacter_Rigid] = { 100, 100, 55 },
    [StoneHapticCharacter_Soft]  = { 55, 100, 55 },
  },
  [StoneHapticMotion_Rising] = {
    [StoneHapticCharacter_Rigid] = { 40, 70, 100 },
    [StoneHapticCharacter_Soft]  = { 35, 65, 100 },
  },
  [StoneHapticMotion_Falling] = {
    [StoneHapticCharacter_Rigid] = { 100, 70, 40 },
    [StoneHapticCharacter_Soft]  = { 100, 65, 35 },
  },
};

//! The named effects, as points in the three-axis space.
typedef struct {
  StoneHapticStrength strength;
  StoneHapticCharacter character;
  StoneHapticMotion motion;
} StoneHapticNamed;

static const StoneHapticNamed s_named[StoneHapticCount] = {
  [StoneHaptic_Tick] =
      { StoneHapticStrength_Light, StoneHapticCharacter_Rigid, StoneHapticMotion_None },
  [StoneHaptic_Select] =
      { StoneHapticStrength_Medium, StoneHapticCharacter_Rigid, StoneHapticMotion_None },
  [StoneHaptic_Enter] =
      { StoneHapticStrength_Firm, StoneHapticCharacter_Soft, StoneHapticMotion_Rising },
  [StoneHaptic_Exit] =
      { StoneHapticStrength_Medium, StoneHapticCharacter_Soft, StoneHapticMotion_Falling },
  [StoneHaptic_Bump] =
      { StoneHapticStrength_Firm, StoneHapticCharacter_Rigid, StoneHapticMotion_None },
};

static RtcTicks s_last_played;

static bool prv_throttled(StoneHapticStrength strength) {
  // Only the light, repeating effects are throttled. Anything heavier is a discrete act, and a
  // wearer who does something deliberate and feels nothing concludes it did not register.
  if (strength != StoneHapticStrength_Light) {
    return false;
  }
  return (sys_get_ticks() - s_last_played) < MS_TO_TICKS(MIN_LIGHT_INTERVAL_MS);
}

bool stone_haptics_would_play(void) {
  return !prv_throttled(StoneHapticStrength_Light);
}

void stone_haptics_emit(StoneHapticStrength strength, StoneHapticCharacter character,
                        StoneHapticMotion motion) {
  if (((size_t)strength >= StoneHapticStrengthCount) ||
      ((size_t)character >= StoneHapticCharacterCount) ||
      ((size_t)motion >= StoneHapticMotionCount)) {
    return;
  }
  if (prv_throttled(strength)) {
    return;
  }
  s_last_played = sys_get_ticks();

  const uint8_t peak = s_peak[strength];
  const uint8_t segment_ms = s_segment_ms[character];
  const uint8_t *shape = s_shape[motion][character];

  // The shape spans floor..peak, not 0..peak. Direction still reads -- the segments differ from
  // each other by the same proportion of the available range -- but no segment is asked for an
  // amplitude the motor cannot produce. A peak already at or below the floor is played flat at
  // the floor rather than shaped into silence.
  const uint32_t floor_pct = MIN(PERCEPTIBLE_FLOOR_PCT, peak);
  const uint32_t span = peak - floor_pct;

  uint32_t durations[MAX_SEGMENTS];
  uint32_t amplitudes[MAX_SEGMENTS];
  for (size_t i = 0; i < MAX_SEGMENTS; i++) {
    durations[i] = segment_ms;
    amplitudes[i] = floor_pct + ((span * shape[i]) / 100u);
  }

  // The amplitude API, not the boolean one: the boolean path scales by the wearer's *alert*
  // intensity preference, which is about how loud notifications are and has nothing to say about
  // how a button should feel.
  vibes_enqueue_custom_pattern_with_amplitudes((VibePatternWithAmplitudes) {
    .durations = durations,
    .amplitudes = amplitudes,
    .num_segments = MAX_SEGMENTS,
  });
}

void stone_haptics_play(StoneHaptic haptic) {
  if ((size_t)haptic >= StoneHapticCount) {
    return;
  }
  stone_haptics_emit(s_named[haptic].strength, s_named[haptic].character, s_named[haptic].motion);
}

void stone_haptics_button(ButtonId button) {
  // The three buttons down the right-hand side, in the order they are mounted. Their envelopes
  // run in the direction they sit, so pressing them in sequence feels like travelling down the
  // side of the watch rather than like three identical buzzes.
  switch (button) {
    case BUTTON_ID_UP:
      stone_haptics_emit(StoneHapticStrength_Light, StoneHapticCharacter_Rigid,
                         StoneHapticMotion_Rising);
      break;
    case BUTTON_ID_SELECT:
      stone_haptics_emit(StoneHapticStrength_Light, StoneHapticCharacter_Rigid,
                         StoneHapticMotion_None);
      break;
    case BUTTON_ID_DOWN:
      stone_haptics_emit(StoneHapticStrength_Light, StoneHapticCharacter_Rigid,
                         StoneHapticMotion_Falling);
      break;
    // Back is deliberately silent for now: the back *gesture* has its own feedback, and pressing
    // the button on the far side of the watch is not part of the directional set. If it should
    // buzz too, it wants Soft/Falling here -- softer and quieter than the right-hand three, so
    // the side you pressed is identifiable without looking.
    case BUTTON_ID_BACK:
    default:
      break;
  }
}

#else  // !CONFIG_STONE || !CONFIG_VIBE

void stone_haptics_play(StoneHaptic haptic) {}
void stone_haptics_emit(StoneHapticStrength strength, StoneHapticCharacter character,
                        StoneHapticMotion motion) {}
void stone_haptics_button(ButtonId button) {}

bool stone_haptics_would_play(void) {
  return false;
}

#endif
