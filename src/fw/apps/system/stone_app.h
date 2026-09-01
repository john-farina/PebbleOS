/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/pebble_process_md.h"

// Not built for the SDK shell, whose registry has four apps and no settings
// module to push.
#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

//! The Stone app: which build is on this watch, at the top of the app list.
//!
//! It is an app rather than a row inside Settings because of what it is for.
//! The question it answers -- "which build am I actually running?" -- is asked
//! before every other question about a WIP build, and twice now a session has
//! been spent debugging behaviour that was simply absent from the build being
//! described. Something you check that often should not be five rows down
//! inside something else.
//!
//! The window itself is the Stone settings module, pushed straight away, so
//! there is one implementation of the list and it keeps the system look.
const PebbleProcessMd *stone_app_get_app_info(void);

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
