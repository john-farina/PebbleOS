/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pan.h"
#include "recognizer.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_STONE) && defined(CONFIG_TOUCH)

//! Swipe in from the left edge to go back, the way iOS does it.
//!
//! This is a *pan*, not a swipe, which is the whole point. A swipe fires once at liftoff, so the
//! screen cannot respond until you have already let go. A pan tracks the finger, so the wearer
//! controls the gesture and can see whether it will commit before committing it.
//!
//! Restricting it to the edge is not decoration either: it is what stops "go back" firing while
//! you are dragging content. A gesture that works anywhere has to compete with everything else on
//! screen; one that only starts in a strip nothing else owns does not.

//! How far in from the left edge a touch may start and still be a back gesture. iOS uses about
//! 20pt of 390 -- around 5% -- but a watch is gripped rather than held, and a finger covers
//! proportionally more of it, so this is a little wider in relative terms.
#define STONE_EDGE_BACK_WIDTH_PX (20)

//! Build the edge-back recognizer into caller-provided storage, sized
//! \ref STONE_EDGE_BACK_STATIC_SIZE.
//!
//! @param storage pointer-aligned storage of at least \ref STONE_EDGE_BACK_STATIC_SIZE bytes
//! @param event_cb called as the gesture progresses
//! @param user_data passed back to @p event_cb
//! @return the recognizer, or NULL if @p event_cb is NULL
Recognizer *stone_edge_back_init_static(void *storage, RecognizerEventCb event_cb,
                                        void *user_data);

//! Storage required by \ref stone_edge_back_init_static. It is a pan underneath, so it needs
//! exactly a pan's storage.
#define STONE_EDGE_BACK_STATIC_SIZE PAN_RECOGNIZER_STATIC_SIZE

//! Whether the gesture should commit if the finger were lifted now.
//!
//! Uses Apple's projection: the resting point is where the content would coast to, given the
//! current velocity and a 0.998 deceleration rate, which works out at roughly half the velocity
//! in pixels. Deciding on the *projected* position rather than the current one is why a quick
//! flick from the very edge commits, and a slow drag that has not got far does not -- both match
//! what the wearer meant.
//!
//! @param recognizer the edge-back recognizer, mid-gesture
//! @param width the width the gesture is measured against, normally the window's
//! @return true if releasing now should go back
bool stone_edge_back_should_commit(const Recognizer *recognizer, int16_t width);

//! How far along the gesture is, 0-100, for driving an interactive transition.
uint8_t stone_edge_back_progress(const Recognizer *recognizer, int16_t width);

#endif  // CONFIG_STONE && CONFIG_TOUCH
