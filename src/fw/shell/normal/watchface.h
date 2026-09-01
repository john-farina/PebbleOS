/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "kernel/events.h"
#include "process_management/app_install_manager.h"
#include "pbl/services/compositor/compositor.h"

void watchface_init(void);

void watchface_handle_button_event(PebbleEvent *e);

void watchface_set_default_install_id(AppInstallId id);

AppInstallId watchface_get_default_install_id(void);

void watchface_launch_default(const CompositorTransition *animation);

void watchface_start_low_power(void);

void watchface_reset_click_manager(void);

#if defined(CONFIG_STONE) && defined(CONFIG_TOUCH)
//! Handle a controller gesture (tap, swipe, long press) delivered while a watchface is running.
//!
//! The applib touch service refuses watchfaces a service state on purpose, so neither the
//! recognizers nor the touch-nav bridge ever reach one. Gestures therefore arrive here from the
//! kernel event loop, exactly as button events already do.
void watchface_handle_gesture_event(PebbleEvent *e);
#endif
