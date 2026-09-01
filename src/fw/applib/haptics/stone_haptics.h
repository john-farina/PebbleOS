/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/drivers/button_id.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//! Stone's haptic vocabulary.
//!
//! There is **one** vibration motor, mounted in one place. Nothing here can make a buzz happen in
//! a corner of the watch -- the whole case moves together, and that is physics, not a limitation
//! of this file. What a single actuator *can* do is vary a feeling over time, and time is enough
//! to encode direction: an envelope that swells reads as arriving, one that fades reads as
//! leaving, and a wearer learns the difference quickly because it is consistent.
//!
//! So the model has three independent axes, and every effect is a point in that space:
//!
//! | Axis | Question it answers |
//! | --- | --- |
//! | \ref StoneHapticStrength | How much of it is there? |
//! | \ref StoneHapticCharacter | Is it a click or a swell? |
//! | \ref StoneHapticMotion | Does it go anywhere? |
//!
//! Keeping them separate is the point. Callers pick meaning; this file owns feel. When the
//! whole watch needs to feel firmer, one table changes and everything moves together --
//! which is not true of a system where each call site picked its own milliseconds.

//! How much energy the effect carries.
typedef enum StoneHapticStrength {
  //! Just perceptible. For things that happen often -- a page, a row, a step.
  StoneHapticStrength_Light,
  //! Clearly present. For things you did on purpose -- a selection, a commit.
  StoneHapticStrength_Medium,
  //! Unmistakable. For things that change mode, or that you should not have done.
  StoneHapticStrength_Firm,
  StoneHapticStrengthCount,
} StoneHapticStrength;

//! The shape of the attack, which is most of what "texture" means on a single actuator.
typedef enum StoneHapticCharacter {
  //! Full amplitude immediately, then away. Reads as a click against something solid.
  StoneHapticCharacter_Rigid,
  //! Swells in. Reads as soft, cushioned, less mechanical.
  StoneHapticCharacter_Soft,
  StoneHapticCharacterCount,
} StoneHapticCharacter;

//! Direction, encoded in time rather than in space.
//!
//! A rising envelope is felt as movement away from the body of the pulse and a falling one as
//! movement toward it. It is an illusion, and it is a reliable one -- but only while every part
//! of the system uses it the same way round, which is why this is an enum and not a number.
typedef enum StoneHapticMotion {
  //! Symmetric. "Here", with no travel.
  StoneHapticMotion_None,
  //! Quiet to loud. Up, forward, opening, arriving.
  StoneHapticMotion_Rising,
  //! Loud to quiet. Down, back, closing, leaving.
  StoneHapticMotion_Falling,
  StoneHapticMotionCount,
} StoneHapticMotion;

//! A named feeling. Prefer these to \ref stone_haptics_emit: an intent survives a retune, a
//! hand-picked envelope does not.
typedef enum StoneHaptic {
  //! Moving between things: a page of the picker, a row picked up or put down.
  StoneHaptic_Tick,
  //! Committing: choosing a watchface, dropping a reordered app.
  StoneHaptic_Select,
  //! Crossing into a mode: the long press that opens the picker.
  StoneHaptic_Enter,
  //! Leaving a mode, dismissing, going back.
  StoneHaptic_Exit,
  //! Refused: the end of a list, a gesture that will not commit.
  StoneHaptic_Bump,
  StoneHapticCount,
} StoneHaptic;

//! Play a named effect.
//!
//! Self-throttling, so it is safe on every gesture update. That is not politeness: the pattern
//! service silently discards anything enqueued while a pattern is playing, so an unthrottled
//! caller gets *intermittent* feedback, which is worse than none.
void stone_haptics_play(StoneHaptic haptic);

//! Play an effect composed from the three axes, for cases the named set does not cover.
//! Reach for \ref stone_haptics_play first.
void stone_haptics_emit(StoneHapticStrength strength, StoneHapticCharacter character,
                        StoneHapticMotion motion);

//! Feedback for a physical button press, carrying where the button is.
//!
//! The three buttons down the right-hand side get envelopes that run in the direction they sit:
//! Up rises, Select stays level, Down falls. Pressing them in sequence feels like travelling down
//! the side of the watch. Back, alone on the left, is deliberately the odd one out -- softer and
//! quieter, so the side you pressed is distinguishable without looking.
void stone_haptics_button(ButtonId button);

//! @return true if a light effect would play right now rather than being throttled away.
bool stone_haptics_would_play(void);
