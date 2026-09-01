/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

//! Navigation haptics: short amplitude-controlled ticks for UI feedback.
//!
//! Callers ask for an *intent*, not a waveform. Where a tick fires is a UI decision; how hard it
//! feels is a tuning decision, and keeping the two apart is the only way the whole system ends up
//! feeling consistent rather than like a dozen separately-chosen buzzes.
//!
//! This is feedback, not an alert, so it deliberately ignores Quiet Time -- the same choice
//! MenuLayer's scroll pulse already makes. It is still subject to the runlevel gate, so there are
//! no vibes at all in stationary mode.

typedef enum StoneHaptic {
  //! Moving between things: a page of the watchface picker, a row picked up or put down. The
  //! lightest tick that is reliably felt.
  StoneHaptic_Tick,
  //! Committing: choosing a watchface, dropping a reordered app. Firmer, so "it happened" reads
  //! differently from "it moved".
  StoneHaptic_Select,
  //! Crossing into a mode: the long press that opens the watchface picker, or an edge-back
  //! gesture passing the point where releasing would commit.
  StoneHaptic_Enter,
  //! Refused: the end of a list, or a gesture that will not commit on release.
  StoneHaptic_Bump,
} StoneHaptic;

//! Play a haptic, if one is not already playing and the rate limit allows it.
//!
//! Safe to call on every gesture update -- it is cheap and self-throttling, which is the point:
//! the underlying pattern service silently drops any enqueue made while a pattern is in flight,
//! so an unthrottled caller gets *inconsistent* feedback rather than dense feedback.
//!
//! @param haptic which feedback to play
void stone_haptics_play(StoneHaptic haptic);

//! @return true if a tick would actually play right now, without playing one. For callers that
//! want to keep their own state in step with what the wearer felt.
bool stone_haptics_would_play(void);
