/* SPDX-FileCopyrightText: 2026 John Farina */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "applib/graphics/gtypes.h"
#include "process_management/app_install_manager.h"
#include "util/uuid.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_STONE) && !defined(CONFIG_SHELL_SDK)

//! Cached miniatures of watchfaces, for the watchface picker.
//!
//! A watchface can only be photographed while it is running: there is one app task and one app
//! framebuffer, so a face that has never been worn has never rendered a pixel and cannot be
//! previewed without launching it. Rather than launch every face in turn -- slow, flickery, and a
//! real process start each time -- the cache fills in as faces are worn, and the picker falls
//! back to the app's icon for anything not yet captured.
//!
//! It lives under apps/system rather than services/ because services/ has no glob: a service
//! there costs a Kconfig symbol and a line in an upstream CMakeLists, and this needs neither.

//! A third of the panel in each direction. About 5KB at 8 bits, so several can be resident at
//! once, against 45KB for a full frame.
#define STONE_FACE_THUMB_W (DISP_COLS / 3)
#define STONE_FACE_THUMB_H (DISP_ROWS / 3)
#define STONE_FACE_THUMB_BYTES (STONE_FACE_THUMB_W * STONE_FACE_THUMB_H)

//! Capture the screen as the thumbnail for @p install_id, if it is a watchface.
//!
//! Must run on KernelMain, while the system framebuffer still holds that face's last frame. The
//! downscale happens inline -- well under the work of a single composite -- and the flash write
//! is handed to the system task, because erasing a NOR page inside an app switch would be felt.
void stone_face_thumb_capture(AppInstallId install_id);

//! Read a stored thumbnail into @p buffer, which must hold STONE_FACE_THUMB_BYTES.
//! Safe from an app task: the settings file layer is threadsafe.
//! @return true if a thumbnail existed and was read
bool stone_face_thumb_load(const Uuid *uuid, uint8_t *buffer);

#endif  // CONFIG_STONE && !CONFIG_SHELL_SDK
