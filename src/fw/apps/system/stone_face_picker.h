/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/pebble_process_md.h"

#define STONE_FACE_PICKER_UUID {0x5f, 0x0e, 0x3a, 0x41, 0x6d, 0x2c, 0x4a, 0x8b, \
                                0x9c, 0x17, 0x2f, 0x6d, 0x8e, 0x0b, 0x4a, 0x93}

//! The watchface picker: hold the watchface to shrink it into a row of faces you page through.
//!
//! Registered as an ordinary system app so it can be bound to any hold slot through the Quick
//! Launch settings that already exist, rather than needing button plumbing of its own.
const PebbleProcessMd *stone_face_picker_get_app_info(void);
